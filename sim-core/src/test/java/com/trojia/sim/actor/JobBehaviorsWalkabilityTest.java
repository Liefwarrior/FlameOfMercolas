package com.trojia.sim.actor;

import com.trojia.sim.actor.job.GoalKind;
import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.actor.job.JobParams;
import com.trojia.sim.actor.job.RenewMode;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.ChunkWriter;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.Walkability;
import com.trojia.sim.world.WorldBuilder;
import com.trojia.sim.world.WorldConfig;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@code JobBehaviors}' patrol/wander target selection against a tiny hand-built world with
 * open water (unauthored {@link TileForm#OPEN}, matching the real docks harbor's authoring)
 * directly in the naive beat/drift path — proves the bounded-retry walkability check (§10.1
 * addendum) actually keeps a patrol beat or a wander drift off the water over many ticks/draws,
 * the coverage gap the design phase flagged (no {@code JobBehaviorsTest} existed before this).
 */
final class JobBehaviorsWalkabilityTest {

    private static final int Z = 10;
    private static final int AX = 45;
    private static final int AY = 45;

    /** An {@link ActorContext} test double whose {@code isWalkable} reads a real built world. */
    private static final class WorldBackedContext extends NoOpActorContext {
        private final TileCursor cursor;

        WorldBackedContext(ActorRegistry registry, TickableWorld world) {
            super(registry);
            this.cursor = world.cursor();
        }

        @Override
        public boolean isWalkable(int cell) {
            return Walkability.isWalkable(cursor.moveTo(cell));
        }
    }

    private static TickableWorld filledFloorWorld(int radius) {
        TickableWorld world = WorldBuilder.create(new WorldConfig(3, 3, 3)).build();
        ChunkWriter writer = world.writer();
        for (int x = AX - radius - 1; x <= AX + radius + 1; x++) {
            for (int y = AY - radius - 1; y <= AY + radius + 1; y++) {
                writer.setForm(PackedPos.pack(x, y, Z), TileForm.FLOOR);
            }
        }
        return world;
    }

    // ======================================================================
    // Patrol: a naive beat corner sits in open water; the actor must never
    // target (or stand on) the blocked corner across a full loop.
    // ======================================================================

    @Test
    void patrolNeverTargetsOrStandsOnTheBlockedCorner() {
        int radius = 5;
        TickableWorld world = filledFloorWorld(radius);
        // Leg 0 walks toward (AX+radius, AY+radius) (PATROL_DX/DY = {1,1,-1,-1}/{1,-1,-1,1}):
        // drown that corner AND its first shrink step in open water (unauthored OPEN, exactly
        // how the real docks harbor water is authored) so the retry must shrink twice.
        int blockedCorner = PackedPos.pack(AX + radius, AY + radius, Z);
        int blockedShrink = PackedPos.pack(AX + radius - 1, AY + radius - 1, Z);
        world.writer().setForm(blockedCorner, TileForm.OPEN);
        world.writer().setForm(blockedShrink, TileForm.OPEN);

        ActorRegistry registry = new ActorRegistry();
        ActorTypeStats stats = ActorTestFixtures.statsWithSpeedAndLeash(MilitiaWatch.TYPE, true, 1, 24);
        Actor watch = registry.spawn(MilitiaWatch.TYPE, stats, PackedPos.pack(AX, AY, Z));
        watch.setAnchorCell(watch.cell());
        WorldBackedContext ctx = new WorldBackedContext(registry, world);

        JobBehaviors.selectRouteStart(watch, ctx);
        TileCursor probe = world.cursor();
        for (int tick = 0; tick < 400; tick++) {
            JobBehaviors.pursuePatrol(watch, ctx, radius);
            assertTrue(Walkability.isWalkable(probe.moveTo(watch.cell())),
                    "actor must never stand on an unwalkable cell, tick " + tick);
            assertFalse(watch.cell() == blockedCorner || watch.cell() == blockedShrink,
                    "actor must never stand on the drowned corner or its first shrink, tick "
                            + tick);
            if (watch.goalTargetKind() == TargetKind.CELL) {
                int target = watch.goalTargetKey();
                assertFalse(target == blockedCorner || target == blockedShrink,
                        "the patrol must never re-target the drowned corner, tick " + tick);
            }
        }
    }

    // ======================================================================
    // Wander: half the reachable area is open water; the drift target must
    // never land on it, over many redraws.
    // ======================================================================

    private static final JobParams WANDER_PARAMS = new JobParams(
            GoalKind.SCAVENGE_CIRCUIT, 150, 0, 1000, 0, 5, 1, RenewMode.IMMEDIATE, 0);

    @Test
    void wanderTargetNeverLandsOnWaterAcrossManyRedraws() {
        ActorRegistry registry = new ActorRegistry();
        // leash 20 -> wanderRadius = max(4, 20/2) = 10 (JobBehaviors.wanderRadius).
        ActorTypeStats stats = ActorTestFixtures.statsWithSpeedAndLeash(Wastrel.TYPE, true, 1, 20);
        Actor wastrel = registry.spawn(Wastrel.TYPE, stats, PackedPos.pack(AX, AY, Z));
        wastrel.setAnchorCell(wastrel.cell());

        int wanderRadius = 10;
        TickableWorld world = filledFloorWorld(wanderRadius);
        // Drown the entire east half of the reachable square (x > AX) — a naive draw lands
        // there roughly half the time, so this exercises the bounded-retry redraw hard.
        ChunkWriter writer = world.writer();
        for (int x = AX + 1; x <= AX + wanderRadius; x++) {
            for (int y = AY - wanderRadius; y <= AY + wanderRadius; y++) {
                writer.setForm(PackedPos.pack(x, y, Z), TileForm.OPEN);
            }
        }
        WorldBackedContext ctx = new WorldBackedContext(registry, world);
        TileCursor probe = world.cursor();

        JobBehaviors.selectWanderTarget(wastrel, ctx);
        for (int tick = 0; tick < 2000; tick++) {
            JobBehaviors.pursueWander(wastrel, ctx, WANDER_PARAMS);
            assertTrue(Walkability.isWalkable(probe.moveTo(wastrel.cell())),
                    "actor must never stand on an unwalkable (drowned) cell, tick " + tick);
            if (wastrel.goalTargetKind() == TargetKind.CELL) {
                assertTrue(Walkability.isWalkable(probe.moveTo(wastrel.goalTargetKey())),
                        "the wander drift must never target open water, tick " + tick);
            }
        }
    }
}
