package com.trojia.sim.actor;

/**
 * The baked FOOD-distribution side-table (the economy-loop pass), injected through the {@link
 * ActorsSystem} constructor exactly like {@link PrisonCellRegistry} / {@link RestrictedZoneTable}
 * — fixed baked {@code int[]}s, no mutable runtime state, rides no save. It answers the two
 * questions a hungry {@link SeekFoodPolicy} and the periodic import ask, without any {@code
 * Map}/{@code Set} or float (purity-gate clean):
 *
 * <ul>
 *   <li><b>vendor shops</b> — the actor ids of the z:+11 shopkeepers who VEND FOOD. A hungry
 *       actor on their z-band walks to the nearest stocked, affordable one and buys one FOOD via
 *       {@link BankVerbs#buyFood} (an ID-authorized Royal transfer). The quay import restocks
 *       their carried stock every period.</li>
 *   <li><b>commons cells</b> — free-food cells any same-z actor may eat from (the terrace/heights
 *       parishes with no cash-market infrastructure). Topped up by the provisioning import.</li>
 *   <li><b>guaranteed larder cells</b> — home cells the provisioning import keeps stocked so their
 *       household always eats at home (the middle class and the off-band serfs, who must not
 *       starve). NOT scanned as public commons — only the cell's own residents reach them via the
 *       home-larder branch — so topping one up feeds that household without feeding a neighbour.</li>
 * </ul>
 *
 * <p>The z-rule ({@code stepToward}/{@code stepAlongRoute} never cross z) is respected by the
 * data itself: every vendor sits on z:+11, and each hungry actor only ever considers a source on
 * its OWN z-band (checked live against the actor's cell), so no channel implies a cross-z walk.
 * Roof-deck dwellers have no same-z vendor, commons, or stocked larder — they are the intended
 * starvation margin.
 */
public final class FoodMarket {

    /** The degraded empty market the world-less bootstrap / economy-free tests inject. */
    public static final FoodMarket EMPTY = new FoodMarket(new int[0], new int[0], new int[0]);

    private final int[] vendorShopIds;
    private final int[] commonsCells;
    private final int[] larderCells;

    public FoodMarket(int[] vendorShopIds, int[] commonsCells, int[] larderCells) {
        this.vendorShopIds = vendorShopIds.clone();
        this.commonsCells = commonsCells.clone();
        this.larderCells = larderCells.clone();
    }

    /** Number of vending z:+11 shopkeepers. */
    public int vendorCount() {
        return vendorShopIds.length;
    }

    /** The vending shopkeeper actor id at dense index {@code i} (ascending bake order). */
    public int vendorAt(int i) {
        return vendorShopIds[i];
    }

    /** Number of free-food commons cells. */
    public int commonsCount() {
        return commonsCells.length;
    }

    /** The commons cell (world-packed) at dense index {@code i}. */
    public int commonsAt(int i) {
        return commonsCells[i];
    }

    /** Number of guaranteed (provisioned) home-larder cells. */
    public int larderCount() {
        return larderCells.length;
    }

    /** The guaranteed larder cell (world-packed) at dense index {@code i}. */
    public int larderAt(int i) {
        return larderCells[i];
    }
}
