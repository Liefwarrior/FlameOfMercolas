package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.BarkSelector;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.RelationshipEdge;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.quest.QuestLog;
import com.trojia.sim.actor.quest.QuestRegistry;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.io.WorldHasher;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint-3 DoD over the real baked docks: THE VANISHED CLERK is playable end-to-end
 * through EXISTING verbs only — three headless runs drive one ordinary citizen (Gabri's
 * body for the day) through every stage to EACH ending via the real play-mode intent
 * setters (talk / pickpocket / act-as / direct placement), asserting per stage the quest
 * log's ordinals and completion ticks, the items' physical journey (leaf desk→carry→party;
 * key Gilt→carry), the endings' exact standing deltas on the PRESENTED face, the
 * FRIEND/GRUDGE edges (directed correctly), COIN conservation, and the TALKED /
 * QUEST_ADVANCED legibility trail.
 *
 * <p>Run A additionally proves the quest system's own determinism BEYOND the inputless
 * gate: every input it injected is recorded as a script and replayed against a second
 * fresh bake — the ACTORS-section hash must match checkpoint for checkpoint and at the
 * final tick (the input-replay twin).
 */
class DocksQuestWalkthroughTest {

    // ================================================================== the driven run

    private enum Op { STATUS_PLAYED, TELEPORT, ACT_AS, TALK, PICKPOCKET }

    /** One recorded input: applied before the step of {@code tick} (1-based). */
    private record Action(long tick, int actorId, Op op, int arg) {
    }

    private static final class Run {
        final FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        final DocksPopulation population =
                DocksPopulation.build(loaded.worldSeed(), loaded.world());
        final SimulationDriver driver = new SimulationDriver(loaded.world(),
                loaded.worldSeed(), List.<SimulationSystem>of(population.system()));
        final ActorRegistry registry = population.registry();
        final QuestRegistry quests = population.questRegistry();
        final QuestLog log = population.system().questLog();
        final Map<String, Integer> notables;
        final int playerId;
        final int watchId;
        final int coinLiveAtBake;

        long tick; // completed steps
        final List<Action> script = new ArrayList<>();
        final List<long[]> checkpoints = new ArrayList<>(); // {tick, actorsHash}
        final HashSet<ReasonCode> observed = new HashSet<>();

        Run() {
            assertEquals(692, registry.size(), "the roster stays 692 — the clerk never spawns");
            assertEquals(1, quests.questCount(), "The Vanished Clerk is baked");
            assertEquals("vanished-clerk", quests.questId(0));
            notables = NameForge.bindNotableActors(registry, population.homes(),
                    NotableRaws.load(RepoPaths.locate("content", "raws")
                            .resolve("names").resolve("notables.json")),
                    DocksPopulation.notableSpawnSites());
            playerId = pickOrdinaryCitizen();
            watchId = pickWatchBody();
            coinLiveAtBake = population.items().liveOfKind(ItemKinds.COIN);
            apply(new Action(tick + 1, playerId, Op.STATUS_PLAYED, 1));
        }

        private int pickOrdinaryCitizen() {
            for (int i = 0; i < registry.size(); i++) {
                if (registry.get(i).typeId().key().equals("serf")
                        && !notables.containsValue(i)) {
                    return i;
                }
            }
            throw new IllegalStateException("no ordinary serf to play");
        }

        private int pickWatchBody() {
            for (int i = 0; i < registry.size(); i++) {
                Actor a = registry.get(i);
                if (a.jobOrdinal() >= 0 && population.jobs().get(a.jobOrdinal())
                        .id().equals(Job.Watch.Patrol.ID)) {
                    return i;
                }
            }
            throw new IllegalStateException("no watch body to present");
        }

        int stage(String key) {
            for (int s = 0; s < quests.stageCount(0); s++) {
                if (quests.stageKey(0, s).equals(key)) {
                    return s;
                }
            }
            throw new IllegalStateException("no stage '" + key + "'");
        }

        // ---- recorded inputs (applied live AND appended to the replay script) ----

        void apply(Action action) {
            script.add(action);
            perform(registry, action);
        }

        static void perform(ActorRegistry registry, Action a) {
            Actor actor = registry.get(a.actorId());
            switch (a.op()) {
                case STATUS_PLAYED -> actor.setStatus(StatusBit.PLAYER_CONTROLLED, true);
                case TELEPORT -> actor.setCell(a.arg());
                case ACT_AS -> actor.setActAs(a.arg());
                case TALK -> actor.setPlayerTalkTarget(a.arg());
                case PICKPOCKET -> actor.setPlayerPickpocketTarget(a.arg());
            }
        }

        void teleportBeside(int targetId) {
            int c = registry.get(targetId).cell();
            apply(new Action(tick + 1, playerId, Op.TELEPORT,
                    PackedPos.pack(PackedPos.x(c) + 1, PackedPos.y(c), PackedPos.z(c))));
        }

        void teleportTo(int cell) {
            apply(new Action(tick + 1, playerId, Op.TELEPORT, cell));
        }

        void actAs(int presentedId) {
            apply(new Action(tick + 1, playerId, Op.ACT_AS, presentedId));
        }

        void talk(int targetId) {
            apply(new Action(tick + 1, playerId, Op.TALK, targetId));
        }

        void pickpocket(int targetId) {
            apply(new Action(tick + 1, playerId, Op.PICKPOCKET, targetId));
        }

        void step() {
            driver.requestStep();
            tick++;
            ReasonCode r = registry.get(playerId).lastReasonCode();
            if (r == ReasonCode.TALKED || r == ReasonCode.QUEST_ADVANCED) {
                observed.add(r);
            }
            if (tick % 500 == 0) {
                checkpoints.add(new long[] {tick, actorsHash(population.system())});
            }
        }

        /** Talks to {@code targetId} (teleporting beside it) every tick until {@code untilStage}. */
        void driveTalkBeat(int targetId, int untilStage, int budgetTicks) {
            long deadline = tick + budgetTicks;
            while (log.stageOf(0) != untilStage) {
                assertTrue(tick < deadline, "talk beat stalled before stage " + untilStage
                        + " (still at " + log.stageOf(0) + " after " + budgetTicks + " ticks)");
                teleportBeside(targetId);
                talk(targetId);
                step();
            }
        }

        /** A desk-adjacent hall cell (one east of the clerk's desk — inside the K36 shell). */
        int besideDesk() {
            int desk = DocksPopulation.clerksDeskCell();
            return PackedPos.pack(PackedPos.x(desk) + 1, PackedPos.y(desk),
                    PackedPos.z(desk));
        }

        int carried(int actorId, short kind) {
            return population.items().countCarriedOfKind(actorId, kind);
        }

        int edgeCount(int fromId, int toId, RelationshipKind kind) {
            int count = 0;
            for (int i = 0; i < population.relationships().size(); i++) {
                RelationshipEdge edge = population.relationships().get(i);
                if (edge.kind() == kind && edge.fromId() == fromId && edge.toId() == toId) {
                    count++;
                }
            }
            return count;
        }

        int standing(String faction) {
            return population.system().factionStandings().standingOf(
                    registry.get(playerId).identity().presentedId(),
                    population.system().factionStandings().factions().rawId(faction));
        }

        /** The shared closing assertions every run must satisfy. */
        void assertCommonPostConditions(String endingKey) {
            assertEquals(stage(endingKey), log.stageOf(0));
            assertTrue(quests.terminal(0, log.stageOf(0)));
            assertEquals(playerId, log.ownerOf(0), "the quest stayed with the body that bound it");
            // The completion trail is stamped in visit order, nondecreasing.
            long previous = 0;
            int stamped = 0;
            for (int s = 0; s < quests.stageCount(0); s++) {
                long at = log.completedTickOf(0, s);
                if (at >= 0) {
                    assertTrue(at >= previous, "completion ticks stamp in visit order");
                    previous = at;
                    stamped++;
                }
            }
            assertEquals(5, stamped, "all five non-terminal stages on the path completed");
            // The legibility trail.
            assertTrue(observed.contains(ReasonCode.TALKED), "TALKED observed on the trail");
            assertTrue(observed.contains(ReasonCode.QUEST_ADVANCED),
                    "QUEST_ADVANCED observed on the trail");
            // COIN conservation: every quest movement was a MOVE, never a mint.
            assertEquals(coinLiveAtBake, population.items().liveOfKind(ItemKinds.COIN),
                    "the closed COIN supply is exact after the quest");
            // The leaf's journey ended with the chosen party; the desk is empty.
            assertEquals(0, population.items().countOnCellOfKind(
                    DocksPopulation.clerksDeskCell(), ItemKinds.LEDGER_LEAF));
            assertEquals(0, carried(playerId, ItemKinds.LEDGER_LEAF));
        }

        /** Proves the TALKED stamp with a valid talk to a NON-party (no trigger fires). */
        void proveTalkTrail() {
            long deadline = tick + 200;
            while (!observed.contains(ReasonCode.TALKED)) {
                assertTrue(tick < deadline, "the talk trail never stamped");
                teleportBeside(watchId);
                talk(watchId);
                step();
            }
            assertEquals(0, log.stageOf(0), "talking to a non-party advances nothing");
            assertEquals(Actor.NONE, log.ownerOf(0));
        }
    }

    private static long actorsHash(com.trojia.sim.actor.ActorsSystem system) {
        WorldHasher hasher = new WorldHasher();
        system.hashInto(hasher.sectionSink(system.id()));
        return hasher.sectionHash(system.id());
    }

    // ================================================================== Run A

    @Test
    void runA_presentTheWatchPryTheDrawer_endCrell_andTheInputReplayTwinMatches() {
        Run run = new Run();
        run.proveTalkTrail();

        // S0 rumor: Onna at the Mission remembers the clerk — binds the talker.
        run.driveTalkBeat(run.notables.get("onna"), run.stage("harbormaster"), 400);
        assertEquals(run.playerId, run.log.ownerOf(0), "first_talker bound Gabri's body");

        // S1: Crell at the Weighhouse names the erased line.
        run.driveTalkBeat(run.notables.get("crell"), run.stage("counting_house"), 400);

        // S2: wear the Watch's colors and walk in — the enter_zone route.
        run.actAs(run.watchId);
        run.teleportTo(run.besideDesk());
        run.step();
        assertEquals(run.stage("drawer"), run.log.stageOf(0),
                "standing in the hall with a respected face advances (enter_zone)");

        // S3: the streetwise pry — cooldown-gated check.search until the drawer gives.
        long deadline = run.tick + 8_000;
        while (run.log.stageOf(0) == run.stage("drawer")) {
            assertTrue(run.tick < deadline, "the pry never gave (search attempts: "
                    + run.log.searchAttemptsOf(0) + ")");
            run.step();
        }
        assertEquals(run.stage("choice"), run.log.stageOf(0));
        assertTrue(run.log.searchAttemptsOf(0) >= 1, "the pry was a real cooldown-gated check");
        assertEquals(1, run.carried(run.playerId, ItemKinds.LEDGER_LEAF),
                "the torn leaf came out of the drawer into Gabri's carry");

        // S4 -> end_crell: drop the mask (the ward must reward GABRI's face), raise the line.
        run.actAs(run.playerId);
        int watchBefore = run.standing("watch");
        int merchantsBefore = run.standing("merchants");
        run.driveTalkBeat(run.notables.get("crell"), run.stage("end_crell"), 600);

        run.assertCommonPostConditions("end_crell");
        int crell = run.notables.get("crell");
        int gilt = run.notables.get("gilt");
        assertEquals(1, run.carried(crell, ItemKinds.LEDGER_LEAF), "Crell holds the proof");
        assertEquals(25, run.standing("watch") - watchBefore, "watch +25 exact");
        assertEquals(15, run.standing("merchants") - merchantsBefore, "merchants +15 exact");
        assertEquals(1, run.edgeCount(Math.min(crell, run.playerId),
                Math.max(crell, run.playerId), RelationshipKind.FRIEND),
                "Crell's friendship is mutual");
        assertEquals(1, run.edgeCount(gilt, run.playerId, RelationshipKind.GRUDGE),
                "Gilt's grudge is directed AT Gabri");
        assertEquals(0, run.edgeCount(run.playerId, gilt, RelationshipKind.GRUDGE),
                "Gabri holds no grudge back");

        // Gilt now greets Gabri [hostile] forever — through the EXISTING greet tables.
        Actor giltBody = run.registry.get(gilt);
        Job giltJob = run.population.jobs().get(giltBody.jobOrdinal());
        BarkSelector.BarkChoice greet = BarkSelector.select(run.loaded.worldSeed(),
                run.tick, giltBody, giltJob,
                run.registry.get(run.playerId).identity().presentedId(),
                run.population.system().factionStandings(), run.population.relationships());
        assertTrue(greet.tableKey().contains(".hostile."),
                "the banker's greeting turned hostile: " + greet.tableKey());

        // ---- the input-replay twin: same bake, same script, byte-identical evolution ----
        run.checkpoints.add(new long[] {run.tick, actorsHash(run.population.system())});
        FixtureWorldLoader.Loaded twinLoaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation twin = DocksPopulation.build(twinLoaded.worldSeed(),
                twinLoaded.world());
        SimulationDriver twinDriver = new SimulationDriver(twinLoaded.world(),
                twinLoaded.worldSeed(), List.<SimulationSystem>of(twin.system()));
        int scriptIndex = 0;
        int checkpointIndex = 0;
        for (long t = 1; t <= run.tick; t++) {
            while (scriptIndex < run.script.size()
                    && run.script.get(scriptIndex).tick() == t) {
                Run.perform(twin.registry(), run.script.get(scriptIndex));
                scriptIndex++;
            }
            twinDriver.requestStep();
            if (t % 500 == 0) {
                assertEquals(run.checkpoints.get(checkpointIndex)[1],
                        actorsHash(twin.system()),
                        "input-replay twin diverged at tick " + t);
                checkpointIndex++;
            }
        }
        assertEquals(run.script.size(), scriptIndex, "the whole script replayed");
        assertEquals(run.checkpoints.get(run.checkpoints.size() - 1)[1],
                actorsHash(twin.system()), "input-replay twin: final hash byte-identical");
        assertEquals(run.log.stageOf(0), twin.system().questLog().stageOf(0));
        assertEquals(run.log.ownerOf(0), twin.system().questLog().ownerOf(0));
        System.out.println("[DocksQuestWalkthroughTest] run A ticks=" + run.tick
                + " searchAttempts=" + run.log.searchAttemptsOf(0)
                + " scriptActions=" + run.script.size());
    }

    // ================================================================== Run B

    @Test
    void runB_pickpocketTheVaultKey_endGilt() {
        Run run = new Run();
        int gilt = run.notables.get("gilt");
        run.proveTalkTrail();

        // S0 rumor via Dagny at the Wrackhouse; S1 Crell.
        run.driveTalkBeat(run.notables.get("dagny"), run.stage("harbormaster"), 400);
        run.driveTalkBeat(run.notables.get("crell"), run.stage("counting_house"), 400);

        // S2/S3: wear the Watch's colors (adjacency to Gilt IS the hall), and keep dipping
        // his pocket — the stock G verb; a SUCCESSFUL dip also yields the key through the
        // engine's lift watcher. The lift stays available on `drawer` (enter_zone advances
        // us there the moment we stand in the hall).
        run.actAs(run.watchId);
        assertEquals(1, run.carried(gilt, ItemKinds.VAULT_KEY), "the bake keyed Gilt");
        long deadline = run.tick + 4_000;
        while (run.carried(run.playerId, ItemKinds.VAULT_KEY) == 0) {
            assertTrue(run.tick < deadline, "the key never landed (thefts="
                    + run.population.system().theftCount() + ")");
            run.teleportBeside(gilt);
            run.pickpocket(gilt);
            run.step();
        }
        assertEquals(0, run.carried(gilt, ItemKinds.VAULT_KEY), "the key LEFT Gilt's ring");
        assertEquals(run.stage("drawer"), run.log.stageOf(0),
                "standing in the hall advanced counting_house -> drawer meanwhile");

        // The key opens the drawer draw-free.
        long attemptsBefore = run.log.searchAttemptsOf(0);
        run.teleportTo(run.besideDesk());
        deadline = run.tick + 50;
        while (run.log.stageOf(0) == run.stage("drawer")) {
            assertTrue(run.tick < deadline, "the keyed drawer must open immediately");
            run.step();
        }
        assertEquals(run.stage("choice"), run.log.stageOf(0));
        assertEquals(attemptsBefore, run.log.searchAttemptsOf(0),
                "with the key in hand the drawer opens DRAW-FREE (no attempts burned)");

        // S4 -> end_gilt: drop the mask and sell the truth back to the banker.
        run.actAs(run.playerId);
        int giltPocketBefore = run.carried(gilt, ItemKinds.COIN);
        int playerCoinBefore = run.carried(run.playerId, ItemKinds.COIN);
        int watchBefore = run.standing("watch");
        int templeBefore = run.standing("temple");
        run.driveTalkBeat(gilt, run.stage("end_gilt"), 600);

        run.assertCommonPostConditions("end_gilt");
        assertEquals(1, run.carried(gilt, ItemKinds.LEDGER_LEAF), "Gilt buries the proof");
        int expectedPay = Math.min(DocksPopulation.VANISHED_CLERK_GILT_POCKET,
                giltPocketBefore);
        assertEquals(expectedPay, run.carried(run.playerId, ItemKinds.COIN) - playerCoinBefore,
                "the banker's price: seize-what-exists off his live pocket");
        assertEquals(giltPocketBefore - expectedPay, run.carried(gilt, ItemKinds.COIN),
                "every Royal MOVED, none minted");
        assertEquals(-20, run.standing("watch") - watchBefore, "watch -20 exact");
        assertEquals(-15, run.standing("temple") - templeBefore, "temple -15 exact");
        assertEquals(1, run.edgeCount(Math.min(gilt, run.playerId),
                Math.max(gilt, run.playerId), RelationshipKind.FRIEND),
                "the King's banker now greets Gabri warmly");
        System.out.println("[DocksQuestWalkthroughTest] run B ticks=" + run.tick
                + " giltPocketAtEnding=" + giltPocketBefore + " paid=" + expectedPay);
    }

    // ================================================================== Run C

    @Test
    void runC_bringTheLeafToTheRooftops_endFinch() {
        Run run = new Run();
        int finch = run.notables.get("finch");
        run.proveTalkTrail();

        // S0 rumor via Tarry Jek down on the Beaching Strand; S1 Crell.
        run.driveTalkBeat(run.notables.get("jek"), run.stage("harbormaster"), 400);
        run.driveTalkBeat(run.notables.get("crell"), run.stage("counting_house"), 400);

        // S2: the Watch face again; S3: the pry (the key stays on Gilt this run).
        run.actAs(run.watchId);
        run.teleportTo(run.besideDesk());
        run.step();
        assertEquals(run.stage("drawer"), run.log.stageOf(0));
        long deadline = run.tick + 8_000;
        while (run.log.stageOf(0) == run.stage("drawer")) {
            assertTrue(run.tick < deadline, "the pry never gave");
            run.step();
        }
        assertEquals(1, run.carried(run.playerId, ItemKinds.LEDGER_LEAF));
        assertEquals(1, run.carried(run.notables.get("gilt"), ItemKinds.VAULT_KEY),
                "run C never touched Gilt's key");

        // S4 -> end_finch: up the wall to the quiet tenant.
        run.actAs(run.playerId);
        int skyrunnersBefore = run.standing("skyrunners");
        int watchBefore = run.standing("watch");
        run.driveTalkBeat(finch, run.stage("end_finch"), 600);

        run.assertCommonPostConditions("end_finch");
        assertEquals(1, run.carried(finch, ItemKinds.LEDGER_LEAF),
                "the rooftops keep the receipt");
        assertEquals(30, run.standing("skyrunners") - skyrunnersBefore, "skyrunners +30 exact");
        assertEquals(-10, run.standing("watch") - watchBefore, "watch -10 exact");
        assertEquals(1, run.edgeCount(Math.min(finch, run.playerId),
                Math.max(finch, run.playerId), RelationshipKind.FRIEND),
                "the FRIEND tie is the ONLY mechanism that can flip Finch's greeting");
        System.out.println("[DocksQuestWalkthroughTest] run C ticks=" + run.tick
                + " searchAttempts=" + run.log.searchAttemptsOf(0));
    }
}
