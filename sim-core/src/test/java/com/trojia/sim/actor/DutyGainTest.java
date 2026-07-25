package com.trojia.sim.actor;

import com.trojia.sim.actor.job.GoalKind;
import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.actor.job.JobParams;
import com.trojia.sim.actor.job.RenewMode;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Sprint 6 slice 1 (Eli's bug 1 — "no one has a way to gain DUTY so it bottoms out"): honest
 * work at one's station refills DUTY. The {@code dutyPerUnit} raws pair lands at exactly the
 * training pair's three discrete-work-event seams — anchor work-unit completion, patrol
 * waypoint/corner ARRIVAL, wander dwell completion — and NEVER per traveling/accruing tick.
 * Draw-free beyond the wander's own named target draws; a 0 {@code dutyPerUnit} (the whole
 * pre-S6 universe) is byte-identical to before.
 */
final class DutyGainTest {

    private static final int Z = 10;
    private static final int ANCHOR = PackedPos.pack(50, 50, Z);
    private static final int DUTY_PER_UNIT = 77;

    /** All-day window, {@code workTicksPerUnit = 2}: unit completes on the second in-place tick. */
    private static JobParams anchorParams(int dutyPerUnit) {
        return new JobParams(GoalKind.HAUL_WORK, 150, 0, 24_000, 0, 2, 5,
                RenewMode.IMMEDIATE, 0, JobParams.TRAINS_NOTHING, 0, dutyPerUnit);
    }

    @Test
    void anchorWorkUnitCompletionRestoresDuty() {
        ActorRegistry registry = new ActorRegistry();
        Actor worker = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), ANCHOR);
        worker.setAnchorCell(ANCHOR);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        JobParams params = anchorParams(DUTY_PER_UNIT);

        int before = worker.need(Need.DUTY);
        JobBehaviors.pursueAtAnchor(worker, ctx, params);
        assertEquals(before, worker.need(Need.DUTY),
                "an accruing (not yet completed) work tick must award no DUTY");
        JobBehaviors.pursueAtAnchor(worker, ctx, params);
        assertEquals(before + DUTY_PER_UNIT, worker.need(Need.DUTY),
                "the completed work-unit is the discrete event that restores dutyPerUnit");
    }

    @Test
    void zeroDutyPerUnitIsByteIdenticalToBefore() {
        ActorRegistry registry = new ActorRegistry();
        Actor worker = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), ANCHOR);
        worker.setAnchorCell(ANCHOR);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        JobParams params = anchorParams(0);

        int before = worker.need(Need.DUTY);
        JobBehaviors.pursueAtAnchor(worker, ctx, params);
        JobBehaviors.pursueAtAnchor(worker, ctx, params);
        assertEquals(before, worker.need(Need.DUTY),
                "dutyPerUnit 0 (every pre-S6 job) must change nothing");
    }

    @Test
    void routeWaypointArrivalRestoresDuty() {
        ActorRegistry registry = new ActorRegistry();
        int waypointA = PackedPos.pack(10, 10, Z);
        int waypointB = PackedPos.pack(14, 10, Z);
        Actor guard = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.stats(MilitiaWatch.TYPE), waypointA);
        guard.setAnchorCell(waypointA);
        PatrolRouteTable routes = PatrolRouteTable.of(
                List.of(List.of(waypointA, waypointB)));
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public PatrolRouteTable patrolRoutes() {
                return routes;
            }
        };
        JobParams params = new JobParams(GoalKind.PATROL_ROUTE, 150, 0, 24_000, 0, 40, 5,
                RenewMode.IMMEDIATE, 0, JobParams.TRAINS_NOTHING, 0, DUTY_PER_UNIT);

        guard.setGoalProgress((short) 0); // current waypoint index = 0 = the cell it stands on
        int before = guard.need(Need.DUTY);
        JobBehaviors.pursueRoutePatrol(guard, ctx, 0, params);
        assertEquals(before + DUTY_PER_UNIT, guard.need(Need.DUTY),
                "waypoint ARRIVAL is the route patrol's discrete work event");
        // The leg toward the next waypoint awards nothing while traveling.
        int mid = guard.need(Need.DUTY);
        JobBehaviors.pursueRoutePatrol(guard, ctx, 0, params);
        assertEquals(mid, guard.need(Need.DUTY),
                "a traveling patrol tick must award no DUTY");
    }

    @Test
    void wanderDwellCompletionRestoresDuty() {
        ActorRegistry registry = new ActorRegistry();
        Actor keeper = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), ANCHOR);
        keeper.setAnchorCell(ANCHOR);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        JobParams params = new JobParams(GoalKind.TEND_BEASTS, 150, 0, 24_000, 0, 1, 3,
                RenewMode.IMMEDIATE, 0, JobParams.TRAINS_NOTHING, 0, DUTY_PER_UNIT);

        // Stand on the dwell target: the next pursue completes the 1-tick dwell and awards.
        keeper.setGoalTarget(TargetKind.CELL, ANCHOR);
        keeper.setGoalWorkTicks(0);
        int before = keeper.need(Need.DUTY);
        JobBehaviors.pursueWander(keeper, ctx, params);
        assertEquals(before + DUTY_PER_UNIT, keeper.need(Need.DUTY),
                "the completed dwell is the wander sweep's discrete work event");
    }
}
