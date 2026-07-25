package com.trojia.sim.actor;

/**
 * The counter-side money verbs (Phase-2 STEP B, Pass 9): the callable, unit-tested primitives a
 * Phase-3 behavior trigger will fire when a citizen reaches the banker or a shop counter. Each is
 * a pure function over the {@link BankLedger} + {@link ItemsLiteRegistry}, keyed off the account
 * an {@link ItemKinds#ID_CARD} authorizes (stolen-card semantics: authorization is the stamped
 * account, not the carrier — {@link BankLedger#purchaseAuth}).
 *
 * <p><b>The two-currency model.</b> Loose {@link ItemKinds#COIN} is pocket specie; {@code Royals}
 * are a ledger balance. The deposit/withdraw counter is the <em>only</em> Coin&lt;-&gt;Royal
 * on/off-ramp, and it moves physical Coins to/from the vault chest in lockstep with the ledger so
 * {@link BankLedger#totalRoyals()} always equals the vault's COIN count. Institutions
 * ({@link #buyFood}) transact in Royals only — loose Coins are refused (never read), so the only
 * way to spend at a shop is a card-authorized ledger transfer.
 */
public final class BankVerbs {

    private BankVerbs() {
    }

    /**
     * Deposit: move ALL of {@code citizen}'s loose {@link ItemKinds#COIN} into the vault chest and
     * credit the card-authorized account by exactly what landed (STEP A's dest-added return). A
     * Coin-&gt;Royal on-ramp: vault COIN count and {@code totalRoyals()} both rise by the same
     * amount, so the conservation invariant holds. Returns the amount deposited (0 if the card
     * authorizes no account or no vault is wired).
     */
    public static long depositCoins(BankLedger bank, ItemsLiteRegistry items, int vaultChestCell,
            int citizenId, ItemsLiteEntry idCard) {
        int account = BankLedger.purchaseAuth(idCard);
        if (account == Actor.NONE || vaultChestCell == Actor.NONE) {
            return 0;
        }
        int loose = items.countCarriedOfKind(citizenId, ItemKinds.COIN);
        int moved = items.moveCarriedToCell(citizenId, vaultChestCell, ItemKinds.COIN, loose);
        bank.credit(account, moved);
        return moved;
    }

    /**
     * Withdraw: debit {@code amount} Royals from the card-authorized account and move that many
     * physical Coins out of the vault chest into the citizen's carry. A Royal-&gt;Coin off-ramp;
     * both the ledger and the vault drop by {@code amount}, preserving the invariant. Rejects
     * (returns 0, no state change) an unauthorized card, an unwired vault, a non-positive amount,
     * or an insufficient balance. Returns the amount actually withdrawn.
     */
    public static long withdrawRoyals(BankLedger bank, ItemsLiteRegistry items, int vaultChestCell,
            int citizenId, ItemsLiteEntry idCard, long amount) {
        int account = BankLedger.purchaseAuth(idCard);
        if (account == Actor.NONE || vaultChestCell == Actor.NONE || amount <= 0
                || bank.balanceOf(account) < amount) {
            return 0;
        }
        bank.debit(account, amount);
        // The vault holds totalRoyals() coins and account <= totalRoyals(), so the move never
        // short-changes: exactly `amount` coins land in the citizen's carry.
        return items.moveCellToCarried(vaultChestCell, citizenId, ItemKinds.COIN,
                (int) Math.min(amount, Integer.MAX_VALUE));
    }

    /**
     * Buy food at a shop: an ID-authorized Royal transfer from the buyer's account to the shop's
     * account ({@code shopId}, the bake convention {@code accountId == actorId}), then {@code
     * foodQty} FOOD moves shop-&gt;buyer in ItemsLite. Loose Coins are refused — payment is a
     * ledger transfer keyed off the buyer's carried card, never physical specie. Returns
     * {@code false} (no state change) if the card authorizes no account or the buyer's balance is
     * insufficient (the transfer is rejected before any FOOD moves).
     */
    public static boolean buyFood(BankLedger bank, ItemsLiteRegistry items, int buyerId, int shopId,
            ItemsLiteEntry buyerIdCard, long price, int foodQty) {
        int buyerAccount = BankLedger.purchaseAuth(buyerIdCard);
        if (buyerAccount == Actor.NONE) {
            return false; // no ID -> no institutional purchase (loose Coins are not accepted)
        }
        if (!bank.transfer(buyerAccount, shopId, price)) {
            return false; // insufficient Royals -> the whole purchase is refused, no FOOD moves
        }
        items.moveCarried(shopId, buyerId, ItemKinds.FOOD, foodQty);
        return true;
    }

    /**
     * Buy ONE meal at a shop counter (Sprint 6): the {@link #buyFood} exchange generalized
     * over the counter's meal stock — the shop vends a {@link ItemKinds#FOOD} if it has one,
     * else a {@link ItemKinds#FISH} it bought off a citizen ({@link #sellToShop}), closing
     * the fish loop: fisher → shop → hungry buyer. Same discipline: ID-authorized Royal
     * transfer BEFORE the item moves, loose Coins refused. Returns the kind vended, or
     * {@code 0} (no state change) when the buyer has no card / cannot pay / the counter
     * holds no meal.
     */
    public static short buyMeal(BankLedger bank, ItemsLiteRegistry items, int buyerId, int shopId,
            ItemsLiteEntry buyerIdCard, long price) {
        int buyerAccount = BankLedger.purchaseAuth(buyerIdCard);
        if (buyerAccount == Actor.NONE) {
            return 0;
        }
        short kind = items.countCarriedOfKind(shopId, ItemKinds.FOOD) > 0 ? ItemKinds.FOOD
                : items.countCarriedOfKind(shopId, ItemKinds.FISH) > 0 ? ItemKinds.FISH
                : 0;
        if (kind == 0 || !bank.transfer(buyerAccount, shopId, price)) {
            return 0;
        }
        items.moveCarried(shopId, buyerId, kind, 1);
        return kind;
    }

    /**
     * The buy-side counter (Sprint 6, Eli's bug 5 — "shopkeepers to both sell AND buy...
     * keep it a simple exchange"): a citizen SELLS carried goods to a shopkeeper — item
     * for coin — paid from the shopkeeper's OWN ledger account ({@code shopId}, the bake
     * convention {@code accountId == actorId}). The citizen coin FAUCET: the only
     * legitimate way street labor (a caught fish, a surplus good) turns back into Royals.
     * Conservation-exact by construction: the Royals TRANSFER (never mint — an
     * insufficient shop pocket clamps the lot to what it can afford) settles BEFORE the
     * items move, and the items MOVE seller → shop (never mint/sink). Returns the units
     * actually sold ({@code 0} when the seller has no card, carries none of the kind, or
     * the shop is broke).
     */
    public static int sellToShop(BankLedger bank, ItemsLiteRegistry items, int sellerId,
            int shopId, ItemsLiteEntry sellerIdCard, long unitPrice, short kind, int qty) {
        int sellerAccount = BankLedger.purchaseAuth(sellerIdCard);
        if (sellerAccount == Actor.NONE || qty <= 0 || unitPrice < 0) {
            return 0;
        }
        int units = Math.min(qty, items.countCarriedOfKind(sellerId, kind));
        if (units <= 0) {
            return 0;
        }
        if (unitPrice > 0) {
            // Clamp the lot to what the shop's own pocket can afford — a partial sale is a
            // smaller honest exchange, never an IOU (the transfer would refuse overdraft).
            units = (int) Math.min(units, bank.balanceOf(shopId) / unitPrice);
            if (units <= 0 || !bank.transfer(shopId, sellerAccount, unitPrice * units)) {
                return 0;
            }
        }
        return items.moveCarried(sellerId, shopId, kind, units);
    }
}
