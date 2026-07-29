package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * S7 slices 3 and 4 — the two sim-side guard-routing fixes.
 *
 * <p>SLICE 3 (the Watch goes off shift like every other trade): {@code watch.patrol} was the
 * ONE job whose pursue had no {@code params.inWindow} branch. Every other one heads home
 * outside its rhythm window; without it, {@link ReturnHomePolicy}'s night-at-home gate —
 * which returns 0 explicitly BECAUSE "pursueAtAnchor's own off-shift target already keeps
 * the actor there through the night" — rested on an assumption that was false for the Watch,
 * and the guard oscillated one cell in and out of its own doorway all night.
 *
 * <p>SLICE 4 (the beat stops walking into cages): the square beat's corner retarget accepted
 * any WALKABLE corner, so radius-6 corners settled inside 1x1 prison cages and 1-wide alley
 * stubs. It now rejects {@link CorridorPinch}-pinched candidates, with the merely-walkable
 * best kept in reserve so a guard in a tight pocket never loses its target.
 *
 * <p>ROUND 2 (the dawn free-duty bug): slice 3's off-shift branch cached the home cell in
 * {@code goalTarget}, and the reopening shift window then paid a work event for it. See
 * {@link #atDawnAGuardOnItsOwnBedIsPaidNothing} — the only test here that crosses a day
 * boundary, because that is the only place the fault is reachable.
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
        // Defensive: homeCellOr's fallback keeps an unbaked watch from wandering off shift.
        // Asserted as BEHAVIOUR (where it stands), not as a cached target — the off-shift
        // walk deliberately leaves no goalTarget behind; see the dawn test below for why.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int post = cell(40, 40);
        Actor watch = spawnWatch(registry, ctx, post);
        watch.setAnchorCell(post);
        Job patrol = patrolJob(ctx);

        ctx.setTick(OFF_SHIFT_TICK);
        for (int i = 0; i < 50; i++) {
            patrol.pursue(watch, ctx);
        }
        assertEquals(post, watch.cell(), "no home baked: the anchor is the off-shift fallback");
    }

    @Test
    void offShiftTheWalkHomeLeavesNoGoalTargetBehind() {
        // The root condition of the dawn free-duty bug, pinned directly: a commute must not
        // leave a CELL target parked in the goal slot for the next shift to inherit.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int post = cell(40, 40);
        int home = cell(46, 44);
        Actor watch = spawnWatch(registry, ctx, post);
        watch.setAnchorCell(post);
        watch.setHomeId(ctx.homes().addHome(home));
        Job patrol = patrolJob(ctx);

        ctx.setTick(OFF_SHIFT_TICK);
        for (int i = 0; i < 60; i++) {
            patrol.pursue(watch, ctx);
            assertEquals(TargetKind.NONE, watch.goalTargetKind(),
                    "off shift the beat holds no work target — it is walking home");
        }
    }

    // ================== the dawn free-duty bug ==================

    /**
     * A guard must not be paid a work event for standing on its own bed when the shift
     * window reopens.
     *
     * <p>The run has to CROSS tick 24,000 to exhibit it — the whole night is spent in the
     * off-shift branch and the fault fires on the single tick the window flips back open —
     * which is why neither a 15,000-tick soak nor a same-day unit test could see it. Before
     * the fix: the off-shift walk parked {@code CELL(homeCell)} in the goal slot,
     * {@code pursuePatrol} skipped its corner retarget because a CELL target was already
     * set, adopted the bed as the leg's corner, found the guard standing on it and awarded
     * {@code dutyPerUnit} DUTY (200) plus {@code trainCp} streetwise (24) for a night of
     * sleep. Once per stationed guard per dawn, forever.
     */
    @Test
    void atDawnAGuardOnItsOwnBedIsPaidNothing() {
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int post = cell(40, 40);
        int home = cell(46, 44);
        Actor watch = spawnWatch(registry, ctx, post);
        watch.setAnchorCell(post);
        watch.setHomeId(ctx.homes().addHome(home));
        Job patrol = patrolJob(ctx);

        // Drain DUTY well clear of the saturating clamp so an award would be visible.
        watch.applyNeedDelta(Need.DUTY, -NeedThresholds.MAX);
        assertEquals(0, watch.need(Need.DUTY), "the fixture starts DUTY-bottomed");

        // Night: walk home and settle there, exactly as the ward's stationed guards do.
        ctx.setTick(DailyRhythm.DAY - 1_000L); // tod 23,000 — off shift
        for (int i = 0; i < 200; i++) {
            patrol.pursue(watch, ctx);
        }
        assertEquals(home, watch.cell(), "the guard is asleep on its own home cell");
        int dutyAsleep = watch.need(Need.DUTY);

        // Dawn: tick 24,000 is tod 0 — the watch.patrol window reopens on a SECOND day.
        ctx.setTick(DailyRhythm.DAY);
        patrol.pursue(watch, ctx);

        assertEquals(dutyAsleep, watch.need(Need.DUTY),
                "standing on its own bed at the shift boundary must pay no DUTY");
        assertNotEquals(home, watch.goalTargetKey(),
                "the first on-shift tick derives a real beat corner, not the bed");
        assertEquals(TargetKind.CELL, watch.goalTargetKind(),
                "and it is a genuine cached corner for the leg");
        int tx = PackedPos.x(watch.goalTargetKey());
        int ty = PackedPos.y(watch.goalTargetKey());
        assertTrue(Math.max(Math.abs(tx - 40), Math.abs(ty - 40)) > 0
                        && Math.abs(tx - 40) == Math.abs(ty - 40),
                "a square-beat corner sits on the leg diagonal off the anchor, saw ("
                        + tx + "," + ty + ")");
    }

    // ================== slice 4: corners off pinched ground ==================

    @Test
    void aBeatCornerInAOneWideCageIsRejectedForOpenGround() {
        // The K34 shape, minimally: the radius-6 corner lands in a 1x1 cage with a single
        // mouth. The old rule accepted it (it IS walkable); the new rule shrinks past it.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int anchor = cell(40, 40);
        int cage = cell(46, 46); // leg 0 is (+1,+1) -> radius 6 lands exactly here
        // Cage it: everything around (46,46) except its single north mouth is wall.
        ctx.walls.add(cell(45, 46));
        ctx.walls.add(cell(47, 46));
        ctx.walls.add(cell(46, 47));
        Actor watch = spawnWatch(registry, ctx, anchor);
        watch.setAnchorCell(anchor);
        watch.setHomeId(ctx.homes().addHome(anchor));
        watch.setGoalProgress((short) 0); // leg 0
        watch.setGoalTarget(TargetKind.NONE, Actor.NONE);
        Job patrol = patrolJob(ctx);

        ctx.setTick(ON_SHIFT_TICK);
        patrol.pursue(watch, ctx);

        assertTrue(CorridorPinch.isPinched(cage, ctx::isWalkable),
                "the fixture really is a pinch (east-west extent 1)");
        assertNotEquals(cage, watch.goalTargetKey(),
                "the beat no longer parks its corner inside the cage");
        assertFalse(CorridorPinch.isPinched(watch.goalTargetKey(), ctx::isWalkable),
                "the chosen corner is on ground two guards can pass on: "
                        + watch.goalTargetKey());
    }

    @Test
    void openGroundAtFullRadiusIsStillTakenUnchanged() {
        // The no-walls case must be byte-identical to the pre-S7 behaviour: full radius.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int anchor = cell(40, 40);
        Actor watch = spawnWatch(registry, ctx, anchor);
        watch.setAnchorCell(anchor);
        watch.setHomeId(ctx.homes().addHome(anchor));
        watch.setGoalProgress((short) 0);
        watch.setGoalTarget(TargetKind.NONE, Actor.NONE);
        Job patrol = patrolJob(ctx);

        ctx.setTick(ON_SHIFT_TICK);
        patrol.pursue(watch, ctx);
        assertEquals(cell(46, 46), watch.goalTargetKey(),
                "open ground: the corner is still anchor + radius on the leg diagonal");
    }

    @Test
    void aWatchInATightPocketKeepsAWalkableTargetRatherThanLosingItsLeg() {
        // The preserved fallback: when EVERY candidate in budget is pinched, the beat takes
        // the best merely-walkable one instead of collapsing onto its anchor every leg.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int anchor = cell(40, 40);
        // A 1-wide diagonal stair of open cells along leg 0: every candidate is pinched.
        for (int r = 1; r <= 6; r++) {
            ctx.walls.add(cell(40 + r - 1, 40 + r));
            ctx.walls.add(cell(40 + r + 1, 40 + r));
        }
        Actor watch = spawnWatch(registry, ctx, anchor);
        watch.setAnchorCell(anchor);
        watch.setHomeId(ctx.homes().addHome(anchor));
        watch.setGoalProgress((short) 0);
        watch.setGoalTarget(TargetKind.NONE, Actor.NONE);
        Job patrol = patrolJob(ctx);

        ctx.setTick(ON_SHIFT_TICK);
        patrol.pursue(watch, ctx);
        int target = watch.goalTargetKey();
        assertTrue(ctx.isWalkable(target), "the fallback target is at least walkable");
        assertEquals(cell(46, 46), target,
                "with nothing un-pinched in budget the widest-radius walkable corner is kept");
    }
}
