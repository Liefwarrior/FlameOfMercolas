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
            self.stepToward(target, true);
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

    /**
     * PURSUE (patrol beat): walk a square loop of side {@code 2*radius} centered
     * on the actor's anchor, advancing to the next corner each time the current
     * one is reached and wrapping forever. A patrol never finishes (its
     * {@code isComplete} is always {@code false}), so it keeps looping through
     * duty hours instead of stopping at a unit quota. {@code radius} is kept
     * inside the type's leash by the caller, so the leash-respecting step always
     * makes progress.
     */
    public static void pursuePatrol(Actor self, ActorContext ctx, int radius) {
        int leg = Math.floorMod(self.goalProgress(), 4);
        int ax = PackedPos.x(self.anchorCell());
        int ay = PackedPos.y(self.anchorCell());
        int z = PackedPos.z(self.anchorCell());
        int tx = clamp(ax + PATROL_DX[leg] * radius, PackedPos.X_MASK);
        int ty = clamp(ay + PATROL_DY[leg] * radius, PackedPos.Y_MASK);
        int target = PackedPos.pack(tx, ty, z);
        if (self.cell() != target) {
            self.stepToward(target);
            return;
        }
        self.setGoalProgress((short) ((leg + 1) % 4));
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
            self.stepToward(target);
            return;
        }
        int dwell = self.goalWorkTicks() + 1;
        if (dwell >= params.workTicksPerUnit()) {
            retargetWander(self, ctx);
        } else {
            self.setGoalWorkTicks(dwell);
        }
    }

    private static void retargetWander(Actor self, ActorContext ctx) {
        int radius = wanderRadius(self);
        long draw = ctx.draw(ActorRngStream.JOB_TARGET_PICK, self.id(), ctx.nextDrawIndex(self.id()));
        int span = 2 * radius + 1;
        int dx = (int) Long.remainderUnsigned(draw, span) - radius;
        int dy = (int) Long.remainderUnsigned(draw >>> 20, span) - radius;
        int ax = PackedPos.x(self.anchorCell());
        int ay = PackedPos.y(self.anchorCell());
        int z = PackedPos.z(self.anchorCell());
        int target = PackedPos.pack(clamp(ax + dx, PackedPos.X_MASK),
                clamp(ay + dy, PackedPos.Y_MASK), z);
        self.setGoalTarget(TargetKind.CELL, target);
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
            self.stepToward(target, true);
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
