package com.trojia.sim.actor;

/**
 * The tunable integer constants of the FOOD/HUNGER economy loop (the make-or-break economy
 * pass): production yield, larder/shop caps, prices, the import cadence, and the HUNGER a
 * single meal restores. One place so the Verifier can tune numbers without hunting call sites.
 *
 * <p><b>The core change this pass makes.</b> HUNGER used to recover PASSIVELY while an actor
 * stood on its home cell ({@code Actor.HUNGER_RECOVERED_PER_TICK_AT_HOME}, now deleted). That
 * crutch is replaced with real consumption: a hungry actor restores HUNGER only by EATING a
 * {@link ItemKinds#FOOD} item, which it must first ACQUIRE — from a home-cell larder (seeded at
 * bake, refilled by farmers or the provisioning import), or bought at a same-z shop counter via
 * an ID-authorized {@link BankVerbs#buyFood} Royal transfer. Farmers PRODUCE the FOOD; eating
 * SINKS it. Food is now a real economic good, and starvation is possible — which is the point.
 *
 * <p><b>Everything is integer</b> (determinism: no float/Map state, integer yields, named-RNG
 * only — none of these levers draw RNG at all). FOOD mint (yield + import + larder seed) and
 * sink (eating) are accounted so the closed-supply conservation identity
 * {@code minted == held(live) + eaten} holds across a soak, alongside the untouched money
 * invariant {@code totalRoyals() == vault COIN count}.
 */
public final class FoodEconomy {

    private FoodEconomy() {
    }

    /** HUNGER reserve restored by eating one FOOD (clamps at {@link NeedThresholds#MAX}). */
    public static final int EAT_RESTORE = 8_000;

    /**
     * Chebyshev radius a hungry actor can reach food across — both to EAT from a larder/commons
     * cell and to BUY from a nearby shopkeeper, WITHOUT walking onto the exact cell. Sized above 1
     * on purpose: a live-in crew of 20-48 serfs shares ONE anchor/home cell, and the 2-per-cell
     * occupancy cap keeps most of them a few tiles off it, so a chebyshev-1 reach would strand the
     * overflow. A reach of {@value} lets the whole crowd draw from the single stocked cell, and lets
     * an actor beside its co-located work-shop buy there without a (possibly unroutable) walk to the
     * counter — the buy path never walks, so no actor is ever stranded chasing an unreachable shop.
     */
    public static final int EAT_REACH = 8;

    /**
     * Spacing (tiles) of the walkable free-food commons grid the scenario lays over every band at
     * bake, sized {@code 2 * EAT_REACH} so every walkable cell sits within {@link #EAT_REACH} of a
     * grid cell. The pathing dead-zone backstop: an actor stranded by a broken long commute in a
     * pocket that reaches neither its home nor its work anchor still finds a stocked commons a few
     * tiles away, instead of starving where it stands.
     */
    public static final int COMMONS_GRID_SPACING = 2 * EAT_REACH;

    /** Royals a shopper pays a shopkeeper for one FOOD (an ID-authorized ledger transfer). */
    public static final long FOOD_PRICE = 5;

    /** Royals a shopkeeper pays a farmer for one surplus FOOD (recirculates Royals to the land). */
    public static final long FARM_SELL_PRICE = 4;

    /** FOOD minted per completed farm work-unit ({@code workTicksPerUnit} = 40 ticks). */
    public static final int FARM_FOOD_PER_UNIT = 1;

    /** FOOD staged on each home cell at bake so the first hunger cycles are covered before ramp-up. */
    public static final int LARDER_SEED = 3;

    /**
     * Cap on FOOD held on one home-cell larder (and on a free commons cell): farm production and
     * the provisioning import both stop topping a larder at this level, so live FOOD stays bounded
     * (demand-driven) instead of growing without limit. Sized generously because one shared
     * work-anchor / bunk cell feeds a whole rotating crowd — a smaller buffer drains between the
     * fixed-cadence refills and the crowd starves mid-period.
     */
    public static final int LARDER_CAP = 40;

    /** Cap on FOOD carried as sale stock by one shopkeeper; the quay import tops up to here. */
    public static final int SHOP_STOCK_CAP = 60;

    /**
     * Import cadence (ticks): every period the quay restocks each z:+11 shop counter to
     * {@link #SHOP_STOCK_CAP} and the provisioning ration tops each guaranteed larder / free
     * commons cell to {@link #LARDER_CAP}. Fixed {@code tick % PERIOD == 0} (mirrors {@code
     * Payroll}) — draw-free, no persisted cadence state. Matches the wage period so the two civic
     * flows share a rhythm.
     */
    public static final int IMPORT_PERIOD = 6_000;
}
