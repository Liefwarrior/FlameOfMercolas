package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 4 "the climb": the cross-z-capable {@link Actor#stepAlongRoute(int, boolean,
 * Actor.WalkabilityQuery, Actor.OccupancyQuery, ZLinkTable)} overload and the
 * {@link Actor#tryStepVertical} commit — stair climbs, ramp exits, descent, the EMPTY-table
 * no-op parity (the old z-rule), the unroutable-climb failure cache, the stair-funnel
 * occupancy cap, the connector-only guard, and the no-persisted-state mid-climb resume.
 */
final class ActorCrossZStepTest {

    private static final Actor.WalkabilityQuery OPEN = cell -> true;

    private static int cell(int x, int y, int z) {
        return PackedPos.pack(x, y, z);
    }

    private static Actor serfAt(ActorRegistry registry, int at) {
        return registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, 200), at);
    }

    /** A z9/z10 stair at (10,10) and a z10/z11 stair at (14,10). */
    private static ZLinkTable twoBandStairs() {
        return new ZLinkTable(
                new int[] {cell(10, 10, 9), cell(14, 10, 10)},
                new int[] {cell(10, 10, 10), cell(14, 10, 11)});
    }

    @Test
    void aTwoBandStairClimbArrivesAndDescentComesBack() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, cell(6, 10, 9));
        int target = cell(18, 10, 11);
        ZLinkTable links = twoBandStairs();

        List<Integer> zTrail = new ArrayList<>();
        for (int i = 0; i < 60 && actor.cell() != target; i++) {
            actor.stepAlongRoute(target, true, OPEN, Actor.OccupancyQuery.UNLIMITED, links);
            zTrail.add(PackedPos.z(actor.cell()));
        }
        assertEquals(target, actor.cell(), "the climb must arrive; z trail " + zTrail);
        assertTrue(zTrail.contains(10), "the climb passes through the middle band");

        int home = cell(6, 10, 9);
        for (int i = 0; i < 60 && actor.cell() != home; i++) {
            actor.stepAlongRoute(home, true, OPEN, Actor.OccupancyQuery.UNLIMITED, links);
        }
        assertEquals(home, actor.cell(), "the descent mirrors the climb");
    }

    @Test
    void aRampExitClimbsDiagonallyAcrossTheLink() {
        ActorRegistry registry = new ActorRegistry();
        // The docks ramp shape: ramp at (10,20,z9), exit floor at (10,21,z10).
        ZLinkTable links = new ZLinkTable(
                new int[] {cell(10, 20, 9)}, new int[] {cell(10, 21, 10)});
        Actor actor = serfAt(registry, cell(10, 16, 9));
        int target = cell(10, 26, 10);
        for (int i = 0; i < 40 && actor.cell() != target; i++) {
            actor.stepAlongRoute(target, true, OPEN, Actor.OccupancyQuery.UNLIMITED, links);
        }
        assertEquals(target, actor.cell(), "the ramp exit carries the diagonal band change");
    }

    @Test
    void theEmptyTableKeepsTheOldZRuleNoOp() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, cell(6, 10, 9));
        int target = cell(18, 10, 11);
        for (int i = 0; i < 20; i++) {
            actor.stepAlongRoute(target, true, OPEN, Actor.OccupancyQuery.UNLIMITED,
                    ZLinkTable.EMPTY);
        }
        assertEquals(cell(6, 10, 9), actor.cell(),
                "no connectors wired: a cross-z target stays a deterministic no-op");
        assertFalse(actor.routeFailedTo(target),
                "the EMPTY-table no-op writes nothing, exactly like the four-arg overload");
    }

    @Test
    void anUnroutableClimbLandsInTheRouteFailureCacheWithTheBoundedCooldown() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, cell(6, 10, 9));
        int target = cell(18, 10, 11); // needs a z10/z11 crossing the table lacks
        ZLinkTable oneBand = new ZLinkTable(
                new int[] {cell(10, 10, 9)}, new int[] {cell(10, 10, 10)});

        actor.stepAlongRoute(target, true, OPEN, Actor.OccupancyQuery.UNLIMITED, oneBand);
        assertEquals(cell(6, 10, 9), actor.cell(), "unroutable: no movement");
        assertTrue(actor.routeFailedTo(target),
                "the unroutable verdict rides the existing route-failure cache");

        // The cooldown throttles re-verdicts, and movement stays a no-op meanwhile.
        for (int i = 0; i < 10; i++) {
            actor.stepAlongRoute(target, true, OPEN, Actor.OccupancyQuery.UNLIMITED, oneBand);
        }
        assertEquals(cell(6, 10, 9), actor.cell());
        assertTrue(actor.routeFailedTo(target));
    }

    @Test
    void theStairHeadIsAOnePerSquareFunnel() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, cell(10, 10, 9));
        int stairHead = cell(10, 10, 10);
        ZLinkTable links = new ZLinkTable(new int[] {cell(10, 10, 9)}, new int[] {stairHead});
        // A full stair head (occupancy at the cap, shove refused) blocks the commit...
        Actor.OccupancyQuery full = new Actor.OccupancyQuery() {
            @Override
            public int occupantsAt(int cell) {
                return cell == stairHead ? Actor.MAX_OCCUPANTS_PER_CELL : 0;
            }

            @Override
            public void onEnter(int fromCell, int toCell) {
            }
        };
        assertFalse(actor.tryStepVertical(stairHead, true, OPEN, full, links));
        assertEquals(cell(10, 10, 9), actor.cell(), "a full stair head blocks like a wall");
        // ...and a free one carries it.
        assertTrue(actor.tryStepVertical(stairHead, true, OPEN,
                Actor.OccupancyQuery.UNLIMITED, links));
        assertEquals(stairHead, actor.cell());
    }

    @Test
    void onlyABakedConnectorCarriesAVerticalStep() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, cell(10, 10, 9));
        ZLinkTable links = new ZLinkTable(
                new int[] {cell(20, 20, 9)}, new int[] {cell(20, 20, 10)});
        // A cross-z destination that is no baked link (a stale/mis-aimed player intent).
        assertFalse(actor.tryStepVertical(cell(10, 10, 10), true, OPEN,
                Actor.OccupancyQuery.UNLIMITED, links));
        assertEquals(cell(10, 10, 9), actor.cell(),
                "an arbitrary cross-z destination is a deterministic no-op");
    }

    /**
     * The save/load-mid-climb property, at the unit level: the climb keeps NO persisted
     * route state (the hop is a pure function of (cell, target, table); the same-z leg
     * cache is documented derived/recomputable), so a fresh actor resumed at the same cell
     * with the same id walks the identical remaining trajectory the uninterrupted climb
     * walks — exactly what the ACTORS-chunk load reproduces.
     */
    @Test
    void aMidClimbResumeWalksTheIdenticalRemainingTrajectory() {
        int start = cell(6, 10, 9);
        int target = cell(18, 10, 11);
        ZLinkTable links = twoBandStairs();

        // The uninterrupted reference run, recording every post-step cell.
        Actor reference = serfAt(new ActorRegistry(), start);
        List<Integer> uninterrupted = new ArrayList<>();
        for (int i = 0; i < 60 && reference.cell() != target; i++) {
            reference.stepAlongRoute(target, true, OPEN, Actor.OccupancyQuery.UNLIMITED, links);
            uninterrupted.add(reference.cell());
        }
        assertEquals(target, reference.cell());

        // The interrupted run: climb 5 ticks, then "reload" — a fresh actor (same id 0,
        // fresh derived caches, exactly the loaded-actor shape) resumed at the same cell.
        Actor firstHalf = serfAt(new ActorRegistry(), start);
        for (int i = 0; i < 5; i++) {
            firstHalf.stepAlongRoute(target, true, OPEN, Actor.OccupancyQuery.UNLIMITED, links);
        }
        assertEquals(uninterrupted.get(4), firstHalf.cell(), "same first half");
        Actor resumed = serfAt(new ActorRegistry(), firstHalf.cell());
        List<Integer> tail = new ArrayList<>();
        for (int i = 0; i < 60 && resumed.cell() != target; i++) {
            resumed.stepAlongRoute(target, true, OPEN, Actor.OccupancyQuery.UNLIMITED, links);
            tail.add(resumed.cell());
        }
        assertEquals(uninterrupted.subList(5, uninterrupted.size()), tail,
                "the resumed climb is byte-identical to the uninterrupted one");
    }
}
