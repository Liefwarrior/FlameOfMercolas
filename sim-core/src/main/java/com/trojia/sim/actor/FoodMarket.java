package com.trojia.sim.actor;

/**
 * The baked FOOD-distribution side-table (the money-gated market pass), injected through the {@link
 * ActorsSystem} constructor exactly like {@link PrisonCellRegistry} / {@link RestrictedZoneTable}
 * — fixed baked {@code int[]}s, no mutable runtime state, rides no save. It answers the two
 * questions a hungry {@link SeekFoodPolicy} and the periodic quay import ask, without any {@code
 * Map}/{@code Set} or float (purity-gate clean):
 *
 * <ul>
 *   <li><b>vendor shops</b> — the actor ids of the shopkeepers who VEND FOOD (one per band, from
 *       the organic dockside shops to the on-hull and off-band victuallers). A hungry actor on
 *       their z-band buys one FOOD from the nearest stocked, affordable one via {@link
 *       BankVerbs#buyFood} (an ID-authorized Royal transfer) — reachable in place or by walking to
 *       the counter. The quay import (the ONLY periodic import) restocks their carried stock.</li>
 *   <li><b>commons cells</b> — the handful of farm-fed shared larders: each compound's atrium/
 *       courtyard and the mission almshouse. Free to any same-z actor who can reach one, but
 *       stocked ONLY by the compound's own farmers (never by a free ration), so a cell exists — and
 *       stays stocked — only where food is actually grown. NOT the old district-wide free grid.</li>
 *   <li><b>provisioned citizens</b> — the actor ids of every citizen (the serf mass + the middle
 *       class, never a wastrel or beast) whose household does its periodic shopping: each import
 *       period {@link ActorsSystem} makes them BUY a {@link FoodEconomy#CARRY_RATION}-meal ration
 *       into their own carry, paying {@link FoodEconomy#FOOD_PRICE} Royals apiece. This is the
 *       money-gated reachability backstop — the ration eats in place ({@link SeekFoodPolicy} step
 *       1), so no walled pocket or crowd jam can strand a paying citizen, while the wageless (absent
 *       from this list) get nothing and starve. Deliberately EXCLUDES wastrels: they are the
 *       intended margin.</li>
 * </ul>
 *
 * <p>The z-rule ({@code stepToward}/{@code stepAlongRoute} never cross z) is respected by the
 * data itself: each hungry actor only ever considers a source on its OWN z-band (checked live
 * against the actor's cell), so no channel implies a cross-z walk. The roof-slum poor have no
 * ration, no same-z vendor they can afford, and no reachable farm larder — the intended margin.
 */
public final class FoodMarket {

    /** The degraded empty market the world-less bootstrap / economy-free tests inject. */
    public static final FoodMarket EMPTY = new FoodMarket(new int[0], new int[0], new int[0]);

    private final int[] vendorShopIds;
    private final int[] commonsCells;
    private final int[] provisionedCitizenIds;

    public FoodMarket(int[] vendorShopIds, int[] commonsCells, int[] provisionedCitizenIds) {
        this.vendorShopIds = vendorShopIds.clone();
        this.commonsCells = commonsCells.clone();
        this.provisionedCitizenIds = provisionedCitizenIds.clone();
    }

    /** Number of vending shopkeepers. */
    public int vendorCount() {
        return vendorShopIds.length;
    }

    /** The vending shopkeeper actor id at dense index {@code i} (ascending bake order). */
    public int vendorAt(int i) {
        return vendorShopIds[i];
    }

    /** Number of farm-fed commons cells. */
    public int commonsCount() {
        return commonsCells.length;
    }

    /** The commons cell (world-packed) at dense index {@code i}. */
    public int commonsAt(int i) {
        return commonsCells[i];
    }

    /** Number of provisioned citizens (the periodic-shopping serf mass + middle class). */
    public int provisionedCount() {
        return provisionedCitizenIds.length;
    }

    /** The provisioned citizen's actor id at dense index {@code i} (ascending bake order). */
    public int provisionedAt(int i) {
        return provisionedCitizenIds[i];
    }
}
