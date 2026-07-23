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

    /**
     * Master Gilt's vault key (Sprint 3 "The Vanished Clerk"): the quest token that opens
     * the clerk's locked desk draw-free. Minted once at bake into Gilt's carry; moved only
     * by the quest engine's key-lift watcher (a successful pickpocket of its declared
     * holder) — ambient theft moves COIN only, so no AI thief can ever strand it.
     */
    public static final short VAULT_KEY = 10;

    /**
     * The torn ledger leaf (Sprint 3 "The Vanished Clerk"): the proof the vanished clerk
     * locked in his desk. Placed once at bake on the clerk's-desk cell; moved only by quest
     * effects (search success → owner's carry; an ending → the chosen party).
     */
    public static final short LEDGER_LEAF = 11;

    /**
     * The Widow Netter's debt paper (Sprint 4 "The Widow's Paper"): the note her late
     * husband signed against the Netter house itself, kept in the strongbox behind
     * Fenner's cage (K15). Placed once at bake on the strongbox cell; moved only by quest
     * verbs (search success → owner's carry; an ending → the chosen party) — ambient
     * theft moves COIN only, so no AI thief can ever strand it.
     */
    public static final short DEBT_PAPER = 12;

    private ItemKinds() {
    }
}
