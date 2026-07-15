package com.trojia.sim.actor;

/**
 * The small, append-only set of sim-core item-kind ids the economy moves
 * (Phase-0 economy/job foundation, DOCKS-GAZETTEER.md §7.1). Before this, kind
 * ids were client-only magic {@code short} constants ({@code DocksPopulation}'s
 * {@code KIND_COIN/KIND_STOCK/…}); the money/food loop needs a shared,
 * sim-core-visible vocabulary so the {@link BankLedger}, {@link
 * ItemsLiteRegistry} verbs, and the observer scenario all agree on which items
 * are money, food and identity.
 *
 * <p><b>Currency decision (resolved).</b> A physical {@link #COIN} is the
 * counted, transferable specie that fills a bank vault; {@code Royals} are
 * <em>not</em> an item — they are a ledger balance ({@link BankLedger}), and a
 * vault chest is one {@link #COIN} stack whose count equals the ledger's total
 * Royals (the hard conservation invariant). This repurposes the client's legacy
 * {@code KIND_COIN == 1}; the newer kinds deliberately start above the client's
 * remaining legacy flavor kinds (2..7) so no id collides with an observer
 * placeholder still riding the same {@code short}.
 *
 * <p>Ids are append-only — they ride the ItemsLite save format ({@link
 * ActorsSystem#serialize}); never reorder or reuse a value.
 */
public final class ItemKinds {

    /** The counted, transferable specie (repurposes the client's legacy {@code KIND_COIN == 1}). */
    public static final short COIN = 1;

    /** A meal: satisfies HUNGER when eaten (farms mint it; shops vend it; clergy alms it). */
    public static final short FOOD = 8;

    /**
     * A bank-identity token: carries an {@code accountId} stamped at mint (the
     * {@link ItemsLiteEntry#accountId()} slot). Authorization reads the stamped
     * id off the card the presenter is <em>carrying</em>, independent of {@link
     * Persona}/{@code trueId}/{@code presentedId} — so a stolen card authorizes
     * that account. See {@link BankLedger#purchaseAuth(ItemsLiteEntry)}.
     */
    public static final short ID_CARD = 9;

    private ItemKinds() {
    }
}
