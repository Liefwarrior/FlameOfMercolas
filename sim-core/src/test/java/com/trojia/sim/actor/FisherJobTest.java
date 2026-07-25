package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobParams;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Shopkeeper;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 6 slice 5: the {@code maritime.fisher} loop end to end — target a live spot this
 * soul can SEE, stand the cast cell, cast on the work cadence (FISHING XP on the ATTEMPT,
 * caught or not), mint FISH on a successful {@code check.fishing} draw (conservation
 * accounted via the ctx hooks), and sell a full catch at the buy-side counter (the coin
 * faucet). A soul the water shows nothing falls back to the waterfront wander.
 */
final class FisherJobTest {

    private static final int Z = 18;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /** One small zone whose cast cell sits mid-map; the fisher spawns ON the cast cell. */
    private static FishingZoneTable oneZone() {
        return new FishingZoneTable(
                new int[] {FishingZoneTable.SMALL},
                new int[] {cell(10, 11)},
                new int[][] {{cell(10, 9), cell(11, 9)}});
    }

    /** The committed jobs bound WITH the skill universe, so trainsSkill=fishing resolves. */
    private static final com.trojia.sim.actor.job.JobRegistry WIRED_JOBS =
            com.trojia.sim.actor.job.JobBinder.bind(
                    FishingSpotsTest.locateRawsDir().resolve("jobs").resolve("jobs.json"),
                    ActorTypes.allTypeIds(),
                    SkillRawsLoader.load(FishingSpotsTest.locateRawsDir()));

    /** Ctx double: wired spots + wired tracks + a mint/eat ledger, pinned to a shift tick. */
    private static final class FisherContext extends NoOpActorContext {
        final FishingSpots spots = new FishingSpots(oneZone());
        final SkillTrackRegistry tracks =
                new SkillTrackRegistry(SkillRawsLoader.load(FishingSpotsTest.locateRawsDir()));
        long fishMinted;
        private final long seed;

        FisherContext(ActorRegistry registry, long seed) {
            super(registry);
            this.seed = seed;
            setTick(5_000); // inside maritime.fisher's [1000, 11000) shift
        }

        @Override
        public long worldSeed() {
            return seed;
        }

        @Override
        public com.trojia.sim.actor.job.JobRegistry jobs() {
            return WIRED_JOBS;
        }

        @Override
        public FishingSpots fishingSpots() {
            return spots;
        }

        @Override
        public SkillTrackRegistry skillTracks() {
            return tracks;
        }

        @Override
        public void recordFishMinted(int n) {
            fishMinted += n;
        }
    }

    /**
     * Deterministically finds a world seed whose first surfaced spot is VISIBLE to actor 0
     * at level 0 (pure functions of the seed — stable forever), and returns the prepared
     * context with the spot live.
     */
    private static FisherContext contextWithVisibleSpot(ActorRegistry registry) {
        for (long seed = 1; seed < 400; seed++) {
            FisherContext ctx = new FisherContext(registry, seed);
            long t = 0;
            while (ctx.spots.liveCount() == 0 && t < 600_000) {
                t += FishingSpots.SPAWN_PERIOD_TICKS;
                ctx.spots.tick(seed, t);
            }
            if (ctx.spots.liveCount() > 0
                    && ctx.spots.visibleTo(0, seed, 0, SkillTrackRegistry.UNWIRED)) {
                return ctx;
            }
        }
        throw new AssertionError("no seed under 400 surfaced a visible spot (implausible)");
    }

    private static Actor fisherAt(ActorRegistry registry, NoOpActorContext ctx, int at) {
        Actor fisher = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, 64), at);
        fisher.setAnchorCell(at);
        fisher.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Maritime.Fisher.ID));
        return fisher;
    }

    @Test
    void castsAtAVisibleSpotEarnsAttemptXpAndAccountsEveryCatch() {
        ActorRegistry registry = new ActorRegistry();
        FisherContext ctx = contextWithVisibleSpot(registry);
        int castCell = ctx.spots.castCellAt(0);
        Actor fisher = fisherAt(registry, ctx, castCell);
        Job job = ctx.jobs().get(ctx.jobs().ordinalOf(Job.Maritime.Fisher.ID));
        JobParams params = job.params();

        job.selectTarget(fisher, ctx);
        int dutyBefore = fisher.need(Need.DUTY);
        int casts = 6;
        for (int i = 0; i < casts * params.workTicksPerUnit(); i++) {
            job.pursue(fisher, ctx);
        }
        assertEquals(castCell, fisher.cell(), "the fisher held its cast stand");
        int carried = ctx.items().countCarriedOfKind(fisher.id(), ItemKinds.FISH);
        assertEquals(ctx.fishMinted, carried,
                "every caught fish was minted-and-accounted into the carry");
        assertTrue(fisher.lastReasonCode() == ReasonCode.CAUGHT_FISH
                        || fisher.lastReasonCode() == ReasonCode.FISH_GOT_AWAY,
                "the cast trail reads CAUGHT_FISH/FISH_GOT_AWAY, got "
                        + fisher.lastReasonCode());
        assertTrue(ctx.tracks.progressGrains(fisher.id(), ctx.tracks.fishingRaw()) > 0
                        || ctx.tracks.level(fisher.id(), ctx.tracks.fishingRaw()) > 0,
                "FISHING XP lands on ATTEMPTS, caught or not");
        assertEquals(dutyBefore + casts * params.dutyPerUnit(), fisher.need(Need.DUTY),
                "each completed cast is honest work: DUTY flows per attempt");
    }

    @Test
    void aFullCatchSellsAtTheBuySideCounter() {
        ActorRegistry registry = new ActorRegistry();
        FisherContext ctx = contextWithVisibleSpot(registry);
        int castCell = ctx.spots.castCellAt(0);
        Actor fisher = fisherAt(registry, ctx, castCell);
        Actor shop = registry.spawn(Shopkeeper.TYPE,
                ActorTestFixtures.stats(Shopkeeper.TYPE), cell(11, 11)); // beside the stand
        FoodMarket market = new FoodMarket(new int[] {shop.id()}, new int[0], new int[0]);

        int accFisher = ctx.bankAccounts().openAccount();
        int accShop = ctx.bankAccounts().openAccount();
        assertEquals(fisher.id(), accFisher);
        assertEquals(shop.id(), accShop);
        ctx.bankAccounts().credit(accShop, 100);
        ctx.items().mintIdCard(fisher.id(), accFisher);
        ctx.items().addCarried(fisher.id(), ItemKinds.FISH,
                com.trojia.sim.actor.job.JobBehaviors.FISHER_SELL_TRIP_CATCH);
        long totalBefore = ctx.bankAccounts().totalRoyals();

        // The market must be visible through the ctx: NoOpActorContext's default is EMPTY,
        // so pursue through a thin wrapper context that carries it.
        NoOpActorContext withMarket = new NoOpActorContext(registry) {
            @Override
            public FoodMarket foodMarket() {
                return market;
            }

            @Override
            public FishingSpots fishingSpots() {
                return ctx.spots;
            }

            @Override
            public SkillTrackRegistry skillTracks() {
                return ctx.tracks;
            }

            @Override
            public ItemsLiteRegistry items() {
                return ctx.items();
            }

            @Override
            public BankLedger bankAccounts() {
                return ctx.bankAccounts();
            }
        };
        withMarket.setTick(5_000);

        Job job = withMarket.jobs().get(withMarket.jobs().ordinalOf(Job.Maritime.Fisher.ID));
        job.selectTarget(fisher, withMarket);
        job.pursue(fisher, withMarket); // shop is within FISHER_SELL_REACH: the sale fires

        assertEquals(ReasonCode.SOLD_CATCH, fisher.lastReasonCode(), "the catch sold");
        assertEquals(com.trojia.sim.actor.job.JobBehaviors.FISHER_KEEP_RATION,
                ctx.items().countCarriedOfKind(fisher.id(), ItemKinds.FISH),
                "everything above the keep-ration sold");
        assertTrue(ctx.bankAccounts().balanceOf(accFisher) > 0,
                "the fisher EARNED coin — the citizen coin faucet is open");
        assertEquals(totalBefore, ctx.bankAccounts().totalRoyals(), "conservation exact");
    }

    @Test
    void aSoulTheWaterShowsNothingWandersTheWaterfront() {
        ActorRegistry registry = new ActorRegistry();
        // A context whose registry HAS no live spot at all (never ticked): the fisher must
        // take the wander fallback rather than freeze.
        FisherContext ctx = new FisherContext(registry, 1L);
        Actor fisher = fisherAt(registry, ctx, cell(30, 30));
        Job job = ctx.jobs().get(ctx.jobs().ordinalOf(Job.Maritime.Fisher.ID));
        job.selectTarget(fisher, ctx);
        for (int i = 0; i < 200; i++) {
            job.pursue(fisher, ctx);
        }
        assertTrue(ActorGeometry.chebyshev(fisher.cell(), cell(30, 30)) > 0
                        || fisher.goalTargetKind() == TargetKind.CELL,
                "no visible spot: the fisher waits the water on a wander, never frozen");
    }
}
