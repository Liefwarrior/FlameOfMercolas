package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * THE COUNTER SALE (S8): a soul carrying MATERIALS walks up to a shopkeeper and turns them
 * into Royals. This is the last link of the sprint's playable loop — cull a downed mouse, take
 * the scalp, carry it, sell it at a counter — and it is deliberately the SAME verb the AI
 * fishers already use ({@link BankVerbs#sellToShop}, the citizen coin faucet), so the player
 * and the ward transact through one exchange with one set of rules.
 *
 * <p>Conservation-exact by construction, because it does nothing itself: the Royals TRANSFER
 * settles before the items MOVE, and neither side mints or sinks. A scalp sold at a counter is
 * still live in the closed-supply proof — it has changed hands, not ceased to exist.
 *
 * <p>Price comes from {@link TradeGoods#basePriceOf}. A kind priced 0 is NOT FOR SALE (quest
 * tokens, identity papers) and is skipped rather than moved for nothing.
 */
public final class SellVerb {

    /** Chebyshev reach of the counter exchange (mirrors the fisher sell trip's reach). */
    public static final int SELL_REACH = 2;

    private SellVerb() {
    }

    /**
     * Sells every carried MATERIALS stack this soul holds to the nearest same-z living
     * shopkeeper within {@link #SELL_REACH}, at the standing base price, in ascending kind-id
     * order (the determinism rule). Returns the total units sold; {@code 0} when there is no
     * counter in reach, no priced materials in the pack, no ID card to settle against, or the
     * counter has no coin.
     *
     * <p>Reason-coded on the seller: SOLD_GOODS on any sale, NO_BUYER_IN_REACH otherwise —
     * the observer toasts off those stamps.
     */
    public static int sellMaterialsInReach(Actor self, ActorContext ctx) {
        int shopId = nearestBuyer(self, ctx);
        if (shopId == Actor.NONE) {
            self.setLastReasonCode(ReasonCode.NO_BUYER_IN_REACH);
            return 0;
        }
        int cardItem = ctx.items().firstCarriedOfKind(self.id(), ItemKinds.ID_CARD);
        ItemsLiteEntry card = cardItem == Actor.NONE ? null : ctx.items().get(cardItem);
        int sold = 0;
        for (int i = 0; i < TradeGoods.count(); i++) {
            TradeGoods.Entry row = TradeGoods.at(i);
            if (row.category() != TradeGoods.Category.MATERIALS || row.basePrice() <= 0) {
                continue;
            }
            int held = ctx.items().countCarriedOfKind(self.id(), row.kind());
            if (held <= 0) {
                continue;
            }
            sold += BankVerbs.sellToShop(ctx.bankAccounts(), ctx.items(), self.id(), shopId,
                    card, row.basePrice(), row.kind(), held);
        }
        self.setLastReasonCode(sold > 0 ? ReasonCode.SOLD_GOODS : ReasonCode.NO_BUYER_IN_REACH);
        return sold;
    }

    /**
     * The nearest same-z LIVING shopkeeper within reach with coin in its pocket — ascending
     * chebyshev, ascending index tiebreak. Reads the baked {@link FoodMarket} vendor roster,
     * the same list the fisher sell trip walks (a counter is a counter).
     */
    public static int nearestBuyer(Actor self, ActorContext ctx) {
        FoodMarket market = ctx.foodMarket();
        int selfZ = PackedPos.z(self.cell());
        int best = Actor.NONE;
        int bestDist = SELL_REACH + 1;
        for (int i = 0; i < market.vendorCount(); i++) {
            int shopId = market.vendorAt(i);
            if (shopId == self.id() || ctx.registry().get(shopId).isDead()) {
                continue;
            }
            int shopCell = ctx.registry().get(shopId).cell();
            if (PackedPos.z(shopCell) != selfZ || ctx.bankAccounts().balanceOf(shopId) <= 0) {
                continue;
            }
            int d = ActorGeometry.chebyshev(self.cell(), shopCell);
            if (d <= SELL_REACH && d < bestDist) {
                bestDist = d;
                best = shopId;
            }
        }
        return best;
    }
}
