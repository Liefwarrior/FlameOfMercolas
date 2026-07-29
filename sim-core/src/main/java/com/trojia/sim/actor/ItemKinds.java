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

    /**
     * A fresh-caught fish (Sprint 6 fishing): minted by a successful catch at a live
     * fishing spot (accounted like farm FOOD in the closed-supply proof), sellable to any
     * shopkeeper through the buy-side counter ({@link BankVerbs#sellToShop} — the citizen
     * coin faucet), vendible back out as a meal ({@link BankVerbs#buyMeal}), and edible
     * exactly like FOOD ({@link SeekFoodPolicy}'s carried-meal fast path).
     */
    public static final short FISH = 13;

    // ---- S8 "The Ward Prices Itself", trade goods (Eli's ruling: exactly four
    // categories — materials, food, commodities, services; no fifth, no renames). These four
    // are the Docks' own MATERIALS, minted by the craft yards' completed work units through
    // the JobParams yield pair. They are ordinary counted stacks: nothing about them is
    // special-cased in ItemsLite, and each carries its weight/category in {@link TradeGoods}. ----

    /** Laid rope off the Ropewalk's stations (MATERIALS). */
    public static final short CORDAGE = 14;

    /** Boiled tar out of the Pitchfield (MATERIALS). */
    public static final short PITCH = 15;

    /** Cut staves and hoops off the cooperage floor (MATERIALS). */
    public static final short BARREL_STOCK = 16;

    /** Panned and raked salt off Salt Row (MATERIALS). */
    public static final short SALT = 17;

    // ---- S8 scalps (Eli's ruling: "kills are counted as SCALPS, not counters" — a scalpable
    // type drops a NAMED ITEM on death, so kill-tracking is an item problem and items already
    // persist). VERMIN ONLY this arc: combat is out (Decision 2b amendment, commit 57a444f),
    // so human scalps defer past S12 and no wastrel scalp exists. A scalp is a raw harvested
    // by-product like a hide, so it is MATERIALS, not a commodity — Brann the chandler
    // (renders fat) and Grandmother Withy (takes gull down) are the notables who buy exactly
    // that. Which type drops which scalp is raws data ({@code scalpItem}), not code. ----

    /** The quay mouse's scalp (MATERIALS) — the vermin bounty the ward actually pays. */
    public static final short RAT_SCALP = 18;

    /** The scavenging gull's scalp (MATERIALS). */
    public static final short GULL_SCALP = 19;

    /** The prowling cat's scalp (MATERIALS). */
    public static final short CAT_SCALP = 20;

    private ItemKinds() {
    }
}
