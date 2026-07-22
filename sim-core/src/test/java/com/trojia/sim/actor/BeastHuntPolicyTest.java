package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.CatActor;
import com.trojia.sim.actor.type.MouseActor;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The BEAST food channel ({@code BEAST_HUNT}, living-docks beast pass): acquire-gated scoring
 * (no actionable prey ⇒ score 0 — the structural fix for the SEEK_FOOD dead-end that pinned
 * every gull), the sense → lock → chase → catch-at-adjacency loop, the DOWNED+timer transition
 * on the mouse, the SEEK_FOOD-parity pricing vs FLEE, and the deterministic two-predators/one-
 * mouse resolution.
 */
final class BeastHuntPolicyTest {

    private static final int Z = 11;
    private static final long SENSE_TICK = 20; // % SENSE_PERIOD_TICKS(10) == 0

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /** Predator stats mirroring the committed cat/gull raws pricing (655 LOW / 1005 CRIT, FLEE 950). */
    private static ActorTypeStats predatorStats(ActorTypeId typeId) {
        NeedConfig[] needs = new NeedConfig[Need.COUNT];
        needs[Need.HUNGER.ordinal()] = new NeedConfig(8000, 1000, 0, 350, 700);
        needs[Need.REST.ordinal()] = new NeedConfig(9000, 800, 0, 150, 300);
        needs[Need.COIN.ordinal()] = new NeedConfig(10000, 0, 0, 0, 0);
        needs[Need.SAFETY.ordinal()] = new NeedConfig(10000, 0, 6, 500, 900);
        needs[Need.DUTY.ordinal()] = new NeedConfig(10000, 0, 0, 0, 0);
        return new ActorTypeStats(typeId, "Test " + typeId, 'c', 0xC8C0B4, "feral",
                (short) 6, 1, 20, 0, needs, false, 0, 0, 950, 305, 305, 0, 0, 0, 20);
    }

    private static ActorTypeStats mouseStats() {
        NeedConfig[] needs = new NeedConfig[Need.COUNT];
        needs[Need.HUNGER.ordinal()] = new NeedConfig(9000, 300, 0, 350, 700);
        needs[Need.REST.ordinal()] = new NeedConfig(9000, 800, 0, 150, 300);
        needs[Need.COIN.ordinal()] = new NeedConfig(10000, 0, 0, 0, 0);
        needs[Need.SAFETY.ordinal()] = new NeedConfig(10000, 0, 25, 500, 900);
        needs[Need.DUTY.ordinal()] = new NeedConfig(10000, 0, 0, 0, 0);
        return new ActorTypeStats(MouseActor.TYPE, "Test mouse", 'r', 0x9A8468, "feral",
                (short) 2, 1, 8, 0, needs, false, 0, 0, 950, 305, 305, 0, 0, 0, 20);
    }

    private static Actor spawnCat(ActorRegistry registry, int x, int y) {
        return registry.spawn(CatActor.TYPE, predatorStats(CatActor.TYPE), cell(x, y));
    }

    private static Actor spawnMouse(ActorRegistry registry, int x, int y) {
        return registry.spawn(MouseActor.TYPE, mouseStats(), cell(x, y));
    }

    private static void setHunger(Actor actor, int reserve) {
        actor.applyNeedDelta(Need.HUNGER, reserve - actor.need(Need.HUNGER));
    }

    @Test
    void hungryPredatorSensesLocksChasesAndCatches() {
        ActorRegistry registry = new ActorRegistry();
        Actor cat = spawnCat(registry, 50, 50);
        Actor mouse = spawnMouse(registry, 60, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        setHunger(cat, 2500); // LOW, not CRITICAL

        ctx.setTick(SENSE_TICK);
        assertEquals(655, Policies.BEAST_HUNT.score(cat, ctx),
                "sensed prey at LOW hunger prices exactly like SEEK_FOOD (305 + 350)");

        Policies.BEAST_HUNT.act(cat, ctx);
        assertEquals(TargetKind.ACTOR, cat.targetKind(), "the sense probe locks the prey");
        assertEquals(mouse.id(), cat.targetKey());
        ctx.setTick(SENSE_TICK + 1); // NOT a sense boundary
        assertEquals(655, Policies.BEAST_HUNT.score(cat, ctx),
                "a live lock scores on ANY tick — never abandon mid-chase");

        int before = ActorGeometry.chebyshev(cat.cell(), mouse.cell());
        for (int t = 1; t <= 40 && !mouse.hasStatus(StatusBit.DOWNED); t++) {
            ctx.setTick(SENSE_TICK + t);
            Policies.BEAST_HUNT.act(cat, ctx);
        }
        assertTrue(ActorGeometry.chebyshev(cat.cell(), mouse.cell()) < before,
                "the chase closed the distance");
        assertTrue(mouse.hasStatus(StatusBit.DOWNED), "the mouse was caught");
        assertEquals(BeastHuntPolicy.PREY_REVIVE_TICKS, mouse.downedTimer());
        assertEquals(ReasonCode.PREY_CAUGHT, mouse.lastReasonCode());
        assertEquals(BeastHuntPolicy.PREY_RESPAWN_HUNGER, mouse.need(Need.HUNGER),
                "the catch refills the prey's hunger — the revive is a fresh mouse from the den");
        assertEquals(10000, cat.need(Need.HUNGER), "2500 + EAT_RESTORE(8000) clamps at MAX");
        assertEquals(TargetKind.NONE, cat.targetKind(), "the lock clears at the catch");
        assertEquals(ReasonCode.ATE_PREY, cat.lastReasonCode());
        assertEquals(0, ctx.items().size(), "predation mints/sinks no FOOD item");
    }

    @Test
    void notHungryScoresZeroEvenWithPreyAdjacent() {
        ActorRegistry registry = new ActorRegistry();
        Actor cat = spawnCat(registry, 50, 50);
        spawnMouse(registry, 51, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(SENSE_TICK);
        assertEquals(0, Policies.BEAST_HUNT.score(cat, ctx));
    }

    @Test
    void hungryWithNoSensablePreyScoresZeroAndTheWanderKeepsWinning() {
        // THE dead-end regression pin: SEEK_FOOD fired unconditionally on the hunger band and
        // pinned a beast forever; BEAST_HUNT with no actionable prey must stand aside so
        // GOAL_PURSUE (the wander job) keeps winning.
        ActorRegistry registry = new ActorRegistry();
        Actor cat = spawnCat(registry, 50, 50);
        spawnMouse(registry, 90, 90); // chebyshev 40 > SENSE_RADIUS 24
        NoOpActorContext ctx = new NoOpActorContext(registry);
        cat.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Beast.Prowler.ID));
        setHunger(cat, 2500);

        for (long t = SENSE_TICK; t < SENSE_TICK + 30; t++) {
            ctx.setTick(t);
            assertEquals(0, Policies.BEAST_HUNT.score(cat, ctx),
                    "no sensable prey -> 0 on every tick (cadence and not)");
        }
        ctx.setTick(SENSE_TICK);
        cat.tick(ctx);
        assertEquals(ReasonCode.JOB_GOAL, cat.lastReasonCode(),
                "the hungry-but-preyless beast wanders instead of dead-ending");
    }

    @Test
    void twoAdjacentPredatorsOneMouseLowerIdEatsHigherIdClosesTargetLost() {
        ActorRegistry registry = new ActorRegistry();
        Actor catA = spawnCat(registry, 50, 50);
        Actor catB = spawnCat(registry, 52, 50);
        Actor mouse = spawnMouse(registry, 51, 50); // adjacent to both
        NoOpActorContext ctx = new NoOpActorContext(registry);
        setHunger(catA, 2500);
        setHunger(catB, 2500);
        catA.setTarget(TargetKind.ACTOR, mouse.id());
        catB.setTarget(TargetKind.ACTOR, mouse.id());
        ctx.setTick(SENSE_TICK);

        Policies.BEAST_HUNT.act(catA, ctx); // ascending-id order: A eats first
        assertTrue(mouse.hasStatus(StatusBit.DOWNED));
        assertEquals(10000, catA.need(Need.HUNGER));

        Policies.BEAST_HUNT.act(catB, ctx);
        assertEquals(2500, catB.need(Need.HUNGER), "no second meal off one mouse");
        assertEquals(TargetKind.NONE, catB.targetKind());
        assertEquals(ReasonCode.TARGET_LOST, catB.lastReasonCode());
    }

    @Test
    void downedMouseIsInvisibleToTheSenseScan() {
        ActorRegistry registry = new ActorRegistry();
        Actor cat = spawnCat(registry, 50, 50);
        Actor mouse = spawnMouse(registry, 55, 50);
        mouse.setStatus(StatusBit.DOWNED, true);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        setHunger(cat, 2500);
        ctx.setTick(SENSE_TICK);
        assertEquals(0, Policies.BEAST_HUNT.score(cat, ctx));
    }

    @Test
    void huntPricingKeepsTheSeekFoodFleeParityLadder() {
        // At LOW the hunt (655) yields to FLEE (950); at CRITICAL (1005) starving outranks
        // scared — the exact citizen SEEK_FOOD-vs-FLEE ladder, asserted as documentation.
        ActorRegistry registry = new ActorRegistry();
        Actor cat = spawnCat(registry, 50, 50);
        spawnMouse(registry, 55, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(SENSE_TICK);

        setHunger(cat, 2500); // LOW
        int lowScore = Policies.BEAST_HUNT.score(cat, ctx);
        assertEquals(655, lowScore);
        assertTrue(lowScore < cat.stats().fleeEmergencyPriority(), "LOW: flee outranks the hunt");

        setHunger(cat, 500);  // CRITICAL
        int critScore = Policies.BEAST_HUNT.score(cat, ctx);
        assertEquals(1005, critScore);
        assertTrue(critScore > cat.stats().fleeEmergencyPriority(),
                "CRITICAL: starving outranks scared (SEEK_FOOD parity)");
    }

    @Test
    void aChokepointFrozenChaseIsAbandonedAtTheBudgetAndTheWanderGetsAFreshDraw() {
        // The docks-soak starvation mode: the A* route to the prey EXISTS (occupancy-blind)
        // but its first hop is a cell parked at the occupancy cap, so the chase makes zero
        // progress while outranking the self-healing wander forever. The chase budget must
        // abandon it, clear the wander goal target so the job redraws a fresh leg, AND back
        // off acquisition (gull#408: without the backoff the next sense cadence re-locked the
        // same hop-blocked prey, giving the wander <= 10 ticks per 100-tick doomed chase).
        ActorRegistry registry = new ActorRegistry();
        Actor cat = spawnCat(registry, 50, 50);
        Actor mouse = spawnMouse(registry, 60, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public Actor.OccupancyQuery occupancy() {
                return new Actor.OccupancyQuery() {
                    @Override
                    public int occupantsAt(int c) {
                        return Actor.MAX_OCCUPANTS_PER_CELL; // every step blocked, route intact
                    }

                    @Override
                    public void onEnter(int fromCell, int toCell) {
                    }
                };
            }
        };
        setHunger(cat, 2500);
        cat.setGoalTarget(TargetKind.CELL, cell(55, 55)); // a live wander leg to be cleared
        cat.setTarget(TargetKind.ACTOR, mouse.id());
        ctx.setTick(SENSE_TICK);

        int before = cat.cell();
        long abandonedAt = -1;
        for (int t = 0; t <= BeastHuntPolicy.CHASE_BUDGET_TICKS
                && cat.targetKind() == TargetKind.ACTOR; t++) {
            ctx.setTick(SENSE_TICK + t);
            Policies.BEAST_HUNT.act(cat, ctx);
            abandonedAt = SENSE_TICK + t;
        }
        assertEquals(before, cat.cell(), "every hop was occupancy-blocked");
        assertEquals(TargetKind.NONE, cat.targetKind(), "the frozen chase was abandoned");
        assertEquals(ReasonCode.TARGET_LOST, cat.lastReasonCode());
        assertEquals(TargetKind.NONE, cat.goalTargetKind(),
                "the wander goal was cleared for a fresh draw away from the blocked corner");
        assertEquals(0, cat.policyTimer());
        assertFalse(mouse.hasStatus(StatusBit.DOWNED));

        // The acquisition backoff: with prey still sensable and hunger still LOW, the policy
        // must stand aside until the deadline so the wander can walk the beast out.
        assertEquals(abandonedAt + BeastHuntPolicy.HUNT_BACKOFF_TICKS,
                cat.huntBackoffUntilTick(), "the hop-blocked abandon stamps the backoff");
        long cadence = ((abandonedAt / BeastHuntPolicy.SENSE_PERIOD_TICKS) + 1)
                * BeastHuntPolicy.SENSE_PERIOD_TICKS;
        ctx.setTick(cadence);
        assertEquals(0, Policies.BEAST_HUNT.score(cat, ctx),
                "backing off: no re-lock at the very next sense cadence");
        long pastBackoff = cat.huntBackoffUntilTick()
                + BeastHuntPolicy.SENSE_PERIOD_TICKS
                - cat.huntBackoffUntilTick() % BeastHuntPolicy.SENSE_PERIOD_TICKS;
        ctx.setTick(pastBackoff);
        assertEquals(655, Policies.BEAST_HUNT.score(cat, ctx),
                "backoff expired: the hunt re-arms at the next cadence");
    }

    @Test
    void routeFailedChaseDropsTheLock() {
        ActorRegistry registry = new ActorRegistry();
        Actor cat = spawnCat(registry, 50, 50);
        Actor mouse = spawnMouse(registry, 55, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public boolean isWalkable(int c) {
                return false; // every cell walled: the chase A* must fail
            }
        };
        setHunger(cat, 2500);
        cat.setTarget(TargetKind.ACTOR, mouse.id());
        ctx.setTick(SENSE_TICK);

        Policies.BEAST_HUNT.act(cat, ctx);
        assertEquals(TargetKind.NONE, cat.targetKind(), "unroutable prey -> bounded abandon");
        assertEquals(ReasonCode.TARGET_LOST, cat.lastReasonCode());
        assertFalse(mouse.hasStatus(StatusBit.DOWNED));
    }
}
