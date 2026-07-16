package com.trojia.sim.actor;

/**
 * The Royals ledger (Phase-0 economy F2): a dense {@code long[]} of account
 * balances, index == {@code accountId} == citizen {@code actorId} (the same
 * "index is the id" convention {@link HomeRegistry} and {@link
 * ItemsLiteRegistry} use). Balances are integer Royals — never a {@code double},
 * never a {@code Map} — so the ledger is bit-for-bit reproducible and passes the
 * actor-package purity gate.
 *
 * <p><b>Royals vs Coins.</b> A {@code Royal} is a ledger balance here; it is NOT
 * an item. A physical {@link ItemKinds#COIN} is the specie a vault chest holds,
 * and the hard conservation invariant is {@code totalRoyals() == } the vault
 * chest's COIN count (asserted by the economy tests). Every value transfer is a
 * ledger {@link #transfer} — money is moved, never minted: {@link #credit} /
 * {@link #debit} are only the deposit/withdraw on/off-ramps a live bank pairs
 * with a physical vault Coin move, and wages/purchases are pure {@link
 * #transfer}s between existing balances.
 *
 * <p><b>Accounts.</b> {@link #openAccount()} assigns ascending ids from {@code
 * 0}; callers open one per actor in ascending {@code actorId} order at bake so
 * {@code accountId == actorId}. The world-less bootstrap opens none (a degraded,
 * empty ledger — the {@link ActorContext#bankAccounts()} analogue of {@code
 * arrestHoldCell == NONE}).
 */
public final class BankLedger {

    private long[] royals = new long[0];
    private int count;

    /** Opens the next account (ascending id from 0), balance 0; returns its {@code accountId}. */
    public int openAccount() {
        if (count == royals.length) {
            royals = java.util.Arrays.copyOf(royals, Math.max(16, royals.length * 2));
        }
        royals[count] = 0L;
        return count++;
    }

    /** Number of open accounts (== the highest {@code accountId} + 1). */
    public int accountCount() {
        return count;
    }

    /** Balance of {@code accountId} in Royals. */
    public long balanceOf(int accountId) {
        return royals[accountId];
    }

    /** Credits {@code amount} Royals into {@code accountId} (a vault deposit's ledger half). */
    public void credit(int accountId, long amount) {
        if (amount < 0) {
            throw new IllegalArgumentException("credit amount must be >= 0: " + amount);
        }
        if (royals[accountId] > Long.MAX_VALUE - amount) {
            throw new IllegalArgumentException("credit would overflow account " + accountId
                    + ": have " + royals[accountId] + ", adding " + amount);
        }
        royals[accountId] += amount;
    }

    /** Debits {@code amount} Royals from {@code accountId} (a vault withdrawal's ledger half). */
    public void debit(int accountId, long amount) {
        if (amount < 0) {
            throw new IllegalArgumentException("debit amount must be >= 0: " + amount);
        }
        if (royals[accountId] < amount) {
            throw new IllegalArgumentException("insufficient Royals in account " + accountId
                    + ": have " + royals[accountId] + ", need " + amount);
        }
        royals[accountId] -= amount;
    }

    /**
     * Moves {@code amount} Royals from {@code fromAccount} to {@code toAccount} — the wage/purchase
     * primitive (money moved, never minted). Rejects (returns {@code false}, no state change) a
     * negative amount or an insufficient source balance; the total money supply is invariant across
     * every transfer.
     */
    public boolean transfer(int fromAccount, int toAccount, long amount) {
        if (amount < 0 || royals[fromAccount] < amount) {
            return false;
        }
        // Overflow guard (STEP A): reject before the destination wraps rather than mint negative
        // Royals. Within a closed supply the total can never exceed a long, so this never fires
        // live — but a rejected transfer leaves both balances untouched, preserving the invariant.
        if (fromAccount != toAccount && royals[toAccount] > Long.MAX_VALUE - amount) {
            return false;
        }
        royals[fromAccount] -= amount;
        royals[toAccount] += amount;
        return true;
    }

    /** Sum of every account balance — the money-supply conservation baseline (== vault COIN count). */
    public long totalRoyals() {
        long total = 0;
        for (int i = 0; i < count; i++) {
            total += royals[i];
        }
        return total;
    }

    /**
     * The account an {@link ItemKinds#ID_CARD} authorizes: the {@code accountId} stamped on the
     * card the presenter is carrying, independent of {@link Persona} and of who currently holds it
     * (a stolen card authorizes its original account). {@link Actor#NONE} for a {@code null} card or
     * any non-card item.
     */
    public static int purchaseAuth(ItemsLiteEntry idCard) {
        if (idCard == null || idCard.kindId() != ItemKinds.ID_CARD) {
            return Actor.NONE;
        }
        return idCard.accountId();
    }
}
