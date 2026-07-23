package com.trojia.sim.actor;

/**
 * Disposition-gated barter (Sprint 2 rank 2): {@code priceFor(buyer, shopkeeper)} replaces the
 * flat {@link FoodEconomy#FOOD_PRICE} at every COUNTER purchase — prices that know who you are
 * (and who you are PRESENTING as). Pure integer functions, draw-free, deterministic.
 *
 * <p><b>The price model</b> (all integer points on the flat base of
 * {@value FoodEconomy#FOOD_PRICE} Royals):
 * <ul>
 *   <li><b>Merchants standing</b> (of the buyer's PRESENTED id — the Persona rule): honest
 *       coin is remembered; {@code standing / 25} points OFF (0..4 — a +100 regular pays
 *       near cost).</li>
 *   <li><b>Watch standing surcharge</b>: the counters trust the law's ledger — a NEGATIVE
 *       presented Watch standing adds {@code -standing / 20} points (0..5), so a crime spree
 *       measurably raises the same actor's price (the DoD probe). At or under
 *       {@link #REFUSAL_WATCH_STANDING} every counter REFUSES outright — the district's most
 *       wanted buys nothing and falls through to the commons/scavenge chain.</li>
 *   <li><b>Relationship</b> (presented buyer &harr; the shopkeeper standing at the counter):
 *       a HOUSEHOLD or FRIEND edge takes {@value #RELATIONSHIP_DISCOUNT} point off — family
 *       and friends get the kitchen price.</li>
 *   <li><b>Streetwise haggling</b> (the buyer's TRUE skill — the body doing the haggling):
 *       {@code level / 25} points off (0..4; the raws list "black-market pricing" among
 *       streetwise's covers).</li>
 * </ul>
 * The result clamps to {@code [}{@value #MIN_PRICE}{@code , }{@value #MAX_PRICE}{@code ]}: a
 * meal is never free (conservation keeps meaning) and never a ransom (liveness: the clamp
 * bounds how far reputation can price anyone out of the market, protecting the starvation
 * bars).
 *
 * <p><b>Scope</b> (declared): counter purchases only — {@link SeekFoodPolicy}'s buy-in-reach
 * and walk-to-counter paths. The periodic household provisioning ({@code
 * ActorsSystem.runFoodProvision}) keeps the flat pool price: it is a wholesale draw against
 * the market POOL with no shopkeeper party standing at any counter, and the S1 economy's
 * wage/price equilibrium (which the starvation bars are tuned against) rides its cadence.
 *
 * <p><b>Cost shape.</b> {@link #quoteFor} bundles the per-buyer components once per policy
 * act (two ledger reads + two skill reads); {@link #priceAt} adds only the per-shopkeeper
 * relationship scan, and callers evaluate it LAST in their vendor scans (after the cheap
 * distance/stock gates), so the O(edges) walk runs for a handful of candidates at most.
 */
public final class Barter {

    /** {@link #priceAt}'s refusal sentinel: this counter will not serve this buyer. */
    public static final long REFUSED = -1;

    /** Presented Watch standing at/below which every counter refuses service. */
    public static final int REFUSAL_WATCH_STANDING = -60;
    /** Price floor: a meal never goes free (the ledger transfer must stay real). */
    public static final long MIN_PRICE = 1;
    /** Price ceiling: reputation can gouge, never ransom (the bars' liveness guard). */
    public static final long MAX_PRICE = 15;
    /** Merchants-standing points per 1-Royal discount. */
    public static final int MERCHANTS_PER_DISCOUNT = 25;
    /** Negative-Watch-standing points per 1-Royal surcharge. */
    public static final int WATCH_PER_SURCHARGE = 20;
    /** Streetwise levels per 1-Royal haggling discount. */
    public static final int STREETWISE_PER_DISCOUNT = 25;
    /** The flat kin/friend discount, in Royals. */
    public static final int RELATIONSHIP_DISCOUNT = 1;

    private Barter() {
    }

    /**
     * The per-buyer half of a price, computed once per policy act: presented-identity
     * standings + true-body haggling, folded to a personal base price, plus the
     * everywhere-refusal verdict.
     *
     * @param presentedId    the identity every counter reads (standings key)
     * @param refusedEverywhere whether every counter refuses this presented identity
     * @param personalPrice  the buyer's price before any per-shopkeeper relationship discount
     */
    public record Quote(int presentedId, boolean refusedEverywhere, long personalPrice) {
    }

    /** Builds the buyer's {@link Quote} from live sim state (standings, tracks). */
    public static Quote quoteFor(Actor buyer, ActorContext ctx) {
        int presentedId = buyer.identity().presentedId();
        FactionStandings standings = ctx.factionStandings();
        int watch = standings.watchStanding(presentedId);
        if (watch <= REFUSAL_WATCH_STANDING) {
            return new Quote(presentedId, true, MAX_PRICE);
        }
        int merchants = Math.max(0, standings.merchantsStanding(presentedId));
        int haggle = ctx.skillTracks().level(buyer.id(), ctx.skillTracks().streetwiseRaw());
        long price = FoodEconomy.FOOD_PRICE
                - merchants / MERCHANTS_PER_DISCOUNT
                - haggle / STREETWISE_PER_DISCOUNT
                + Math.max(0, -watch) / WATCH_PER_SURCHARGE;
        return new Quote(presentedId, false,
                Math.max(MIN_PRICE, Math.min(MAX_PRICE, price)));
    }

    /**
     * The price {@code shopkeeperId}'s counter charges this buyer for one FOOD, or
     * {@link #REFUSED}. The only per-shopkeeper component is the kin/friend discount, so
     * callers keep this check LAST in their scans.
     */
    public static long priceAt(Quote quote, int shopkeeperId, RelationshipRegistry relationships) {
        if (quote.refusedEverywhere()) {
            return REFUSED;
        }
        long price = quote.personalPrice();
        if (isKinOrFriend(relationships, quote.presentedId(), shopkeeperId)) {
            price -= RELATIONSHIP_DISCOUNT;
        }
        return Math.max(MIN_PRICE, price);
    }

    /**
     * The buyer's baseline meal price ({@code personalPrice}), or {@link Long#MAX_VALUE}
     * when refused everywhere — the "can this actor buy a meal at all" affordability floor
     * {@link SeekFoodPolicy}'s broke check reads. Deliberately IGNORES the possible
     * kin/friend counter discount: the broke threshold must never leave an actor stranded
     * between "too rich to scavenge, too poor to buy" on the hope of a discount only a
     * specific counter would grant (a neutral, unwired actor's floor is therefore exactly
     * the pre-Sprint-2 {@link FoodEconomy#FOOD_PRICE} — the starvation bars' baseline).
     */
    public static long floorPriceFor(Quote quote) {
        if (quote.refusedEverywhere()) {
            return Long.MAX_VALUE;
        }
        return quote.personalPrice();
    }

    /** Whether a HOUSEHOLD or FRIEND edge joins the two ids (allocation-free edge walk). */
    static boolean isKinOrFriend(RelationshipRegistry relationships, int a, int b) {
        for (int i = 0; i < relationships.size(); i++) {
            RelationshipEdge edge = relationships.get(i);
            RelationshipKind kind = edge.kind();
            if (kind != RelationshipKind.HOUSEHOLD && kind != RelationshipKind.FRIEND) {
                continue;
            }
            if ((edge.fromId() == a && edge.toId() == b)
                    || (edge.fromId() == b && edge.toId() == a)) {
                return true;
            }
        }
        return false;
    }
}
