package com.trojia.sim.actor.job;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorContext;
import com.trojia.sim.actor.ActorGeometry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorRngStream;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.FoodEconomy;
import com.trojia.sim.actor.FoodMarket;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.PrisonCellRegistry;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.StatusBit;
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
            self.stepAlongRoute(target, true, ctx::isWalkable, ctx.occupancy());
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
    // Farm cycle (Serf.Farmer) — the anchor cycle that also PRODUCES FOOD
    // ======================================================================

    /**
     * PURSUE (farm): identical commute to {@link #pursueAtAnchor} — plot during the rhythm window,
     * home outside it — but every completed work-unit at the plot mints FOOD (the money-gated market
     * pass). The yield cascades: it first fills the farming household's OWN home-cell larder up to
     * {@link FoodEconomy#LARDER_CAP} (the family eats what it grew); once that is full it fills the
     * compound's SHARED atrium/courtyard larder — the farmer's own work anchor, same z — so the
     * whole same-band courtyard eats the harvest free (the "subsistence larder stocked by real farm
     * production", the legitimately non-market half of the economy); and only once BOTH are full is
     * the surplus sold to the nearest same-z shop for {@link FoodEconomy#FARM_SELL_PRICE} Royals (a
     * Royal transfer shop-&gt;farmer, recirculating money to the land). With no room and no market
     * it pauses — demand-driven, so live FOOD stays bounded. Integer yield, no draws.
     */
    public static void pursueFarm(Actor self, ActorContext ctx, JobParams params) {
        int workplace = self.anchorCell();
        int home = homeCellOr(self, ctx, workplace);
        boolean onShift = params.inWindow(DailyRhythm.tickOfDay(ctx.tick()));
        int target = onShift ? workplace : home;
        self.setGoalTarget(TargetKind.CELL, target);
        if (self.cell() != target) {
            self.stepAlongRoute(target, true, ctx::isWalkable, ctx.occupancy());
            return;
        }
        if (self.cell() == workplace) {
            accrueFarmWork(self, ctx, params, home);
        }
    }

    private static void accrueFarmWork(Actor self, ActorContext ctx, JobParams params, int homeCell) {
        int workTicks = self.goalWorkTicks() + 1;
        if (workTicks < params.workTicksPerUnit()) {
            self.setGoalWorkTicks(workTicks);
            return;
        }
        self.setGoalWorkTicks(0);
        self.setGoalProgress((short) (self.goalProgress() + 1));
        produceFood(self, ctx, homeCell);
    }

    /**
     * One work-unit's FOOD yield, cascaded: household home larder -&gt; shared compound atrium (the
     * farmer's own work anchor) -&gt; sell surplus at the nearest same-z shop for Royals.
     */
    private static void produceFood(Actor self, ActorContext ctx, int homeCell) {
        ItemsLiteRegistry items = ctx.items();
        // 1. The farm family eats first: fill its own home-cell larder.
        if (items.countOnCellOfKind(homeCell, ItemKinds.FOOD) < FoodEconomy.LARDER_CAP) {
            ctx.recordFoodMinted(items.addOnCell(homeCell, ItemKinds.FOOD, FoodEconomy.FARM_FOOD_PER_UNIT));
            return;
        }
        // 2. The compound eats what it grew: fill the shared atrium/courtyard larder — the farmer's
        //    own work anchor, always the farmer's own band, so no cross-z channel is implied.
        int atrium = self.anchorCell();
        if (atrium != homeCell && PackedPos.z(atrium) == PackedPos.z(homeCell)
                && items.countOnCellOfKind(atrium, ItemKinds.FOOD) < FoodEconomy.LARDER_CAP) {
            ctx.recordFoodMinted(items.addOnCell(atrium, ItemKinds.FOOD, FoodEconomy.FARM_FOOD_PER_UNIT));
            return;
        }
        // 3. Both full: sell the surplus to the nearest same-z shop for Royals (money to the land).
        int shopId = nearestSameZVendor(self, ctx);
        if (shopId != Actor.NONE
                && items.countCarriedOfKind(shopId, ItemKinds.FOOD) < FoodEconomy.SHOP_STOCK_CAP
                && ctx.bankAccounts().transfer(shopId, self.id(), FoodEconomy.FARM_SELL_PRICE)) {
            ctx.recordFoodMinted(items.addCarried(shopId, ItemKinds.FOOD, FoodEconomy.FARM_FOOD_PER_UNIT));
        }
        // else: everything full and no same-z market with room/funds -> pause (demand-driven).
    }

    /** Nearest same-z vendor shop by chebyshev (ascending index tiebreak), or {@link Actor#NONE}. */
    private static int nearestSameZVendor(Actor self, ActorContext ctx) {
        FoodMarket market = ctx.foodMarket();
        ActorRegistry registry = ctx.registry();
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        int best = Actor.NONE;
        int bestDist = Integer.MAX_VALUE;
        for (int i = 0; i < market.vendorCount(); i++) {
            int shopId = market.vendorAt(i);
            int shopCell = registry.get(shopId).cell();
            if (PackedPos.z(shopCell) != selfZ) {
                continue;
            }
            int d = ActorGeometry.chebyshev(selfCell, shopCell);
            if (d < bestDist) {
                bestDist = d;
                best = shopId;
            }
        }
        return best;
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
            self.stepAlongRoute(target, false, ctx::isWalkable, ctx.occupancy());
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
            self.stepAlongRoute(target, false, ctx::isWalkable, ctx.occupancy());
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
            self.stepToward(target, true, ctx::isWalkable, ctx.occupancy());
        }
    }

    // ======================================================================
    // Arrest exposure (Villain jobs only) — ARREST-SPEC addendum, Eli's
    // 2026-07-14 directive (guards hold non-Skyrunners 1-3 days; a Skyrunner
    // is maimed on a 1st repeat offense and hanged on a 2nd, superseding
    // ACTORS-SPEC.md's older "the Watch arrests, never executes" for
    // Skyrunners specifically — see the DECISIONS.md addendum).
    // ======================================================================

    /** Chebyshev radius (same z only) within which a working Villain can be spotted. */
    private static final int ARREST_DETECT_RADIUS = 8;
    /** Per-exposure arrest chance, Q16 (~10%: 6554/65536) — placeholder pending Eli's numbers. */
    private static final int ARREST_CHANCE_Q16 = 6554;
    private static final int Q16_SCALE = 65536;
    /** Sentence floor: 1 day (DailyRhythm.DAY = 24,000 ticks). */
    private static final long SENTENCE_MIN_TICKS = 24_000L;
    /** Draw span so {@code MIN + (draw mod SPAN)} lands uniformly in [24000, 72000] (1-3 days). */
    private static final long SENTENCE_SPAN_TICKS = 48_001L;

    /**
     * True exactly when self's current wander dwell is about to complete this tick (the
     * cadence {@link #pursueWander} itself uses to decide whether to retarget) — lets a
     * {@link Job.Villain} leaf's {@code pursue()} hook {@link #checkArrestExposure} at the
     * dwell-boundary cadence (~20-40 ticks per active Villain, matching each job's
     * {@code workTicksPerUnit}) instead of every tick, without touching this shared helper's
     * own internals. Draw-free (pure state read).
     */
    public static boolean isWanderDwellComplete(Actor self, JobParams params) {
        return self.goalTargetKind() == TargetKind.CELL
                && self.cell() == self.goalTargetKey()
                && self.goalWorkTicks() + 1 >= params.workTicksPerUnit();
    }

    /**
     * Crime detection + arrest transition. Scans {@link ActorContext#registry()} by index
     * (mirrors {@code ActorsSystem.wielderId()}'s shape — no {@code .all()} allocation) for
     * any same-z actor whose bound job is a {@link Job.Watch} within
     * {@link #ARREST_DETECT_RADIUS}; on a hit, draws {@link ActorRngStream#WATCH_ARREST_CHECK}
     * and, on success, transitions {@code self} into custody: an ordinary Robber/Cutpurse is
     * {@code HELD} with a freshly drawn 1-3 day sentence (§ below); a Skyrunner's 1st offense
     * is {@code MAIMED} only (cosmetic — resumes its own job untouched, no combat system
     * exists to attach a stat penalty to); a Skyrunner's 2nd offense is permanently
     * {@code DOWNED + EXECUTED} (hanged — inert forever, never removed from the registry).
     *
     * @return {@code true} iff {@code self} is now {@code HELD} or {@code EXECUTED} — the
     *         caller's cue to skip this tick's own wander step, since one of those two
     *         policies now dominates the type's stack and will win next tick's selection
     *         regardless (this only avoids one redundant step the same tick as the transition)
     */
    public static boolean checkArrestExposure(Actor self, ActorContext ctx, Job.Villain job) {
        if (self.hasStatus(StatusBit.HELD) || self.hasStatus(StatusBit.EXECUTED)) {
            return true; // defensive: HELD/EXECUTED already dominate the policy stack
        }
        if (!watchIsNearby(self, ctx)) {
            return false;
        }
        long draw = ctx.draw(ActorRngStream.WATCH_ARREST_CHECK, self.id(), ctx.nextDrawIndex(self.id()));
        if (Long.remainderUnsigned(draw, Q16_SCALE) >= ARREST_CHANCE_Q16) {
            return false; // exposed to a nearby Watch but not caught this time
        }
        if (job instanceof Job.Villain.Skyrunner) {
            return escalateSkyrunner(self);
        }
        arrestAndHold(self, ctx);
        return true;
    }

    /**
     * Index-based scan (bounded: ~14 active Villains x one ~350-actor scan every ~30-40 ticks
     * each — cheaper than {@code ActorsSystem.wielderId()}'s precedent, which scans every
     * tick per deferring actor) for any same-z {@link Job.Watch} within detection radius.
     */
    private static boolean watchIsNearby(Actor self, ActorContext ctx) {
        ActorRegistry registry = ctx.registry();
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            if (other.jobOrdinal() < 0 || PackedPos.z(other.cell()) != selfZ) {
                continue;
            }
            if (ctx.jobs().get(other.jobOrdinal()) instanceof Job.Watch
                    && ActorGeometry.chebyshev(selfCell, other.cell()) <= ARREST_DETECT_RADIUS) {
                return true;
            }
        }
        return false;
    }

    /** Ordinary Robber/Cutpurse arrest: HELD + a freshly drawn 1-3 day sentence + a prison cell. */
    private static void arrestAndHold(Actor self, ActorContext ctx) {
        self.setOffenseCount((byte) (self.offenseCount() + 1));
        self.setStatus(StatusBit.HELD, true);
        long draw = ctx.draw(ActorRngStream.WATCH_SENTENCE_LENGTH, self.id(), ctx.nextDrawIndex(self.id()));
        long sentence = SENTENCE_MIN_TICKS + Long.remainderUnsigned(draw, SENTENCE_SPAN_TICKS);
        self.setHeldUntilTick(ctx.tick() + sentence);
        self.setAssignedHoldCell(assignPrisonCell(self, ctx));
        self.setLastReasonCode(ReasonCode.ARRESTED);
    }

    /**
     * Assigns the least-crowded free K34 prison cell (Phase-2 STEP C, Pass 10): a deterministic
     * ascending scan of {@link ActorContext#prisonCells()} that picks the cell holding the FEWEST
     * already-{@code HELD} prisoners (ties broken by lowest index), provided it is under {@link
     * Actor#MAX_OCCUPANTS_PER_CELL}. Spreading to the emptiest cell means six simultaneous arrests
     * fan out across six distinct cells (none piled in the street), and a cell only doubles up once
     * every cell already holds one — respecting the 2-occupant cap. Returns {@link Actor#NONE} when
     * no cell is wired or all are at capacity, whereupon {@link com.trojia.sim.actor.HeldPolicy}
     * falls back to the single {@link ActorContext#arrestHoldCell()}. {@code self} is not yet placed
     * (its assigned cell is still {@link Actor#NONE}), so it never counts against its own candidate.
     * Draw-free, pure scan.
     */
    private static int assignPrisonCell(Actor self, ActorContext ctx) {
        PrisonCellRegistry cells = ctx.prisonCells();
        int bestCell = Actor.NONE;
        int bestCount = Actor.MAX_OCCUPANTS_PER_CELL; // only cells strictly below the cap qualify
        for (int i = 0; i < cells.size(); i++) {
            int cell = cells.cellAt(i);
            int count = occupantsHeldAt(ctx, cell);
            if (count < bestCount) { // strict + ascending scan -> lowest-index emptiest cell wins
                bestCount = count;
                bestCell = cell;
                if (count == 0) {
                    break; // an empty cell is the global optimum; the lowest-index one is found first
                }
            }
        }
        return bestCell;
    }

    /** Count of currently-{@code HELD} actors assigned to {@code cell} (ascending-index scan). */
    private static int occupantsHeldAt(ActorContext ctx, int cell) {
        ActorRegistry registry = ctx.registry();
        int count = 0;
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            if (other.hasStatus(StatusBit.HELD) && other.assignedHoldCell() == cell) {
                count++;
            }
        }
        return count;
    }

    /** Skyrunner escalation: 1st offense maims (cosmetic), 2nd offense hangs (permanent). */
    private static boolean escalateSkyrunner(Actor self) {
        int offense = self.offenseCount() + 1;
        self.setOffenseCount((byte) offense);
        if (offense <= 1) {
            self.setStatus(StatusBit.MAIMED, true);
            self.setLastReasonCode(ReasonCode.MAIMED_FIRST_OFFENSE);
            return false; // cosmetic only — self resumes its own job untouched this tick
        }
        self.setStatus(StatusBit.DOWNED, true);
        self.setStatus(StatusBit.EXECUTED, true);
        self.setLastReasonCode(ReasonCode.EXECUTED_SECOND_OFFENSE);
        return true;
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
