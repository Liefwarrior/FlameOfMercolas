package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@code HOUSE_ARREST} (density revisit): sits just under {@code HELD} on the score ladder,
 * routes the shover HOME (leash-ignoring), holds it there through the 24,000-tick sentence
 * (sleeping — REST recovery is the home-cell tick machinery), FEEDS it at the hearth (the
 * custody-starvation landmine), and releases with the {@link HeldPolicy}-shaped goal reset at
 * exactly the deadline.
 */
final class HouseArrestPolicyTest {

    private static final int Z = 11;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    private static Actor serfAt(ActorRegistry registry, int x, int y) {
        return registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, 8), cell(x, y));
    }

    @Test
    void scoreLadderSitsJustUnderHeldAndAbovePlayerControl() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 10);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        assertEquals(0, Policies.HOUSE_ARREST.score(actor, ctx), "no bit, no score");

        actor.setStatus(StatusBit.HOUSE_ARREST, true);
        int houseArrest = Policies.HOUSE_ARREST.score(actor, ctx);
        actor.setStatus(StatusBit.HELD, true);
        assertTrue(houseArrest < Policies.HELD.score(actor, ctx),
                "real custody supersedes house arrest");
        assertTrue(houseArrest > 2000, "above PLAYER_CONTROL (2000) and every NEED band");
    }

    @Test
    void actWalksTheOffenderHomeIgnoringTheLeash() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 30, 10);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        // Home far OUTSIDE the leash radius (8) — the march home must ignore the leash.
        actor.setHomeId(ctx.homes().addHome(cell(10, 10)));
        actor.setStatus(StatusBit.HOUSE_ARREST, true);
        actor.setHouseArrestUntilTick(24_100L);
        ctx.setTick(100L);

        Policies.HOUSE_ARREST.act(actor, ctx);

        // One leash-ignoring A* hop toward home (the exact hop varies with the per-actor route
        // jitter — assert progress, not a pinned cell).
        assertEquals(19, ActorGeometry.chebyshev(actor.cell(), cell(10, 10)),
                "one step closer to home, from 20 tiles out");
        assertEquals(ReasonCode.UNDER_HOUSE_ARREST, actor.lastReasonCode());
        assertTrue(actor.hasStatus(StatusBit.HOUSE_ARREST), "still serving");
    }

    @Test
    void actHoldsAtHomeAndEatsCarriedRationWhileHungry() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 10);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        actor.setHomeId(ctx.homes().addHome(cell(10, 10)));
        actor.setStatus(StatusBit.HOUSE_ARREST, true);
        actor.setHouseArrestUntilTick(24_100L);
        actor.applyNeedDelta(Need.HUNGER, -6000); // hungry, far below RECOVERED
        ctx.items().addCarried(actor.id(), ItemKinds.FOOD, 2);
        int hungerBefore = actor.need(Need.HUNGER);
        ctx.setTick(100L);

        Policies.HOUSE_ARREST.act(actor, ctx);

        assertEquals(cell(10, 10), actor.cell(), "held in place at home");
        assertTrue(actor.need(Need.HUNGER) > hungerBefore,
                "house arrest FEEDS at the hearth — the custody-starvation landmine is closed");
        assertEquals(1, ctx.items().countCarriedOfKind(actor.id(), ItemKinds.FOOD),
                "one carried ration eaten (sink-accounted)");
    }

    @Test
    void releasesWithTheGoalResetAtExactlyTheDeadline() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 10);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        actor.setHomeId(ctx.homes().addHome(cell(10, 10)));
        actor.setStatus(StatusBit.HOUSE_ARREST, true);
        actor.setHouseArrestUntilTick(24_000L);
        actor.setGoalState(GoalState.PURSUING);
        actor.setGoalTarget(TargetKind.CELL, cell(50, 50));

        ctx.setTick(23_999L);
        Policies.HOUSE_ARREST.act(actor, ctx);
        assertTrue(actor.hasStatus(StatusBit.HOUSE_ARREST), "one tick early: still held");

        ctx.setTick(24_000L);
        Policies.HOUSE_ARREST.act(actor, ctx);
        assertFalse(actor.hasStatus(StatusBit.HOUSE_ARREST), "released at exactly the deadline");
        assertEquals(GoalState.SELECTING, actor.goalState(), "goal machine reset (HeldPolicy shape)");
        assertEquals(TargetKind.NONE, actor.goalTargetKind());
        assertEquals(ReasonCode.RELEASED_FROM_HOUSE_ARREST, actor.lastReasonCode());
    }
}
