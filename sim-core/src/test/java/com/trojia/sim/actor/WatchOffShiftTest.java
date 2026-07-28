package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * S7 slice 3 — the Watch goes off shift like every other trade.
 *
 * <p>{@code watch.patrol} was the ONE job whose pursue had no {@code params.inWindow} branch. Every other one heads home
 * outside its rhythm window; without it, {@link ReturnHomePolicy}'s night-at-home gate —
 * which returns 0 explicitly BECAUSE "pursueAtAnchor's own off-shift target already keeps
 * the actor there through the night" — rested on an assumption that was false for the Watch,
 * and the guard oscillated one cell in and out of its own doorway all night.
 */
final class WatchOffShiftTest {

    private static final int Z = 11;

    /** {@code watch.patrol} rhythmWindow is [0,12000) in the committed jobs.json. */
    private static final long ON_SHIFT_TICK = 3_000L;
    private static final long OFF_SHIFT_TICK = 18_000L;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /** Ctx double with an explicit wall set (everything else walkable). */
    private static final class WalledContext extends NoOpActorContext {
        private final List<Integer> walls = new ArrayList<>();

        WalledContext(ActorRegistry registry) {
            super(registry);
        }

        @Override
        public boolean isWalkable(int c) {
            return !walls.contains(c);
        }
    }

    private static Actor spawnWatch(ActorRegistry registry, ActorContext ctx, int at) {
        Actor watch = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.stats(MilitiaWatch.TYPE), at);
        watch.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
        return watch;
    }

    private static Job patrolJob(ActorContext ctx) {
        return ctx.jobs().get(ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
    }

    // ================== slice 3: the off-shift branch ==================

    @Test
    void offShiftTheBeatWalksHomeAndStaysThere() {
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int post = cell(40, 40);
        int home = cell(46, 44);
        Actor watch = spawnWatch(registry, ctx, post);
        watch.setAnchorCell(post);
        watch.setHomeId(ctx.homes().addHome(home));
        Job patrol = patrolJob(ctx);

        ctx.setTick(OFF_SHIFT_TICK);
        for (int i = 0; i < 60 && watch.cell() != home; i++) {
            patrol.pursue(watch, ctx);
        }
        assertEquals(home, watch.cell(), "off shift, the beat's target is the home cell");

        // ...and it does not wander back out: 200 further off-shift ticks, zero movement.
        for (int i = 0; i < 200; i++) {
            patrol.pursue(watch, ctx);
            assertEquals(home, watch.cell(), "off shift, a watch at home stays at home");
        }
        assertEquals(0, watch.goalWorkTicks(),
                "at home there is no step attempt in flight, so no blocked-leg clock");
    }

    @Test
    void onShiftTheBeatStillLeavesHomeAndPatrols() {
        // The mirror of the above: the off-shift branch must not leak into duty hours.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int post = cell(40, 40);
        Actor watch = spawnWatch(registry, ctx, post);
        watch.setAnchorCell(post);
        watch.setHomeId(ctx.homes().addHome(post));
        Job patrol = patrolJob(ctx);

        ctx.setTick(ON_SHIFT_TICK);
        for (int i = 0; i < 40; i++) {
            patrol.pursue(watch, ctx);
        }
        assertNotEquals(post, watch.cell(), "on shift, the beat leaves the post and walks");
    }

    @Test
    void offShiftABlockedWalkHomeStillCountsAsBlocked() {
        // The blocked-leg clock keeps its honest meaning across the shift boundary: it is
        // NOT zeroed unconditionally off shift, or a guard stuck all night would launder
        // itself out of the blocked-spell metric.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int post = cell(40, 40);
        int home = cell(44, 40);
        // Wall the guard into its own cell: every neighbour is unwalkable.
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 || dy != 0) {
                    ctx.walls.add(cell(40 + dx, 40 + dy));
                }
            }
        }
        Actor watch = spawnWatch(registry, ctx, post);
        watch.setAnchorCell(post);
        watch.setHomeId(ctx.homes().addHome(home));
        Job patrol = patrolJob(ctx);

        ctx.setTick(OFF_SHIFT_TICK);
        for (int i = 0; i < 5; i++) {
            patrol.pursue(watch, ctx);
        }
        assertEquals(post, watch.cell(), "walled in: it cannot reach home");
        assertTrue(watch.goalWorkTicks() >= 2,
                "a genuinely failed step attempt still registers off shift, saw "
                        + watch.goalWorkTicks());
    }

    @Test
    void offShiftAWatchWithNoHomeFallsBackToItsAnchor() {
        // Defensive: homeCellOr's fallback keeps an unbaked watch from losing its target.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int post = cell(40, 40);
        Actor watch = spawnWatch(registry, ctx, post);
        watch.setAnchorCell(post);
        Job patrol = patrolJob(ctx);

        ctx.setTick(OFF_SHIFT_TICK);
        patrol.pursue(watch, ctx);
        assertEquals(TargetKind.CELL, watch.goalTargetKind());
        assertEquals(post, watch.goalTargetKey(), "no home baked: the anchor is the fallback");
    }
}
