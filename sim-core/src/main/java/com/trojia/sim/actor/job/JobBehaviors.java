package com.trojia.sim.actor.job;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorContext;
import com.trojia.sim.actor.ActorRngStream;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.TargetKind;
import com.trojia.sim.world.PackedPos;

/**
 * The shared generic goal mechanics every {@link Job} leaf delegates to
 * (ACTORS-SPEC.md §10.1's SELECT/PURSUE/COMPLETE steps). This foundation
 * milestone gives every civic job a genuinely mobile daily life (Dwarf-Fortress
 * texture: commuters walk to workshops, the Watch loops a beat, Wastrels drift,
 * beasts trail their Keeper) while staying deterministic and reusing the base's
 * one mover, {@link Actor#stepToward}:
 *
 * <ul>
 *   <li><b>anchor cycle</b> ({@link #pursueAtAnchor}) — commute-aware: work at
 *       the anchor during the job's rhythm window, walk home outside it. When
 *       anchor == home (a household worker with no distinct workplace) both legs
 *       collapse to one cell, so it degrades to the original "accrue work in
 *       place" behavior with no movement.</li>
 *   <li><b>patrol</b> ({@link #pursuePatrol}) — a looping square beat that never
 *       "completes".</li>
 *   <li><b>wander</b> ({@link #pursueWander}) — a bounded deterministic drift
 *       (beg circuit / scavenge sweep).</li>
 *   <li><b>follow-owner</b> ({@link #pursueFollowOwner}) — trail the owner's
 *       live cell every tick.</li>
 * </ul>
 *
 * <p>Leaves stay one-line thin (§10.2): they name one of these delegates. All
 * randomness flows through named {@link ActorRngStream} draws (§2.2), never
 * {@code java.util.Random}; there is no hash/float state here.
 */
public final class JobBehaviors {

    private JobBehaviors() {
    }

    // ======================================================================
    // Anchor cycle (commute-aware) — the civic default for placed workers
    // ======================================================================

    /** SELECT: target the actor's own job anchor — draw-free (§10.1). */
    public static void selectAnchorTarget(Actor self, ActorContext ctx) {
        self.setGoalTarget(TargetKind.CELL, self.anchorCell());
        self.setGoalWorkTicks(0);
    }

    /**
     * PURSUE (commute-aware anchor cycle): during the job's rhythm window the
     * actor heads for its work anchor and accrues work once standing on it;
     * outside the window it heads for its home cell. For the common case where
     * {@code anchorCell == home} (a household worker with no distinct second
     * location) both legs resolve to the same cell — a pure in-place work
     * accrual, byte-identical to the pre-commute behavior, no movement. For an
     * actor whose anchor is a real workplace distinct from home (a commuter),
     * the two legs become a genuine daily round trip driven by the ONE clock the
     * job already owns — its rhythm window — so "off shift" is the shift ending,
     * not the actor type's generic night window (which stays RETURN_HOME's
     * separate concern: exhaustion / deep-night sleep).
     */
    public static void pursueAtAnchor(Actor self, ActorContext ctx, JobParams params) {
        int workplace = self.anchorCell();
        int home = homeCellOr(self, ctx, workplace);
        boolean onShift = params.inWindow(DailyRhythm.tickOfDay(ctx.tick()));
        int target = onShift ? workplace : home;
        self.setGoalTarget(TargetKind.CELL, target);
        if (self.cell() != target) {
            // Commuting home<->work legitimately crosses the work leash (a home
            // routinely sits outside the workplace anchor's leash), exactly as
            // RETURN_HOME's walk home does — so this leg ignores the leash too.
            self.stepToward(target, true, ctx::isWalkable);
            return;
        }
        if (self.cell() == workplace) {
            accrueWork(self, params);
        }
    }

    /** COMPLETE check: pure, {@code goalProgress >= unitsToComplete}. */
    public static boolean isCompleteAtUnits(Actor self, JobParams params) {
        return self.goalProgress() >= params.unitsToComplete();
    }

    private static void accrueWork(Actor self, JobParams params) {
        int workTicks = self.goalWorkTicks() + 1;
        if (workTicks >= params.workTicksPerUnit()) {
            self.setGoalProgress((short) (self.goalProgress() + 1));
            self.setGoalWorkTicks(0);
        } else {
            self.setGoalWorkTicks(workTicks);
        }
    }

    // ======================================================================
    // Patrol route (Watch) — a beat that loops forever
    // ======================================================================

    /** The four corners of the square beat, as signed unit offsets walked in order. */
    private static final int[] PATROL_DX = {1, 1, -1, -1};
    private static final int[] PATROL_DY = {1, -1, -1, 1};

    /** SELECT for a looping route: (re)start the beat at its first corner. */
    public static void selectRouteStart(Actor self, ActorContext ctx) {
        self.setGoalProgress((short) 0);
        self.setGoalWorkTicks(0);
    }

    /** Bounded retry budget for {@link #retargetPatrolCorner} — draw-free, fixed geometry. */
    private static final int PATROL_RETRY_BUDGET = 8;

    /**
     * PURSUE (patrol beat): walk a square loop of side {@code 2*radius} centered
     * on the actor's anchor, advancing to the next corner each time the current
     * one is reached and wrapping forever. A patrol never finishes (its
     * {@code isComplete} is always {@code false}), so it keeps looping through
     * duty hours instead of stopping at a unit quota. {@code radius} is kept
     * inside the type's leash by the caller, so the leash-respecting step always
     * makes progress.
     *
     * <p>Corners are fixed geometry ({@code anchor + radius*dir}), not draws, so
     * a water/wall corner is not resampled with a redraw — instead the corner is
     * pre-validated once per leg and cached in {@code goalTarget} ({@link
     * #retargetPatrolCorner}), mirroring the wander/anchor-cycle pattern already
     * in this file, rather than recomputing (and re-walkability-checking) every
     * tick.
     */
    public static void pursuePatrol(Actor self, ActorContext ctx, int radius) {
        if (self.goalTargetKind() != TargetKind.CELL) {
            retargetPatrolCorner(self, ctx, radius);
        }
        int target = self.goalTargetKey();
        if (self.cell() != target) {
            self.stepToward(target, false, ctx::isWalkable);
            return;
        }
        self.setGoalProgress((short) ((Math.floorMod(self.goalProgress(), 4) + 1) % 4));
        self.setGoalTarget(TargetKind.NONE, Actor.NONE); // force next leg's corner to be revalidated
    }

    /**
     * Deterministically shrinks the radius along the current leg's fixed corner
     * direction — {@code radius, radius-1, ...} for up to
     * {@link #PATROL_RETRY_BUDGET} attempts (capped at {@code radius} itself) —
     * and caches the first walkable candidate as the leg's target. Falls back to
     * {@code anchorCell()} (guaranteed walkable by spawn/bake) if nothing in
     * budget is walkable. Draw-free (fixed geometry, not randomness), bounded,
     * deterministic.
     */
    private static void retargetPatrolCorner(Actor self, ActorContext ctx, int radius) {
        int leg = Math.floorMod(self.goalProgress(), 4);
        int ax = PackedPos.x(self.anchorCell());
        int ay = PackedPos.y(self.anchorCell());
        int z = PackedPos.z(self.anchorCell());
        int attempts = Math.min(radius, PATROL_RETRY_BUDGET);
        for (int attempt = 0; attempt < attempts; attempt++) {
            int r = radius - attempt;
            int tx = clamp(ax + PATROL_DX[leg] * r, PackedPos.X_MASK);
            int ty = clamp(ay + PATROL_DY[leg] * r, PackedPos.Y_MASK);
            int candidate = PackedPos.pack(tx, ty, z);
            if (ctx.isWalkable(candidate)) {
                self.setGoalTarget(TargetKind.CELL, candidate);
                return;
            }
        }
        self.setGoalTarget(TargetKind.CELL, self.anchorCell());
    }

    // ======================================================================
    // Bounded wander (Wastrels, Ferals) — beg circuit / scavenge sweep
    // ======================================================================

    /** SELECT (wander): pick a fresh nearby cell within the wander radius (named draw). */
    public static void selectWanderTarget(Actor self, ActorContext ctx) {
        retargetWander(self, ctx);
    }

    /**
     * PURSUE (bounded wander): walk to the current wander target, loiter there
     * for {@code workTicksPerUnit} ticks, then draw a fresh target within a
     * bounded radius of the anchor and repeat — the visible drift of a Wastrel's
     * beg circuit or a Feral's scavenge sweep. Never completes; the drift is
     * perpetual while this job wins selection. The radius is derived from the
     * type's leash, so the sweep stays inside the leashed home range without a
     * new raws field, and the leash-respecting step never stalls.
     */
    public static void pursueWander(Actor self, ActorContext ctx, JobParams params) {
        if (self.goalTargetKind() != TargetKind.CELL) {
            retargetWander(self, ctx);
            return;
        }
        int target = self.goalTargetKey();
        if (self.cell() != target) {
            self.stepToward(target, false, ctx::isWalkable);
            return;
        }
        int dwell = self.goalWorkTicks() + 1;
        if (dwell >= params.workTicksPerUnit()) {
            retargetWander(self, ctx);
        } else {
            self.setGoalWorkTicks(dwell);
        }
    }

    /** Bounded redraw budget for {@link #retargetWander} — same named draw, never unbounded. */
    private static final int WANDER_RETRY_BUDGET = 8;

    /**
     * Draws a fresh nearby cell within the wander radius; if the draw lands on an
     * unwalkable cell (water, a wall), redraws with the next {@code JOB_TARGET_PICK}
     * index (the same named-RNG stream, never unnamed/unseeded randomness) up to
     * {@link #WANDER_RETRY_BUDGET} attempts, then falls back to
     * {@code self.anchorCell()} (guaranteed walkable by spawn/bake) — bounded, can
     * never infinite-loop.
     */
    private static void retargetWander(Actor self, ActorContext ctx) {
        int radius = wanderRadius(self);
        int ax = PackedPos.x(self.anchorCell());
        int ay = PackedPos.y(self.anchorCell());
        int z = PackedPos.z(self.anchorCell());
        int span = 2 * radius + 1;
        for (int attempt = 0; attempt < WANDER_RETRY_BUDGET; attempt++) {
            long draw = ctx.draw(ActorRngStream.JOB_TARGET_PICK, self.id(),
                    ctx.nextDrawIndex(self.id()));
            int dx = (int) Long.remainderUnsigned(draw, span) - radius;
            int dy = (int) Long.remainderUnsigned(draw >>> 20, span) - radius;
            int candidate = PackedPos.pack(clamp(ax + dx, PackedPos.X_MASK),
                    clamp(ay + dy, PackedPos.Y_MASK), z);
            if (ctx.isWalkable(candidate)) {
                self.setGoalTarget(TargetKind.CELL, candidate);
                self.setGoalWorkTicks(0);
                return;
            }
        }
        self.setGoalTarget(TargetKind.CELL, self.anchorCell());
        self.setGoalWorkTicks(0);
    }

    /** Half the leash (min 4): a sweep that visibly ranges without leaving the leashed range. */
    private static int wanderRadius(Actor self) {
        return Math.max(4, self.stats().leashRadius() / 2);
    }

    // ======================================================================
    // Follow owner (Chattel) — trail the Keeper's live cell
    // ======================================================================

    /** SELECT (follow): aim at the owner's current cell (recomputed live each pursue tick). */
    public static void selectOwnerTarget(Actor self, ActorContext ctx) {
        int owner = self.ownerId();
        int target = owner == Actor.NONE ? self.anchorCell() : ctx.registry().get(owner).cell();
        self.setGoalTarget(TargetKind.CELL, target);
    }

    /**
     * PURSUE (stay-near-owner): trail the owner's LIVE cell every tick so the
     * beast visibly follows its Keeper around — leash-ignoring, because a pet
     * legitimately leaves the pen at its owner's heel (the pen stays its
     * RETURN_HOME roost for the night). With no owner it degrades to a bounded
     * wander around its own anchor. Never completes.
     */
    public static void pursueFollowOwner(Actor self, ActorContext ctx, JobParams params) {
        int owner = self.ownerId();
        if (owner == Actor.NONE) {
            pursueWander(self, ctx, params);
            return;
        }
        int target = ctx.registry().get(owner).cell();
        self.setGoalTarget(TargetKind.CELL, target);
        if (self.cell() != target) {
            self.stepToward(target, true, ctx::isWalkable);
        }
    }

    // ======================================================================
    // Shared helpers
    // ======================================================================

    private static int homeCellOr(Actor self, ActorContext ctx, int fallback) {
        if (self.homeId() == Actor.NONE) {
            return fallback;
        }
        return ctx.homes().get(self.homeId()).homeCell();
    }

    private static int clamp(int coordinate, int max) {
        if (coordinate < 0) {
            return 0;
        }
        return Math.min(coordinate, max);
    }
}
