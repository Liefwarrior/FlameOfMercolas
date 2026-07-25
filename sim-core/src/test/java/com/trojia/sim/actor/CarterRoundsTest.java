package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.actor.job.JobParams;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 6 slice 3 (Eli's bug 4 — "get those extra laborers working and moving around the
 * city"): the {@code serf.carter} multi-stop rounds. A carter bound (by anchor) to a baked
 * circuit visits its stops IN ORDER with one {@code workTicksPerUnit} dwell of honest work
 * per stop, wrapping forever through the shift; crews staggered by id start at different
 * stops; off shift it heads home; and on an unwired table the carter degrades to the plain
 * laborer anchor cycle (pre-rounds bakes stay byte-identical).
 */
final class CarterRoundsTest {

    private static final int Z = 11;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /** Ctx double with a synthetic rounds table, pinned to a work-window tick. */
    private static final class RoundsContext extends NoOpActorContext {
        private PatrolRouteTable rounds = PatrolRouteTable.EMPTY;

        RoundsContext(ActorRegistry registry) {
            super(registry);
            setTick(5_000); // tod 5000: inside serf.carter's [1000, 11000) shift
        }

        @Override
        public PatrolRouteTable workRounds() {
            return rounds;
        }
    }

    private static Actor carterAt(ActorRegistry registry, ActorContext ctx, int at) {
        Actor carter = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, 64), at);
        carter.setAnchorCell(at);
        carter.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Serf.Carter.ID));
        return carter;
    }

    private static Job carterJob(ActorContext ctx) {
        return ctx.jobs().get(ctx.jobs().ordinalOf(Job.Serf.Carter.ID));
    }

    private static JobParams carterParams(ActorContext ctx) {
        return carterJob(ctx).params();
    }

    @Test
    void visitsTheCircuitStopsInOrderWithADwellAtEach() {
        ActorRegistry registry = new ActorRegistry();
        RoundsContext ctx = new RoundsContext(registry);
        int warehouse = cell(10, 10);
        int quay = cell(16, 10);
        int store = cell(10, 16);
        ctx.rounds = PatrolRouteTable.of(List.of(List.of(warehouse, quay, store)));
        Actor carter = carterAt(registry, ctx, warehouse);
        Job job = carterJob(ctx);
        int dwell = carterParams(ctx).workTicksPerUnit();

        job.selectTarget(carter, ctx); // id 0 -> starting stop 0 (the warehouse it stands on)
        List<Integer> workedStops = new ArrayList<>();
        int lastProgress = Math.floorMod(carter.goalProgress(), 3);
        for (int i = 0; i < 6 * (dwell + 20); i++) {
            int stopBefore = ctx.rounds.waypoint(0, Math.floorMod(carter.goalProgress(), 3));
            job.pursue(carter, ctx);
            int progress = Math.floorMod(carter.goalProgress(), 3);
            if (progress != lastProgress) {
                // The stop only advances when its dwell WORKED to completion — within
                // WORK_REACH of the stop (the crowded-anchor rule).
                assertTrue(ActorGeometry.chebyshev(carter.cell(), stopBefore)
                        <= JobBehaviors.WORK_REACH, "worked within reach of the stop");
                workedStops.add(stopBefore);
                lastProgress = progress;
            }
        }
        assertTrue(workedStops.size() >= 4, "several stops must have been worked: " + workedStops);
        assertEquals(List.of(warehouse, quay, store, warehouse), workedStops.subList(0, 4),
                "stops worked IN ORDER, wrapping back around (the circuit)");
    }

    @Test
    void crewmatesStartAtIdStaggeredStops() {
        ActorRegistry registry = new ActorRegistry();
        RoundsContext ctx = new RoundsContext(registry);
        int a = cell(10, 10);
        int b = cell(16, 10);
        int c = cell(10, 16);
        ctx.rounds = PatrolRouteTable.of(List.of(List.of(a, b, c)));
        Actor first = carterAt(registry, ctx, a);  // id 0
        Actor second = carterAt(registry, ctx, a); // id 1 — same circuit anchor

        JobBehaviors.selectRoundsStart(first, ctx);
        JobBehaviors.selectRoundsStart(second, ctx);
        assertNotEquals(Math.floorMod(first.goalProgress(), 3),
                Math.floorMod(second.goalProgress(), 3),
                "crewmates fan out across the circuit instead of marching in lockstep");
    }

    @Test
    void offShiftTheCarterHeadsHome() {
        ActorRegistry registry = new ActorRegistry();
        RoundsContext ctx = new RoundsContext(registry);
        int stop = cell(10, 10);
        int home = cell(30, 30);
        ctx.rounds = PatrolRouteTable.of(List.of(List.of(stop, cell(16, 10))));
        Actor carter = carterAt(registry, ctx, stop);
        int homeId = ctx.homes().addHome(home);
        carter.setHomeId(homeId);
        ctx.setTick(20_000); // deep night: outside [1000, 11000)

        Job job = carterJob(ctx);
        job.selectTarget(carter, ctx);
        for (int i = 0; i < 60 && carter.cell() != home; i++) {
            job.pursue(carter, ctx);
        }
        assertEquals(home, carter.cell(), "off shift the rounds head home, not to a stop");
    }

    @Test
    void anUnwiredTableDegradesToThePlainAnchorCycle() {
        ActorRegistry registry = new ActorRegistry();
        RoundsContext ctx = new RoundsContext(registry); // rounds = EMPTY
        int anchor = cell(10, 10);
        Actor carter = carterAt(registry, ctx, anchor);
        Job job = carterJob(ctx);
        int dwell = carterParams(ctx).workTicksPerUnit();

        job.selectTarget(carter, ctx);
        short before = carter.goalProgress();
        for (int i = 0; i < dwell; i++) {
            job.pursue(carter, ctx);
        }
        assertEquals(anchor, carter.cell(), "no circuit: the carter works its anchor in place");
        assertEquals(before + 1, carter.goalProgress(),
                "…accruing plain anchor-cycle work units (the laborer shape)");
    }
}
