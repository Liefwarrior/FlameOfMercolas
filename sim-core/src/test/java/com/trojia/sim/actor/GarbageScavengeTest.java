package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Shopkeeper;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.engine.EngineConfig;
import com.trojia.sim.engine.SimulationEngine;
import com.trojia.sim.engine.Simulations;
import com.trojia.sim.actor.job.JobBinder;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Garbage bins + the SCAVENGE last resort (law &amp; order pass, Eli's garbage-can request):
 * a daily deterministic scrap drop tops each bin cell up to {@link FoodEconomy#BIN_SCRAP_CAP},
 * and {@link SeekFoodPolicy} eats off a bin ONLY when the money gate is genuinely shut (broke +
 * nothing carried + no affordable counter) — strictly worse than every legitimate branch, so a
 * solvent citizen always buys instead and the serf/middle food path is untouched.
 */
final class GarbageScavengeTest {

    private static final int Z = 11;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /** Ctx double with a synthetic FoodMarket (vendors + bins). */
    private static final class MarketContext extends NoOpActorContext {
        private FoodMarket market = FoodMarket.EMPTY;

        MarketContext(ActorRegistry registry) {
            super(registry);
        }

        @Override
        public FoodMarket foodMarket() {
            return market;
        }
    }

    private static Actor hungryWastrel(ActorRegistry registry, NoOpActorContext ctx, int at) {
        Actor wastrel = registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE), at);
        wastrel.setHomeId(ctx.homes().addHome(at));
        wastrel.applyNeedDelta(Need.HUNGER, -8500); // below CRITICAL: must eat
        return wastrel;
    }

    @Test
    void aBrokeActorScavengesAScrapOffABinInReach() {
        ActorRegistry registry = new ActorRegistry();
        MarketContext ctx = new MarketContext(registry);
        Actor broke = hungryWastrel(registry, ctx, cell(50, 50)); // no ID card at all: broke
        int bin = cell(52, 50);
        ctx.market = new FoodMarket(new int[0], new int[0], new int[0], new int[] {bin});
        ctx.items().addOnCell(bin, ItemKinds.FOOD, 2);
        int hungerBefore = broke.need(Need.HUNGER);

        Policies.SEEK_FOOD.act(broke, ctx);

        assertEquals(ReasonCode.SCAVENGED_FOOD, broke.lastReasonCode());
        assertEquals(1, ctx.items().countOnCellOfKind(bin, ItemKinds.FOOD), "one scrap eaten");
        assertTrue(broke.need(Need.HUNGER) > hungerBefore, "the scrap restored HUNGER");
    }

    @Test
    void aSolventActorBuysAtTheCounterAndNeverTouchesTheBin() {
        ActorRegistry registry = new ActorRegistry();
        MarketContext ctx = new MarketContext(registry);
        Actor solvent = hungryWastrel(registry, ctx, cell(50, 50));
        Actor shop = registry.spawn(Shopkeeper.TYPE, ActorTestFixtures.stats(Shopkeeper.TYPE),
                cell(51, 50));
        BankLedger bank = ctx.bankAccounts();
        int account = bank.openAccount(); // 0 == solvent
        bank.openAccount();               // 1 == shop
        bank.credit(account, 20);
        ctx.items().mintIdCard(solvent.id(), account);
        ctx.items().addCarried(shop.id(), ItemKinds.FOOD, 5); // stocked counter in reach
        int bin = cell(52, 50);
        ctx.market = new FoodMarket(new int[] {shop.id()}, new int[0], new int[0], new int[] {bin});
        ctx.items().addOnCell(bin, ItemKinds.FOOD, 2);

        Policies.SEEK_FOOD.act(solvent, ctx);

        assertEquals(ReasonCode.BOUGHT_FOOD, solvent.lastReasonCode(),
                "buying is strictly better than scavenging in the ordering");
        assertEquals(2, ctx.items().countOnCellOfKind(bin, ItemKinds.FOOD), "bin untouched");
        assertEquals(20 - FoodEconomy.FOOD_PRICE, bank.balanceOf(account), "a genuine purchase");
    }

    @Test
    void aSolventActorWithNoCounterStillNeverEatsGarbage() {
        ActorRegistry registry = new ActorRegistry();
        MarketContext ctx = new MarketContext(registry);
        Actor solvent = hungryWastrel(registry, ctx, cell(50, 50));
        BankLedger bank = ctx.bankAccounts();
        int account = bank.openAccount();
        bank.credit(account, 20); // can afford meals — the gate is open, just nothing to buy
        ctx.items().mintIdCard(solvent.id(), account);
        int bin = cell(52, 50);
        ctx.market = new FoodMarket(new int[0], new int[0], new int[0], new int[] {bin});
        ctx.items().addOnCell(bin, ItemKinds.FOOD, 2);

        Policies.SEEK_FOOD.act(solvent, ctx);

        assertEquals(2, ctx.items().countOnCellOfKind(bin, ItemKinds.FOOD),
                "scavenge is the BROKE's branch only — solvency never eats garbage");
    }

    @Test
    void aBrokeActorWalksToADistantStockedBin() {
        ActorRegistry registry = new ActorRegistry();
        MarketContext ctx = new MarketContext(registry);
        Actor broke = hungryWastrel(registry, ctx, cell(50, 50));
        int bin = cell(70, 50); // far beyond EAT_REACH
        ctx.market = new FoodMarket(new int[0], new int[0], new int[0], new int[] {bin});
        ctx.items().addOnCell(bin, ItemKinds.FOOD, 3);

        for (int i = 0; i < 60 && broke.lastReasonCode() != ReasonCode.SCAVENGED_FOOD; i++) {
            Policies.SEEK_FOOD.act(broke, ctx);
        }

        assertEquals(ReasonCode.SCAVENGED_FOOD, broke.lastReasonCode(),
                "the broke walk-target scan reaches the distant bin");
        assertTrue(ActorGeometry.chebyshev(broke.cell(), bin) <= FoodEconomy.EAT_REACH,
                "walked within EAT_REACH of the bin to eat");
    }

    // ======================================================================
    // The daily scrap drop (ActorsSystem cadence)
    // ======================================================================

    private static Path committedJobsJson() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws").resolve("jobs")
                    .resolve("jobs.json");
            if (Files.isRegularFile(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("jobs.json not found");
    }

    @Test
    void theDailyDropTopsEachBinUpToTheCapAndIsConservationAccounted() {
        ActorRegistry registry = new ActorRegistry(); // empty district: pure cadence test
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int bin = cell(30, 30);
        JobRegistry jobs = JobBinder.bind(committedJobsJson(), ActorTypes.allTypeIds());
        FoodMarket market = new FoodMarket(new int[0], new int[0], new int[0], new int[] {bin});
        CivicFixtures fixtures = new CivicFixtures(Actor.NONE, RestrictedZoneTable.EMPTY,
                Actor.NONE, Actor.NONE, BankQueue.EMPTY, PrisonCellRegistry.EMPTY, Payroll.NONE,
                market, PatrolRouteTable.EMPTY);
        ActorsSystem system = new ActorsSystem(7L,
                ActorTypeStatsTable.of(List.of(ActorTestFixtures.stats(Wastrel.TYPE))), jobs,
                registry, new HomeRegistry(), new RelationshipRegistry(), items, new BankLedger(),
                null, fixtures);
        SimulationEngine engine = Simulations.create(new EngineConfig(7L), List.of(system));

        engine.step(FoodEconomy.SCRAP_DROP_PERIOD - 1);
        assertEquals(0, items.countOnCellOfKind(bin, ItemKinds.FOOD), "no drop before the day turns");

        engine.step(1); // the daily boundary
        assertEquals(FoodEconomy.BIN_SCRAP_CAP, items.countOnCellOfKind(bin, ItemKinds.FOOD),
                "topped up to the cap");
        assertEquals(FoodEconomy.BIN_SCRAP_CAP, system.foodMinted(),
                "every dropped scrap is minted + accounted (minted == live + eaten)");

        items.takeOnCell(bin, ItemKinds.FOOD, 1); // someone scavenges one scrap
        engine.step(FoodEconomy.SCRAP_DROP_PERIOD);
        assertEquals(FoodEconomy.BIN_SCRAP_CAP, items.countOnCellOfKind(bin, ItemKinds.FOOD),
                "next day tops back to the cap — never beyond it");
        assertEquals(FoodEconomy.BIN_SCRAP_CAP + 1, system.foodMinted(),
                "only the deficit was minted");
    }
}
