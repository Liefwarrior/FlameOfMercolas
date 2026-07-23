package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
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
 * Sprint-4 WORLD DoD over the real baked docks: THE WIDOW'S PAPER (quest #2) is playable
 * end-to-end through EXISTING verbs only — the proof that the S3 quest machine is a
 * general, data-driven machine (zero engine edits; the quest raws + one append-only item
 * kind + bake bindings are the whole implementation). Three headless runs drive one
 * ordinary citizen through every stage to EACH ending, and each run enters {@code holding}
 * by a DIFFERENT acquisition route:
 *
 * <ul>
 *   <li><b>Run A</b> — crack the strongbox (the cooldown-gated {@code cracksmanship}
 *       search) and deliver the paper to Widow Annis Netter; carries the input-replay
 *       twin (same bake, same recorded script, byte-identical evolution).</li>
 *   <li><b>Run B</b> — pay Fenner's thirty-Royal price (talk-with-royals → the bought
 *       stage's give_item price), then sell the paper BACK to him for twenty-five.</li>
 *   <li><b>Run C</b> — wear a Guild-vouched face ({@code standing_at_least merchants 20}
 *       — Bregga's authored leaning), let Fenner yield, then KEEP the paper until the
 *       {@code after_ticks} choice makes itself.</li>
 * </ul>
 *
 * The widows-paper entry rides index 1; the vanished-clerk keeps entry 0 and must stay
 * COMPLETELY idle through every run here (the two casts share no talk party).
 */
class DocksWidowsPaperWalkthroughTest {

    /** The widows-paper entry index (vanished-clerk keeps 0; quests.json appends). */
    private static final int E = 1;

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
        final int spawnCell;
        final int coinLiveAtBake;

        long tick; // completed steps
        final List<Action> script = new ArrayList<>();
        final List<long[]> checkpoints = new ArrayList<>(); // {tick, actorsHash}
        final HashSet<ReasonCode> observed = new HashSet<>();

        Run() {
            assertEquals(692, registry.size(), "the roster stays 692 — nobody new spawns");
            assertEquals(2, quests.questCount(), "both authored quests are baked");
            assertEquals("widows-paper", quests.questId(E), "quest #2 rides entry 1");
            notables = NameForge.bindNotableActors(registry, population.homes(),
                    NotableRaws.load(RepoPaths.locate("content", "raws")
                            .resolve("names").resolve("notables.json")),
                    DocksPopulation.notableSpawnSites());
            playerId = pickOrdinaryCitizen();
            watchId = pickWatchBody();
            spawnCell = registry.get(playerId).cell();
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
            for (int s = 0; s < quests.stageCount(E); s++) {
                if (quests.stageKey(E, s).equals(key)) {
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
            while (log.stageOf(E) != untilStage) {
                assertTrue(tick < deadline, "talk beat stalled before stage " + untilStage
                        + " (still at " + log.stageOf(E) + " after " + budgetTicks + " ticks)");
                teleportBeside(targetId);
                talk(targetId);
                step();
            }
        }

        /**
         * A talk-stand cell beside {@code targetId} that is at least chebyshev 2 from the
         * strongbox. Needed for the Fenner beats: his 7x7 shop is so cramped that "beside
         * Fenner" is usually ALSO beside the box — and at the {@code paper} stage the
         * search trigger is evaluated FIRST, so standing there would crack the box on the
         * very tick meant to strike the deal (a real first-match-wins property of the
         * engine, discovered by this test's first draft).
         */
        int besideAwayFromBox(int targetId) {
            int c = registry.get(targetId).cell();
            int box = DocksPopulation.fennerStrongboxCell();
            int[][] offsets = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
            for (int[] o : offsets) {
                int cell = PackedPos.pack(PackedPos.x(c) + o[0], PackedPos.y(c) + o[1],
                        PackedPos.z(c));
                int cheb = Math.max(Math.abs(PackedPos.x(cell) - PackedPos.x(box)),
                        Math.abs(PackedPos.y(cell) - PackedPos.y(box)));
                if (cheb >= 2) {
                    return cell;
                }
            }
            throw new IllegalStateException("no talk-stand cell clear of the strongbox");
        }

        /** {@link #driveTalkBeat} standing clear of the strongbox (the Fenner beats). */
        void driveTalkBeatClearOfBox(int targetId, int untilStage, int budgetTicks) {
            long deadline = tick + budgetTicks;
            while (log.stageOf(E) != untilStage) {
                assertTrue(tick < deadline, "talk beat stalled before stage " + untilStage
                        + " (still at " + log.stageOf(E) + " after " + budgetTicks + " ticks)");
                teleportTo(besideAwayFromBox(targetId));
                talk(targetId);
                step();
            }
        }

        /** A back-room floor cell one west of the strongbox (inside the K15 shell). */
        int besideStrongbox() {
            int box = DocksPopulation.fennerStrongboxCell();
            return PackedPos.pack(PackedPos.x(box) - 1, PackedPos.y(box), PackedPos.z(box));
        }

        /** Steps (standing beside the strongbox) until the search advances to {@code toStage}. */
        void driveSearch(int toStage, int budgetTicks) {
            teleportTo(besideStrongbox());
            long deadline = tick + budgetTicks;
            while (log.stageOf(E) != toStage) {
                assertTrue(tick < deadline, "the strongbox never gave (attempts: "
                        + log.searchAttemptsOf(E) + ", stage " + log.stageOf(E) + ")");
                teleportTo(besideStrongbox()); // stay put: shoves/policing must not drift us
                step();
            }
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

        /**
         * The shared closing assertions. {@code stampedStages} is path-dependent: the
         * steal path visits 3 non-terminal stages, the bought/yielded paths 4.
         */
        void assertCommonPostConditions(String endingKey, int stampedStages,
                int paperStillCarried) {
            assertEquals(stage(endingKey), log.stageOf(E));
            assertTrue(quests.terminal(E, log.stageOf(E)));
            assertEquals(playerId, log.ownerOf(E), "the quest stayed with the body that bound it");
            long previous = 0;
            int stamped = 0;
            for (int s = 0; s < quests.stageCount(E); s++) {
                long at = log.completedTickOf(E, s);
                if (at >= 0) {
                    assertTrue(at >= previous, "completion ticks stamp in visit order");
                    previous = at;
                    stamped++;
                }
            }
            assertEquals(stampedStages, stamped,
                    "exactly the path's non-terminal stages completed");
            assertTrue(observed.contains(ReasonCode.TALKED), "TALKED observed on the trail");
            assertTrue(observed.contains(ReasonCode.QUEST_ADVANCED),
                    "QUEST_ADVANCED observed on the trail");
            // COIN conservation: the price, the pay and every dip were MOVES, never mints.
            assertEquals(coinLiveAtBake, population.items().liveOfKind(ItemKinds.COIN),
                    "the closed COIN supply is exact after the quest");
            // The paper's physical journey: out of the strongbox, exactly one in the world.
            assertEquals(0, population.items().countOnCellOfKind(
                    DocksPopulation.fennerStrongboxCell(), ItemKinds.DEBT_PAPER),
                    "the strongbox is empty");
            assertEquals(1, population.items().liveOfKind(ItemKinds.DEBT_PAPER),
                    "the one debt paper still exists");
            assertEquals(paperStillCarried, carried(playerId, ItemKinds.DEBT_PAPER));
            // The vanished-clerk entry slept through the whole run (disjoint casts).
            assertEquals(0, log.stageOf(0), "the clerk's quest never woke");
            assertEquals(Actor.NONE, log.ownerOf(0), "the clerk's quest never bound");
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
            assertEquals(0, log.stageOf(E), "talking to a non-party advances nothing");
            assertEquals(Actor.NONE, log.ownerOf(E));
        }

        /**
         * Run B's purse: the walkthrough must prove the FULL thirty-Royal price, so the
         * pocket is raised to 30+ first — by dipping Gilt's prime pocket with the stock
         * G verb (the S3 run-B pattern, watch face on; a successful dip moves the whole
         * loose pocket). Deterministic either way: the bake fixes the branch.
         */
        void ensureRoyals() {
            if (carried(playerId, ItemKinds.COIN) >= 30) {
                return;
            }
            int gilt = notables.get("gilt");
            actAs(watchId);
            long deadline = tick + 4_000;
            while (carried(playerId, ItemKinds.COIN) < 30) {
                assertTrue(tick < deadline, "no full purse ever landed (thefts="
                        + population.system().theftCount() + ")");
                teleportBeside(gilt);
                pickpocket(gilt);
                step();
            }
            actAs(playerId);
        }
    }

    private static long actorsHash(com.trojia.sim.actor.ActorsSystem system) {
        WorldHasher hasher = new WorldHasher();
        system.hashInto(hasher.sectionSink(system.id()));
        return hasher.sectionHash(system.id());
    }

    // ================================================================== Run A

    @Test
    void runA_crackTheStrongbox_endNetter_andTheInputReplayTwinMatches() {
        Run run = new Run();
        int netter = run.notables.get("netter");
        int fenner = run.notables.get("fenner");
        run.proveTalkTrail();

        // S0 trouble: Herdis at the pens knows whose house stands on whose patience.
        run.driveTalkBeat(run.notables.get("herdis"), run.stage("paper"), 400);
        assertEquals(run.playerId, run.log.ownerOf(E), "first_talker bound the body");

        // S1 paper -> holding, the CRACK route: wear the pawnbroker's own face (the
        // policed Trader zone respects it) and pry the box on the cracksmanship cooldown.
        run.actAs(fenner);
        long attemptsBefore = run.log.searchAttemptsOf(E);
        run.driveSearch(run.stage("holding"), 8_000);
        assertTrue(run.log.searchAttemptsOf(E) > attemptsBefore,
                "the crack was a real cooldown-gated check");
        assertEquals(1, run.carried(run.playerId, ItemKinds.DEBT_PAPER),
                "the paper came out of the box into the player's carry");

        // S2 -> end_netter: drop the mask (the quay must credit GABRI's face) and bring
        // the widow her signing.
        run.actAs(run.playerId);
        int dockhandsBefore = run.standing("dockhands");
        int merchantsBefore = run.standing("merchants");
        run.driveTalkBeat(netter, run.stage("end_netter"), 600);

        run.assertCommonPostConditions("end_netter", 3, 0);
        assertEquals(1, run.carried(netter, ItemKinds.DEBT_PAPER),
                "the widow holds her own paper");
        assertEquals(25, run.standing("dockhands") - dockhandsBefore, "dockhands +25 exact");
        assertEquals(-15, run.standing("merchants") - merchantsBefore, "merchants -15 exact");
        assertEquals(1, run.edgeCount(Math.min(netter, run.playerId),
                Math.max(netter, run.playerId), RelationshipKind.FRIEND),
                "the widow's friendship is mutual");
        assertEquals(1, run.edgeCount(fenner, run.playerId, RelationshipKind.GRUDGE),
                "Fenner's grudge is directed AT the player");
        assertEquals(0, run.edgeCount(run.playerId, fenner, RelationshipKind.GRUDGE),
                "the player holds no grudge back");

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
        assertEquals(run.log.stageOf(E), twin.system().questLog().stageOf(E));
        assertEquals(run.log.ownerOf(E), twin.system().questLog().ownerOf(E));
        System.out.println("[DocksWidowsPaperWalkthroughTest] run A ticks=" + run.tick
                + " searchAttempts=" + run.log.searchAttemptsOf(E)
                + " scriptActions=" + run.script.size());
    }

    // ================================================================== Run B

    @Test
    void runB_payFennersPrice_thenSellItBack_endFenner() {
        Run run = new Run();
        int fenner = run.notables.get("fenner");
        run.proveTalkTrail();

        // S0 trouble via Grandmother Withy on the Arcade.
        run.driveTalkBeat(run.notables.get("withy"), run.stage("paper"), 400);
        run.ensureRoyals();

        // S1 paper -> bought: strike the deal at the window. ONE latch tick advances and
        // the bought stage's entry effects count the price out of the player's pocket.
        int playerCoinBefore = run.carried(run.playerId, ItemKinds.COIN);
        int fennerPocketBefore = run.carried(fenner, ItemKinds.COIN);
        assertTrue(playerCoinBefore >= 30,
                "the walkthrough proves the FULL thirty-Royal price, not a short pocket");
        run.driveTalkBeatClearOfBox(fenner, run.stage("bought"), 400);
        int expectedPrice = Math.min(30, playerCoinBefore);
        assertEquals(expectedPrice, playerCoinBefore - run.carried(run.playerId, ItemKinds.COIN),
                "the price: up to thirty Royals, seize-what-exists per coin");
        assertEquals(fennerPocketBefore + expectedPrice, run.carried(fenner, ItemKinds.COIN),
                "every Royal of the price MOVED to Fenner, none minted");

        // S2 bought -> holding: Fenner unlocked the box; the resist-0 pickup is quick.
        run.actAs(fenner); // the Trader face keeps the policed back room civil
        run.driveSearch(run.stage("holding"), 200);
        assertEquals(1, run.carried(run.playerId, ItemKinds.DEBT_PAPER));

        // S3 -> end_fenner: drop the mask and sell the pawnbroker back his patience.
        run.actAs(run.playerId);
        int pocketAtEnding = run.carried(fenner, ItemKinds.COIN);
        int playerCoinAtEnding = run.carried(run.playerId, ItemKinds.COIN);
        int merchantsBefore = run.standing("merchants");
        int dockhandsBefore = run.standing("dockhands");
        run.driveTalkBeatClearOfBox(fenner, run.stage("end_fenner"), 600);

        run.assertCommonPostConditions("end_fenner", 4, 0);
        assertEquals(1, run.carried(fenner, ItemKinds.DEBT_PAPER),
                "the paper went back behind the cage");
        int expectedPay = Math.min(25, pocketAtEnding);
        assertEquals(expectedPay,
                run.carried(run.playerId, ItemKinds.COIN) - playerCoinAtEnding,
                "Fenner's buy-back: seize-what-exists off his live pocket");
        assertEquals(15, run.standing("merchants") - merchantsBefore, "merchants +15 exact");
        assertEquals(-20, run.standing("dockhands") - dockhandsBefore, "dockhands -20 exact");
        assertEquals(1, run.edgeCount(Math.min(fenner, run.playerId),
                Math.max(fenner, run.playerId), RelationshipKind.FRIEND),
                "the pawnbroker now greets the player warmly");
        assertEquals(1, run.edgeCount(run.notables.get("netter"), run.playerId,
                RelationshipKind.GRUDGE), "the widow's grudge is directed AT the player");
        System.out.println("[DocksWidowsPaperWalkthroughTest] run B ticks=" + run.tick
                + " price=" + expectedPrice + " payback=" + expectedPay);
    }

    // ================================================================== Run C

    @Test
    void runC_wearTheGuildsRegard_thenKeepThePaper_endKept() {
        Run run = new Run();
        int fenner = run.notables.get("fenner");
        int bregga = run.notables.get("bregga");
        run.proveTalkTrail();

        // S0 trouble from the widow herself.
        run.driveTalkBeat(run.notables.get("netter"), run.stage("paper"), 400);

        // S1 paper -> yielded: borrow Bregga's Guild-vouched face. The standing trigger
        // reads the PRESENTED face's merchants standing — position-independent, so it
        // fires from the open street on the next engine pass.
        run.actAs(bregga);
        long deadline = run.tick + 20;
        while (run.log.stageOf(E) != run.stage("yielded")) {
            assertTrue(run.tick < deadline, "the Guild face never made Fenner fold");
            run.step();
        }

        // S2 yielded -> holding: walk in (a Trader face keeps it civil) and take it.
        run.actAs(fenner);
        run.driveSearch(run.stage("holding"), 200);
        assertEquals(1, run.carried(run.playerId, ItemKinds.DEBT_PAPER));

        // S3 -> end_kept: drop every mask, walk home, and simply KEEP it. The
        // after_ticks trigger is the authored choice-by-sitting-on-it.
        run.actAs(run.playerId);
        run.teleportTo(run.spawnCell);
        int skyrunnersBefore = run.standing("skyrunners");
        int merchantsBefore = run.standing("merchants");
        long enteredHolding = run.tick;
        deadline = run.tick + 1_500 + 200;
        while (run.log.stageOf(E) != run.stage("end_kept")) {
            assertTrue(run.tick < deadline, "the kept ending never fired");
            run.step();
        }
        assertTrue(run.tick - enteredHolding >= 1_400,
                "the kept ending waited out the authored fortnight, not a short-circuit");

        run.assertCommonPostConditions("end_kept", 4, 1);
        assertEquals(15, run.standing("skyrunners") - skyrunnersBefore, "skyrunners +15 exact");
        assertEquals(-10, run.standing("merchants") - merchantsBefore, "merchants -10 exact");
        assertEquals(1, run.edgeCount(fenner, run.playerId, RelationshipKind.GRUDGE),
                "Fenner's grudge is directed AT the player");
        assertEquals(0, run.edgeCount(Math.min(fenner, run.playerId),
                Math.max(fenner, run.playerId), RelationshipKind.FRIEND),
                "keeping the paper made no friends");
        assertEquals(0, run.carried(run.notables.get("netter"), ItemKinds.DEBT_PAPER),
                "the widow never got her paper");
        assertEquals(0, run.carried(fenner, ItemKinds.DEBT_PAPER),
                "neither did the pawnbroker");
        System.out.println("[DocksWidowsPaperWalkthroughTest] run C ticks=" + run.tick
                + " searchAttempts=" + run.log.searchAttemptsOf(E));
    }
}
