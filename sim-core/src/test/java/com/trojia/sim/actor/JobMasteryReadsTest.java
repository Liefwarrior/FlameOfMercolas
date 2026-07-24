package com.trojia.sim.actor;

import com.trojia.sim.actor.job.GoalKind;
import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.actor.job.JobParams;
import com.trojia.sim.actor.job.RenewMode;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 5 "mastery pays" — the feedback reads, each bounded by construction:
 * <ul>
 *   <li><b>Farm bonus yield:</b> {@code yieldBonusUnits} is a pure integer accumulator
 *       reconstructed from persisted state (~+1% FOOD per fieldcraft level), and a seeded
 *       veteran farmer measurably out-yields a novice through the SAME demand-capped
 *       cascade;</li>
 *   <li><b>Veteran guard sense:</b> {@code guardSenseRadius} is monotonic in streetwise and
 *       HARD-CAPPED at {@link JobBehaviors#SENSE_RADIUS_CAP};</li>
 *   <li><b>Veteran haggling (the pinned free feedback):</b> the existing clamped
 *       streetwise/25 Barter read deepens on its own as the awards wave raises levels —
 *       pinned here so the price drift is measured, not mysterious.</li>
 * </ul>
 */
final class JobMasteryReadsTest {

    private static final int Z = 10;
    private static final int PLOT = PackedPos.pack(50, 50, Z);

    private static final class WiredContext extends NoOpActorContext {
        final SkillTrackRegistry tracks =
                new SkillTrackRegistry(SkillRawsLoader.load(locateRawsDir()));

        WiredContext(ActorRegistry registry) {
            super(registry);
        }

        @Override
        public SkillTrackRegistry skillTracks() {
            return tracks;
        }
    }

    // ======================================================================
    // Farm bonus yield
    // ======================================================================

    @Test
    void yieldBonusUnitsIsAnExactStatelessAccumulator() {
        // Level 0: never a bonus.
        for (int unit = 1; unit <= 10; unit++) {
            assertEquals(0, JobBehaviors.yieldBonusUnits(0, unit));
        }
        // Level 40 across a 5-unit cycle: crossings at units 3 (120/100) and 5 (200/100).
        int[] expected40 = {0, 0, 1, 0, 1};
        for (int unit = 1; unit <= 5; unit++) {
            assertEquals(expected40[unit - 1], JobBehaviors.yieldBonusUnits(40, unit),
                    "level 40, unit " + unit);
        }
        // A full cycle's bonus == floor(level * units / 100): the accumulator identity.
        for (int level = 0; level <= 100; level += 5) {
            int total = 0;
            for (int unit = 1; unit <= 5; unit++) {
                total += JobBehaviors.yieldBonusUnits(level, unit);
            }
            assertEquals(level * 5 / 100, total, "cycle total at level " + level);
        }
        // The level cap itself bounds the per-unit bonus at 1 (level <= 100).
        for (int unit = 1; unit <= 10; unit++) {
            assertEquals(1, JobBehaviors.yieldBonusUnits(100, unit),
                    "a grand master yields exactly +1 per unit, never more");
        }
    }

    @Test
    void aSeededVeteranFarmerOutYieldsANoviceThroughTheSameCappedCascade() {
        ActorRegistry registry = new ActorRegistry();
        Actor veteran = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), PLOT);
        veteran.setAnchorCell(PLOT);
        int novicePlot = PackedPos.pack(60, 60, Z);
        Actor novice = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), novicePlot);
        novice.setAnchorCell(novicePlot);
        WiredContext ctx = new WiredContext(registry);
        int fieldcraft = ctx.tracks.skills().id("fieldcraft").raw();
        ctx.tracks.seedLevel(veteran.id(), fieldcraft, 40);
        JobParams params = new JobParams(GoalKind.TEND_PLOT, 150, 0, 24_000, 0, 1, 5,
                RenewMode.IMMEDIATE, 0, fieldcraft, 25);

        for (int unit = 0; unit < 5; unit++) {
            JobBehaviors.pursueFarm(veteran, ctx, params);
            JobBehaviors.pursueFarm(novice, ctx, params);
        }
        // Home falls back to the plot (no homeId), so the whole yield lands on the plot
        // cell's larder: 5 base units for both; the level-40 hands add 2 bonus FOOD.
        assertEquals(5 + 2, ctx.items().countOnCellOfKind(PLOT, ItemKinds.FOOD),
                "the veteran's cycle: 5 base + floor(40*5/100) bonus");
        assertEquals(5, ctx.items().countOnCellOfKind(novicePlot, ItemKinds.FOOD),
                "the novice's cycle stays the pre-mastery baseline");
    }

    @Test
    void bonusYieldStaysDemandCappedByTheLarder() {
        ActorRegistry registry = new ActorRegistry();
        Actor veteran = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), PLOT);
        veteran.setAnchorCell(PLOT);
        WiredContext ctx = new WiredContext(registry);
        int fieldcraft = ctx.tracks.skills().id("fieldcraft").raw();
        ctx.tracks.seedLevel(veteran.id(), fieldcraft, 100);
        // Pre-fill the larder to the cap: with no atrium (anchor == home cell) and no
        // vendor wired, EVERY unit's yield — base and bonus alike — must pause.
        ctx.items().addOnCell(PLOT, ItemKinds.FOOD, FoodEconomy.LARDER_CAP);
        JobParams params = new JobParams(GoalKind.TEND_PLOT, 150, 0, 24_000, 0, 1, 5,
                RenewMode.IMMEDIATE, 0, fieldcraft, 25);
        for (int unit = 0; unit < 5; unit++) {
            JobBehaviors.pursueFarm(veteran, ctx, params);
        }
        assertEquals(FoodEconomy.LARDER_CAP, ctx.items().countOnCellOfKind(PLOT, ItemKinds.FOOD),
                "a full larder caps mastery exactly as it caps the base yield");
    }

    // ======================================================================
    // Veteran guard sense
    // ======================================================================

    @Test
    void guardSenseRadiusDeepensWithStreetwiseAndHardCapsAtEleven() {
        ActorRegistry registry = new ActorRegistry();
        Actor guard = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), PLOT);
        WiredContext ctx = new WiredContext(registry);
        int streetwise = ctx.tracks.streetwiseRaw();

        assertEquals(8, JobBehaviors.guardSenseRadius(8, ctx, guard.id()),
                "an unschooled rookie senses at the base radius");
        int previous = 8;
        int[][] cases = {{24, 8}, {25, 9}, {45, 9}, {50, 10}, {74, 10}, {75, 11}, {100, 11}};
        for (int[] c : cases) {
            ctx.tracks.seedLevel(guard.id(), streetwise, c[0]);
            int radius = JobBehaviors.guardSenseRadius(8, ctx, guard.id());
            assertEquals(c[1], radius, "streetwise " + c[0]);
            assertTrue(radius >= previous, "monotonic in level");
            assertTrue(radius <= JobBehaviors.SENSE_RADIUS_CAP, "the hard cap is the bound");
            previous = radius;
        }
        NoOpActorContext unwired = new NoOpActorContext(registry);
        assertEquals(8, JobBehaviors.guardSenseRadius(8, unwired, guard.id()),
                "unwired tracks degrade to the base radius exactly");
    }

    // ======================================================================
    // Veteran haggling — the PINNED free feedback (no formula change)
    // ======================================================================

    @Test
    void streetwiseSpreadDeepensTheBarterDiscountInsideTheExistingClamp() {
        ActorRegistry registry = new ActorRegistry();
        Actor novice = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), PLOT);
        Actor veteran = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), PackedPos.pack(51, 50, Z));
        WiredContext ctx = new WiredContext(registry);
        ctx.tracks.seedLevel(veteran.id(), ctx.tracks.streetwiseRaw(), 50);

        long novicePrice = Barter.quoteFor(novice, ctx).personalPrice();
        long veteranPrice = Barter.quoteFor(veteran, ctx).personalPrice();
        assertEquals(FoodEconomy.FOOD_PRICE, novicePrice,
                "a level-0 buyer still pays the flat baseline");
        assertEquals(FoodEconomy.FOOD_PRICE - 50 / Barter.STREETWISE_PER_DISCOUNT,
                veteranPrice, "a level-50 haggler takes the clamped streetwise discount");
        assertTrue(veteranPrice >= Barter.MIN_PRICE, "the clamp holds: never free");
    }

    @Test
    void aLevelledMarkPopulationHardensAgainstPickpockets() {
        // The other pinned free feedback: as the awards wave raises streetwise across the
        // stallkeeps and the Watch, the pickpocket contest's resist side rises with it —
        // clamped by the existing floor/ceiling, so the drift is bounded AND measured.
        ActorRegistry registry = new ActorRegistry();
        Actor thief = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), PLOT);
        Actor greenMark = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), PackedPos.pack(52, 50, Z));
        Actor veteranMark = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), PackedPos.pack(53, 50, Z));
        WiredContext ctx = new WiredContext(registry);
        ctx.tracks.seedLevel(veteranMark.id(), ctx.tracks.streetwiseRaw(), 45);

        int oddsVsGreen = SkillChecks.pickpocketContestPermille(
                ctx.tracks, thief.id(), greenMark.id());
        int oddsVsVeteran = SkillChecks.pickpocketContestPermille(
                ctx.tracks, thief.id(), veteranMark.id());
        assertTrue(oddsVsVeteran < oddsVsGreen,
                "a streetwise-45 mark must be measurably harder to rob");
        assertTrue(oddsVsVeteran >= SkillChecks.PICKPOCKET_FLOOR_PERMILLE
                        && oddsVsGreen <= SkillChecks.PICKPOCKET_CEIL_PERMILLE,
                "the contest clamp bounds the whole drift");
    }

    private static Path locateRawsDir() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException(
                "content/raws not found above " + Path.of("").toAbsolutePath());
    }
}
