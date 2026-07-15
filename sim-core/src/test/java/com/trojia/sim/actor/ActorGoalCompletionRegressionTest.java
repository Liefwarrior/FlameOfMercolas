package com.trojia.sim.actor;

import com.trojia.sim.actor.job.GoalKind;
import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.actor.job.JobParams;
import com.trojia.sim.actor.job.RenewMode;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Eli's 2026-07-15 directive verbatim reproductions: "People walk in circles or walk back and
 * forth... Make sure all goals can be completed." Each test here reproduces one of the
 * diagnosis's three traced permanently-stuck classes (a serf's axis-aligned approach to a
 * hovel whose door sits on the wrong wall face, a Militia Watch patrol corner behind a wide
 * obstacle, and a commute blocked by a "table pocket") with a hand-built walkability maze,
 * proves the OLD greedy {@link Actor#stepToward} really does get permanently/decisively stuck
 * on it (the sanity half of each test), and then proves the fixed
 * {@link Actor#stepAlongRoute} — now wired into {@code ReturnHomePolicy}/{@code SeekFoodPolicy},
 * {@code JobBehaviors.pursuePatrol}, and {@code JobBehaviors.pursueAtAnchor} respectively —
 * actually completes the goal.
 */
final class ActorGoalCompletionRegressionTest {

    private static final int Z = 9;

    // ======================================================================
    // Case 1 (axis-aligned stuck-near-home): a hovel whose only door is on a
    // wall face perpendicular to the actor's straight-line approach.
    // stepToward's wall-slide only ever fires on a blocked DIAGONAL primary
    // step (dx != 0 && dy != 0) — a straight (axis-aligned) approach has zero
    // fallback and freezes forever.
    // ======================================================================

    @Test
    void axisAlignedApproachToADoorOnTheWrongWallFaceNoLongerGetsPermanentlyStuck() {
        // The box sits well away from the world origin (not near x/y == 0) so the route around
        // to the door — which passes one row "above" the box's north face — is not clipped by
        // PathFinder's bounding-box clamp to non-negative coordinates.
        int boxMin = 100;
        int boxMax = 110;
        int doorX = 105;
        Actor.WalkabilityQuery walk = cell -> {
            int x = PackedPos.x(cell);
            int y = PackedPos.y(cell);
            boolean edgeX = (x == boxMin || x == boxMax) && y >= boxMin && y <= boxMax;
            boolean edgeY = (y == boxMin || y == boxMax) && x >= boxMin && x <= boxMax;
            boolean isDoor = x == doorX && y == boxMin; // the one door, on the NORTH face
            return !((edgeX || edgeY) && !isDoor);
        };
        ActorRegistry registry = new ActorRegistry();
        ActorTypeStats stats = ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, 200);
        int home = PackedPos.pack(doorX, 105, Z); // interior, same row (y=105) as the approach below

        Actor stuckProbe = registry.spawn(Serf.TYPE, stats, PackedPos.pack(115, 105, Z));
        for (int i = 0; i < 50; i++) {
            stuckProbe.stepToward(home, true, walk); // the OLD greedy-only mover
        }
        assertTrue(ActorGeometry.chebyshev(stuckProbe.cell(), home) > 1,
                "sanity: the old greedy mover is permanently stuck on a wrong-face door approach "
                        + "(halted adjacent to the wall at " + describe(stuckProbe.cell()) + ")");

        Actor fixed = registry.spawn(Serf.TYPE, stats, PackedPos.pack(115, 105, Z));
        for (int i = 0; i < 200 && fixed.cell() != home; i++) {
            fixed.stepAlongRoute(home, true, walk);
        }
        assertEquals(home, fixed.cell(), "the fixed mover must route around to the door and arrive");
    }

    // ======================================================================
    // Case 2 (Militia Watch patrol-corner stuck): a wide obstacle straddles
    // the diagonal approach to a patrol corner, with its one gap positioned
    // BEYOND the corner's own coordinate — greedy's per-tick dx/dy is always
    // recomputed toward reducing distance to the (fixed) corner, so it can
    // never choose a step that overshoots past it to find the gap: a
    // provable permanent freeze, not just an inefficient hug.
    // ======================================================================

    @Test
    void patrolCornerBehindAWideObstacleNoLongerGetsPermanentlyStuck() {
        int wallY = 60;
        Actor.WalkabilityQuery walk = cell -> {
            int x = PackedPos.x(cell);
            int y = PackedPos.y(cell);
            return !(y == wallY && x >= 45 && x <= 74); // solid; the only gap is at x=75
        };
        int anchor = PackedPos.pack(50, 50, Z);
        int corner0 = PackedPos.pack(65, 65, Z); // leg 0 = anchor + radius(15)*(1,1)

        ActorRegistry registry = new ActorRegistry();
        ActorTypeStats stats = ActorTestFixtures.statsWithSpeedAndLeash(MilitiaWatch.TYPE, true, 1, 40);

        Actor stuckProbe = registry.spawn(MilitiaWatch.TYPE, stats, anchor);
        for (int i = 0; i < 100; i++) {
            stuckProbe.stepToward(corner0, false, walk); // the OLD greedy-only mover
        }
        assertTrue(ActorGeometry.chebyshev(stuckProbe.cell(), corner0) > 1,
                "sanity: the old greedy mover cannot pass a gap positioned beyond the corner");

        Actor watch = registry.spawn(MilitiaWatch.TYPE, stats, anchor);
        watch.setAnchorCell(anchor);
        ActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public boolean isWalkable(int cell) {
                return walk.isWalkable(cell);
            }
        };
        JobBehaviors.selectRouteStart(watch, ctx);
        boolean[] visited = new boolean[4];
        for (int tick = 0; tick < 3000 && !(visited[0] && visited[1] && visited[2] && visited[3]); tick++) {
            JobBehaviors.pursuePatrol(watch, ctx, 15);
            visited[Math.floorMod(watch.goalProgress(), 4)] = true;
        }
        assertTrue(visited[0] && visited[1] && visited[2] && visited[3],
                "the fixed patrol must actually visit every corner despite the obstacle");
    }

    // ======================================================================
    // Case 3 (commute "table pocket"): a corridor between home and the job
    // anchor is plugged by a table across its full width; the only detour is
    // a side room reached by moving perpendicular to the corridor first (a
    // genuine temporary retreat away from the target) — something no local
    // single-step retry can ever discover.
    // ======================================================================

    private static Actor.WalkabilityQuery corridorWithSideDetourAroundATablePocket() {
        return cell -> {
            int x = PackedPos.x(cell);
            int y = PackedPos.y(cell);
            if (y == 10 && x >= 0 && x <= 20) {
                return !(x >= 10 && x <= 12); // the corridor, blocked only at the table
            }
            if (y == 9 && (x == 2 || x == 18)) {
                return true; // the two side-detour gaps
            }
            if ((y == 9 || y == 11) && x >= 0 && x <= 20) {
                return false; // solid corridor walls otherwise
            }
            if (y >= 1 && y <= 8 && x >= 2 && x <= 18) {
                return true; // the detour room, north of the corridor
            }
            return true; // open ground outside the maze entirely
        };
    }

    @Test
    void commuteThroughATablePocketNoLongerGetsPermanentlyStuck() {
        Actor.WalkabilityQuery walk = corridorWithSideDetourAroundATablePocket();
        int home = PackedPos.pack(0, 10, Z);
        int anchor = PackedPos.pack(20, 10, Z);

        ActorRegistry registry = new ActorRegistry();
        ActorTypeStats stats = ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, 100);

        Actor stuckProbe = registry.spawn(Serf.TYPE, stats, home);
        for (int i = 0; i < 50; i++) {
            stuckProbe.stepToward(anchor, true, walk); // the OLD greedy-only mover
        }
        assertTrue(ActorGeometry.chebyshev(stuckProbe.cell(), anchor) > 1,
                "sanity: the old greedy mover is permanently stuck at the table, axis-aligned, zero "
                        + "fallback (halted at " + describe(stuckProbe.cell()) + ")");

        Actor serf = registry.spawn(Serf.TYPE, stats, home);
        serf.setAnchorCell(anchor);
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public boolean isWalkable(int cell) {
                return walk.isWalkable(cell);
            }
        };
        int homeId = ctx.homes().addHome(home);
        serf.setHomeId(homeId);
        JobParams params = new JobParams(GoalKind.SCAVENGE_CIRCUIT, 150, 0, 24_000, 0, 5, 1, RenewMode.IMMEDIATE, 0);

        for (int tick = 0; tick < 3000 && serf.cell() != anchor; tick++) {
            JobBehaviors.pursueAtAnchor(serf, ctx, params);
        }
        assertEquals(anchor, serf.cell(), "the fixed commute must route through the side detour and arrive");
    }

    private static String describe(int cell) {
        return "(" + PackedPos.x(cell) + "," + PackedPos.y(cell) + "," + PackedPos.z(cell) + ")";
    }
}
