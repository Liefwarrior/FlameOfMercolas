package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.progression.SkillRegistry;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The push CONTEST (Sprint 1 skill-check core's first consumer): with a wired
 * {@link SkillTrackRegistry}, a shove is an open_hand+AGI vs grit+VIG check on the
 * {@code check.push} stream — a trained pusher displaces a hard target measurably more
 * often than a novice; a LOST contest burns no cooldown and moves nobody; a WON contest
 * awards use-XP to both parties (open_hand to the pusher, grit to the shovee) with the
 * level-ups landing in the {@link SkillLevelLog}. The unwired path stays byte-identical to
 * the pre-progression shove ({@code PushMechanicsTest}'s unchanged coverage).
 */
final class PushContestTest {

    private static final int Z = 11;
    private static final long WORLD_SEED = 7L;
    private static final SkillRegistry SKILLS = SkillRawsLoader.load(committedRawsRoot());

    private static Path committedRawsRoot() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws not found above " + Path.of("").toAbsolutePath());
    }

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /** Trains a TRAINED-aptitude skill (open_hand/grit, aptNum 20) to exactly {@code level}. */
    private static void trainTo(SkillTrackRegistry tracks, int actorId, int skillRaw, int level) {
        long grains = 0;
        for (int k = 1; k <= level; k++) {
            grains += k * 100L * 20L;
        }
        tracks.award(actorId, skillRaw, (int) (grains / 20), 999L, 0L);
        assertEquals(level, tracks.level(actorId, skillRaw));
    }

    /** One isolated contested-shove attempt at {@code tick}; returns whether the push landed. */
    private static boolean attempt(long tick, SkillTrackRegistry tracks, ShoveLog log) {
        ActorRegistry registry = new ActorRegistry();
        Actor pusher = registry.spawn(Wastrel.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Wastrel.TYPE, true, 1, 64), cell(10, 10));
        registry.spawn(Wastrel.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Wastrel.TYPE, true, 1, 64), cell(11, 10));
        return PushMechanics.tryPush(pusher, cell(11, 10), registry, tick, c -> true,
                Actor.OccupancyQuery.UNLIMITED, log, tracks,
                pusherId -> NamedDraws.draw(ActorRngStream.CHECK_PUSH, WORLD_SEED, tick,
                        pusherId, 0));
    }

    @Test
    void aTrainedPusherLandsMeasurablyMoreContestsThanANovice() {
        // Novice table: pusher 0 untrained, shovee 1 grit 60 (a hard target -> floor-band odds).
        SkillTrackRegistry noviceTracks = new SkillTrackRegistry(SKILLS);
        trainTo(noviceTracks, 1, noviceTracks.gritRaw(), 60);
        // Master table: pusher 0 open_hand 50 vs the same grit-60 target.
        SkillTrackRegistry masterTracks = new SkillTrackRegistry(SKILLS);
        trainTo(masterTracks, 1, masterTracks.gritRaw(), 60);
        trainTo(masterTracks, 0, masterTracks.openHandRaw(), 50);

        int noviceWins = 0;
        int masterWins = 0;
        int trials = 600;
        for (long t = 1; t <= trials; t++) {
            noviceWins += attempt(t, noviceTracks, ShoveLog.EMPTY) ? 1 : 0;
            masterWins += attempt(t, masterTracks, ShoveLog.EMPTY) ? 1 : 0;
        }
        assertTrue(masterWins > noviceWins,
                "open_hand 50 must displace the grit-60 target more often: novice="
                        + noviceWins + " master=" + masterWins + " / " + trials);
        assertTrue(noviceWins > trials / 3,
                "the liveness floor keeps even the novice shoving through crowds ("
                        + noviceWins + "/" + trials + ")");
    }

    @Test
    void aLostContestMovesNobodyBurnsNoCooldownAndLogsNothing() {
        SkillTrackRegistry tracks = new SkillTrackRegistry(SKILLS);
        trainTo(tracks, 1, tracks.gritRaw(), 60); // floor-band odds: failures are frequent
        ShoveLog log = new ShoveLog(16);

        for (long t = 1; t <= 200; t++) {
            ActorRegistry registry = new ActorRegistry();
            Actor pusher = registry.spawn(Wastrel.TYPE,
                    ActorTestFixtures.statsWithSpeedAndLeash(Wastrel.TYPE, true, 1, 64),
                    cell(10, 10));
            Actor occupant = registry.spawn(Wastrel.TYPE,
                    ActorTestFixtures.statsWithSpeedAndLeash(Wastrel.TYPE, true, 1, 64),
                    cell(11, 10));
            long cooldownBefore = pusher.lastPushTick();
            long tick = t;
            boolean pushed = PushMechanics.tryPush(pusher, cell(11, 10), registry, tick, c -> true,
                    Actor.OccupancyQuery.UNLIMITED, log, tracks,
                    pusherId -> NamedDraws.draw(ActorRngStream.CHECK_PUSH, WORLD_SEED, tick,
                            pusherId, 0));
            if (!pushed) {
                assertEquals(cell(11, 10), occupant.cell(), "a lost contest moves nobody");
                assertEquals(cooldownBefore, pusher.lastPushTick(),
                        "no cooldown burn on a lost contest (the liveness guard)");
                assertEquals(0, log.size(), "a lost contest is not riot material");
                return; // one verified failure is the fixture
            }
        }
        throw new AssertionError("no contest failure in 200 floor-band trials — check the odds");
    }

    @Test
    void aWonContestAwardsOpenHandToThePusherAndGritToTheShovee() {
        SkillTrackRegistry tracks = new SkillTrackRegistry(SKILLS);
        ActorRegistry registry = new ActorRegistry();
        Actor pusher = registry.spawn(Wastrel.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Wastrel.TYPE, true, 1, 64), cell(10, 10));
        Actor occupant = registry.spawn(Wastrel.TYPE,
                ActorTestFixtures.statsWithSpeedAndLeash(Wastrel.TYPE, true, 1, 64), cell(11, 10));

        int wins = 0;
        long tick = 0;
        while (wins < 2 && tick < 500) {
            tick += PushMechanics.PUSH_COOLDOWN_TICKS; // clear both parties' cooldowns
            // Reset the pair to the contested shape for the next attempt.
            pusher.setCell(cell(10, 10));
            occupant.setCell(cell(11, 10));
            long t = tick;
            if (PushMechanics.tryPush(pusher, cell(11, 10), registry, t, c -> true,
                    Actor.OccupancyQuery.UNLIMITED, ShoveLog.EMPTY, tracks,
                    pusherId -> NamedDraws.draw(ActorRngStream.CHECK_PUSH, WORLD_SEED, t,
                            pusherId, 0))) {
                wins++;
            }
        }
        assertEquals(2, wins, "two contested wins within the trial budget");

        // Grit pays 150 cp: one tier-0 shove = 3000 grains >= the 2000-grain L1 threshold.
        assertEquals(1, tracks.level(occupant.id(), tracks.gritRaw()),
                "being shoved teaches grit (pain teaches)");
        // Open hand pays 90 cp: 1800 + 1440 (tier 1, same victim) = 3240 -> level 1 by win 2.
        assertEquals(1, tracks.level(pusher.id(), tracks.openHandRaw()),
                "shoving teaches open_hand");
        assertTrue(tracks.levelLog().size() >= 2, "the level-ups landed in the client seam");
    }
}
