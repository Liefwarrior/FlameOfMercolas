package com.trojia.sim.actor;

/**
 * The bundle of baked civic seams wired into {@link ActorsSystem} at scenario-bake time
 * (Phase-2 Passes 9-10): the arrest holding-cell, the restricted-zone table, and the bank +
 * prison + payroll fixtures the live economy and justice plumbing read. Grouping them keeps the
 * {@code ActorsSystem} constructor from growing a dozen positional {@code int} arguments while
 * preserving the "one baked seam, {@code Actor.NONE}/EMPTY when unwired" pattern each field has.
 *
 * <p>All fields are immutable baked config (never serialized): the world-less bootstrap and the
 * economy-free tests inject {@link #NONE}, and every consumer degrades gracefully on the unwired
 * value. Purity-gate clean — {@code int} scalars and the pure {@code int[]}/{@code long[]}-backed
 * registries, no {@code Map}/{@code Set}/float anywhere.
 *
 * @param arrestHoldCell   the single well-known K34 escort cell (ARREST-SPEC addendum), or
 *                         {@link Actor#NONE}; the multi-cell fallback when {@code prisonCells} is
 *                         empty or fully occupied
 * @param zones            the restricted-zone side-table (Phase-0 F3), or {@link RestrictedZoneTable#EMPTY}
 * @param vaultChestCell   the bank vault chest cell holding the single Royal-backing COIN stack,
 *                         or {@link Actor#NONE}
 * @param bankerCell       the banker's counter cell (deposit/withdraw happen adjacent), or
 *                         {@link Actor#NONE}
 * @param bankQueue        the deterministic waiting queue, or {@link BankQueue#EMPTY}
 * @param prisonCells      the multi-cell prison registry (Pass 10), or {@link PrisonCellRegistry#EMPTY}
 * @param payroll          the wage table + finite employer pool (Pass 9), or {@link Payroll#NONE}
 * @param foodMarket       the FOOD-distribution side-table (economy-loop pass: vendor shops, free
 *                         commons, guaranteed larders), or {@link FoodMarket#EMPTY}
 * @param patrolRoutes     the ordered guard patrol routes (law &amp; order pass, Pass 13; cross-z
 *                         waypoints legal since Sprint 4's climb), or {@link PatrolRouteTable#EMPTY}
 * @param rooftops         the baked rooftop-region table (Sprint 1 progression wiring: the
 *                         played actor's skyrunning hook), or {@link RooftopTable#EMPTY}
 * @param zLinks           the baked cross-z connector table (Sprint 4 "the climb": stair pairs +
 *                         ramp exits extracted from the world at bake), or {@link ZLinkTable#EMPTY}
 *                         — with it empty, every opt-in cross-z mover degrades to the old no-op
 */
public record CivicFixtures(
        int arrestHoldCell,
        RestrictedZoneTable zones,
        int vaultChestCell,
        int bankerCell,
        BankQueue bankQueue,
        PrisonCellRegistry prisonCells,
        Payroll payroll,
        FoodMarket foodMarket,
        PatrolRouteTable patrolRoutes,
        RooftopTable rooftops,
        ZLinkTable zLinks) {

    /** The fully-unwired bundle (world-less bootstrap, economy-free tests). */
    public static final CivicFixtures NONE = new CivicFixtures(
            Actor.NONE, RestrictedZoneTable.EMPTY, Actor.NONE, Actor.NONE,
            BankQueue.EMPTY, PrisonCellRegistry.EMPTY, Payroll.NONE, FoodMarket.EMPTY,
            PatrolRouteTable.EMPTY, RooftopTable.EMPTY, ZLinkTable.EMPTY);

    /**
     * The pre-climb 10-component shape (Sprint-3 compatibility): existing call sites keep
     * compiling with {@link ZLinkTable#EMPTY} wired (no vertical passages — the z-rule as
     * it stood); the live docks bake uses the canonical constructor with its extracted
     * connector table.
     */
    public CivicFixtures(int arrestHoldCell, RestrictedZoneTable zones, int vaultChestCell,
            int bankerCell, BankQueue bankQueue, PrisonCellRegistry prisonCells,
            Payroll payroll, FoodMarket foodMarket, PatrolRouteTable patrolRoutes,
            RooftopTable rooftops) {
        this(arrestHoldCell, zones, vaultChestCell, bankerCell, bankQueue, prisonCells,
                payroll, foodMarket, patrolRoutes, rooftops, ZLinkTable.EMPTY);
    }

    /**
     * The pre-rooftop 9-component shape (Sprint-1 compatibility): existing call sites keep
     * compiling with {@link RooftopTable#EMPTY} wired; the live docks bake uses the
     * canonical constructor with its authored roof planes.
     */
    public CivicFixtures(int arrestHoldCell, RestrictedZoneTable zones, int vaultChestCell,
            int bankerCell, BankQueue bankQueue, PrisonCellRegistry prisonCells,
            Payroll payroll, FoodMarket foodMarket, PatrolRouteTable patrolRoutes) {
        this(arrestHoldCell, zones, vaultChestCell, bankerCell, bankQueue, prisonCells,
                payroll, foodMarket, patrolRoutes, RooftopTable.EMPTY, ZLinkTable.EMPTY);
    }

    /** A bundle wiring only the Phase-0 seams ({@code arrestHoldCell} + {@code zones}); bank/prison unwired. */
    public static CivicFixtures ofJustice(int arrestHoldCell, RestrictedZoneTable zones) {
        return new CivicFixtures(arrestHoldCell, zones, Actor.NONE, Actor.NONE,
                BankQueue.EMPTY, PrisonCellRegistry.EMPTY, Payroll.NONE, FoodMarket.EMPTY,
                PatrolRouteTable.EMPTY, RooftopTable.EMPTY, ZLinkTable.EMPTY);
    }
}
