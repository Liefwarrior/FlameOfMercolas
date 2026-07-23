package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint-2 pickpocket (the DoD fixtures): the lift is a real gamble on the pickpocket
 * check family, a SUCCESS is an item MOVE (conservation exact across 1000 thefts, by
 * count), a FAILURE lands the witnessed crime row that feeds the justice pipeline, and the
 * level gap measurably moves the odds.
 */
final class TheftMechanicsTest {

    private static final int Z = 11;

    private static Path committedRawsRoot() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws not found above "
                + Path.of("").toAbsolutePath());
    }

    /** Ctx double with a live crime log (and optionally wired tracks). */
    private static final class TheftContext extends NoOpActorContext {
        private final CrimeLog crimeLog = new CrimeLog(256);
        private final SkillTrackRegistry tracks;
        long thefts;
        long caught;
        long coins;

        TheftContext(ActorRegistry registry, SkillTrackRegistry tracks) {
            super(registry);
            this.tracks = tracks;
        }

        @Override
        public CrimeLog crimeLog() {
            return crimeLog;
        }

        @Override
        public SkillTrackRegistry skillTracks() {
            return tracks;
        }

        @Override
        public void recordTheft(boolean success, int coinsMoved) {
            thefts++;
            if (success) {
                coins += coinsMoved;
            } else {
                caught++;
            }
        }
    }

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    @Test
    void successMovesTheWholePocketAndFailureLandsAWitnessedRow() {
        ActorRegistry registry = new ActorRegistry();
        TheftContext ctx = new TheftContext(registry, SkillTrackRegistry.UNWIRED);
        Actor thief = registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                cell(10, 10));
        Actor mark = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(11, 10));

        ctx.items().addCarried(mark.id(), ItemKinds.COIN, 5); // the only 5 Royals in the world
        int successes = 0;
        int failures = 0;
        for (long t = 1; t <= 200; t++) {
            ctx.setTick(t);
            boolean lifted = TheftMechanics.pickpocket(thief, mark, ctx);
            int live = ctx.items().totalOfKind(ItemKinds.COIN)
                    - ctx.items().sunkOfKind(ItemKinds.COIN);
            assertEquals(5, live, "a lift MOVES coin, never mints or burns (tick " + t + ")");
            if (lifted) {
                successes++;
                assertEquals(0, ctx.items().countCarriedOfKind(mark.id(), ItemKinds.COIN),
                        "success empties the mark's pocket");
                assertEquals(5, ctx.items().countCarriedOfKind(thief.id(), ItemKinds.COIN),
                        "and every unit lands in the thief's");
                assertEquals(ReasonCode.PICKPOCKETED, thief.lastReasonCode());
                assertFalse(ctx.crimeLog().witnessedAt(ctx.crimeLog().size() - 1),
                        "a clean lift is unwitnessed");
            } else {
                failures++;
                assertEquals(5, ctx.items().countCarriedOfKind(mark.id(), ItemKinds.COIN),
                        "failure moves nothing");
                assertEquals(ReasonCode.CAUGHT_STEALING, thief.lastReasonCode());
                assertTrue(ctx.crimeLog().witnessedAt(ctx.crimeLog().size() - 1),
                        "failure is the WITNESSED crime row (the justice stim)");
            }
            // Hand the pocket back so every trial starts from the same stock.
            ctx.items().moveCarried(thief.id(), mark.id(), ItemKinds.COIN, 5);
        }
        assertTrue(successes > 0, "at ~700 permille, 200 trials must land some lifts");
        assertTrue(failures > 0, "and some catches — failure feeds the pipeline");
        assertEquals(200, ctx.thefts);
        assertEquals(failures, ctx.caught);
        assertEquals(200, ctx.crimeLog().totalRecorded());
    }

    @Test
    void conservationHoldsExactlyAcrossAThousandThefts() {
        ActorRegistry registry = new ActorRegistry();
        // A ring of four mutually adjacent citizens robbing each other blind (spawned
        // before the ctx double so its per-actor draw counters cover all four ids).
        Actor[] ring = {
                registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE), cell(10, 10)),
                registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(11, 10)),
                registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(10, 11)),
                registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE), cell(11, 11)),
        };
        TheftContext ctx = new TheftContext(registry, SkillTrackRegistry.UNWIRED);
        for (Actor a : ring) {
            ctx.items().addCarried(a.id(), ItemKinds.COIN, 25);
        }
        int minted = ctx.items().totalOfKind(ItemKinds.COIN);
        assertEquals(100, minted);

        for (int i = 0; i < 1_000; i++) {
            ctx.setTick(i + 1);
            Actor thief = ring[i % ring.length];
            Actor mark = ring[(i + 1) % ring.length];
            TheftMechanics.pickpocket(thief, mark, ctx);
            int live = ctx.items().totalOfKind(ItemKinds.COIN)
                    - ctx.items().sunkOfKind(ItemKinds.COIN);
            assertEquals(100, live, "closed COIN supply after theft #" + (i + 1));
        }
        int held = 0;
        for (Actor a : ring) {
            held += ctx.items().countCarriedOfKind(a.id(), ItemKinds.COIN);
        }
        assertEquals(100, held, "every Royal is still in exactly one pocket");
        assertEquals(1_000, ctx.thefts);
    }

    @Test
    void reachAndEligibilityGatesHoldAndNothingIsLogged() {
        ActorRegistry registry = new ActorRegistry();
        TheftContext ctx = new TheftContext(registry, SkillTrackRegistry.UNWIRED);
        Actor thief = registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                cell(10, 10));
        Actor far = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(14, 10));
        ctx.items().addCarried(far.id(), ItemKinds.COIN, 5);

        assertFalse(TheftMechanics.pickpocket(thief, far, ctx), "no reach across 4 tiles");
        assertFalse(TheftMechanics.pickpocket(thief, thief, ctx), "no self-lift");
        assertEquals(0, ctx.crimeLog().totalRecorded(), "a non-attempt logs nothing");
        assertEquals(0, ctx.thefts);
    }

    @Test
    void theLevelGapMeasurablyMovesTheOdds() {
        SkillTrackRegistry tracks = new SkillTrackRegistry(SkillRawsLoader.load(committedRawsRoot()));
        int noviceThief = 0;
        int trainedThief = 1;
        int wiseVictim = 2;
        int naiveVictim = 3;
        pump(tracks, trainedThief, tracks.skyrunningRaw(), 50);
        pump(tracks, wiseVictim, tracks.streetwiseRaw(), 50);

        int baseline = SkillChecks.pickpocketContestPermille(tracks, noviceThief, naiveVictim);
        int trained = SkillChecks.pickpocketContestPermille(tracks, trainedThief, naiveVictim);
        int against = SkillChecks.pickpocketContestPermille(tracks, noviceThief, wiseVictim);

        assertEquals(SkillChecks.PICKPOCKET_BASE_PERMILLE, baseline,
                "equal scores sit at the family base");
        assertTrue(trained > baseline, "a trained skyrunner lifts more reliably: " + trained);
        assertTrue(against < baseline, "a streetwise mark is harder to rob: " + against);
        assertTrue(trained <= SkillChecks.PICKPOCKET_CEIL_PERMILLE
                && against >= SkillChecks.PICKPOCKET_FLOOR_PERMILLE, "the clamps hold");
    }

    @Test
    void aWiredLiftAwardsSkyrunningUseXpToTheThief() {
        SkillTrackRegistry tracks = new SkillTrackRegistry(SkillRawsLoader.load(committedRawsRoot()));
        ActorRegistry registry = new ActorRegistry();
        TheftContext ctx = new TheftContext(registry, tracks);
        Actor thief = registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                cell(10, 10));
        Actor mark = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(11, 10));
        ctx.items().addCarried(mark.id(), ItemKinds.COIN, 3);

        boolean anySuccess = false;
        for (long t = 1; t <= 100 && !anySuccess; t++) {
            ctx.setTick(t);
            anySuccess = TheftMechanics.pickpocket(thief, mark, ctx);
        }
        assertTrue(anySuccess, "100 trials at ~700 permille must land a lift");
        // 150 cp x 20 grains/cp = 3000 grains vs a favored 0->1 threshold of 100·aptNum:
        // one clean lift levels skyrunning immediately — the use-XP DoD, assertable hard.
        assertTrue(tracks.level(thief.id(), tracks.skyrunningRaw()) > 0,
                "the successful lift banked skyrunning use-XP on the TRUE doer");
    }

    @Test
    void ambientTheftWorksTheAdjacentCrowdAndDesperationGatesTheWastrel() {
        ActorRegistry registry = new ActorRegistry();
        TheftContext ctx = new TheftContext(registry, SkillTrackRegistry.UNWIRED);
        Actor villain = registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                cell(10, 10));
        villain.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Villain.Cutpurse.ID));
        Actor mark = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(11, 10));
        mark.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Serf.Laborer.ID));

        for (long t = 1; t <= 400; t++) {
            ctx.setTick(t);
            ctx.items().addCarried(mark.id(), ItemKinds.COIN, 1);
            TheftMechanics.ambientTheft(villain, ctx,
                    TheftMechanics.VILLAIN_THEFT_IMPULSE_PERMILLE);
        }
        assertTrue(ctx.thefts > 0, "the impulse gate opens sometimes over 400 boundaries");
        assertTrue(ctx.thefts < 400, "and stays shut most of the time (the volume knob)");

        // The no-ID test double reads as desperate (the wastrel theft gate).
        assertTrue(TheftMechanics.isDesperate(villain, ctx),
                "no ID card -> cannot buy -> desperate");
    }

    /** Awards use-XP with fresh contexts until the skill reaches {@code targetLevel}. */
    private static void pump(SkillTrackRegistry tracks, int actorId, int skillRaw,
            int targetLevel) {
        long context = 1_000;
        long tick = 1;
        while (tracks.level(actorId, skillRaw) < targetLevel) {
            tracks.award(actorId, skillRaw, 10_000, context++, tick++);
            if (context > 100_000) {
                throw new IllegalStateException("skill pump did not converge");
            }
        }
    }
}
