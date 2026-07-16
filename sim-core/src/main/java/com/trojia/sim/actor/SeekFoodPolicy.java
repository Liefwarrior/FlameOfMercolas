package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * {@code SEEK_FOOD} (ACTORS-SPEC.md §3.3, the needs-hierarchy pass) — reworked by the economy
 * loop pass into a real acquire-and-eat state machine. The score is unchanged: this policy fires
 * once HUNGER crosses {@link NeedThresholds#LOW}, scoring {@code priority + urgencyBonus}, and —
 * the oscillation-hysteresis fix ({@link NeedThresholds#RECOVERED}) — keeps winning at home until
 * HUNGER is comfortably {@code RECOVERED}, so it never flip-flops back to {@code GOAL_PURSUE}
 * mid-recovery. What changed is {@link #act}: HUNGER no longer recovers passively for standing on
 * the home cell (that crutch, {@code Actor.HUNGER_RECOVERED_PER_TICK_AT_HOME}, is deleted). A
 * hungry actor now restores HUNGER only by EATING a {@link ItemKinds#FOOD} item, which it must
 * first ACQUIRE.
 *
 * <p><b>The acquire-and-eat machine</b> (evaluated in order; every walk is same-z A* via {@link
 * Actor#stepAlongRoute}, so the z-rule is respected and no branch ever implies a cross-z step):
 * <ol>
 *   <li><b>Eat from own carry</b> — a shopkeeper's shelf stock, a farmer's fresh yield, or a FOOD
 *       just bought: consume one, {@code +}{@link FoodEconomy#EAT_RESTORE} HUNGER (no pathing).</li>
 *   <li><b>Nearby shop counter</b> — buy one FOOD at a same-z, stocked, affordable shopkeeper within
 *       {@link FoodEconomy#SHOP_NEAR} tiles via {@link BankVerbs#buyFood} (an ID-authorized Royal
 *       transfer), then eat it. This is where the cash market runs and the money lever bites (the
 *       broke cannot buy) — most working actors are near their work-site shop, so it fires often.</li>
 *   <li><b>Home / anchor larder</b> — else eat one, free, from whichever of the actor's own home
 *       cell or work-anchor cell it is at/beside (or walk to the nearer stocked one). Both are kept
 *       stocked for every non-wastrel, and an actor always dwells at one or the other, so this is
 *       the reliable, pathing-proof safety net that guarantees the middle class and the working
 *       serf mass never starve even when a shop is unreachable or out of stock.</li>
 *   <li><b>Free commons</b> — else eat one from the nearest same-z commons cell, free.</li>
 *   <li><b>No reachable source</b> — keep heading to a larder but do NOT recover: HUNGER falls to 0
 *       and the actor starves. The intended margin: roof-deck dwellers (cross-z from every source)
 *       and the wageless poor whose seed Royals ran out (wastrels get no stocked larder).</li>
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
        ItemsLiteRegistry items = ctx.items();
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);

        // Fast path: resume walking to the larder/commons cell chosen on a prior tick (cached in the
        // otherwise-unused target slot), so a walk tick does one probe instead of the full plan scan.
        if (self.targetKind() == TargetKind.CELL) {
            int cell = self.targetKey();
            if (cell != Actor.NONE && PackedPos.z(cell) == selfZ
                    && items.countOnCellOfKind(cell, ItemKinds.FOOD) > 0) {
                if (eatFromCellWhenReached(self, ctx, cell, ReasonCode.ATE_FOOD)) {
                    clearTarget(self);
                } else {
                    walkTo(self, ctx, cell);
                }
                return;
            }
            clearTarget(self); // the cached source emptied / went cross-z: replan below
        }

        // ---- Plan a fresh source (the scans run here, roughly once per hunger trip) ----

        // 1. Eat what we already carry (shopkeeper shelf stock / farmer fresh yield).
        if (items.countCarriedOfKind(self.id(), ItemKinds.FOOD) > 0) {
            items.takeCarried(self.id(), ItemKinds.FOOD, 1);
            eat(self, ctx, ReasonCode.ATE_FOOD);
            return;
        }

        // 2. Shop counter (paid): if a same-z, stocked, affordable shopkeeper is within reach, buy
        //    and eat WITHOUT walking to it — this never strands an actor chasing an unroutable
        //    counter, and it is where the cash market runs (the money lever: the broke cannot buy).
        int shopId = nearestAffordableVendor(self, ctx, selfCell, selfZ, FoodEconomy.EAT_REACH);
        if (shopId != Actor.NONE) {
            buyAndEat(self, ctx, shopId);
            clearTarget(self);
            return;
        }

        // 3. Home / anchor larder (free): the pathing-proof safety net. Eat from whichever of the
        //    actor's own home or anchor cell it is already beside; otherwise walk to the nearer
        //    stocked one (an actor always dwells at one or the other, so the nearer is reachable).
        int larder = reachableOwnLarder(self, ctx, selfCell, selfZ);
        if (larder != Actor.NONE) {
            self.setTarget(TargetKind.CELL, larder);
            if (eatFromCellWhenReached(self, ctx, larder, ReasonCode.ATE_FOOD)) {
                clearTarget(self);
            } else {
                walkTo(self, ctx, larder);
            }
            return;
        }

        // 4. Free commons cell on the same band (the pathing dead-zone backstop grid).
        int commons = nearestCommons(self, ctx, selfZ);
        if (commons != Actor.NONE) {
            self.setTarget(TargetKind.CELL, commons);
            if (eatFromCellWhenReached(self, ctx, commons, ReasonCode.ATE_FOOD)) {
                clearTarget(self);
            } else {
                walkTo(self, ctx, commons);
            }
            return;
        }

        // 5. No reachable source: keep heading to the home cell, but recover nothing — HUNGER falls
        //    to 0 (the intended margin: roof decks + the broke wageless poor).
        clearTarget(self);
        walkTo(self, ctx, ctx.homes().get(self.homeId()).homeCell());
    }

    /**
     * The actor's own reachable free larder: whichever of its home cell or work-anchor cell (both
     * kept stocked for non-wastrels) it is already within {@link FoodEconomy#EAT_REACH} of —
     * preferred so it eats in place with no pathing — else the nearer of the two that currently
     * holds FOOD, to walk to. {@link Actor#NONE} if neither is same-z and stocked. This "eat where
     * you dwell" rule, with a reach above 1, is what makes the safety net robust against both the
     * long-commute / walled-interior pathing failures AND the occupancy-cap crowding that packs a
     * 20-48-strong live-in crew a few tiles off its single shared anchor cell.
     */
    private static int reachableOwnLarder(Actor self, ActorContext ctx, int selfCell, int selfZ) {
        int home = ctx.homes().get(self.homeId()).homeCell();
        int anchor = self.anchorCell();
        ItemsLiteRegistry items = ctx.items();
        boolean homeOk = PackedPos.z(home) == selfZ
                && items.countOnCellOfKind(home, ItemKinds.FOOD) > 0;
        boolean anchorOk = anchor != home && PackedPos.z(anchor) == selfZ
                && items.countOnCellOfKind(anchor, ItemKinds.FOOD) > 0;
        // Prefer a source we can already reach (no walk, no pathing risk).
        if (homeOk && ActorGeometry.chebyshev(selfCell, home) <= FoodEconomy.EAT_REACH) {
            return home;
        }
        if (anchorOk && ActorGeometry.chebyshev(selfCell, anchor) <= FoodEconomy.EAT_REACH) {
            return anchor;
        }
        if (homeOk && anchorOk) {
            return ActorGeometry.chebyshev(selfCell, home) <= ActorGeometry.chebyshev(selfCell, anchor)
                    ? home : anchor;
        }
        if (homeOk) {
            return home;
        }
        return anchorOk ? anchor : Actor.NONE;
    }

    /**
     * Buys one FOOD (ID-authorized Royal transfer) then immediately eats it. {@link
     * BankVerbs#buyFood} transfers the Royals before moving the FOOD, so we only eat / count a meal
     * once the FOOD actually landed in carry (defensive — callers pre-check stock, so the shop is
     * never empty here — but this keeps the conservation count exact even if that ever slipped).
     */
    private static void buyAndEat(Actor self, ActorContext ctx, int shopId) {
        ItemsLiteEntry card = idCardOf(self, ctx);
        boolean bought = BankVerbs.buyFood(ctx.bankAccounts(), ctx.items(), self.id(), shopId, card,
                FoodEconomy.FOOD_PRICE, 1);
        if (bought && ctx.items().takeCarried(self.id(), ItemKinds.FOOD, 1) > 0) {
            eat(self, ctx, ReasonCode.BOUGHT_FOOD);
        } else {
            // Card gone / can't afford (the broke starve), or the counter was bare: no recovery.
            self.setLastReasonCode(ReasonCode.NEED_HUNGER_LOW);
        }
    }

    /** Eats one FOOD off {@code cell} when within {@link FoodEconomy#EAT_REACH}; returns if it ate. */
    private static boolean eatFromCellWhenReached(Actor self, ActorContext ctx, int cell,
            ReasonCode reason) {
        if (ActorGeometry.chebyshev(self.cell(), cell) <= FoodEconomy.EAT_REACH
                && ctx.items().takeOnCell(cell, ItemKinds.FOOD, 1) > 0) {
            eat(self, ctx, reason);
            return true;
        }
        return false;
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
     * The nearest same-z commons cell by chebyshev (ascending index breaks ties). Deliberately does
     * NOT probe stock per candidate — the commons grid can be a few hundred cells and an O(items)
     * probe on each would dominate the tick; the import refills every commons to cap each period and
     * only the rare stranded actor draws on one, so a commons is effectively always stocked, and the
     * on-arrival {@code takeOnCell} handles the exceptional empty cell (the fast path then replans).
     */
    private static int nearestCommons(Actor self, ActorContext ctx, int selfZ) {
        FoodMarket market = ctx.foodMarket();
        int selfCell = self.cell();
        int best = Actor.NONE;
        int bestDist = Integer.MAX_VALUE;
        for (int i = 0; i < market.commonsCount(); i++) {
            int cell = market.commonsAt(i);
            if (PackedPos.z(cell) != selfZ) {
                continue;
            }
            int d = ActorGeometry.chebyshev(selfCell, cell);
            if (d < bestDist) {
                bestDist = d;
                best = cell;
            }
        }
        return best;
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
