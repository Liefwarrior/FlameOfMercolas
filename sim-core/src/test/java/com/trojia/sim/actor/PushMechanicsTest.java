package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The shove verb (density revisit): a step blocked ONLY by the 1-per-square occupancy cap
 * displaces the occupant to a deterministic adjacent free cell (fixed W/E/N/S-then-diagonals
 * scan), on a 10-tick cooldown; being shoved staggers the pushee (no push chains); the cap is
 * never violated; the shove is logged for riot detection.
 */
final class PushMechanicsTest {

    private static final int Z = 11;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /** A live occupancy view over a real {@link OccupancyIndex}, with the push hook wired. */
    private static final class LiveOccupancy implements Actor.OccupancyQuery {
        private final ActorRegistry registry;
        private final OccupancyIndex index;
        private final ShoveLog log = new ShoveLog(16);
        private Actor.WalkabilityQuery walk = c -> true;
        private long tick;

        LiveOccupancy(ActorRegistry registry) {
            this.registry = registry;
            this.index = new OccupancyIndex(registry.size());
            for (int i = 0; i < registry.size(); i++) {
                index.add(registry.get(i).cell());
            }
        }

        @Override
        public int occupantsAt(int cell) {
            return index.count(cell);
        }

        @Override
        public void onEnter(int fromCell, int toCell) {
            index.remove(fromCell);
            index.add(toCell);
        }

        @Override
        public boolean tryPush(Actor pusher, int cell) {
            return PushMechanics.tryPush(pusher, cell, registry, tick, walk, this, log);
        }
    }

    private static Actor wastrelAt(ActorRegistry registry, int x, int y) {
        return registry.spawn(Wastrel.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Wastrel.TYPE, true, 1, 64), cell(x, y));
    }

    @Test
    void pushDisplacesTheOccupantAndCommitsThePushersStep() {
        ActorRegistry registry = new ActorRegistry();
        Actor pusher = wastrelAt(registry, 10, 10);
        Actor occupant = wastrelAt(registry, 11, 10);
        LiveOccupancy occ = new LiveOccupancy(registry);
        occ.tick = 100;

        pusher.stepToward(cell(12, 10), false, c -> true, occ);

        assertEquals(cell(11, 10), pusher.cell(), "pusher took the vacated square");
        // Displacement scan around (11,10): W=(10,10) held the pusher pre-commit -> skipped;
        // E=(12,10) is the first free cell in the fixed order.
        assertEquals(cell(12, 10), occupant.cell(), "occupant displaced to the first free cell");
        assertEquals(100, pusher.lastPushTick(), "cooldown clock stamped on the pusher");
        assertEquals(100, occupant.lastPushTick(), "stagger stamped on the pushee");
        assertEquals(1, occ.log.size(), "the shove was recorded");
        assertEquals(cell(11, 10), occ.log.cellAt(0), "recorded at the contested cell");
        assertEquals(pusher.id(), occ.log.pusherIdAt(0));
        assertEquals(1, occ.occupantsAt(cell(11, 10)), "cap intact after the shove");
        assertEquals(1, occ.occupantsAt(cell(12, 10)));
    }

    @Test
    void cooldownBlocksASecondPushUntilTenTicksPass() {
        ActorRegistry registry = new ActorRegistry();
        Actor pusher = wastrelAt(registry, 10, 10);
        wastrelAt(registry, 11, 10);
        LiveOccupancy occ = new LiveOccupancy(registry);
        occ.tick = 100;
        pusher.stepToward(cell(12, 10), false, c -> true, occ); // shove #1 lands at tick 100

        occ.tick = 105; // pusher now at (11,10); occupant at (12,10) blocks the next step
        pusher.stepToward(cell(13, 10), false, c -> true, occ);
        assertEquals(cell(11, 10), pusher.cell(), "inside the cooldown the full cell is a wall");

        occ.tick = 110; // exactly PUSH_COOLDOWN_TICKS after the last shove
        pusher.stepToward(cell(13, 10), false, c -> true, occ);
        assertEquals(cell(12, 10), pusher.cell(), "cooldown elapsed: the shove works again");
    }

    @Test
    void aJustDisplacedActorCannotItselfPushThisTick() {
        ActorRegistry registry = new ActorRegistry();
        Actor pusher = wastrelAt(registry, 10, 10);
        Actor middle = wastrelAt(registry, 11, 10);
        Actor bystander = wastrelAt(registry, 11, 11);
        LiveOccupancy occ = new LiveOccupancy(registry);
        occ.tick = 100;

        pusher.stepToward(cell(12, 10), false, c -> true, occ); // displaces middle to (12,10)
        assertEquals(cell(12, 10), middle.cell());

        // Same tick, the displaced actor walks into the bystander: the stagger must block the
        // chain shove (its lastPushTick was just stamped), leaving it a plain blocked no-op.
        middle.stepToward(cell(10, 12), false, c -> true, occ);
        assertEquals(cell(11, 11), bystander.cell(), "no push chain: the bystander stands firm");
        assertFalse(occ.log.size() > 1, "exactly one shove logged this tick");
    }

    @Test
    void pushFailsWhenTheOccupantHasNowhereFreeToGo() {
        ActorRegistry registry = new ActorRegistry();
        Actor pusher = wastrelAt(registry, 10, 10);
        Actor occupant = wastrelAt(registry, 11, 10);
        LiveOccupancy occ = new LiveOccupancy(registry);
        occ.tick = 100;
        // Only the contested cell and the pusher's cell are walkable: no displacement target.
        occ.walk = c -> c == cell(11, 10) || c == cell(10, 10);

        pusher.stepToward(cell(11, 10), false, occ.walk, occ);

        assertEquals(cell(10, 10), pusher.cell(), "blocked: the full cell stays a wall");
        assertEquals(cell(11, 10), occupant.cell(), "occupant untouched");
        assertEquals(0, occ.log.size(), "a failed shove is not logged");
        assertEquals(-PushMechanics.PUSH_COOLDOWN_TICKS, pusher.lastPushTick(),
                "a failed shove burns no cooldown");
    }

    @Test
    void displacedActorsRouteCacheIsInvalidated() {
        ActorRegistry registry = new ActorRegistry();
        Actor pusher = wastrelAt(registry, 10, 10);
        Actor occupant = wastrelAt(registry, 11, 10);
        LiveOccupancy occ = new LiveOccupancy(registry);
        occ.tick = 100;
        // Seed the occupant's route cache with an observable entry: a search against an
        // unwalkable target caches the FAILURE (routeFailedTo == true, retry cooldown armed).
        occupant.stepAlongRoute(cell(20, 10), false, c -> c != cell(20, 10),
                Actor.OccupancyQuery.UNLIMITED);
        assertTrue(occupant.routeFailedTo(cell(20, 10)), "sanity: failure cached pre-shove");

        pusher.stepToward(cell(12, 10), false, c -> true, occ);

        assertTrue(occupant.cell() != cell(11, 10), "displaced");
        assertFalse(occupant.routeFailedTo(cell(20, 10)),
                "the shove invalidated the cache — the displaced actor replans fresh");
    }
}
