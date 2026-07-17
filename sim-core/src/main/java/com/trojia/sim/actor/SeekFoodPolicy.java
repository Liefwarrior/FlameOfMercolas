package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * {@code SEEK_FOOD} (ACTORS-SPEC.md §3.3, the needs-hierarchy pass) — the acquire-and-eat state
 * machine of the money-gated market. The score is unchanged: this policy fires once HUNGER crosses
 * {@link NeedThresholds#LOW}, scoring {@code priority + urgencyBonus}, and — the
 * oscillation-hysteresis fix ({@link NeedThresholds#RECOVERED}) — keeps winning at home until
 * HUNGER is comfortably {@code RECOVERED}, so it never flip-flops back to {@code GOAL_PURSUE}
 * mid-recovery. HUNGER recovers ONLY by EATING a {@link ItemKinds#FOOD} item the actor first
 * ACQUIRES; there is no passive at-home recovery and no free-food blanket.
 *
 * <p><b>The reachability rule (the stranding fix).</b> Every tick {@link #act} first tries to eat
 * something ALREADY within {@link FoodEconomy#EAT_REACH} — carried stock, a counter it can buy at,
 * its own subsistence larder, a farm-fed commons — and a cached walk target can NEVER suppress
 * that in-reach meal (the old step-1 lock: a hungry actor pinned to a far, unroutable cell while
 * standing beside food). Only when nothing is in reach does it WALK to the nearest reachable
 * stocked same-z source, and if that source turns out to be A*-unroutable ({@link
 * Actor#routeFailedTo}) it re-scans for another instead of freezing.
 *
 * <p><b>The two ways to eat (money gates one of them):</b>
 * <ol>
 *   <li><b>Buy at a shop</b> — a same-z, stocked, AFFORDABLE shopkeeper (an ID-authorized {@link
 *       BankVerbs#buyFood} Royal transfer). The broke cannot buy — the money lever that starves the
 *       wageless margin (wastrels, the roof-slum poor) while every waged, solvent citizen eats.
 *       Reachable in place ({@link FoodEconomy#EAT_REACH}) or by walking to the nearest routable
 *       counter.</li>
 *   <li><b>Eat a subsistence larder / commons</b> — free, but stocked ONLY by real farm production:
 *       a farming household's own home-cell yield, or the shared atrium/courtyard larder its
 *       farmers fill (the compound eating what it grew). No such cell exists where no farmer works,
 *       so this never feeds a shop-dependent cohort for free — money still decides who eats there.</li>
 * </ol>
 * Eating SINKS the FOOD ({@code takeCarried}/{@code takeOnCell}); the mint/sink counts feed the
 * closed-supply conservation proof. No branch draws RNG — the whole machine is deterministic
 * (nearest source by chebyshev, ascending-id/ascending-index tiebreak).
 */
public final class SeekFoodPolicy implements BehaviorPolicy {

    @Override
    public PolicyId id() {
        return PolicyId.SEEK_FOOD;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        if (self.homeId() == Actor.NONE) {
            return 0; // not yet baked (defensive — every actor has a home post-bake, §11.4 step 5)
        }
        Home home = ctx.homes().get(self.homeId());
        int hunger = self.need(Need.HUNGER);
        if (self.cell() == home.homeCell()) {
            // Gated on RECOVERED, not merely "not LOW" (the oscillation fix): keep winning — and
            // thus keep acquiring/eating — until comfortably recovered, then release to the job.
            if (NeedThresholds.isRecovered(hunger)) {
                return 0;
            }
            NeedConfig cfg = self.stats().need(Need.HUNGER);
            int urgencyBonus = NeedThresholds.isCritical(hunger) ? cfg.critBonus() : cfg.lowBonus();
            return self.stats().seekFoodPriority() + urgencyBonus;
        }
        if (!NeedThresholds.isLow(hunger)) {
            return 0;
        }
        NeedConfig cfg = self.stats().need(Need.HUNGER);
        int urgencyBonus = NeedThresholds.isCritical(hunger) ? cfg.critBonus() : cfg.lowBonus();
        return self.stats().seekFoodPriority() + urgencyBonus;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        // A cached walk target must NEVER suppress a meal already within reach: always try to eat
        // in place THIS tick before honoring any walk (the step-1 lock fix).
        if (eatInReach(self, ctx)) {
            clearTarget(self);
            return;
        }
        // Nothing in reach: walk to the nearest reachable stocked same-z source. An unroutable
        // candidate is skipped (routeFailedTo) so the actor re-scans instead of freezing.
        int target = planWalkTarget(self, ctx);
        if (target != Actor.NONE) {
            self.setTarget(TargetKind.CELL, target);
            walkTo(self, ctx, target);
            return;
        }
        // No reachable source at all: head home, recover nothing — HUNGER falls to 0 and the actor
        // starves (the intended margin: roof decks cross-z from every source, and the broke poor).
        clearTarget(self);
        walkTo(self, ctx, ctx.homes().get(self.homeId()).homeCell());
    }

    // ======================================================================
    // Eat in place (within EAT_REACH) — never suppressed by a cached walk target
    // ======================================================================

    /**
     * Eats one FOOD from whatever source is already within {@link FoodEconomy#EAT_REACH} this tick,
     * in priority order (carried stock, a buyable counter, the own subsistence larder, a farm-fed
     * commons); returns whether it ate. No walking — this is the "eat where you stand" fast path
     * that makes the safety net robust against pathing failures and occupancy crowding.
     */
    private static boolean eatInReach(Actor self, ActorContext ctx) {
        ItemsLiteRegistry items = ctx.items();
        // 1. Eat what we already carry (a shopkeeper's shelf stock, a farmer's fresh yield, a
        //    FOOD just bought). No pathing, no cost.
        if (items.countCarriedOfKind(self.id(), ItemKinds.FOOD) > 0) {
            items.takeCarried(self.id(), ItemKinds.FOOD, 1);
            eat(self, ctx, ReasonCode.ATE_FOOD);
            return true;
        }
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        // 2. Buy from a same-z, stocked, affordable counter within reach (the money lever). The
        //    scan already rejects an unaffordable/bare shop, so a returned shop always sells here.
        int shopId = nearestAffordableVendor(self, ctx, selfCell, selfZ, FoodEconomy.EAT_REACH);
        if (shopId != Actor.NONE) {
            buyAndEat(self, ctx, shopId);
            return true;
        }
        // 3. Eat free from the own home/anchor subsistence larder within reach (farm/seed stocked).
        int larder = ownLarderInReach(self, ctx, selfCell, selfZ);
        if (larder != Actor.NONE) {
            items.takeOnCell(larder, ItemKinds.FOOD, 1);
            eat(self, ctx, ReasonCode.ATE_FOOD);
            return true;
        }
        // 4. Eat free from a same-z farm-fed commons within reach (a compound atrium / the mission).
        int commons = nearestStockedCommons(self, ctx, selfCell, selfZ, FoodEconomy.EAT_REACH, false);
        if (commons != Actor.NONE) {
            items.takeOnCell(commons, ItemKinds.FOOD, 1);
            eat(self, ctx, ReasonCode.ATE_FOOD);
            return true;
        }
        // 5. SCAVENGE (law & order pass, the broke's last resort): only when the money gate is
        //    shut — no carried FOOD (step 1), no affordable counter (step 2), no larder/commons
        //    (steps 3-4) AND genuinely broke — eat a scrap off a same-z garbage bin in reach.
        //    Strictly last in the ordering, so a solvent citizen always buys instead.
        if (isBroke(self, ctx)) {
            int bin = nearestStockedBin(self, ctx, selfCell, selfZ, FoodEconomy.EAT_REACH, false);
            if (bin != Actor.NONE) {
                items.takeOnCell(bin, ItemKinds.FOOD, 1);
                eat(self, ctx, ReasonCode.SCAVENGED_FOOD);
                return true;
            }
        }
        return false;
    }

    // ======================================================================
    // Plan a walk to the nearest reachable stocked source (route-fail aware)
    // ======================================================================

    /**
     * The cell to walk toward when no meal is in reach: the nearest reachable stocked same-z source,
     * preferring a FREE one it owns (its own subsistence larder, then a farm-fed commons) over a
     * paid counter — but a candidate the last A* could not route to ({@link Actor#routeFailedTo}) is
     * skipped so the actor tries the next one instead of pinning itself to an unreachable source.
     * {@link Actor#NONE} when this actor has no reachable stocked same-z source at all (the margin).
     */
    private static int planWalkTarget(Actor self, ActorContext ctx) {
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        // Prefer a free source the actor owns / can share, if it is routable.
        int larder = ownStockedLarder(self, ctx, selfZ);
        if (larder != Actor.NONE && !self.routeFailedTo(larder)) {
            return larder;
        }
        int commons = nearestStockedCommons(self, ctx, selfCell, selfZ, Integer.MAX_VALUE, true);
        if (commons != Actor.NONE) {
            return commons;
        }
        // Else buy: walk to the nearest affordable stocked counter that is not known-unroutable.
        int shopCell = nearestAffordableVendorCell(self, ctx, selfCell, selfZ);
        if (shopCell != Actor.NONE) {
            return shopCell;
        }
        // SCAVENGE walk (law & order pass): broke, nothing carried, no larder/commons/counter —
        // walk to the nearest same-z garbage bin holding scraps (skipping A*-unroutable bins,
        // so an isolated roof deck still starves: the intended margin). Strictly after every
        // legitimate branch above, so it never outcompetes a purchase.
        if (isBroke(self, ctx)) {
            int bin = nearestStockedBin(self, ctx, selfCell, selfZ, Integer.MAX_VALUE, true);
            if (bin != Actor.NONE) {
                return bin;
            }
        }
        // Last resort: keep heading for the own larder even if the route just failed (it may open up
        // as the crowd shifts), rather than giving up while a stocked cell exists on the band.
        return larder;
    }

    /** Broke = the money gate is shut: no carried ID at all, or a balance under one meal. */
    private static boolean isBroke(Actor self, ActorContext ctx) {
        int account = BankLedger.purchaseAuth(idCardOf(self, ctx));
        return account == Actor.NONE || ctx.bankAccounts().balanceOf(account) < FoodEconomy.FOOD_PRICE;
    }

    /**
     * The nearest same-z garbage-bin cell currently holding FOOD scraps, within {@code maxDist}
     * chebyshev (ascending index breaks ties) — the SCAVENGE source scan, shaped exactly like
     * {@link #nearestStockedCommons}. When {@code skipRouteFailed}, a bin the last A* could not
     * route to is skipped (an unroutable bin cannot pin a starving actor).
     */
    private static int nearestStockedBin(Actor self, ActorContext ctx, int selfCell, int selfZ,
            int maxDist, boolean skipRouteFailed) {
        FoodMarket market = ctx.foodMarket();
        ItemsLiteRegistry items = ctx.items();
        int best = Actor.NONE;
        int bestDist = maxDist == Integer.MAX_VALUE ? Integer.MAX_VALUE : maxDist + 1;
        for (int i = 0; i < market.garbageBinCount(); i++) {
            int cell = market.garbageBinAt(i);
            if (PackedPos.z(cell) != selfZ) {
                continue;
            }
            int d = ActorGeometry.chebyshev(selfCell, cell);
            if (d < bestDist && items.countOnCellOfKind(cell, ItemKinds.FOOD) > 0
                    && !(skipRouteFailed && self.routeFailedTo(cell))) {
                bestDist = d;
                best = cell;
            }
        }
        return best;
    }

    // ======================================================================
    // Source scans (deterministic: nearest by chebyshev, ascending tiebreak)
    // ======================================================================

    /**
     * The actor's own subsistence larder within {@link FoodEconomy#EAT_REACH}: whichever of its home
     * cell or work-anchor cell (both potentially farm/seed stocked) it is already beside and that
     * currently holds FOOD — preferred so it eats in place with no pathing — or {@link Actor#NONE}.
     */
    private static int ownLarderInReach(Actor self, ActorContext ctx, int selfCell, int selfZ) {
        int home = ctx.homes().get(self.homeId()).homeCell();
        int anchor = self.anchorCell();
        ItemsLiteRegistry items = ctx.items();
        if (PackedPos.z(home) == selfZ && ActorGeometry.chebyshev(selfCell, home) <= FoodEconomy.EAT_REACH
                && items.countOnCellOfKind(home, ItemKinds.FOOD) > 0) {
            return home;
        }
        if (anchor != home && PackedPos.z(anchor) == selfZ
                && ActorGeometry.chebyshev(selfCell, anchor) <= FoodEconomy.EAT_REACH
                && items.countOnCellOfKind(anchor, ItemKinds.FOOD) > 0) {
            return anchor;
        }
        return Actor.NONE;
    }

    /**
     * The nearer of the actor's own home / work-anchor cell that is same-z and currently stocked,
     * to walk to (used only once the in-reach check has already failed). {@link Actor#NONE} if
     * neither is same-z and holds FOOD.
     */
    private static int ownStockedLarder(Actor self, ActorContext ctx, int selfZ) {
        int home = ctx.homes().get(self.homeId()).homeCell();
        int anchor = self.anchorCell();
        ItemsLiteRegistry items = ctx.items();
        boolean homeOk = PackedPos.z(home) == selfZ && items.countOnCellOfKind(home, ItemKinds.FOOD) > 0;
        boolean anchorOk = anchor != home && PackedPos.z(anchor) == selfZ
                && items.countOnCellOfKind(anchor, ItemKinds.FOOD) > 0;
        if (homeOk && anchorOk) {
            return ActorGeometry.chebyshev(self.cell(), home) <= ActorGeometry.chebyshev(self.cell(), anchor)
                    ? home : anchor;
        }
        if (homeOk) {
            return home;
        }
        return anchorOk ? anchor : Actor.NONE;
    }

    /**
     * The nearest same-z commons cell that currently holds FOOD, within {@code maxDist} chebyshev
     * (pass {@link Integer#MAX_VALUE} for an unbounded walk-target scan), ascending index breaking
     * ties. The commons set is now a handful of farm-fed atrium/mission cells (not a district-wide
     * grid), so probing FOOD per candidate is cheap. When {@code skipRouteFailed}, a cell the last
     * A* could not route to is skipped so an unroutable commons cannot pin the actor.
     */
    private static int nearestStockedCommons(Actor self, ActorContext ctx, int selfCell, int selfZ,
            int maxDist, boolean skipRouteFailed) {
        FoodMarket market = ctx.foodMarket();
        ItemsLiteRegistry items = ctx.items();
        int best = Actor.NONE;
        int bestDist = maxDist + 1;
        for (int i = 0; i < market.commonsCount(); i++) {
            int cell = market.commonsAt(i);
            if (PackedPos.z(cell) != selfZ) {
                continue;
            }
            int d = ActorGeometry.chebyshev(selfCell, cell);
            if (d < bestDist && items.countOnCellOfKind(cell, ItemKinds.FOOD) > 0
                    && !(skipRouteFailed && self.routeFailedTo(cell))) {
                bestDist = d;
                best = cell;
            }
        }
        return best;
    }

    /**
     * The nearest same-z vendor shop within {@code maxDist} tiles that is STOCKED and that this
     * actor can AFFORD, by ascending chebyshev (ascending vendor index breaks ties) — {@link
     * Actor#NONE} if none, or if the actor has no ID card / cannot cover {@link
     * FoodEconomy#FOOD_PRICE} (the money lever). Skips self so a hungry shopkeeper does not "buy"
     * from its own counter (it eats its carry in step 1 first).
     */
    private static int nearestAffordableVendor(Actor self, ActorContext ctx, int selfCell,
            int selfZ, int maxDist) {
        int account = BankLedger.purchaseAuth(idCardOf(self, ctx));
        if (account == Actor.NONE || ctx.bankAccounts().balanceOf(account) < FoodEconomy.FOOD_PRICE) {
            return Actor.NONE;
        }
        FoodMarket market = ctx.foodMarket();
        ItemsLiteRegistry items = ctx.items();
        ActorRegistry registry = ctx.registry();
        int best = Actor.NONE;
        int bestDist = maxDist + 1; // strict-< below keeps the lowest-index shop on a distance tie
        for (int i = 0; i < market.vendorCount(); i++) {
            int shopId = market.vendorAt(i);
            if (shopId == self.id()) {
                continue;
            }
            int shopCell = registry.get(shopId).cell();
            if (PackedPos.z(shopCell) != selfZ) {
                continue;
            }
            int d = ActorGeometry.chebyshev(selfCell, shopCell);
            if (d < bestDist && items.countCarriedOfKind(shopId, ItemKinds.FOOD) > 0) {
                bestDist = d;
                best = shopId;
            }
        }
        return best;
    }

    /**
     * The CELL of the nearest same-z stocked affordable vendor to WALK to — the same scan as {@link
     * #nearestAffordableVendor} but returning the counter's cell and skipping the one counter the
     * last A* could not route to, so the actor walks to a routable market instead of re-pinning an
     * unreachable one. {@link Actor#NONE} if the actor cannot afford any / none is stocked/routable.
     */
    private static int nearestAffordableVendorCell(Actor self, ActorContext ctx, int selfCell,
            int selfZ) {
        int account = BankLedger.purchaseAuth(idCardOf(self, ctx));
        if (account == Actor.NONE || ctx.bankAccounts().balanceOf(account) < FoodEconomy.FOOD_PRICE) {
            return Actor.NONE;
        }
        FoodMarket market = ctx.foodMarket();
        ItemsLiteRegistry items = ctx.items();
        ActorRegistry registry = ctx.registry();
        int best = Actor.NONE;
        int bestDist = Integer.MAX_VALUE;
        for (int i = 0; i < market.vendorCount(); i++) {
            int shopId = market.vendorAt(i);
            if (shopId == self.id()) {
                continue;
            }
            int shopCell = registry.get(shopId).cell();
            if (PackedPos.z(shopCell) != selfZ) {
                continue;
            }
            int d = ActorGeometry.chebyshev(selfCell, shopCell);
            if (d < bestDist && items.countCarriedOfKind(shopId, ItemKinds.FOOD) > 0
                    && !self.routeFailedTo(shopCell)) {
                bestDist = d;
                best = shopCell;
            }
        }
        return best;
    }

    // ======================================================================
    // Eat / buy verbs
    // ======================================================================

    /**
     * Buys one FOOD (ID-authorized Royal transfer) then immediately eats it. {@link
     * BankVerbs#buyFood} transfers the Royals before moving the FOOD, so we only eat / count a meal
     * once the FOOD actually landed in carry (defensive — callers pre-check stock+affordability, so
     * this never fails in practice — but it keeps the conservation count exact if that ever slipped).
     */
    private static void buyAndEat(Actor self, ActorContext ctx, int shopId) {
        ItemsLiteEntry card = idCardOf(self, ctx);
        boolean bought = BankVerbs.buyFood(ctx.bankAccounts(), ctx.items(), self.id(), shopId, card,
                FoodEconomy.FOOD_PRICE, 1);
        if (bought && ctx.items().takeCarried(self.id(), ItemKinds.FOOD, 1) > 0) {
            clearTarget(self);
            eat(self, ctx, ReasonCode.BOUGHT_FOOD);
        } else {
            // Card gone / can't afford (the broke starve), or the counter was bare: no recovery.
            self.setLastReasonCode(ReasonCode.NEED_HUNGER_LOW);
        }
    }

    /** Applies the eaten meal: {@code +EAT_RESTORE} HUNGER, sink-accounted, reason-coded. */
    private static void eat(Actor self, ActorContext ctx, ReasonCode reason) {
        ctx.recordFoodEaten(1);
        self.applyNeedDelta(Need.HUNGER, FoodEconomy.EAT_RESTORE);
        self.setLastReasonCode(reason);
    }

    /** Drops the cached food target (after eating, or when the cached source went stale). */
    private static void clearTarget(Actor self) {
        self.setTarget(TargetKind.NONE, Actor.NONE);
    }

    /** This actor's carried ID card entry (authorizes its account), or {@code null}. */
    private static ItemsLiteEntry idCardOf(Actor self, ActorContext ctx) {
        int itemId = ctx.items().firstCarriedOfKind(self.id(), ItemKinds.ID_CARD);
        return itemId == Actor.NONE ? null : ctx.items().get(itemId);
    }

    /**
     * One same-z A* hop toward {@code target}; leash-ignoring (like {@code RETURN_HOME}) because a
     * hungry actor's food source routinely sits outside its work anchor's leash. Reason-coded
     * {@code NEED_HUNGER_LOW} — the actor is visibly walking to food.
     */
    private static void walkTo(Actor self, ActorContext ctx, int target) {
        self.stepAlongRoute(target, true, ctx::isWalkable, ctx.occupancy());
        self.setLastReasonCode(ReasonCode.NEED_HUNGER_LOW);
    }
}
