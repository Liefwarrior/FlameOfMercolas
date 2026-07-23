package com.trojia.sim.actor.quest;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorContext;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorRngStream;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.BankLedger;
import com.trojia.sim.actor.CrimeLog;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.NamedDraws;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.NeedConfig;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.RelationshipEdge;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.RestrictedZone;
import com.trojia.sim.actor.RestrictedZoneTable;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.actor.faction.FactionRawsLoader;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The quest engine's world-less rule matrix (Sprint 3): every trigger kind fires under its
 * exact conditions and no others, authored order and one-advance-per-tick hold, effects
 * apply exactly once with seize-what-exists pay, the key-lift watcher moves the token only
 * for the owner on a lift-declaring stage, EXECUTED-party talks are ignored, and
 * {@code first_talker} binds once and stays bound.
 */
final class QuestEngineTest {

    private static final long SEED = 42L;
    private static final int Z = 11;
    private static final int CHEST_CELL = PackedPos.pack(20, 20, Z);
    private static final int YARD_CELL = PackedPos.pack(30, 30, Z);
    private static final int FAR_CELL = PackedPos.pack(50, 50, Z);

    private static Path committedRawsRoot() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws not found");
    }

    private static ActorTypeStats testStats() {
        NeedConfig[] needs = new NeedConfig[Need.COUNT];
        needs[Need.HUNGER.ordinal()] = new NeedConfig(9000, 700, 0, 250, 500);
        needs[Need.REST.ordinal()] = new NeedConfig(9000, 550, 0, 250, 500);
        needs[Need.COIN.ordinal()] = new NeedConfig(8000, 200, 0, 100, 200);
        needs[Need.SAFETY.ordinal()] = new NeedConfig(10000, 0, 2, 400, 800);
        needs[Need.DUTY.ordinal()] = new NeedConfig(9000, 900, 0, 300, 400);
        return new ActorTypeStats(Serf.TYPE, "Test serf", 'X', 0xFFFFFF, "test",
                (short) 20, 1, 24, 4, needs, true, 980, 6, 940, 305, 305, 80, 12000, 24000, 10);
    }

    /** A minimal deterministic context double for the engine (the NoOpActorContext shape). */
    private static final class EngineContext implements ActorContext {
        final ActorRegistry registry = new ActorRegistry();
        final HomeRegistry homes = new HomeRegistry();
        final RelationshipRegistry relationships = new RelationshipRegistry();
        final ItemsLiteRegistry items = new ItemsLiteRegistry();
        final BankLedger bank = new BankLedger();
        final CrimeLog crimeLog = new CrimeLog(16);
        final FactionStandings standings =
                new FactionStandings(FactionRawsLoader.load(committedRawsRoot()));
        final SkillTrackRegistry skillTracks =
                new SkillTrackRegistry(SkillRawsLoader.load(committedRawsRoot()));
        final RestrictedZoneTable zones = new RestrictedZoneTable(List.of(
                new RestrictedZone(Job.Watch.Patrol.ID, Actor.NONE, new int[] {YARD_CELL})));
        long tick = 1L;
        int[] drawCounters = new int[8];
        int searchDraws;

        void setTick(long tick) {
            this.tick = tick;
            java.util.Arrays.fill(drawCounters, 0); // the per-tick counter reset
        }

        @Override
        public long tick() {
            return tick;
        }

        @Override
        public long worldSeed() {
            return SEED;
        }

        @Override
        public ActorRegistry registry() {
            return registry;
        }

        @Override
        public HomeRegistry homes() {
            return homes;
        }

        @Override
        public RelationshipRegistry relationships() {
            return relationships;
        }

        @Override
        public ItemsLiteRegistry items() {
            return items;
        }

        @Override
        public BankLedger bankAccounts() {
            return bank;
        }

        @Override
        public RestrictedZoneTable restrictedZones() {
            return zones;
        }

        @Override
        public JobRegistry jobs() {
            return null; // the engine never resolves jobs
        }

        @Override
        public long draw(ActorRngStream stream, int actorId, int drawIndex) {
            if (stream == ActorRngStream.CHECK_SEARCH) {
                searchDraws++;
            }
            return NamedDraws.draw(stream, SEED, tick, actorId, drawIndex);
        }

        @Override
        public int nextDrawIndex(int actorId) {
            return drawCounters[actorId]++;
        }

        @Override
        public int wielderCell() {
            return Actor.NONE;
        }

        @Override
        public int wielderId() {
            return Actor.NONE;
        }

        @Override
        public boolean isWalkable(int cell) {
            return true;
        }

        @Override
        public Actor.OccupancyQuery occupancy() {
            return Actor.OccupancyQuery.UNLIMITED;
        }

        @Override
        public int arrestHoldCell() {
            return Actor.NONE;
        }

        @Override
        public CrimeLog crimeLog() {
            return crimeLog;
        }

        @Override
        public FactionStandings factionStandings() {
            return standings;
        }

        @Override
        public SkillTrackRegistry skillTracks() {
            return skillTracks;
        }
    }

    /** The whole fixture: player 0, alice 1, bob 2, ambient thief 3; the compiled quest. */
    private static final class Fixture {
        final EngineContext ctx = new EngineContext();
        final Actor player;
        final Actor alice;
        final Actor bob;
        final Actor thief;
        final QuestRegistry quests;
        final QuestLog log;

        Fixture() {
            ActorTypeStats stats = testStats();
            player = ctx.registry.spawn(Serf.TYPE, stats, FAR_CELL);
            alice = ctx.registry.spawn(Serf.TYPE, stats, PackedPos.pack(51, 50, Z));
            bob = ctx.registry.spawn(Serf.TYPE, stats, PackedPos.pack(52, 50, Z));
            thief = ctx.registry.spawn(Serf.TYPE, stats, PackedPos.pack(53, 50, Z));
            int streetwise = ctx.skillTracks.streetwiseRaw();
            int watch = ctx.standings.factions().rawId("watch");
            quests = QuestRegistry.bind(QuestTestFixtures.parseFixture(),
                    QuestTestFixtures.bindings(alice.id(), bob.id(), 0, CHEST_CELL,
                            streetwise, watch));
            log = new QuestLog(quests);
            // The prize waits in the chest (the bake's ledger-leaf placement).
            ctx.items.addOnCell(CHEST_CELL, QuestTestFixtures.PRIZE_KIND, 1);
        }

        void tick(long tick) {
            ctx.setTick(tick);
            QuestEngine.tick(quests, log, ctx);
        }

        void talkAndTick(int talker, int target, long tick) {
            ctx.setTick(tick);
            log.noteTalk(talker, target, tick);
            QuestEngine.tick(quests, log, ctx);
        }

        /** Drives the fixture from fresh to the `middle` stage (owner = player). */
        void reachMiddle() {
            talkAndTick(player.id(), alice.id(), 10L);
            assertEquals(1, log.stageOf(0), "talk alice -> middle");
            assertEquals(player.id(), log.ownerOf(0));
        }

        int edgeCount(int a, int b, RelationshipKind kind) {
            int count = 0;
            for (int i = 0; i < ctx.relationships.size(); i++) {
                RelationshipEdge edge = ctx.relationships.get(i);
                if (edge.kind() == kind && edge.fromId() == a && edge.toId() == b) {
                    count++;
                }
            }
            return count;
        }
    }

    // ================================================================== binding + talk

    @Test
    void anInputlessRunIdlesUnboundWithZeroDrawsAndZeroWrites() {
        Fixture f = new Fixture();
        for (long t = 1; t <= 100; t++) {
            f.tick(t);
        }
        assertEquals(0, f.log.stageOf(0));
        assertEquals(Actor.NONE, f.log.ownerOf(0));
        assertEquals(0L, f.log.totalAdvances());
        assertEquals(0L, f.log.searchAttemptsOf(0));
        assertEquals(0, f.ctx.searchDraws, "no owner -> no draws, ever");
    }

    @Test
    void firstTalkerBindsOnTheStageZeroTalkAndStaysBound() {
        Fixture f = new Fixture();
        // A talk to the WRONG party never fires.
        f.talkAndTick(f.player.id(), f.bob.id(), 5L);
        assertEquals(0, f.log.stageOf(0));
        assertEquals(Actor.NONE, f.log.ownerOf(0));
        // The right party binds the talker and advances; effects stamp the trail.
        f.talkAndTick(f.player.id(), f.alice.id(), 10L);
        assertEquals(1, f.log.stageOf(0));
        assertEquals(f.player.id(), f.log.ownerOf(0));
        assertEquals(10L, f.log.completedTickOf(0, 0));
        assertEquals(1L, f.log.totalAdvances());
        assertEquals(ReasonCode.QUEST_ADVANCED, f.player.lastReasonCode());
        // The quest stays with the body that bound it: at `search`, a THIRD actor's talks
        // never advance anything, and the owner never rebinds.
        f.ctx.items.addCarried(f.player.id(), QuestTestFixtures.TOKEN_KIND, 1);
        f.tick(11L); // item trigger -> search
        assertEquals(2, f.log.stageOf(0));
        f.talkAndTick(f.thief.id(), f.alice.id(), 12L);
        assertEquals(2, f.log.stageOf(0));
        assertEquals(f.player.id(), f.log.ownerOf(0), "first_talker binds ONCE");
    }

    @Test
    void anExecutedPartyIsSilentAndAStaleLatchIsInert() {
        Fixture f = new Fixture();
        f.alice.setStatus(StatusBit.EXECUTED, true);
        f.talkAndTick(f.player.id(), f.alice.id(), 10L);
        assertEquals(0, f.log.stageOf(0), "the gibbet does not advance quests");
        assertEquals(Actor.NONE, f.log.ownerOf(0));
        f.alice.setStatus(StatusBit.EXECUTED, false);
        // The latch matches its own tick only: noting at 20 but evaluating at 21 is inert.
        f.ctx.setTick(20L);
        f.log.noteTalk(f.player.id(), f.alice.id(), 20L);
        f.ctx.setTick(21L);
        QuestEngine.tick(f.quests, f.log, f.ctx);
        assertEquals(0, f.log.stageOf(0), "stale latches never fire");
    }

    // ================================================================== order + pacing

    @Test
    void authoredOrderWinsAndChainedStagesSettleOnSubsequentTicks() {
        // Authored order: two talk triggers on the same party, different destinations —
        // the FIRST authored one must win.
        QuestRaws ordered = QuestRawsLoader.parse("""
                {
                  "id": "quests",
                  "quests": [
                    { "id": "order-quest", "title": "Order", "binding": "first_talker",
                      "parties": ["alice"], "items": [], "zones": [], "cells": [],
                      "stages": [
                        { "key": "start", "objective": "o", "log": "l",
                          "advance": [
                            {"kind": "talk", "party": "alice", "to": "first"},
                            {"kind": "talk", "party": "alice", "to": "second"} ] },
                        { "key": "first", "objective": "o", "log": "l", "terminal": true },
                        { "key": "second", "objective": "o", "log": "l", "terminal": true }
                      ] }
                  ]
                }
                """.getBytes(StandardCharsets.UTF_8));
        Fixture f = new Fixture();
        QuestRegistry registry = QuestRegistry.bind(ordered,
                QuestTestFixtures.bindings(f.alice.id(), f.bob.id(), 0, CHEST_CELL, 0, 0));
        QuestLog log = new QuestLog(registry);
        f.ctx.setTick(10L);
        log.noteTalk(f.player.id(), f.alice.id(), 10L);
        QuestEngine.tick(registry, log, f.ctx);
        assertEquals(1, log.stageOf(0), "the FIRST authored trigger wins");

        // One advance per entry per tick: at `middle` with the token carried AND the key
        // in hand beside the chest, the item trigger advances to `search` — but the search
        // trigger must NOT evaluate the same tick (chained stages settle next tick).
        Fixture g = new Fixture();
        g.reachMiddle();
        g.ctx.items.addCarried(g.player.id(), QuestTestFixtures.TOKEN_KIND, 1);
        g.player.setCell(CHEST_CELL);
        g.tick(20L);
        assertEquals(2, g.log.stageOf(0), "item trigger -> search");
        assertEquals(2L, g.log.totalAdvances(), "exactly ONE advance this tick");
        assertEquals(0, g.ctx.items.countCarriedOfKind(g.player.id(),
                QuestTestFixtures.PRIZE_KIND), "the drawer was NOT opened this tick");
        g.tick(21L);
        assertEquals(3, g.log.stageOf(0), "the chained search settles next tick");
        assertEquals(1, g.ctx.items.countCarriedOfKind(g.alice.id(),
                QuestTestFixtures.PRIZE_KIND), "prize delivered by the end effects");
    }

    // ================================================================== zone + search

    @Test
    void enterZoneFiresOnMembershipOfTheBoundZone() {
        Fixture f = new Fixture();
        f.reachMiddle();
        f.tick(11L);
        assertEquals(1, f.log.stageOf(0), "outside the yard nothing fires");
        f.player.setCell(YARD_CELL);
        f.tick(12L);
        assertEquals(2, f.log.stageOf(0), "standing in the bound zone advances");
    }

    @Test
    void theSearchIsCooldownGatedDrawnOncePerAttemptAndKeyedOpenIsDrawFree() {
        // Without the key: attempts respect retryTicks (25) and draw exactly once each.
        Fixture f = new Fixture();
        f.reachMiddle();
        f.player.setCell(YARD_CELL);
        f.tick(11L); // enter_zone -> search
        assertEquals(2, f.log.stageOf(0));
        f.player.setCell(PackedPos.pack(21, 20, Z)); // adjacent to the chest
        long start = 100L;
        f.tick(start);
        assertEquals(1L, f.log.searchAttemptsOf(0), "first pry the moment eligibility holds");
        assertEquals(1, f.ctx.searchDraws);
        f.tick(start + 1);
        f.tick(start + 24);
        assertEquals(1L, f.log.searchAttemptsOf(0), "cooling down: no attempt, no draw");
        assertEquals(1, f.ctx.searchDraws);
        long t = start + 25;
        while (f.log.stageOf(0) == 2 && t < start + 5_000) {
            f.tick(t);
            t += 25;
        }
        assertEquals(3, f.log.stageOf(0), "the pry eventually gives (350-band threshold)");
        assertEquals((long) f.ctx.searchDraws, f.log.searchAttemptsOf(0),
                "exactly one draw per attempt — the toast cursor equals the draw count");
        assertEquals(1, f.ctx.items.countCarriedOfKind(f.alice.id(),
                QuestTestFixtures.PRIZE_KIND), "the prize reached alice via the end effects");

        // With the key: draw-free, instant.
        Fixture g = new Fixture();
        g.reachMiddle();
        g.player.setCell(YARD_CELL);
        g.tick(11L);
        g.ctx.items.addCarried(g.player.id(), QuestTestFixtures.TOKEN_KIND, 1);
        g.player.setCell(CHEST_CELL);
        g.tick(12L);
        assertEquals(3, g.log.stageOf(0), "the key opens the drawer immediately");
        assertEquals(0, g.ctx.searchDraws, "keyed open is draw-free");
        assertEquals(0L, g.log.searchAttemptsOf(0));
    }

    // ================================================================== effects

    @Test
    void endEffectsApplyExactlyOnceAndPaySeizesWhatExists() {
        Fixture f = new Fixture();
        f.reachMiddle();
        f.ctx.items.addCarried(f.bob.id(), ItemKinds.COIN, 25); // short of the authored 40
        f.ctx.items.addCarried(f.player.id(), QuestTestFixtures.TOKEN_KIND, 1);
        f.player.setCell(CHEST_CELL);
        f.tick(20L); // item -> search
        f.tick(21L); // keyed search -> end (terminal), effects fire
        assertEquals(3, f.log.stageOf(0));
        assertTrue(f.quests.terminal(0, 3));

        // give_item: the prize went owner -> alice.
        assertEquals(1, f.ctx.items.countCarriedOfKind(f.alice.id(),
                QuestTestFixtures.PRIZE_KIND));
        assertEquals(0, f.ctx.items.countCarriedOfKind(f.player.id(),
                QuestTestFixtures.PRIZE_KIND));
        // pay: seize-what-exists — bob's 25 of the authored 40 moved, never minted.
        assertEquals(25, f.ctx.items.countCarriedOfKind(f.player.id(), ItemKinds.COIN));
        assertEquals(0, f.ctx.items.countCarriedOfKind(f.bob.id(), ItemKinds.COIN));
        // standing: +25 watch on the owner's PRESENTED id.
        int watch = f.ctx.standings.factions().rawId("watch");
        assertEquals(25, f.ctx.standings.standingOf(
                f.player.identity().presentedId(), watch));
        // edges: FRIEND mutual (canonical min/max order) + GRUDGE directed bob -> owner.
        assertEquals(1, f.edgeCount(Math.min(f.alice.id(), f.player.id()),
                Math.max(f.alice.id(), f.player.id()), RelationshipKind.FRIEND));
        assertEquals(1, f.edgeCount(f.bob.id(), f.player.id(), RelationshipKind.GRUDGE));
        assertEquals(0, f.edgeCount(f.player.id(), f.bob.id(), RelationshipKind.GRUDGE),
                "the grudge is directed: only bob resents the owner");
        // award_xp: streetwise cp landed on the TRUE doer.
        assertTrue(f.ctx.skillTracks.level(f.player.id(),
                f.ctx.skillTracks.streetwiseRaw()) >= 0);

        // Exactly once: a terminal stage never re-fires its effects.
        long advances = f.log.totalAdvances();
        f.tick(22L);
        f.tick(47L);
        assertEquals(advances, f.log.totalAdvances());
        assertEquals(25, f.ctx.items.countCarriedOfKind(f.player.id(), ItemKinds.COIN),
                "no double pay");
        assertEquals(25, f.ctx.standings.standingOf(
                f.player.identity().presentedId(), watch), "no double standing");
        assertEquals(1, f.edgeCount(f.bob.id(), f.player.id(), RelationshipKind.GRUDGE),
                "no duplicate edges");
    }

    // ================================================================== key-lift watcher

    @Test
    void theKeyLiftWatcherMovesTheTokenOnlyForTheOwnerOnALiftDeclaringStage() {
        Fixture f = new Fixture();
        f.ctx.items.addCarried(f.bob.id(), QuestTestFixtures.TOKEN_KIND, 1);

        // A successful owner-shaped row BEFORE anyone owns the quest: consumed, no lift.
        f.ctx.crimeLog.record(5L, FAR_CELL, f.player.id(), f.player.id(), f.bob.id(), false);
        f.tick(5L);
        assertEquals(1, f.ctx.items.countCarriedOfKind(f.bob.id(),
                QuestTestFixtures.TOKEN_KIND), "unbound: nothing lifts");

        f.reachMiddle(); // now owner = player, stage `middle` declares the lift

        // The consumed row must NOT retroactively lift (the cursor moved past it).
        f.tick(11L);
        assertEquals(1, f.ctx.items.countCarriedOfKind(f.bob.id(),
                QuestTestFixtures.TOKEN_KIND), "consumed rows never re-harvest");

        // An AMBIENT thief's successful dip of bob never moves the token.
        f.ctx.crimeLog.record(12L, FAR_CELL, f.thief.id(), f.thief.id(), f.bob.id(), false);
        f.tick(12L);
        assertEquals(1, f.ctx.items.countCarriedOfKind(f.bob.id(),
                QuestTestFixtures.TOKEN_KIND), "ambient thieves can never take the token");

        // The OWNER's WITNESSED (failed) dip moves nothing.
        f.ctx.crimeLog.record(13L, FAR_CELL, f.player.id(), f.player.id(), f.bob.id(), true);
        f.tick(13L);
        assertEquals(1, f.ctx.items.countCarriedOfKind(f.bob.id(),
                QuestTestFixtures.TOKEN_KIND), "a caught hand takes nothing");

        // The owner's successful dip of the WRONG victim moves nothing.
        f.ctx.crimeLog.record(14L, FAR_CELL, f.player.id(), f.player.id(), f.alice.id(),
                false);
        f.tick(14L);
        assertEquals(1, f.ctx.items.countCarriedOfKind(f.bob.id(),
                QuestTestFixtures.TOKEN_KIND), "only the declared fromParty yields the token");

        // The owner's successful dip of the declared party lifts the token — and the item
        // trigger then advances the same tick (watcher runs before the trigger scan).
        f.ctx.crimeLog.record(15L, FAR_CELL, f.player.id(), f.player.id(), f.bob.id(), false);
        f.tick(15L);
        assertEquals(1, f.ctx.items.countCarriedOfKind(f.player.id(),
                QuestTestFixtures.TOKEN_KIND), "the declared dip yields the token");
        assertEquals(0, f.ctx.items.countCarriedOfKind(f.bob.id(),
                QuestTestFixtures.TOKEN_KIND));
        assertEquals(2, f.log.stageOf(0), "the fresh token satisfies the item trigger");

        // On a stage with NO liftItems (search), another successful dip moves nothing new.
        f.ctx.items.moveCarried(f.player.id(), f.bob.id(), QuestTestFixtures.TOKEN_KIND, 1);
        f.ctx.crimeLog.record(16L, FAR_CELL, f.player.id(), f.player.id(), f.bob.id(), false);
        f.tick(16L);
        assertEquals(1, f.ctx.items.countCarriedOfKind(f.bob.id(),
                QuestTestFixtures.TOKEN_KIND), "no lift declaration on this stage: no lift");
    }
}
