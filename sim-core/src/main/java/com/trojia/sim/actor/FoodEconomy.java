package com.trojia.sim.actor;

/**
 * The tunable integer constants of the FOOD/HUNGER economy loop (the make-or-break economy
 * pass): production yield, larder/shop caps, prices, the import cadence, and the HUNGER a
 * single meal restores. One place so the Verifier can tune numbers without hunting call sites.
 *
 * <p><b>A real, money-gated market (this pass's rework).</b> HUNGER recovers only by EATING a
 * {@link ItemKinds#FOOD} item, which an actor must first ACQUIRE. There is NO free food blanket:
 * the two legitimate ways food reaches a mouth are
 * <ol>
 *   <li><b>BUY it</b> at a reachable same-z shop counter via an ID-authorized {@link
 *       BankVerbs#buyFood} Royal transfer — the money lever: a waged, solvent citizen eats, the
 *       wageless broke (wastrels, the roof-slum poor) cannot. Shops are stocked ONLY by the
 *       <b>same-z quay FOOD import</b> (ships landing provisions at the market), so money still
 *       gates every mouthful even though the supply is imported; or</li>
 *   <li><b>eat a subsistence larder stocked by REAL farm production</b> — a farming household
 *       eats its own home-cell yield, and a compound eats the shared atrium/courtyard larder its
 *       farmers fill (legitimately non-market: you grew it). Never topped by a free import.</li>
 * </ol>
 * Farmers PRODUCE the FOOD; the quay imports it to the market; eating SINKS it. Food is a real
 * scarce good, and starvation is the intended fate of anyone with neither Royals for the market
 * nor a reachable farm larder.
 *
 * <p><b>Everything is integer</b> (determinism: no float/Map state, integer yields, named-RNG
 * only — none of these levers draw RNG at all). FOOD mint (farm yield + quay import + bake seed)
 * and sink (eating) are accounted so the closed-supply conservation identity
 * {@code minted == held(live) + eaten} holds across a soak, alongside the untouched money
 * invariant {@code totalRoyals() == vault COIN count}.
 */
public final class FoodEconomy {

    private FoodEconomy() {
    }

    /** HUNGER reserve restored by eating one FOOD (clamps at {@link NeedThresholds#MAX}). */
    public static final int EAT_RESTORE = 8_000;

    /**
     * FOOD each provisioned citizen keeps in its own carry as a household ration — the money-gated
     * reachability backstop. Every import period a citizen tops its carry up to this many FOOD by
     * BUYING them from the market ({@link #FOOD_PRICE} Royals each, transferred to the market pool):
     * the "household does its shopping". Because the ration rides in the citizen's OWN carry, a
     * hungry actor eats it exactly where it stands ({@link SeekFoodPolicy} step 1) — no walk to a
     * counter, no crowd jam, no walled-pocket stranding, which the walk-to-shop path cannot survive
     * on this map. It is a genuine purchase (Royals leave the buyer), so the wageless margin — who
     * are NOT provisioned and cannot sustain counter purchases — still starves. Two meals covers ~2
     * import periods of HUNGER decay, so a citizen never runs its pantry dry between shops.
     */
    public static final int CARRY_RATION = 2;

    /**
     * Chebyshev radius a hungry actor can reach food across — to EAT in place from a larder/commons
     * cell it is beside, and to BUY in place from a shopkeeper it is beside, WITHOUT walking onto the
     * exact cell. Sized above 1 on purpose: a live-in crew of 20-48 serfs shares ONE anchor/home
     * cell, and the 2-per-cell occupancy cap keeps most of them a few tiles off it, so a chebyshev-1
     * reach would strand the overflow. A reach of {@value} lets the whole crowd draw from the single
     * stocked cell, and lets an actor beside its co-located work-shop buy there in place. When the
     * nearest source is FURTHER than this, the eat machine WALKS to it (route-following A*, re-scans
     * on an unroutable target) rather than pinning itself to an unreachable one and starving.
     */
    public static final int EAT_REACH = 8;

    /** Royals a shopper pays a shopkeeper for one FOOD (an ID-authorized ledger transfer). */
    public static final long FOOD_PRICE = 5;

    /** Royals a shopkeeper pays a farmer for one surplus FOOD (recirculates Royals to the land). */
    public static final long FARM_SELL_PRICE = 4;

    /** FOOD minted per completed farm work-unit ({@code workTicksPerUnit} = 40 ticks). */
    public static final int FARM_FOOD_PER_UNIT = 1;

    /** FOOD staged on each home cell at bake so the first hunger cycles are covered before ramp-up. */
    public static final int LARDER_SEED = 3;

    /**
     * Cap on FOOD held on one subsistence larder cell (a farming household's home cell, or a
     * compound's shared atrium/courtyard larder): farm production stops topping a larder at this
     * level, so live FOOD stays bounded (demand-driven) rather than growing without limit. Sized
     * generously because a compound atrium feeds a whole same-band courtyard's worth of households.
     */
    public static final int LARDER_CAP = 40;

    /**
     * Cap on FOOD carried as sale stock by one shopkeeper; the quay import tops up to here, and the
     * bake seeds each vendor to this level so the market is already stocked before the first import.
     * Sized generously because one dockside/hull victualler can be the sole reachable market for a
     * 20-48-strong live-in crew whose hunger waves arrive near-synchronised — a thin counter would
     * drain between the fixed-cadence imports and the crew would starve mid-period.
     */
    public static final int SHOP_STOCK_CAP = 120;

    /**
     * Garbage-scrap drop cadence (law &amp; order pass, Eli's garbage-can request): once a DAY
     * ({@code tick % 24000 == 0}) each garbage-bin cell beside a FOOD business is topped up to
     * {@link #BIN_SCRAP_CAP} FOOD scraps — the businesses' daily refuse. Minted (and
     * conservation-accounted) like the quay import; capped, so live FOOD stays bounded.
     */
    public static final int SCRAP_DROP_PERIOD = 24_000;

    /**
     * Cap on FOOD scraps sitting on one garbage-bin cell: the daily drop tops up to here, never
     * beyond. Deliberately thin — the whole point is a REAL margin: bins keep the reachable
     * street wastrels alive between beg-circuits, but there is not enough refuse for everyone
     * (the roof decks can't route to any bin at all and still starve).
     */
    public static final int BIN_SCRAP_CAP = 4;

    /**
     * Import cadence (ticks): every period the quay restocks each vendor shop counter to
     * {@link #SHOP_STOCK_CAP} (the ONLY periodic import — larders and the compound atria are fed by
     * farm production alone, never a free ration). Fixed {@code tick % PERIOD == 0} (mirrors {@code
     * Payroll}) — draw-free, no persisted cadence state. Matches the wage period so the citizen's
     * pay and the market's restock share a rhythm.
     */
    public static final int IMPORT_PERIOD = 6_000;
}
