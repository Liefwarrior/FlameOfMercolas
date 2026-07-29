package com.trojia.sim.actor;

/**
 * The trade vocabulary beside {@link ItemKinds} (S8, "The Ward Prices Itself"): one static row
 * per item kind carrying its raws SYMBOL, its per-unit WEIGHT and its trade CATEGORY.
 *
 * <p><b>Eli's ruling, binding across the S8-S12 arc.</b> Trade goods are exactly four fixed
 * categories — {@link Category#MATERIALS}, {@link Category#FOOD}, {@link Category#COMMODITIES},
 * {@link Category#SERVICES}. No fifth, no renames. {@code SERVICES} deliberately holds no item
 * kind and never will: a service IS a quest (S10), so it is a contract to perform work, not a
 * stack you can carry. The category exists here because the four are the vocabulary, not
 * because items will fill it.
 *
 * <p><b>Scalps are MATERIALS</b> (settled, not up for relitigation): a scalp is a raw harvested
 * by-product exactly like a hide or gull down — Brann the chandler renders fat and Grandmother
 * Withy takes down, and those are the counters that buy this sort of thing.
 *
 * <p><b>Why plain Java and not a raws loader.</b> Kind ids ride the ItemsLite save format
 * ({@link ActorsSystem#serialize}) and {@link ItemKinds} is an append-only hardcoded vocabulary
 * for exactly that reason. A JSON loader for this table would reintroduce the id-allocation
 * problem the hardcoded vocabulary exists to avoid, so the table is code and the things that
 * genuinely vary per world (which actor type drops which scalp, which yard yields which good)
 * are the raws.
 *
 * <p><b>The symbol column is load-bearing.</b> It is the ONE place a content string resolves to
 * a kind id: the actor raws' {@code scalpItem} field and the quest raws' item symbols both come
 * through {@link #kindForSymbol}, replacing what used to be a hardcoded 5-case switch in the
 * observer scenario. Deterministic ascending-index scans; no {@code java.util} maps, no state.
 */
public final class TradeGoods {

    /** The four fixed trade categories (Eli's ruling — no fifth, no renames). */
    public enum Category {
        /** Raws and harvested by-products: cordage, pitch, staves, salt, scalps. */
        MATERIALS,
        /** Anything a body eats: meals and fish. */
        FOOD,
        /** Finished/negotiable stock and instruments: specie, cards, keys, papers. */
        COMMODITIES,
        /** Work performed for another — a service IS a quest (S10), never a carried stack. */
        SERVICES
    }

    /** {@link #kindForSymbol} sentinel: no kind carries that symbol. */
    public static final short NO_KIND = -1;

    /**
     * One row of the table.
     *
     * @param kind     the {@link ItemKinds} id
     * @param symbol   the raws-facing snake_case name (the ONE content-string spelling)
     * @param weight    per-unit carry weight in drams (integer — no float anywhere in sim-core)
     * @param category  the trade category this kind sells under
     * @param basePrice the standing counter price in Royals per unit, {@code 0} for kinds no
     *                  counter buys (quest tokens, identity papers). S8 ships this as a FIXED
     *                  number on purpose: the daily price tick is S9's, and it will move off
     *                  this base rather than replace it.
     */
    public record Entry(short kind, String symbol, int weight, Category category,
            int basePrice) {
    }

    /**
     * Every kind, in ascending {@link ItemKinds} id order (the iteration order every report and
     * every scan uses — determinism rule: no unordered iteration reaches sim state or report
     * text).
     */
    private static final Entry[] ROWS = {
        new Entry(ItemKinds.COIN, "coin", 1, Category.COMMODITIES, 1),
        new Entry(ItemKinds.FOOD, "food", 10, Category.FOOD, 5),
        new Entry(ItemKinds.ID_CARD, "id_card", 1, Category.COMMODITIES, 0),
        new Entry(ItemKinds.VAULT_KEY, "vault_key", 2, Category.COMMODITIES, 0),
        new Entry(ItemKinds.LEDGER_LEAF, "ledger_leaf", 1, Category.COMMODITIES, 0),
        new Entry(ItemKinds.DEBT_PAPER, "debt_paper", 1, Category.COMMODITIES, 0),
        new Entry(ItemKinds.FISH, "fish", 12, Category.FOOD, 3),
        new Entry(ItemKinds.CORDAGE, "cordage", 20, Category.MATERIALS, 2),
        new Entry(ItemKinds.PITCH, "pitch", 30, Category.MATERIALS, 2),
        new Entry(ItemKinds.BARREL_STOCK, "barrel_stock", 40, Category.MATERIALS, 3),
        new Entry(ItemKinds.SALT, "salt", 15, Category.MATERIALS, 2),
        new Entry(ItemKinds.RAT_SCALP, "rat_scalp", 2, Category.MATERIALS, 2),
        new Entry(ItemKinds.GULL_SCALP, "gull_scalp", 2, Category.MATERIALS, 3),
        new Entry(ItemKinds.CAT_SCALP, "cat_scalp", 3, Category.MATERIALS, 4),
    };

    /**
     * Content-facing spellings that resolve to a kind already named above. Only one exists
     * today: the quest raws call plain specie {@code "royals"} (the S4 buy-route price), which
     * is the ordinary {@link ItemKinds#COIN} stack under the ward's own word for money. Kept
     * separate from {@link #ROWS} so the primary table stays 1:1 with the vocabulary.
     */
    private static final String[] ALIAS_SYMBOLS = {"royals"};
    private static final short[] ALIAS_KINDS = {ItemKinds.COIN};

    private TradeGoods() {
    }

    /** How many kinds the table describes. */
    public static int count() {
        return ROWS.length;
    }

    /** Row {@code index}, in ascending kind-id order. */
    public static Entry at(int index) {
        return ROWS[index];
    }

    /** The row for {@code kind}, or {@code null} when the kind is not in the table. */
    public static Entry entryOf(short kind) {
        for (Entry row : ROWS) {
            if (row.kind() == kind) {
                return row;
            }
        }
        return null;
    }

    /**
     * The standing counter price in Royals for one unit of {@code kind}; {@code 0} where no
     * counter buys it (and {@code 0} for a kind the table does not describe). Callers MUST
     * treat 0 as "not for sale" rather than "free", or a sell verb would move stock for
     * nothing.
     */
    public static int basePriceOf(short kind) {
        Entry row = entryOf(kind);
        return row == null ? 0 : row.basePrice();
    }

    /** Per-unit carry weight in drams; {@code 0} for a kind the table does not describe. */
    public static int weightOf(short kind) {
        Entry row = entryOf(kind);
        return row == null ? 0 : row.weight();
    }

    /** The trade category of {@code kind}; {@code null} for a kind not in the table. */
    public static Category categoryOf(short kind) {
        Entry row = entryOf(kind);
        return row == null ? null : row.category();
    }

    /** The raws-facing symbol for {@code kind}, or {@code null} when it has no row. */
    public static String symbolOf(short kind) {
        Entry row = entryOf(kind);
        return row == null ? null : row.symbol();
    }

    /**
     * The kind a raws symbol names, or {@link #NO_KIND}. THE resolution point for every content
     * string that has to become an item id — actor raws {@code scalpItem}, quest raws item
     * symbols. Aliases resolve too.
     */
    public static short kindForSymbol(String symbol) {
        if (symbol == null) {
            return NO_KIND;
        }
        for (Entry row : ROWS) {
            if (row.symbol().equals(symbol)) {
                return row.kind();
            }
        }
        for (int i = 0; i < ALIAS_SYMBOLS.length; i++) {
            if (ALIAS_SYMBOLS[i].equals(symbol)) {
                return ALIAS_KINDS[i];
            }
        }
        return NO_KIND;
    }

    /** Whether {@code kind} sells under {@code category}. */
    public static boolean isCategory(short kind, Category category) {
        return categoryOf(kind) == category;
    }
}
