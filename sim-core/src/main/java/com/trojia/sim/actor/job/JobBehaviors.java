package com.trojia.sim.actor.job;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorContext;
import com.trojia.sim.actor.ActorGeometry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorRngStream;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.CorridorPinch;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.FishingSpots;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.FoodEconomy;
import com.trojia.sim.actor.FoodMarket;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.PrisonCellRegistry;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillChecks;
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
    // Job training (Sprint 5 "the awards wave", PROGRESSION-SPEC.md §2 map)
    // ======================================================================

    /**
     * Awards one discrete work event's use-XP to the TRUE doer (Sprint 5 "every job trains
     * its trade"): the job's bind-resolved {@code trainSkillRaw}/{@code trainCp} pair, at
     * exactly the three seams that mark a completed unit of real work — anchor/farm
     * work-unit completion, patrol corner/waypoint ARRIVAL, wander dwell completion — and
     * NEVER per tick (§3.2 rule 4, the Morrowind Athletics lesson). The satiation context is
     * the event's cell (§3.3): an anchor worker grinds one context toward the 25% floor
     * while a patrol's waypoints each stay fresh — which is why the raws price patrol cp
     * under the anchor scale (the multi-context inflation guard,
     * {@code JobTrainingCommittedTest}).
     *
     * <p>Draw-free and shape-free by construction: no RNG stream is touched and no new
     * persisted state exists — awards flow through the same {@link
     * com.trojia.sim.actor.SkillTrackRegistry#award} path every existing site uses. XP lands
     * on {@code self.id()} — the body doing the work, disguised or not (only SOCIAL reads
     * key on presentedId). Beasts award nothing because no beast job declares training
     * (raws-pinned); non-training params ({@link JobParams#TRAINS_NOTHING}) and unwired
     * registries no-op here.</p>
     */
    private static void awardWorkEvent(Actor self, ActorContext ctx, JobParams params,
            int eventCell) {
        // Sprint 6 (Eli's bug 1 — "no one has a way to gain DUTY"): honest work at one's
        // station refills DUTY. Applied at exactly the training pair's three seams — unit
        // completion / waypoint-or-corner arrival / dwell completion — never per tick, so
        // the gain is priced per discrete work event exactly like cp. Data-driven per job
        // (jobs.json dutyPerUnit, the trainsSkill pattern); a 0 is a no-op delta and the
        // saturating clamp bounds a busy day at NeedThresholds.MAX. Draw-free.
        if (params.dutyPerUnit() > 0) {
            self.applyNeedDelta(Need.DUTY, params.dutyPerUnit());
        }
        if (!params.trains()) {
            return;
        }
        ctx.skillTracks().award(self.id(), params.trainSkillRaw(), params.trainCp(),
                eventCell, ctx.tick());
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
     * Chebyshev radius around the work anchor within which work COUNTS (Sprint 6, the
     * crowded-anchor fix): under ONE-per-square, a crew of 10-50 sharing a single-cell
     * anchor could only ever have ONE member standing on it — everyone else ringed it for
     * the whole shift, completing no units, earning no training and (since slice 1) no
     * DUTY, and churning hundreds of thousands of hover-shoves trying to claim the cell
     * (measured: 177/692 souls DUTY-bottomed and ~580k pushes in a 15k-tick soak). Working
     * anywhere within this reach of the anchor counts as working the site — the whole
     * ring works in parallel, visibly spread by the occupancy cap — and an in-reach worker
     * STOPS walking (no more churn against the held cell).
     */
    public static final int WORK_REACH = 2;

    /** Whether {@code self} stands close enough to {@code workplace} for work to count. */
    private static boolean withinWorkReach(Actor self, int workplace) {
        return PackedPos.z(self.cell()) == PackedPos.z(workplace)
                && ActorGeometry.chebyshev(self.cell(), workplace) <= WORK_REACH;
    }

    /**
     * PURSUE (commute-aware anchor cycle): during the job's rhythm window the
     * actor heads for its work anchor and accrues work once within
     * {@link #WORK_REACH} of it (the crowded-anchor fix — an exact-cell
     * requirement starved every crew bigger than one under the 1-per-square
     * cap); outside the window it heads for its home cell. For the common case
     * where {@code anchorCell == home} (a household worker with no distinct
     * second location) both legs resolve to the same cell — an in-place work
     * accrual with no movement. For an actor whose anchor is a real workplace
     * distinct from home (a commuter), the two legs become a genuine daily
     * round trip driven by the ONE clock the job already owns — its rhythm
     * window — so "off shift" is the shift ending, not the actor type's generic
     * night window (which stays RETURN_HOME's separate concern).
     */
    public static void pursueAtAnchor(Actor self, ActorContext ctx, JobParams params) {
        int workplace = self.anchorCell();
        int home = homeCellOr(self, ctx, workplace);
        boolean onShift = params.inWindow(DailyRhythm.tickOfDay(ctx.tick()));
        if (onShift && withinWorkReach(self, workplace)) {
            // Close enough to work the site: accrue in place — no step, no hover-churn
            // against whoever holds the exact anchor cell.
            self.setGoalTarget(TargetKind.CELL, workplace);
            accrueWork(self, ctx, params);
            return;
        }
        int target = onShift ? workplace : home;
        self.setGoalTarget(TargetKind.CELL, target);
        if (self.cell() != target) {
            // Commuting home<->work legitimately crosses the work leash (a home
            // routinely sits outside the workplace anchor's leash), exactly as
            // RETURN_HOME's walk home does — so this leg ignores the leash too.
            self.stepAlongRoute(target, true, ctx::isWalkable, ctx.occupancy());
        }
    }

    /** COMPLETE check: pure, {@code goalProgress >= unitsToComplete}. */
    public static boolean isCompleteAtUnits(Actor self, JobParams params) {
        return self.goalProgress() >= params.unitsToComplete();
    }

    private static void accrueWork(Actor self, ActorContext ctx, JobParams params) {
        int workTicks = self.goalWorkTicks() + 1;
        if (workTicks >= params.workTicksPerUnit()) {
            self.setGoalProgress((short) (self.goalProgress() + 1));
            self.setGoalWorkTicks(0);
            // Sprint 5: a completed work-unit is the anchor cycle's discrete work event —
            // the job's trade skill trains here. Context = the ANCHOR (the site being
            // worked), not the worker's own ring cell: with WORK_REACH a crew spans up to
            // 25 cells, and per-cell contexts would let one site evade the §3.3 satiation
            // floor 25 times over.
            awardWorkEvent(self, ctx, params, self.anchorCell());
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
        if (onShift && withinWorkReach(self, workplace)) {
            // The crowded-anchor fix (see pursueAtAnchor): the whole plot crew tends the
            // rows within WORK_REACH of the plot marker, in parallel, without hover-churn.
            self.setGoalTarget(TargetKind.CELL, workplace);
            accrueFarmWork(self, ctx, params, home);
            return;
        }
        int target = onShift ? workplace : home;
        self.setGoalTarget(TargetKind.CELL, target);
        if (self.cell() != target) {
            self.stepAlongRoute(target, true, ctx::isWalkable, ctx.occupancy());
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
        // Sprint 5: the farm's completed unit is the same discrete work event as the anchor
        // cycle's — fieldcraft trains at the plot (context = the plot ANCHOR, the
        // accrueWork satiation reasoning).
        awardWorkEvent(self, ctx, params, self.anchorCell());
        produceFood(self, ctx, homeCell);
        // Sprint 5 "mastery pays": a veteran's hands yield more. Each completed unit accrues
        // fieldcraftLevel toward a 100-point bonus threshold; every crossing mints ONE bonus
        // FOOD through the SAME demand-capped cascade (LARDER_CAP -> atrium -> SHOP_STOCK_CAP
        // + shop funds — the caps are the blast-radius bound; recordFoodMinted keeps the
        // conservation identity by construction). The accumulator is realized STATELESSLY off
        // the already-persisted goalProgress — bonus = crossings of (level * units)/100 within
        // the cycle — so a resumed save yields byte-identically to a continuous run and no new
        // persisted scalar exists; the sub-100 residual at cycle end deliberately drops.
        int bonus = yieldBonusUnits(masteryLevel(self, ctx, params), self.goalProgress());
        for (int i = 0; i < bonus; i++) {
            produceFood(self, ctx, homeCell);
        }
    }

    /** The doer's live level in the job's own trained skill (0 untrained/unwired). */
    private static int masteryLevel(Actor self, ActorContext ctx, JobParams params) {
        if (!params.trains()) {
            return 0;
        }
        return ctx.skillTracks().level(self.id(), params.trainSkillRaw());
    }

    /**
     * Bonus FOOD earned by the {@code completedUnits}-th work-unit of the current cycle at
     * mastery {@code level}: the number of 100-point boundaries {@code level * units} crosses
     * at this unit — a pure integer accumulator reconstructed from persisted state (level ~
     * +1% yield per level: 0 at level 0, ~1 per 3 units at 40, capped by construction at
     * {@code level/100 + 1} per unit). Public for the mastery-yield unit test (pure math).
     */
    public static int yieldBonusUnits(int level, int completedUnits) {
        if (level <= 0 || completedUnits <= 0) {
            return 0;
        }
        return (level * completedUnits) / 100 - (level * (completedUnits - 1)) / 100;
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
            if (PackedPos.z(shopCell) != selfZ || registry.get(shopId).isDead()) {
                continue; // Sprint 6: a dead vendor's counter is shut (estate frozen)
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

    /**
     * SELECT for a looping route — with the Sprint-6 phase stagger (Eli's bug 2, the
     * pile-up fix's scheduling half): instead of every beat starting at index 0 in
     * lockstep, the starting waypoint/corner is offset deterministically by the actor's
     * own id, so two Watch bound to the SAME route (or two identical square beats on
     * adjacent anchors — the guardhouse garrison pair) walk out of phase and never
     * co-occupy a narrow segment for a whole shift. A pure function of
     * {@code (id, route shape)} — no draw, no new state; the offset rides the
     * already-persisted {@code goalProgress} exactly as the waypoint index does.
     */
    public static void selectRouteStart(Actor self, ActorContext ctx) {
        int route = ctx.patrolRoutes().routeContaining(self.anchorCell());
        int offset;
        if (route >= 0 && ctx.patrolRoutes().waypointCount(route) > 0) {
            offset = Math.floorMod(self.id(), ctx.patrolRoutes().waypointCount(route));
        } else {
            offset = Math.floorMod(self.id(), 4); // square beat: stagger the starting corner
        }
        self.setGoalProgress((short) offset);
        self.setGoalWorkTicks(0);
    }

    /**
     * Ticks a patrol leg may stand completely still (blocked by a full cell it may not
     * shove through — since Sprint 6 a guard never shoves an on-duty guard) before the
     * beat YIELDS: it gives up the contested leg and advances to the next waypoint/corner,
     * dissolving guard-vs-guard head-on jams by walking away instead of wrestling
     * (Eli's bug 2 — "guards pushing each other... shouldn't last this long"). Sized
     * well above the speed accumulator's every-other-tick no-move rhythm, so only a
     * genuinely wedged guard yields.
     */
    public static final int PATROL_BLOCKED_YIELD_TICKS = 40;

    /**
     * Ticks a route leg may keep STEPPING without ever getting closer to its waypoint before
     * the beat yields — the progress-toward-goal budget, and the hole
     * {@link #PATROL_BLOCKED_YIELD_TICKS} could not see.
     *
     * <p>S6's yield counts ticks spent standing <em>dead still</em>, and zeroes the instant the
     * actor moves. Two souls meeting head-on in a 1-wide connector do not stand still: each
     * one's cached route goes through the other, the occupancy cap refuses the hop, the route
     * is dropped and replanned, and the pair shuffle back and forth stepping every tick
     * forever. Every one of those ticks reads as "moving, therefore fine" to a stillness
     * counter, which is why Sergeant #371 could work the Saltgate Rise until roughly tick
     * 15,000 and then spend the remaining 45,000 walking on the spot with no metric able to
     * say so. A LIVE-lock is not a dead-lock and needs its own clock.
     *
     * <p>Sized for real detours, not for the pathological case. A route leg legitimately walks
     * AWAY from its waypoint whenever the only door faces the wrong way or the only stair up
     * stands behind the walker; at {@code speedTicksPerStep=2} this budget is about a hundred
     * cells of such walking, far more than any leg on the authored routes needs and far less
     * than a live-lock, which never ends at all.
     */
    public static final int PATROL_NO_PROGRESS_YIELD_TICKS = 200;

    /**
     * Stall weight of a DEAD-STILL tick, so one counter carries both budgets: a still tick is
     * worth {@code PATROL_NO_PROGRESS_YIELD_TICKS / PATROL_BLOCKED_YIELD_TICKS} units and a
     * stepping-but-not-closing tick is worth one. A wedged guard therefore still yields on its
     * 40th motionless tick, bit-for-bit S6's number, while a live-locked one yields on its
     * 200th fruitless step — two budgets, one already-persisted scalar, no new state.
     */
    private static final int PATROL_STILL_STALL_WEIGHT =
            PATROL_NO_PROGRESS_YIELD_TICKS / PATROL_BLOCKED_YIELD_TICKS;

    /**
     * Deterministic de-phase of the stall budget by soul id. Two guards wedged against each
     * other start their stall clocks on the same tick, so a shared budget would fire for both
     * on the same tick — both would yield, both would turn around, and the pair would meet
     * again in lockstep: a polite deadlock instead of a rude one. Spreading the budget over a
     * band of ids means whoever is going to give way gives way first, and the other walks
     * through the freed connector. Same discipline as {@link #selectRouteStart}'s corner
     * stagger: fixed arithmetic on the id, never a draw.
     */
    private static final int PATROL_STALL_DEPHASE = 37;

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
    public static void pursuePatrol(Actor self, ActorContext ctx, int radius, JobParams params) {
        if (self.goalTargetKind() != TargetKind.CELL) {
            retargetPatrolCorner(self, ctx, radius);
        }
        int target = self.goalTargetKey();
        if (self.cell() != target) {
            int before = self.cell();
            self.stepAlongRoute(target, false, ctx::isWalkable, ctx.occupancy());
            if (self.cell() != before) {
                self.setGoalWorkTicks(0); // moving: the blocked-leg clock stays clean
                return;
            }
            // Sprint 6 yield (Eli's bug 2): a leg standing dead still — a full cell it may
            // not shove through (a fellow guard) — is abandoned after a bounded wait; the
            // beat advances to its next corner and walks away instead of wrestling.
            int blocked = self.goalWorkTicks() + 1;
            if (blocked >= PATROL_BLOCKED_YIELD_TICKS) {
                self.setGoalProgress((short) ((Math.floorMod(self.goalProgress(), 4) + 1) % 4));
                self.setGoalTarget(TargetKind.NONE, Actor.NONE);
                self.setGoalWorkTicks(0);
            } else {
                self.setGoalWorkTicks(blocked);
            }
            return;
        }
        // Sprint 5: reaching a beat corner is the square patrol's discrete work event —
        // the beat's trade skill trains here (context = the corner cell; each corner is its
        // own §3.3 context, which is why patrol cp is priced under the anchor scale).
        awardWorkEvent(self, ctx, params, target);
        self.setGoalProgress((short) ((Math.floorMod(self.goalProgress(), 4) + 1) % 4));
        self.setGoalTarget(TargetKind.NONE, Actor.NONE); // force next leg's corner to be revalidated
    }

    /**
     * Deterministically shrinks the radius along the current leg's fixed corner
     * direction — {@code radius, radius-1, ...} for up to
     * {@link #PATROL_RETRY_BUDGET} attempts (capped at {@code radius} itself) —
     * and caches the first ACCEPTABLE candidate as the leg's target. Falls back to
     * {@code anchorCell()} (guaranteed walkable by spawn/bake) if nothing in
     * budget qualifies. Draw-free (fixed geometry, not randomness), bounded,
     * deterministic.
     *
     * <p>S7 (the beat stops walking into cages): "acceptable" used to mean merely
     * WALKABLE, which is why blind radius-6 corners settled inside 1x1 prison cages
     * and 1-wide alley stubs — a corner two guards cannot share, and cannot pass each
     * other on, is a pile-up generator by construction. A candidate must now also be
     * un-{@link CorridorPinch#isPinched pinched}: its narrower walkable axis must be
     * wider than one cell. The shrink walks the whole budget looking for open ground
     * first and only then, rather than give up the leg, accepts the best merely-walkable
     * candidate it saw — so a guard genuinely quartered in a tight pocket keeps a target
     * instead of collapsing onto its anchor every leg.
     */
    private static void retargetPatrolCorner(Actor self, ActorContext ctx, int radius) {
        int leg = Math.floorMod(self.goalProgress(), 4);
        int ax = PackedPos.x(self.anchorCell());
        int ay = PackedPos.y(self.anchorCell());
        int z = PackedPos.z(self.anchorCell());
        int attempts = Math.min(radius, PATROL_RETRY_BUDGET);
        int walkableFallback = Actor.NONE;
        for (int attempt = 0; attempt < attempts; attempt++) {
            int r = radius - attempt;
            int tx = clamp(ax + PATROL_DX[leg] * r, PackedPos.X_MASK);
            int ty = clamp(ay + PATROL_DY[leg] * r, PackedPos.Y_MASK);
            int candidate = PackedPos.pack(tx, ty, z);
            if (!ctx.isWalkable(candidate)) {
                continue;
            }
            if (!CorridorPinch.isPinched(candidate, ctx::isWalkable)) {
                self.setGoalTarget(TargetKind.CELL, candidate);
                return;
            }
            if (walkableFallback == Actor.NONE) {
                walkableFallback = candidate; // widest pinched corner seen — keep it in reserve
            }
        }
        self.setGoalTarget(TargetKind.CELL,
                walkableFallback == Actor.NONE ? self.anchorCell() : walkableFallback);
    }

    /**
     * PURSUE (off shift): head for the home cell and stop working — the branch every
     * other job's pursue already has (see {@link #pursueAtAnchor}, {@link #pursueFarm},
     * {@link #pursueFishing}, {@link #pursueRounds}, each gated on
     * {@code params.inWindow(tickOfDay)}) and the one {@code watch.patrol} was missing.
     * Leash-ignoring like every commute (a home routinely sits outside the work anchor's
     * leash) and cross-z capable, exactly as {@code RETURN_HOME}'s own walk home is.
     *
     * <p>The patrol's blocked-leg clock ({@code goalWorkTicks}) keeps its honest meaning
     * across the shift boundary: zeroed when the actor is home or made ground this tick,
     * incremented when a step attempt genuinely failed. It is not zeroed unconditionally —
     * that would launder a guard stuck all night out of the blocked-spell metric.
     * No new state: reads {@code params} and {@code homeCellOr}, both already present.
     *
     * <p><b>The walk home is a COMMUTE, not a goal, so it clears {@code goalTarget}
     * instead of parking the bed in it.</b> The first draft cached {@code CELL(homeCell)}
     * here, and that one line paid a guard for sleeping: at the dawn tick the window
     * reopens, {@link #pursuePatrol} sees a {@code CELL} target already set and therefore
     * SKIPS {@link #retargetPatrolCorner}, adopts the bed as this leg's "corner", finds
     * itself standing on it, and fires {@code awardWorkEvent} — {@code dutyPerUnit} DUTY
     * and a full {@code trainCp} of streetwise for having stood still all night. DUTY
     * restoration is an EARNED seam (Sprint 6, Eli's bug 1); it must not be payable from
     * one's own bunk. Clearing the target means the first on-shift tick ALWAYS re-derives
     * a genuine beat corner, so the only cell that can pay a patrol work event is a cell
     * the beat actually walked to. Route-bound patrollers were never affected —
     * {@link #pursueRoutePatrol} reads {@code goalProgress}, never {@code goalTarget}.
     */
    public static void pursueOffShiftHome(Actor self, ActorContext ctx) {
        int home = homeCellOr(self, ctx, self.anchorCell());
        self.setGoalTarget(TargetKind.NONE, Actor.NONE);
        if (self.cell() == home) {
            self.setGoalWorkTicks(0);
            return;
        }
        int before = self.cell();
        self.stepAlongRoute(home, true, ctx::isWalkable, ctx.occupancy(), ctx.zLinks());
        self.setGoalWorkTicks(self.cell() != before ? 0 : self.goalWorkTicks() + 1);
    }

    // ======================================================================
    // Waypoint route patrol (law & order pass, Pass 13) — the REAL beat
    // ======================================================================

    /**
     * PURSUE (waypoint route patrol): walk the ordered waypoint list of {@code routeIndex}
     * (a baked {@link com.trojia.sim.actor.PatrolRouteTable} route) waypoint {@code i} →
     * {@code i+1} → … , wrapping forever — the genuine Tarwalk/quay/Ropewynd beat replacing
     * the blind square loop for every Watch whose anchor sits on a route. The current
     * waypoint index rides the already-persisted {@code goalProgress} (no new scalar); legs
     * are single-z by route construction (the z-rule) and leash-ignoring like a commute (a
     * route's far end routinely sits outside the anchor leash). An UNREACHABLE waypoint —
     * the route-following A* failed and that failure is still cached ({@link
     * Actor#routeFailedTo}) — is skipped by advancing to the next waypoint instead of
     * freezing on the failed leg. Draw-free, never completes.
     *
     * <p><b>S7 round 3 — the leg yields on PROGRESS, not on stillness.</b> The S6 yield asked
     * "did this soul move?", which is the wrong question for the jam it was written against: a
     * pair meeting head-on in a 1-wide connector both keep moving forever (see
     * {@link #PATROL_NO_PROGRESS_YIELD_TICKS}). The leg now keeps a HIGH-WATER MARK — the
     * cell from which it has come closest to this waypoint — and a step that beats the mark is
     * progress (clock zeroed, mark advanced); anything else, moving or not, is stall. The mark
     * rides {@code goalTarget}, which a route-bound patroller has always left unused ({@link
     * #pursuePatrol} reads it, this method never did), so the fix adds no persisted state and
     * the mark is always a cell this actor stood on and therefore always walkable.
     */
    public static void pursueRoutePatrol(Actor self, ActorContext ctx, int routeIndex,
            JobParams params) {
        var routes = ctx.patrolRoutes();
        int count = routes.waypointCount(routeIndex);
        if (count == 0) {
            return; // degenerate empty route: deterministic no-op
        }
        int index = Math.floorMod(self.goalProgress(), count);
        int waypoint = routes.waypoint(routeIndex, index);
        if (self.cell() == waypoint) {
            // Sprint 5: waypoint ARRIVAL is the route patrol's discrete work event — the
            // beat's trade skill trains here (context = the waypoint cell). The failed-leg
            // skip below deliberately awards nothing: skipping is not arriving.
            awardWorkEvent(self, ctx, params, waypoint);
            advanceRouteLeg(self, index, count); // arrived: next leg next tick
            return;
        }
        // Sprint 4 (the climb): an OPT-IN cross-z consumer — a route may now carry
        // waypoints on different bands (the Saltgate Rise beat, z11<->z13); legs between
        // same-z waypoints are byte-identical to the pre-climb walker.
        int before = self.cell();
        self.stepAlongRoute(waypoint, true, ctx::isWalkable, ctx.occupancy(), ctx.zLinks());
        if (self.routeFailedTo(waypoint)) {
            // Route-failure cache says this waypoint is unreachable right now: skip it rather
            // than freeze the whole beat on one blocked leg (Pass-13 DoD).
            advanceRouteLeg(self, index, count);
            return;
        }
        int mark = self.goalTargetKind() == TargetKind.CELL ? self.goalTargetKey() : Actor.NONE;
        if (mark == Actor.NONE
                || legDistance(self.cell(), waypoint) < legDistance(mark, waypoint)) {
            // Genuine ground gained (or the first tick of the leg): re-mark and start clean.
            self.setGoalTarget(TargetKind.CELL, self.cell());
            self.setGoalWorkTicks(0);
            return;
        }
        // Sprint 6 yield (Eli's bug 2), re-aimed: a leg that is not closing on its waypoint —
        // whether wedged dead still against a full cell it may not shove through, or shuffling
        // back and forth against a fellow guard in a 1-wide gut — is abandoned after a bounded
        // wait; the patrol advances to the next waypoint and walks away instead of wrestling.
        // One counter, two budgets (see PATROL_STILL_STALL_WEIGHT), on the persisted
        // goalWorkTicks this method already owned.
        int stall = self.goalWorkTicks()
                + (self.cell() == before ? PATROL_STILL_STALL_WEIGHT : 1);
        if (stall >= PATROL_NO_PROGRESS_YIELD_TICKS
                + Math.floorMod(self.id(), PATROL_STALL_DEPHASE)) {
            advanceRouteLeg(self, index, count);
        } else {
            self.setGoalWorkTicks(stall);
        }
    }

    /**
     * Leave the current leg for the next waypoint: advance the index, zero the stall clock and
     * DROP the high-water mark, so the new leg re-marks from wherever the actor is standing
     * rather than inheriting the old leg's best cell.
     */
    private static void advanceRouteLeg(Actor self, int index, int count) {
        self.setGoalProgress((short) ((index + 1) % count));
        self.setGoalWorkTicks(0);
        self.setGoalTarget(TargetKind.NONE, Actor.NONE);
    }

    /**
     * How far a route leg still has to go, counting bands: x/y Chebyshev plus one unit per
     * band still to climb. The z term is what lets a cross-z leg (the Saltgate Rise) register
     * the climb itself as progress — a soul that steps from z:+11 to z:+12 has plainly gained
     * ground on a z:+13 waypoint even when its x/y distance did not move. Pure integer
     * geometry; no draws, no allocation.
     */
    private static int legDistance(int cell, int waypoint) {
        return ActorGeometry.chebyshev(cell, waypoint)
                + Math.abs(PackedPos.z(cell) - PackedPos.z(waypoint));
    }

    // ======================================================================
    // Fishing (Sprint 6, Eli's bug 6) — spots, perception, casts, the catch
    // ======================================================================

    /** Carried FISH that triggers a sell trip to the nearest counter with room and coin. */
    public static final int FISHER_SELL_TRIP_CATCH = 3;
    /** FISH a fisher keeps back from every sale (its own supper — fish is food). */
    public static final int FISHER_KEEP_RATION = 1;
    /** Chebyshev reach of the counter exchange (the pickpocket/talk adjacency shape). */
    public static final int FISHER_SELL_REACH = 2;
    /** Carried FISH beyond which a marketless fisher stops casting (the carry bound). */
    public static final int FISHER_CARRY_CAP = 8;
    /** FISHING base cp per completed cast — XP on ATTEMPTS, caught or not (Eli's spec). */
    public static final int FISHING_CAST_CP = 60;

    /**
     * PURSUE (fisher): during the shift — work the water. Carrying a sell-trip's worth of
     * catch, walk to the nearest counter with room and coin and SELL through the buy-side
     * verb (the coin faucet); otherwise target the nearest live spot this soul can SEE
     * (perception-gated per actor — an invisible spot is not targetable), walk to its cast
     * cell (cross-z legal: pier decks stand one band over the water) and cast:
     * {@code workTicksPerUnit} ticks per cast, one discrete work event per completed cast
     * (FISHING XP on the attempt + DUTY), then the {@code check.fishing} catch draw —
     * success mints the spot's yield into the carry (accounted). No visible spot (or an
     * unroutable stand): wander the anchored waterfront — the fisher visibly waits the
     * water, which alone puts daily traffic on the dead piers. Off shift: home.
     */
    public static void pursueFishing(Actor self, ActorContext ctx, JobParams params) {
        boolean onShift = params.inWindow(DailyRhythm.tickOfDay(ctx.tick()));
        if (!onShift) {
            int home = homeCellOr(self, ctx, self.anchorCell());
            self.setGoalTarget(TargetKind.CELL, home);
            if (self.cell() != home) {
                self.stepAlongRoute(home, true, ctx::isWalkable, ctx.occupancy(), ctx.zLinks());
            }
            return;
        }
        int catchCount = ctx.items().countCarriedOfKind(self.id(), ItemKinds.FISH);
        if (catchCount >= FISHER_SELL_TRIP_CATCH && trySellCatch(self, ctx, catchCount)) {
            return;
        }
        FishingSpots spots = ctx.fishingSpots();
        int slot = nearestTargetableSpot(self, ctx, spots);
        if (slot == Actor.NONE || catchCount >= FISHER_CARRY_CAP) {
            pursueWander(self, ctx, params); // wait the water near the anchored pier
            return;
        }
        int castCell = spots.castCellAt(slot);
        self.setGoalTarget(TargetKind.CELL, castCell);
        if (!withinWorkReach(self, castCell)) {
            // WORK_REACH here too: two fishers may share one authored stand's stretch of
            // deck without hover-churning over the exact cell (the crowded-anchor fix).
            self.setGoalWorkTicks(0);
            self.stepAlongRoute(castCell, true, ctx::isWalkable, ctx.occupancy(), ctx.zLinks());
            return;
        }
        int castTicks = self.goalWorkTicks() + 1;
        if (castTicks < params.workTicksPerUnit()) {
            self.setGoalWorkTicks(castTicks);
            return;
        }
        self.setGoalWorkTicks(0);
        // One completed cast = one discrete work event: the training pair (FISHING cp — XP
        // on the ATTEMPT, caught or not) + DUTY, then the catch draw itself.
        awardWorkEvent(self, ctx, params, castCell);
        resolveCastAttempt(self, ctx, slot, false);
    }

    /**
     * Resolves ONE cast at live spot {@code slot} (shared by the fisher job and the
     * play-mode fish verb): the {@code check.fishing} named draw against the fisher's
     * {@code fishing + AGI} vs the spot size's resist; success mints the spot's yield into
     * the carry (conservation-accounted). When {@code awardCastXp}, the FISHING attempt-XP
     * is awarded here too (the play-mode path — the job path already awarded through its
     * own params pair). Reason-coded CAUGHT_FISH / FISH_GOT_AWAY.
     */
    public static void resolveCastAttempt(Actor self, ActorContext ctx, int slot,
            boolean awardCastXp) {
        FishingSpots spots = ctx.fishingSpots();
        if (awardCastXp) {
            var tracks = ctx.skillTracks();
            tracks.award(self.id(), tracks.fishingRaw(), FISHING_CAST_CP,
                    spots.castCellAt(slot), ctx.tick());
        }
        long draw = ctx.draw(ActorRngStream.CHECK_FISHING, self.id(),
                ctx.nextDrawIndex(self.id()));
        int permille = SkillChecks.fishCatchPermille(ctx.skillTracks(), self.id(),
                spots.catchResistAt(slot));
        if (SkillChecks.passes(draw, permille)) {
            int minted = ctx.items().addCarried(self.id(), ItemKinds.FISH, spots.yieldAt(slot));
            ctx.recordFishMinted(minted);
            self.setLastReasonCode(ReasonCode.CAUGHT_FISH);
        } else {
            self.setLastReasonCode(ReasonCode.FISH_GOT_AWAY);
        }
    }

    /**
     * The nearest live spot this soul can SEE whose cast cell is not known-unroutable —
     * chebyshev to the cast cell, ascending slot breaking ties. {@link Actor#NONE} when the
     * water shows this soul nothing (low FISHING = most spots stay invisible).
     */
    private static int nearestTargetableSpot(Actor self, ActorContext ctx, FishingSpots spots) {
        int best = Actor.NONE;
        int bestDist = Integer.MAX_VALUE;
        for (int s = 0; s < spots.slotCapacity(); s++) {
            if (!spots.isLive(s)
                    || !spots.visibleTo(s, ctx.worldSeed(), self.id(), ctx.skillTracks())
                    || self.routeFailedTo(spots.castCellAt(s))) {
                continue;
            }
            int d = ActorGeometry.chebyshev(self.cell(), spots.castCellAt(s));
            if (d < bestDist) {
                bestDist = d;
                best = s;
            }
        }
        return best;
    }

    /**
     * The sell trip: walk to the nearest vendor whose counter has meal room and whose
     * pocket holds coin, and sell everything above {@link #FISHER_KEEP_RATION} through
     * {@link com.trojia.sim.actor.BankVerbs#sellToShop} once within {@link
     * #FISHER_SELL_REACH}. Returns {@code false} when no such counter exists right now —
     * the caller keeps fishing (bounded by {@link #FISHER_CARRY_CAP}).
     */
    private static boolean trySellCatch(Actor self, ActorContext ctx, int catchCount) {
        int shopId = nearestVendorWithRoom(self, ctx);
        if (shopId == Actor.NONE) {
            return false;
        }
        int shopCell = ctx.registry().get(shopId).cell();
        if (PackedPos.z(shopCell) == PackedPos.z(self.cell())
                && ActorGeometry.chebyshev(self.cell(), shopCell) <= FISHER_SELL_REACH) {
            int itemId = ctx.items().firstCarriedOfKind(self.id(), ItemKinds.ID_CARD);
            var card = itemId == Actor.NONE ? null : ctx.items().get(itemId);
            int sold = com.trojia.sim.actor.BankVerbs.sellToShop(ctx.bankAccounts(),
                    ctx.items(), self.id(), shopId, card, FoodEconomy.FISH_BUY_PRICE,
                    ItemKinds.FISH, catchCount - FISHER_KEEP_RATION);
            if (sold > 0) {
                self.setLastReasonCode(ReasonCode.SOLD_CATCH);
            }
            return sold > 0;
        }
        self.setGoalTarget(TargetKind.CELL, shopCell);
        self.setGoalWorkTicks(0);
        self.stepAlongRoute(shopCell, true, ctx::isWalkable, ctx.occupancy(), ctx.zLinks());
        return true;
    }

    /**
     * The nearest same-z vendor with meal room under the counter cap, coin in its pocket
     * for at least one fish, a carried ID on the seller's side to settle against, and no
     * cached route failure — ascending chebyshev, ascending index tiebreak.
     */
    private static int nearestVendorWithRoom(Actor self, ActorContext ctx) {
        FoodMarket market = ctx.foodMarket();
        ItemsLiteRegistry items = ctx.items();
        ActorRegistry registry = ctx.registry();
        int selfZ = PackedPos.z(self.cell());
        int best = Actor.NONE;
        int bestDist = Integer.MAX_VALUE;
        for (int i = 0; i < market.vendorCount(); i++) {
            int shopId = market.vendorAt(i);
            if (shopId == self.id() || registry.get(shopId).isDead()) {
                continue; // Sprint 6: a dead vendor buys nothing (estate frozen)
            }
            int shopCell = registry.get(shopId).cell();
            int stock = items.countCarriedOfKind(shopId, ItemKinds.FOOD)
                    + items.countCarriedOfKind(shopId, ItemKinds.FISH);
            if (PackedPos.z(shopCell) != selfZ || stock >= FoodEconomy.SHOP_STOCK_CAP
                    || ctx.bankAccounts().balanceOf(shopId) < FoodEconomy.FISH_BUY_PRICE
                    || self.routeFailedTo(shopCell)) {
                continue;
            }
            int d = ActorGeometry.chebyshev(self.cell(), shopCell);
            if (d < bestDist) {
                bestDist = d;
                best = shopId;
            }
        }
        return best;
    }

    // ======================================================================
    // Carter rounds (Sprint 6, Eli's bug 4) — the multi-stop work circuit
    // ======================================================================

    /**
     * SELECT (rounds): stagger the starting stop by actor id (the {@link #selectRouteStart}
     * discipline) so a whole crew bound to one circuit fans out across its stops instead of
     * marching in lockstep. Draw-free; the offset rides the persisted {@code goalProgress}.
     */
    public static void selectRoundsStart(Actor self, ActorContext ctx) {
        var rounds = ctx.workRounds();
        int route = rounds.routeContaining(self.anchorCell());
        int offset = route >= 0 && rounds.waypointCount(route) > 0
                ? Math.floorMod(self.id(), rounds.waypointCount(route))
                : 0;
        self.setGoalProgress((short) offset);
        self.setGoalWorkTicks(0);
        self.setGoalTarget(TargetKind.CELL, self.anchorCell());
    }

    /**
     * PURSUE (carter rounds): during the rhythm window, walk the bound circuit's ordered
     * stops — one {@code workTicksPerUnit} dwell of honest work per stop, then on to the
     * next, wrapping forever — so the warehouse, the quay markers and the stores all see
     * real traffic through the day (Eli's bug 4). Outside the window, walk home. The
     * current stop index rides {@code goalProgress}; the dwell/blocked clock rides
     * {@code goalWorkTicks} (dwelling at the stop, or waiting out a human wall while
     * traveling — the {@link #PATROL_BLOCKED_YIELD_TICKS} yield, then skip the stop). An
     * A*-unreachable stop is skipped via the route-failure cache like a patrol's dead leg.
     * A carter whose anchor matches no baked circuit — or an unwired table — degrades to
     * the plain {@link #pursueAtAnchor} laborer cycle, byte-identical to pre-rounds bakes.
     * Cross-z stops are legal (the climb's opt-in table). Draw-free.
     */
    public static void pursueRounds(Actor self, ActorContext ctx, JobParams params) {
        var rounds = ctx.workRounds();
        int route = rounds.routeContaining(self.anchorCell());
        if (route < 0 || rounds.waypointCount(route) == 0) {
            pursueAtAnchor(self, ctx, params); // unwired/unbound: the plain laborer cycle
            return;
        }
        boolean onShift = params.inWindow(DailyRhythm.tickOfDay(ctx.tick()));
        if (!onShift) {
            int home = homeCellOr(self, ctx, self.anchorCell());
            self.setGoalTarget(TargetKind.CELL, home);
            if (self.cell() != home) {
                self.stepAlongRoute(home, true, ctx::isWalkable, ctx.occupancy(),
                        ctx.zLinks());
            }
            return;
        }
        int count = rounds.waypointCount(route);
        int index = Math.floorMod(self.goalProgress(), count);
        int stop = rounds.waypoint(route, index);
        self.setGoalTarget(TargetKind.CELL, stop);
        if (!withinWorkReach(self, stop)) {
            int before = self.cell();
            self.stepAlongRoute(stop, true, ctx::isWalkable, ctx.occupancy(), ctx.zLinks());
            if (self.routeFailedTo(stop)) {
                self.setGoalProgress((short) ((index + 1) % count)); // dead stop: skip it
                self.setGoalWorkTicks(0);
            } else if (self.cell() != before) {
                self.setGoalWorkTicks(0); // moving: the blocked clock stays clean
            } else {
                int blocked = self.goalWorkTicks() + 1;
                if (blocked >= PATROL_BLOCKED_YIELD_TICKS) {
                    self.setGoalProgress((short) ((index + 1) % count)); // walled: walk away
                    self.setGoalWorkTicks(0);
                } else {
                    self.setGoalWorkTicks(blocked);
                }
            }
            return;
        }
        int worked = self.goalWorkTicks() + 1;
        if (worked >= params.workTicksPerUnit()) {
            // One stop worked to completion = one discrete work event (training + DUTY).
            awardWorkEvent(self, ctx, params, stop);
            self.setGoalProgress((short) ((index + 1) % count));
            self.setGoalWorkTicks(0);
        } else {
            self.setGoalWorkTicks(worked);
        }
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
    /**
     * Ticks a wander leg may spend traveling before the target is declared a lost cause and
     * redrawn. Generous: the widest committed wander envelope (gull leash 48 → radius 24) is
     * ~50 direct cells plus detours at 1 tile/tick — a leg that has not arrived in {@value}
     * ticks is stuck, not slow. Bounds BOTH stall modes the beast-pass soak surfaced (below).
     */
    private static final int WANDER_TRAVEL_BUDGET_TICKS = 200;

    public static void pursueWander(Actor self, ActorContext ctx, JobParams params) {
        if (self.goalTargetKind() != TargetKind.CELL) {
            retargetWander(self, ctx);
            return;
        }
        int target = self.goalTargetKey();
        if (self.cell() != target) {
            int before = self.cell();
            self.stepAlongRoute(target, false, ctx::isWalkable, ctx.occupancy());
            if (self.cell() == target) {
                self.setGoalWorkTicks(0); // arrived: the dwell count starts clean next tick
                return;
            }
            if (self.routeFailedTo(target)) {
                // Stall mode 1 — an UNREACHABLE target (walled pocket, over-budget detour):
                // stepAlongRoute retries the failed search on its 500-tick cooldown while this
                // pursue never re-targeted, freezing the sweep forever (the beast-pass gull
                // soak surfaced actors parked on one cell for thousands of ticks). Draw a
                // fresh target instead — the pursueRoutePatrol failed-leg skip, applied here.
                retargetWander(self, ctx);
                return;
            }
            if (self.cell() == before) {
                // Stall mode 2 — a ROUTABLE target whose next hop is held at the occupancy
                // cap (residents parked in a doorway/corridor for a whole shift): A* is
                // occupancy-blind, so the identical route is replanned and blocked every
                // tick with routeFailedTo never set. Drift one random orthogonal cell (the
                // LOITER shuffle discipline: named draw, leash-respecting, walkability- and
                // occupancy-checked) so a crowd-boxed actor bleeds out through transient
                // gaps instead of standing wedged for thousands of ticks.
                driftOneCell(self, ctx);
            }
            // Stall backstop — the travel budget rides the already-persisted goalWorkTicks
            // (unused while traveling; reset on arrival above and by every retarget), so a
            // leg that cannot complete costs a bounded wait and one redraw, never the sweep.
            int travelTicks = self.goalWorkTicks() + 1;
            if (travelTicks >= WANDER_TRAVEL_BUDGET_TICKS) {
                retargetWander(self, ctx);
            } else {
                self.setGoalWorkTicks(travelTicks);
            }
            return;
        }
        int dwell = self.goalWorkTicks() + 1;
        if (dwell >= params.workTicksPerUnit()) {
            // Sprint 5: a completed dwell is the wander sweep's discrete work event — the
            // job's trade skill trains here (context = the dwell cell; the Keeper tending
            // among the pens). Travel-budget/stall retargets above award nothing: only a
            // dwell WORKED to completion is a qualifying use. Beast/villain wander params
            // carry no training, so this line is a no-op for them by raws construction.
            awardWorkEvent(self, ctx, params, self.cell());
            retargetWander(self, ctx);
        } else {
            self.setGoalWorkTicks(dwell);
        }
    }

    /** Bounded redraw budget for {@link #retargetWander} — same named draw, never unbounded. */
    private static final int WANDER_RETRY_BUDGET = 8;

    /**
     * Draws a fresh nearby cell within the wander radius; if the draw lands on an
     * unwalkable cell (water, a wall) — or, for a BEAST, inside a citizen's hearth (below) —
     * redraws with the next {@code JOB_TARGET_PICK} index (the same named-RNG stream, never
     * unnamed/unseeded randomness) up to {@link #WANDER_RETRY_BUDGET} attempts, then falls
     * back to {@code self.anchorCell()} (guaranteed walkable by spawn/bake) — bounded, can
     * never infinite-loop.
     *
     * <p><b>Beasts never wander into somebody's bedroom (density revisit fix pass):</b> a
     * beast's idle sweep rejects draw cells within {@link #HEARTH_RADIUS} of any citizen's
     * home cell. A walkable condo/bunkroom interior is a death trap for a beast under the
     * 1-per-square cap — the resident household parks the room to saturation for whole
     * shifts, and a beast that drifts in is crowd-locked while its prey lives outside
     * (soak-measured: gull#410 spent 15,000 ticks starving between a bunkroom's dead-end
     * alcove and its neck; the map pass had already flagged "wide envelopes reach into
     * crewed/walled interiors where an occupancy wedge can outlast the hunger buffer" and
     * could only shrink leashes around it). Behaviorally honest, not just defensive: gulls
     * sweep quays and streets, cats prowl yards and lanes — neither forages in an occupied
     * bedroom. Citizens' own wanders (Wastrel beg circuits) are exempt — people may call on
     * houses. Draw-free (a pure ascending-id registry/home scan per candidate).
     */
    private static void retargetWander(Actor self, ActorContext ctx) {
        boolean beast = ctx.presentedJob(self) instanceof Job.Beast;
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
            if (ctx.isWalkable(candidate) && !(beast && isCitizenHearthArea(ctx, candidate))) {
                self.setGoalTarget(TargetKind.CELL, candidate);
                self.setGoalWorkTicks(0);
                return;
            }
        }
        self.setGoalTarget(TargetKind.CELL, self.anchorCell());
        self.setGoalWorkTicks(0);
    }

    /** Chebyshev radius around a citizen's home cell that a beast's wander never targets. */
    private static final int HEARTH_RADIUS = 2;

    /**
     * Whether {@code cell} lies within {@link #HEARTH_RADIUS} of the home cell of any CITIZEN
     * (any actor whose presented job is not {@link Job.Beast} — a mouse den or a gull roost is
     * itself a beast home and must never block its own species' sweep). Ascending-id registry
     * scan, same z, draw-free.
     */
    private static boolean isCitizenHearthArea(ActorContext ctx, int cell) {
        ActorRegistry registry = ctx.registry();
        int z = PackedPos.z(cell);
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            if (other.homeId() == Actor.NONE || ctx.presentedJob(other) instanceof Job.Beast) {
                continue;
            }
            int home = ctx.homes().get(other.homeId()).homeCell();
            if (PackedPos.z(home) == z && ActorGeometry.chebyshev(cell, home) <= HEARTH_RADIUS) {
                return true;
            }
        }
        return false;
    }

    /** Half the leash (min 4): a sweep that visibly ranges without leaving the leashed range. */
    private static int wanderRadius(Actor self) {
        return Math.max(4, self.stats().leashRadius() / 2);
    }

    /** W/E/N/S drift offsets (orthogonal only — the corner rule makes diagonals wall-gated). */
    private static final int[] DRIFT_DX = {-1, 1, 0, 0};
    private static final int[] DRIFT_DY = {0, 0, -1, 1};

    /**
     * One random orthogonal escape step for an occupancy-blocked wanderer (stall mode 2 in
     * {@link #pursueWander}): a named {@code ACTOR_WANDER} draw picks a heading, and the
     * ordinary leash-respecting, walkability- and occupancy-checked {@code stepToward}
     * commits it or no-ops. Deterministic (named stream, per-actor draw counter).
     */
    private static void driftOneCell(Actor self, ActorContext ctx) {
        long draw = ctx.draw(ActorRngStream.ACTOR_WANDER, self.id(), ctx.nextDrawIndex(self.id()));
        int heading = (int) Long.remainderUnsigned(draw, DRIFT_DX.length);
        int x = clamp(PackedPos.x(self.cell()) + DRIFT_DX[heading], PackedPos.X_MASK);
        int y = clamp(PackedPos.y(self.cell()) + DRIFT_DY[heading], PackedPos.Y_MASK);
        self.stepToward(PackedPos.pack(x, y, PackedPos.z(self.cell())), false,
                ctx::isWalkable, ctx.occupancy());
    }

    // ======================================================================
    // Prey scurry (Mouse, living-docks beast pass) — wander + vigilance + nibble
    // ======================================================================

    /** Predator-vigilance scan cadence (absolute tick % PERIOD == 0) — the exposure-scan shape. */
    static final int PREY_SENSE_PERIOD_TICKS = 8;
    /** Same-z chebyshev radius at which a nearby gull/cat scares a mouse. */
    static final int PREY_SENSE_RADIUS = 6;
    /**
     * SAFETY debit per vigilance hit: three consecutive hits (a predator LINGERING ~24 ticks)
     * drive SAFETY under CRITICAL (1000) and the shared {@code FleePolicy} takes over, while a
     * fast direct chase usually catches the mouse before it panics (the hunt-closure math
     * depends on catches actually landing). The mouse raws' {@code safety.recoverPerTick 25}
     * ends the panic in ~40 ticks.
     */
    static final int PREY_SCARE = 3500;
    /**
     * HUNGER restored at each wander-dwell boundary — the den nibble (crumbs and spilled grain
     * around the den hole; every dwell is within the den radius by construction). Deliberately
     * NOT the bin-scrap item channel: mice consuming the daily {@code BIN_SCRAP_CAP} scraps
     * would eat the wastrel scavenge margin — the nibble touches no item, so the FOOD
     * conservation identity and the citizen margins are byte-identical.
     */
    static final int MOUSE_NIBBLE_RESTORE = 1500;

    private static final ActorTypeId PREDATOR_FERAL = ActorTypeId.of("feral");
    private static final ActorTypeId PREDATOR_CAT = ActorTypeId.of("cat");

    /**
     * PURSUE (prey scurry): {@link #pursueWander} plus the two mouse hooks, nested inside the
     * job's own pursue exactly the way the villain leaves hook {@code checkArrestExposure}:
     * (1) on the vigilance cadence, a same-z ascending-index scan for any gull/cat within
     * {@link #PREY_SENSE_RADIUS} debits SAFETY by {@link #PREY_SCARE} (a lingering predator
     * eventually triggers FLEE); (2) at the wander-dwell boundary the mouse nibbles the den
     * (+{@link #MOUSE_NIBBLE_RESTORE} HUNGER, no FOOD item). Draw-free beyond the wander's own
     * named draws; all scans ascending-index, same-z, chebyshev, allocation-free.
     */
    public static void pursuePreyScurry(Actor self, ActorContext ctx, JobParams params) {
        if (ctx.tick() % PREY_SENSE_PERIOD_TICKS == 0 && predatorIsNear(self, ctx)) {
            self.applyNeedDelta(Need.SAFETY, -PREY_SCARE);
        }
        if (isWanderDwellComplete(self, params)) {
            self.applyNeedDelta(Need.HUNGER, MOUSE_NIBBLE_RESTORE);
            self.setLastReasonCode(ReasonCode.NIBBLED_DEN);
        }
        pursueWander(self, ctx, params);
    }

    /** Any same-z gull/cat within {@link #PREY_SENSE_RADIUS} chebyshev (ascending-index scan). */
    private static boolean predatorIsNear(Actor self, ActorContext ctx) {
        ActorRegistry registry = ctx.registry();
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            ActorTypeId type = other.typeId();
            if ((!type.equals(PREDATOR_FERAL) && !type.equals(PREDATOR_CAT))
                    || other.isDead()) {
                continue; // Sprint 6: a dead predator scares no mouse
            }
            int cell = other.cell();
            if (PackedPos.z(cell) == selfZ
                    && ActorGeometry.chebyshev(selfCell, cell) <= PREY_SENSE_RADIUS) {
                return true;
            }
        }
        return false;
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
    /** Hard cap on the streetwise-deepened detect/sense radius (Sprint 5 "mastery pays"). */
    public static final int SENSE_RADIUS_CAP = 11;
    /** Streetwise levels per +1 tile of a guard's detect/sense radius. */
    public static final int SENSE_LEVELS_PER_TILE = 25;

    /**
     * A guard's streetwise-deepened sense radius (Sprint 5 "mastery pays"): {@code base +
     * streetwise/25}, HARD-CAPPED at {@link #SENSE_RADIUS_CAP} — a veteran of the ward
     * senses further, bounded so the justice pipeline's equilibrium can only shift inside a
     * 3-tile band (the 274/691 house-arrest incident is the cautionary tale). Reads the
     * guard's TRUE body (skill reads never key on presentedId). Degrades to {@code base}
     * exactly for every guard under streetwise 25 and wherever tracks are unwired.
     */
    public static int guardSenseRadius(int base, ActorContext ctx, int guardId) {
        var tracks = ctx.skillTracks();
        int radius = base + tracks.level(guardId, tracks.streetwiseRaw()) / SENSE_LEVELS_PER_TILE;
        return Math.min(SENSE_RADIUS_CAP, radius);
    }
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
            return escalateSkyrunner(self, ctx);
        }
        arrestAndHold(self, ctx);
        return true;
    }

    /**
     * Index-based scan (bounded: ~14 active Villains x one ~350-actor scan every ~30-40 ticks
     * each — cheaper than {@code ActorsSystem.wielderId()}'s precedent, which scans every
     * tick per deferring actor) for any same-z {@link Job.Watch} within detection radius —
     * since Sprint 5 EACH watch's own streetwise deepens its radius ({@link
     * #guardSenseRadius}, capped): a rookie spots at 8, Sergeant Vess at 9, a level-75
     * veteran would top out at 11.
     */
    private static boolean watchIsNearby(Actor self, ActorContext ctx) {
        ActorRegistry registry = ctx.registry();
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            if (other.jobOrdinal() < 0 || PackedPos.z(other.cell()) != selfZ
                    || other.isDead()) {
                continue; // Sprint 6: a dead guard deters no one
            }
            if (ctx.jobs().get(other.jobOrdinal()) instanceof Job.Watch
                    && ActorGeometry.chebyshev(selfCell, other.cell())
                            <= guardSenseRadius(ARREST_DETECT_RADIUS, ctx, other.id())) {
                return true;
            }
        }
        return false;
    }

    /** Ordinary Robber/Cutpurse arrest: HELD + a freshly drawn 1-3 day sentence + a prison cell. */
    private static void arrestAndHold(Actor self, ActorContext ctx) {
        long draw = ctx.draw(ActorRngStream.WATCH_SENTENCE_LENGTH, self.id(), ctx.nextDrawIndex(self.id()));
        arrestAndHold(self, ctx, SENTENCE_MIN_TICKS + Long.remainderUnsigned(draw, SENTENCE_SPAN_TICKS));
    }

    /**
     * The shared arrest transition (villain-exposure drawn sentences AND the guard-side
     * APPREHEND's fixed 1-day loiter sentence, law &amp; order pass): bumps {@code offenseCount},
     * sets {@link StatusBit#HELD} with an absolute-tick sentence end, assigns the
     * least-crowded free prison cell, and reason-codes the transition. Draw-free — a caller
     * wanting a random sentence draws it first. From here {@link
     * com.trojia.sim.actor.HeldPolicy} dominates the offender's stack, escorts it to the
     * assigned cell, and releases it at exactly {@code heldUntilTick}.
     */
    public static void arrestAndHold(Actor offender, ActorContext ctx, long sentenceTicks) {
        offender.setOffenseCount((byte) (offender.offenseCount() + 1));
        offender.setStatus(StatusBit.HELD, true);
        offender.setHeldUntilTick(ctx.tick() + sentenceTicks);
        offender.setAssignedHoldCell(assignPrisonCell(offender, ctx));
        offender.setLastReasonCode(ReasonCode.ARRESTED);
        // Faction ledger (Sprint 1): every arrest — exposure-drawn or APPREHEND-fixed —
        // stains the PRESENTED identity's Watch standing and warms the Skyrunner
        // brotherhood to it (the Persona rule: the Watch booked who it BELIEVES it booked;
        // Sprint 2's unmasking moves the stain to the true id). Deterministic clamped
        // deltas; a no-op when unwired.
        ctx.factionStandings().onArrest(offender.identity().presentedId());
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

    /**
     * Skyrunner escalation: 1st offense maims (cosmetic), 2nd offense hangs — and since
     * Sprint 6 a hanging actually KILLS: {@link StatusBit#DEAD} lands alongside
     * DOWNED + EXECUTED, the death is announced by name ({@link ActorContext#recordDeath}),
     * and from the next tick the corpse is skipped by {@code tickAll} and vacates its tile.
     * Public since Sprint 2: the guard-side theft correction ({@code ApprehendPolicy})
     * applies the same discipline to a Skyrunner caught red-handed lifting purses.
     */
    public static boolean escalateSkyrunner(Actor self, ActorContext ctx) {
        int offense = self.offenseCount() + 1;
        self.setOffenseCount((byte) offense);
        if (offense <= 1) {
            self.setStatus(StatusBit.MAIMED, true);
            self.setLastReasonCode(ReasonCode.MAIMED_FIRST_OFFENSE);
            return false; // cosmetic only — self resumes its own job untouched this tick
        }
        self.setStatus(StatusBit.DOWNED, true);
        self.setStatus(StatusBit.EXECUTED, true);
        self.setStatus(StatusBit.DEAD, true); // Sprint 6: the noose is real
        self.setLastReasonCode(ReasonCode.EXECUTED_SECOND_OFFENSE);
        ctx.recordDeath(self.id(), ReasonCode.EXECUTED_SECOND_OFFENSE);
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
