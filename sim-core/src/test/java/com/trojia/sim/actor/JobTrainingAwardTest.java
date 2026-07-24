package com.trojia.sim.actor;

import com.trojia.sim.actor.job.GoalKind;
import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.actor.job.JobParams;
import com.trojia.sim.actor.job.RenewMode;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-5 award seams (the awards wave's behavior half): a completed anchor work-unit,
 * a patrol corner arrival and a completed wander dwell each award the job's bind-resolved
 * training pair to the TRUE doer through {@link SkillTrackRegistry#award} — priced by the
 * live §3.3 satiation math (pinned end-to-end here: the exact grain ramp 500/400/300/200/125
 * for a same-context cp-25 grind, and the exact unit count to level 1) — while traveling
 * ticks, non-training params and unwired registries award nothing. Draw-free: no test here
 * consumes an RNG stream beyond the wander's own named target draws.
 */
final class JobTrainingAwardTest {

    private static final int Z = 10;
    private static final int ANCHOR = PackedPos.pack(50, 50, Z);

    /** A context double with a REAL wired track table over the committed 18-skill raws. */
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

    /** All-day window, {@code workTicksPerUnit = 1}: every in-place pursue completes a unit. */
    private static JobParams anchorParams(int trainSkillRaw, int trainCp) {
        return new JobParams(GoalKind.HAUL_WORK, 150, 0, 24_000, 0, 1, 5,
                RenewMode.IMMEDIATE, 0, trainSkillRaw, trainCp);
    }

    @Test
    void anchorWorkUnitsTrainTheJobSkillThroughTheExactSatiationRamp() {
        ActorRegistry registry = new ActorRegistry();
        Actor worker = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), ANCHOR);
        worker.setAnchorCell(ANCHOR);
        WiredContext ctx = new WiredContext(registry);
        int kitKeeping = ctx.tracks.skills().id("kit_keeping").raw();
        JobParams params = anchorParams(kitKeeping, 25);

        // Same-context cp-25 awards ramp 500/400/300/200 then floor at 125 grains
        // (satFactor 20/16/12/8/5 — §3.3). kit_keeping is TRAINED (aptNum 20), so level
        // 0 -> 1 needs 2000 grains: 8 units bank 1900, the 9th crosses with 25 left over.
        for (int unit = 1; unit <= 8; unit++) {
            JobBehaviors.pursueAtAnchor(worker, ctx, params);
        }
        assertEquals(0, ctx.tracks.level(worker.id(), kitKeeping), "8 units stay level 0");
        assertEquals(1900, ctx.tracks.progressGrains(worker.id(), kitKeeping),
                "the §3.3 ramp: 500+400+300+200+125*4");
        assertEquals(2000, ctx.tracks.thresholdGrains(worker.id(), kitKeeping),
                "TRAINED level 0 -> 1 threshold");

        JobBehaviors.pursueAtAnchor(worker, ctx, params);
        assertEquals(1, ctx.tracks.level(worker.id(), kitKeeping),
                "the 9th same-context unit crosses level 1");
        assertEquals(25, ctx.tracks.progressGrains(worker.id(), kitKeeping),
                "excess grains carry (2025 - 2000)");
        assertEquals(4000, ctx.tracks.thresholdGrains(worker.id(), kitKeeping),
                "TRAINED level 1 -> 2 threshold");
        assertEquals(1, ctx.tracks.levelLog().totalRecorded(),
                "exactly one level-up row lands in the client ring");
    }

    @Test
    void patrolCornerArrivalsTrainWhileTravelingTicksAwardNothing() {
        ActorRegistry registry = new ActorRegistry();
        Actor watch = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(MilitiaWatch.TYPE, true, 1, 24),
                ANCHOR);
        watch.setAnchorCell(ANCHOR);
        WiredContext ctx = new WiredContext(registry);
        int streetwise = ctx.tracks.skills().id("streetwise").raw();
        JobParams params = new JobParams(GoalKind.PATROL_ROUTE, 150, 0, 24_000, 0, 40, 5,
                RenewMode.IMMEDIATE, 0, streetwise, 10);

        JobBehaviors.selectRouteStart(watch, ctx);
        int arrivals = 0;
        for (int tick = 0; tick < 60; tick++) {
            int progressBefore = Math.floorMod(watch.goalProgress(), 4);
            long totalBefore = totalStreetwiseGrains(ctx, watch.id(), streetwise);
            JobBehaviors.pursuePatrol(watch, ctx, 3, params);
            boolean arrived = Math.floorMod(watch.goalProgress(), 4) != progressBefore;
            long totalAfter = totalStreetwiseGrains(ctx, watch.id(), streetwise);
            if (arrived) {
                arrivals++;
                assertTrue(totalAfter > totalBefore,
                        "a corner arrival must award, tick " + tick);
            } else {
                assertEquals(totalBefore, totalAfter,
                        "a traveling tick must award NOTHING (§3.2 rule 4), tick " + tick);
            }
        }
        assertTrue(arrivals >= 4, "60 ticks on a radius-3 beat must round several corners");
        // Every corner is its own fresh §3.3 context: each of the first 4 arrivals pays
        // the full tier-0 rate (10 cp * 20 = 200 grains), so the floor-rate lower bound
        // holds with room to spare.
        assertTrue(totalStreetwiseGrains(ctx, watch.id(), streetwise) >= arrivals * 50L,
                "multi-context corners pay at least the floor rate per arrival");
    }

    /**
     * Banked grains PLUS the grains already consumed by level-ups (streetwise is FAVORED,
     * aptNum 15: threshold {@code l -> l+1} is {@code (l+1)*1500}), so a level crossing
     * never makes the monotonic-award probe read a decrease.
     */
    private static long totalStreetwiseGrains(WiredContext ctx, int actorId, int streetwise) {
        int level = ctx.tracks.level(actorId, streetwise);
        long consumed = 1500L * level * (level + 1) / 2;
        return consumed + ctx.tracks.progressGrains(actorId, streetwise);
    }

    @Test
    void wanderDwellCompletionsTrainTheKeeperButNeverTheTrainless() {
        ActorRegistry registry = new ActorRegistry();
        Actor keeper = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, 8), ANCHOR);
        keeper.setAnchorCell(ANCHOR);
        Actor beastish = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, 8),
                PackedPos.pack(70, 70, Z));
        beastish.setAnchorCell(beastish.cell());
        WiredContext ctx = new WiredContext(registry);
        int fieldcraft = ctx.tracks.skills().id("fieldcraft").raw();
        JobParams trained = new JobParams(GoalKind.TEND_BEASTS, 150, 0, 24_000, 0, 3, 5,
                RenewMode.IMMEDIATE, 0, fieldcraft, 25);
        JobParams trainless = new JobParams(GoalKind.SCAVENGE_CIRCUIT, 150, 0, 24_000, 0, 3, 5,
                RenewMode.IMMEDIATE, 0);

        JobBehaviors.selectWanderTarget(keeper, ctx);
        JobBehaviors.selectWanderTarget(beastish, ctx);
        for (int tick = 0; tick < 400; tick++) {
            JobBehaviors.pursueWander(keeper, ctx, trained);
            JobBehaviors.pursueWander(beastish, ctx, trainless);
        }
        int grains = ctx.tracks.progressGrains(keeper.id(), fieldcraft)
                + 2500 * ctx.tracks.level(keeper.id(), fieldcraft);
        assertTrue(grains > 0, "400 ticks of dwell-completing sweep must train fieldcraft");
        for (int s = 0; s < ctx.tracks.skills().size(); s++) {
            assertEquals(0, ctx.tracks.level(beastish.id(), s),
                    "trainless wander params must never level anything");
            assertEquals(0, ctx.tracks.progressGrains(beastish.id(), s),
                    "trainless wander params must never bank a grain");
        }
    }

    @Test
    void unwiredRegistriesDegradeTheWholeWaveToANoOp() {
        ActorRegistry registry = new ActorRegistry();
        Actor worker = registry.spawn(Serf.TYPE,
                ActorTestFixtures.statsWithDefer(Serf.TYPE, true), ANCHOR);
        worker.setAnchorCell(ANCHOR);
        NoOpActorContext ctx = new NoOpActorContext(registry); // skillTracks() == UNWIRED
        JobParams params = anchorParams(3, 25); // a "resolved" raw against no universe
        for (int unit = 0; unit < 20; unit++) {
            JobBehaviors.pursueAtAnchor(worker, ctx, params);
        }
        assertEquals(20, worker.goalProgress(),
                "work itself still accrues normally under an unwired registry");
        assertEquals(0, ctx.skillTracks().level(worker.id(), 3),
                "unwired award path must stay a level-0 no-op");
        assertEquals(0, ctx.skillTracks().progressGrains(worker.id(), 3),
                "unwired progress read must degrade to 0");
        assertEquals(0, ctx.skillTracks().thresholdGrains(worker.id(), 3),
                "unwired threshold read must degrade to 0");
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
