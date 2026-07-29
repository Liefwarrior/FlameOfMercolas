package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 6 slice 2 (Eli's bug 2 — "guards pushing each other... shouldn't last this long"):
 * the three sim-side pile-up fixes. (1) ETIQUETTE — a guard never shoves an on-duty guard:
 * the {@link PushMechanics.ShoveEtiquette} gate refuses the pairing draw-free and
 * cooldown-free. (2) YIELD — a patrol leg standing dead still for the bounded budget gives
 * the leg up and advances instead of wrestling forever. (3) STAGGER — same-route (and
 * same-beat) watch start at id-offset waypoints so identical beats walk out of phase.
 */
final class GuardEtiquetteTest {

    private static final int Z = 11;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /** A live occupancy view with the production-shaped etiquette wired in. */
    private static final class EtiquetteOccupancy implements Actor.OccupancyQuery {
        private final ActorRegistry registry;
        private final OccupancyIndex index;
        private final ShoveLog log = new ShoveLog(16);
        private final PushMechanics.ShoveEtiquette etiquette;
        private long tick = 100;

        EtiquetteOccupancy(ActorRegistry registry, PushMechanics.ShoveEtiquette etiquette) {
            this.registry = registry;
            this.etiquette = etiquette;
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
            return PushMechanics.tryPush(pusher, cell, registry, tick, c -> true, this, log,
                    SkillTrackRegistry.UNWIRED, id -> 0L, etiquette);
        }
    }

    /** The production gate shape: watch-vs-watch refused, everything else allowed. */
    private static PushMechanics.ShoveEtiquette watchEtiquette(NoOpActorContext ctx) {
        return (p, q) -> !(isWatch(ctx, p) && isWatch(ctx, q));
    }

    private static boolean isWatch(NoOpActorContext ctx, Actor a) {
        return a.jobOrdinal() >= 0 && ctx.jobs().get(a.jobOrdinal()) instanceof Job.Watch;
    }

    private static Actor watchAt(ActorRegistry registry, NoOpActorContext ctx, int at) {
        Actor watch = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(MilitiaWatch.TYPE, true, 1, 64), at);
        watch.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
        return watch;
    }

    @Test
    void aGuardNeverShovesAGuardOnDuty() {
        ActorRegistry registry = new ActorRegistry();
        NoOpActorContext ctx = new NoOpActorContext(registry);
        Actor pusher = watchAt(registry, ctx, cell(10, 10));
        Actor blocker = watchAt(registry, ctx, cell(11, 10));
        EtiquetteOccupancy occ = new EtiquetteOccupancy(registry, watchEtiquette(ctx));

        pusher.stepToward(cell(12, 10), false, c -> true, occ);

        assertEquals(cell(10, 10), pusher.cell(), "the blocked guard waits — no step commits");
        assertEquals(cell(11, 10), blocker.cell(), "the on-duty guard is never displaced");
        assertEquals(0, occ.log.size(), "no shove row — the pairing was refused, not lost");
        assertEquals(-PushMechanics.PUSH_COOLDOWN_TICKS, pusher.lastPushTick(),
                "no cooldown burned on a refused pairing (the step retries freely)");
    }

    @Test
    void aGuardStillShovesACivilianAndACivilianStillShovesAGuard() {
        ActorRegistry registry = new ActorRegistry();
        NoOpActorContext ctx = new NoOpActorContext(registry);
        Actor guard = watchAt(registry, ctx, cell(10, 10));
        Actor idler = registry.spawn(Wastrel.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Wastrel.TYPE, true, 1, 64), cell(11, 10));
        EtiquetteOccupancy occ = new EtiquetteOccupancy(registry, watchEtiquette(ctx));

        guard.stepToward(cell(12, 10), false, c -> true, occ);
        assertEquals(cell(11, 10), guard.cell(), "guard-vs-civilian shove still lands");
        assertNotEquals(cell(11, 10), idler.cell(), "the civilian was displaced");

        // And the reverse pairing (civilian shoving a guard) is also legal.
        ActorRegistry registry2 = new ActorRegistry();
        NoOpActorContext ctx2 = new NoOpActorContext(registry2);
        Actor guard2 = watchAt(registry2, ctx2, cell(21, 10));
        Actor pushy = registry2.spawn(Wastrel.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Wastrel.TYPE, true, 1, 64), cell(20, 10));
        EtiquetteOccupancy occ2 = new EtiquetteOccupancy(registry2, watchEtiquette(ctx2));
        pushy.stepToward(cell(22, 10), false, c -> true, occ2);
        assertEquals(cell(21, 10), pushy.cell(), "civilian-vs-guard shove still lands");
    }

    @Test
    void aBlockedRoutePatrolYieldsTheLegAfterTheBoundedWait() {
        ActorRegistry registry = new ActorRegistry();
        int w1 = cell(10, 10);
        int w2 = cell(14, 10);
        int w3 = cell(10, 14);
        PatrolRouteTable routes = PatrolRouteTable.of(
                java.util.List.of(java.util.List.of(w1, w2, w3)));
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public PatrolRouteTable patrolRoutes() {
                return routes;
            }

            @Override
            public boolean isWalkable(int c) {
                // A 1-wide corridor along y=10: the only path to w2 runs through the blocker.
                return PackedPos.z(c) == Z && PackedPos.y(c) == 10
                        && PackedPos.x(c) >= 5 && PackedPos.x(c) <= 20;
            }

            @Override
            public Actor.OccupancyQuery occupancy() {
                // Every cell but the ones held reads free; the blocker's cell is full and
                // tryPush always refuses (the etiquette shape) — a permanent human wall.
                ActorRegistry reg = registry();
                return new Actor.OccupancyQuery() {
                    @Override
                    public int occupantsAt(int cell) {
                        int count = 0;
                        for (int i = 0; i < reg.size(); i++) {
                            if (reg.get(i).cell() == cell) {
                                count++;
                            }
                        }
                        return count;
                    }

                    @Override
                    public void onEnter(int fromCell, int toCell) {
                    }
                };
            }
        };
        Actor patroller = watchAt(registry, ctx, w1);
        // A fellow guard stands mid-corridor: the only cell toward w2, held by someone the
        // etiquette forbids shoving — a permanent human wall until the beat yields.
        watchAt(registry, ctx, cell(11, 10));

        Job patrol = ctx.jobs().get(ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
        patroller.setGoalProgress((short) 0);
        patrol.pursue(patroller, ctx); // at w1: arrival, advance to the w2 leg
        assertEquals(1, Math.floorMod(patroller.goalProgress(), 3));

        // The w2 leg is human-walled: after the bounded yield budget the beat gives it up.
        boolean yielded = false;
        for (int i = 0; i < JobBehaviors.PATROL_BLOCKED_YIELD_TICKS + 20 && !yielded; i++) {
            patrol.pursue(patroller, ctx);
            yielded = Math.floorMod(patroller.goalProgress(), 3) == 2;
        }
        assertTrue(yielded, "a dead-still leg is yielded, not wrestled forever");
        assertFalse(patroller.cell() == w2, "the contested leg was walked away from");
    }

    @Test
    void theProgressYieldDidNotLengthenTheDeadStillWait() {
        // S7 round 3 re-aimed the stall clock from stillness to PROGRESS toward the waypoint,
        // and folded two budgets onto one already-persisted scalar by weighting a motionless
        // tick. The arithmetic is the whole safety of that trick: if the weight and the
        // budgets ever drift apart, the S6 dead-still yield silently gets slower -- a guard
        // that used to give way after 40 motionless ticks would wrestle for 200, and the only
        // symptom would be a jam metric creeping up months later. Pin it.
        //
        // S7 round 4: the round-3 version of this test asserted A == B * (A / B), which is
        // integer-divisibility and NOTHING else -- it passed while the javadoc beside it
        // claimed a wedged guard "yields on its 40th motionless tick", which the id de-phase
        // had already made false. It now measures the SHIPPING function over a whole band of
        // ids, so a wrong claim about the wait cannot pass.
        int weight = JobBehaviors.PATROL_NO_PROGRESS_YIELD_TICKS
                / JobBehaviors.PATROL_BLOCKED_YIELD_TICKS;
        assertEquals(JobBehaviors.PATROL_NO_PROGRESS_YIELD_TICKS,
                JobBehaviors.PATROL_BLOCKED_YIELD_TICKS * weight,
                "the no-progress budget must be an exact multiple of the dead-still budget:"
                        + " the still-tick weight is their integer ratio, so a remainder means"
                        + " a wedged guard waits longer than S6's 40 ticks");
        assertTrue(JobBehaviors.PATROL_NO_PROGRESS_YIELD_TICKS
                        > JobBehaviors.PATROL_BLOCKED_YIELD_TICKS,
                "a soul that is at least MOVING gets more rope than one standing dead still --"
                        + " route legs really do have to walk away from a waypoint sometimes");

        // The real claim, measured: min 22 / max 58 motionless ticks, MEAN exactly S6's 40.
        int min = Integer.MAX_VALUE;
        int max = 0;
        long sum = 0;
        int band = 0;
        for (int id = 0; id < 37; id++) {
            int wait = JobBehaviors.patrolMotionlessTicksToYield(id);
            min = Math.min(min, wait);
            max = Math.max(max, wait);
            sum += wait;
            band++;
        }
        assertEquals(22, min, "the earliest-yielding soul in the band");
        assertEquals(58, max, "the latest-yielding soul in the band -- the wait stays bounded");
        assertEquals(JobBehaviors.PATROL_BLOCKED_YIELD_TICKS, (int) (sum / band),
                "the de-phase is CENTRED on S6's 40 motionless ticks, not stacked on top of it");
    }

    @Test
    void theDePhaseSeparatesAdjacentSoulsByAWholeMotionlessTick() {
        // S7 round 4 (the round-3 de-phase was quantized away in exactly the case it was
        // written for). The budget is denominated in stall UNITS and a motionless tick is
        // worth five of them, so round 3's raw floorMod(id, 37) moved neighbouring souls by
        // a FIFTH of a tick -- which rounds to zero. Two consecutively-spawned guards wedged
        // against each other (the garrison pair; the two Saltgate walkers) therefore still
        // yielded on the very same tick, both turned, and re-met in lockstep: the polite
        // deadlock the de-phase exists to prevent. Adjacent ids must differ by a WHOLE tick.
        for (int id = 0; id < 36; id++) {
            assertEquals(JobBehaviors.patrolMotionlessTicksToYield(id) + 1,
                    JobBehaviors.patrolMotionlessTicksToYield(id + 1),
                    "souls #" + id + " and #" + (id + 1) + " must not yield on the same tick");
        }
        // And no two of the ward's nineteen Watch (consecutive ids) share a yield tick at all.
        java.util.HashSet<Integer> waits = new java.util.HashSet<>();
        for (int id = 371; id <= 389; id++) {
            assertTrue(waits.add(JobBehaviors.patrolMotionlessTicksToYield(id)),
                    "watch soul #" + id + " shares its yield tick with another guard");
        }
    }

    @Test
    void aWedgedSquareBeatGivesUpTheLegOnItsOwnDePhasedTick() {
        // S7 round 4, defect 2: the blind square beat -- walked by 13 of the 19 Watch -- gets
        // the same progress-yield the route beat got in round 3, on the same shared budget.
        // This pins the DEAD-STILL half of it against the arithmetic, on the square beat.
        ActorRegistry registry = new ActorRegistry();
        int anchor = cell(10, 10);
        int corner = cell(16, 16); // leg 0 = (+r,+r) at BEAT_RADIUS 6 -- walkable but UNREACHABLE
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public boolean isWalkable(int c) {
                return c == anchor || c == corner;
            }
        };
        Actor guard = watchAt(registry, ctx, anchor); // id 0
        JobBehaviors.selectRouteStart(guard, ctx);    // no route table: square beat, leg 0
        assertEquals(0, Math.floorMod(guard.goalProgress(), 4));

        int wait = JobBehaviors.patrolMotionlessTicksToYield(guard.id());
        // Tick 1 marks the leg's high-water distance; ticks 2..wait+1 are the motionless wait.
        for (int i = 0; i < wait; i++) {
            JobBehaviors.pursuePatrol(guard, ctx, 6, params(ctx));
            assertEquals(0, Math.floorMod(guard.goalProgress(), 4),
                    "the leg is still being worked at motionless tick " + i);
        }
        JobBehaviors.pursuePatrol(guard, ctx, 6, params(ctx));
        assertEquals(1, Math.floorMod(guard.goalProgress(), 4),
                "the wedged square-beat leg is given up on the soul's own de-phased tick");
        assertEquals(anchor, guard.cell(), "it never moved -- this is the dead-still branch");
    }

    @Test
    void aSquareBeatThatKeepsMovingWithoutClosingStillYields() {
        // The live-lock the stillness counter could not see, on the square beat. The guard
        // walks a 1-wide snake every single tick and never once beats its high-water distance
        // to the corner; S6's rule zeroed the clock on every one of those steps, so it would
        // have wrestled the leg forever. The progress rule gives it up on the budget.
        ActorRegistry registry = new ActorRegistry();
        int anchor = cell(10, 10);
        int corner = cell(16, 16);
        // A 1-wide snake that reaches the corner only the LONG way round: 171 steps of which
        // the first 160 never beat the leg's opening distance of 6. No self-intersections, so
        // the bounded A* has exactly one route and the walk is forced.
        java.util.HashSet<Integer> snake = new java.util.HashSet<>();
        for (int x = 0; x <= 10; x++) {           // west along y=10, away from the corner
            snake.add(cell(x, 10));
        }
        for (int y = 10; y <= 44; y++) {          // south down x=0
            snake.add(cell(0, y));
        }
        for (int x = 0; x <= 44; x++) {           // east along y=44
            snake.add(cell(x, 44));
        }
        for (int y = 0; y <= 44; y++) {           // north up x=44
            snake.add(cell(44, y));
        }
        for (int x = 16; x <= 44; x++) {          // west along y=0
            snake.add(cell(x, 0));
        }
        for (int y = 0; y <= 16; y++) {           // and finally south down x=16 to the corner
            snake.add(cell(16, y));
        }
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public boolean isWalkable(int c) {
                return snake.contains(c);
            }
        };
        Actor guard = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(MilitiaWatch.TYPE, true, 1, 4096),
                anchor);
        guard.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
        JobBehaviors.selectRouteStart(guard, ctx);

        int moved = 0;
        int ticks = 0;
        boolean yielded = false;
        for (int i = 0; i < 400 && !yielded; i++) {
            int before = guard.cell();
            JobBehaviors.pursuePatrol(guard, ctx, 6, params(ctx));
            ticks++;
            moved += guard.cell() != before ? 1 : 0;
            yielded = Math.floorMod(guard.goalProgress(), 4) != 0;
        }
        assertTrue(yielded, "a leg that never closes is given up even while the soul is moving");
        assertNotEquals(corner, guard.cell(), "it never reached the corner -- it walked away");
        assertTrue(moved > ticks / 2,
                "the soul was MOVING for most of the leg (" + moved + " of " + ticks
                        + ") -- a stillness counter would have read zero the whole time");
    }

    @Test
    void theSquareBeatsHighWaterMarkRidesBesideTheLegIndex() {
        // No new persisted state: the mark lives in the free high bits of goalProgress, whose
        // low two bits are the leg. If a future edit ever reads goalProgress raw for the
        // square beat instead of floorMod(_, 4), this is the test that says so.
        ActorRegistry registry = new ActorRegistry();
        int anchor = cell(10, 10);
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public boolean isWalkable(int c) {
                return PackedPos.z(c) == Z;
            }
        };
        Actor guard = watchAt(registry, ctx, anchor);
        JobBehaviors.selectRouteStart(guard, ctx);
        int leg = Math.floorMod(guard.goalProgress(), 4);

        JobBehaviors.pursuePatrol(guard, ctx, 6, params(ctx)); // steps once, closing
        assertEquals(leg, Math.floorMod(guard.goalProgress(), 4),
                "marking progress must not disturb the leg index");
        assertTrue(guard.goalProgress() >> 2 > 0, "the leg marked its high-water distance");
        assertEquals(0, guard.goalWorkTicks(), "closing ground zeroes the stall clock");

        for (int i = 0; i < 12; i++) { // open ground: it reaches the corner and rolls the leg
            JobBehaviors.pursuePatrol(guard, ctx, 6, params(ctx));
        }
        assertNotEquals(leg, Math.floorMod(guard.goalProgress(), 4), "the beat looped a corner");
        assertTrue(guard.goalProgress() >= 0 && guard.goalProgress() < 16384,
                "the packed progress word stays inside the short");
    }

    private static com.trojia.sim.actor.job.JobParams params(NoOpActorContext ctx) {
        return ctx.jobs().get(ctx.jobs().ordinalOf(Job.Watch.Patrol.ID)).params();
    }

    @Test
    void sameRouteWatchStartAtIdStaggeredWaypoints() {
        ActorRegistry registry = new ActorRegistry();
        int w1 = cell(10, 10);
        int w2 = cell(14, 10);
        int w3 = cell(10, 14);
        PatrolRouteTable routes = PatrolRouteTable.of(
                java.util.List.of(java.util.List.of(w1, w2, w3)));
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public PatrolRouteTable patrolRoutes() {
                return routes;
            }
        };
        Actor a = watchAt(registry, ctx, w1); // id 0
        Actor b = watchAt(registry, ctx, w1); // id 1 — same anchor, same route

        JobBehaviors.selectRouteStart(a, ctx);
        JobBehaviors.selectRouteStart(b, ctx);
        assertEquals(0, Math.floorMod(a.goalProgress(), 3));
        assertEquals(1, Math.floorMod(b.goalProgress(), 3));
        assertNotEquals(Math.floorMod(a.goalProgress(), 3), Math.floorMod(b.goalProgress(), 3),
                "same-route watch start out of phase (the lockstep pile-up fix)");
    }

    @Test
    void offRouteSquareBeatsStaggerTheStartingCorner() {
        ActorRegistry registry = new ActorRegistry();
        NoOpActorContext ctx = new NoOpActorContext(registry);
        Actor a = watchAt(registry, ctx, cell(40, 40)); // id 0
        Actor b = watchAt(registry, ctx, cell(41, 40)); // id 1 — the garrison-pair shape

        JobBehaviors.selectRouteStart(a, ctx);
        JobBehaviors.selectRouteStart(b, ctx);
        assertNotEquals(Math.floorMod(a.goalProgress(), 4), Math.floorMod(b.goalProgress(), 4),
                "adjacent-anchor square beats start on different corners");
    }
}
