package com.trojia.sim.actor;

import com.trojia.sim.actor.faction.FactionRawsLoader;
import com.trojia.sim.actor.faction.FactionRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.bark.BarkTableRegistry;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-2 bark selection core: a pure, deterministic function from readable sim state
 * to {@code (tableKey, rowDraw)} — the table-driven matrix over (faction standing x time x
 * status) the plan's DoD names, plus the disguise flip (the listener's PRESENTED id drives
 * the attitude) and the sparse-authoring fallback resolution.
 */
final class BarkSelectorTest {

    private static final long SEED = 42L;
    private static final FactionRegistry FACTIONS = FactionRawsLoader.load(committedRawsRoot());

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

    private static final class Fixture {
        final ActorRegistry registry = new ActorRegistry();
        final NoOpActorContext ctx = new NoOpActorContext(registry);
        final FactionStandings standings = new FactionStandings(FACTIONS);
        final RelationshipRegistry relationships = new RelationshipRegistry();
        final Actor guard;
        final Actor listener;

        Fixture() {
            guard = registry.spawn(MilitiaWatch.TYPE,
                    ActorTestFixtures.stats(MilitiaWatch.TYPE), PackedPos.pack(10, 10, 11));
            guard.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
            listener = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                    PackedPos.pack(11, 10, 11));
            listener.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Serf.Laborer.ID));
        }

        JobRegistry jobs() {
            return ctx.jobs();
        }

        Job guardJob() {
            return jobs().get(guard.jobOrdinal());
        }

        BarkSelector.BarkChoice greet(long tick) {
            return BarkSelector.select(SEED, tick, guard, guardJob(),
                    listener.identity().presentedId(), standings, relationships);
        }
    }

    // ---- the ask verb's topic seam (Sprint 4) ------------------------------------------

    @Test
    void selectAskServesPersonalAndGossipKeysAndRotatesAcrossTicks() {
        Fixture f = new Fixture();
        List<String> histories = List.of("netter-fenner-debt", "cobb-cull-feud");
        boolean sawPersonal = false;
        boolean sawGossipA = false;
        boolean sawGossipB = false;
        for (long tick = 1; tick <= 200; tick++) {
            BarkSelector.BarkChoice choice = BarkSelector.selectAsk(SEED, tick, f.guard,
                    "withy", histories);
            switch (choice.tableKey()) {
                case "personal.withy" -> sawPersonal = true;
                case "gossip.netter-fenner-debt" -> sawGossipA = true;
                case "gossip.cobb-cull-feud" -> sawGossipB = true;
                default -> throw new AssertionError("unexpected key " + choice.tableKey());
            }
        }
        assertTrue(sawPersonal && sawGossipA && sawGossipB,
                "repeated asks rotate through every speakable topic");
        // Pure function: the same inputs always serve the same topic and row.
        BarkSelector.BarkChoice first = BarkSelector.selectAsk(SEED, 77L, f.guard, "withy",
                histories);
        BarkSelector.BarkChoice second = BarkSelector.selectAsk(SEED, 77L, f.guard, "withy",
                histories);
        assertEquals(first.tableKey(), second.tableKey());
        assertEquals(first.rowDraw(), second.rowDraw());
    }

    @Test
    void selectAskOnAForgedSoulServesOnlyGossipAndSilenceWhenUnstoried() {
        Fixture f = new Fixture();
        for (long tick = 1; tick <= 50; tick++) {
            BarkSelector.BarkChoice choice = BarkSelector.selectAsk(SEED, tick, f.listener,
                    null, List.of("netter-fenner-debt"));
            assertEquals("gossip.netter-fenner-debt", choice.tableKey(),
                    "a forged soul has no personal table");
        }
        assertNull(BarkSelector.selectAsk(SEED, 9L, f.listener, "", List.of()),
                "no topic at all: the consumer stays silent");
    }

    @Test
    void selectAskKeepsTheMoodOverride() {
        Fixture f = new Fixture();
        f.guard.setStatus(StatusBit.HELD, true);
        BarkSelector.BarkChoice choice = BarkSelector.selectAsk(SEED, 5L, f.guard, "withy",
                List.of("netter-fenner-debt"));
        assertEquals("mood.held", choice.tableKey(), "a held soul does not gossip");
        f.guard.setStatus(StatusBit.HELD, false);
    }

    @Test
    void theStandingByTimeMatrixSelectsTheDocumentedKeys() {
        Fixture f = new Fixture();
        int listener = f.listener.identity().presentedId();
        int watch = FACTIONS.rawId("watch");
        long morning = 3_000;
        long day = 8_000;
        long evening = 14_000;
        long night = 20_000;

        // Neutral standing across the four time buckets.
        assertEquals("greet.watch.neutral.morning", f.greet(morning).tableKey());
        assertEquals("greet.watch.neutral.day", f.greet(day).tableKey());
        assertEquals("greet.watch.neutral.evening", f.greet(evening).tableKey());
        assertEquals("greet.watch.neutral.night", f.greet(night).tableKey());

        // The standing ladder at one time of day.
        f.standings.adjust(listener, watch, 30);
        assertEquals("greet.watch.warm.day", f.greet(day).tableKey(),
                "+30 with the Watch: a friend of the garrison");
        f.standings.adjust(listener, watch, -60); // net -30
        assertEquals("greet.watch.cold.day", f.greet(day).tableKey(),
                "-30: a known offender is greeted cold");
        f.standings.adjust(listener, watch, -40); // net -70
        assertEquals("greet.watch.hostile.day", f.greet(day).tableKey(),
                "-70: the guild you robbed does not greet, it warns");
    }

    @Test
    void moodBitsOverrideTheGreetingInPriorityOrder() {
        Fixture f = new Fixture();
        long day = 8_000;
        f.guard.setStatus(StatusBit.MOVE_ALONG, true);
        assertEquals("mood.harried", f.greet(day).tableKey());
        f.guard.setStatus(StatusBit.PANICKED, true);
        assertEquals("mood.panicked", f.greet(day).tableKey());
        f.guard.setStatus(StatusBit.HOUSE_ARREST, true);
        assertEquals("mood.confined", f.greet(day).tableKey());
        f.guard.setStatus(StatusBit.HELD, true);
        assertEquals("mood.held", f.greet(day).tableKey());
        f.guard.setStatus(StatusBit.DOWNED, true);
        assertEquals("mood.downed", f.greet(day).tableKey());
        f.guard.setStatus(StatusBit.EXECUTED, true);
        assertEquals("mood.dead", f.greet(day).tableKey());
    }

    @Test
    void kinAndFriendTiesOutrankStandingAndReadThePresentedFace() {
        Fixture f = new Fixture();
        long day = 8_000;
        f.relationships.addSymmetric(f.guard.id(), f.listener.id(), RelationshipKind.FRIEND);
        assertEquals("greet.watch.friend.day", f.greet(day).tableKey());

        // The disguise flip: a THIRD actor presenting as the guard's friend is greeted as
        // the friend; dropping the mask restores the stranger's greeting.
        Actor stranger = f.registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                PackedPos.pack(12, 10, 11));
        stranger.setJobOrdinal((short) f.jobs().ordinalOf(Job.Wastrel.Streetlife.ID));
        BarkSelector.BarkChoice asSelf = BarkSelector.select(SEED, day, f.guard, f.guardJob(),
                stranger.identity().presentedId(), f.standings, f.relationships);
        assertEquals("greet.watch.neutral.day", asSelf.tableKey());
        stranger.setActAs(f.listener.id());
        BarkSelector.BarkChoice disguised = BarkSelector.select(SEED, day, f.guard, f.guardJob(),
                stranger.identity().presentedId(), f.standings, f.relationships);
        assertEquals("greet.watch.friend.day", disguised.tableKey(),
                "the greeting follows the FACE presented (the Persona rule)");
    }

    /** Sprint 3: HOUSEHOLD &gt; GRUDGE &gt; FRIEND, and only the HOLDER's greeting turns. */
    @Test
    void aDirectedGrudgeOutranksFriendshipButNeverKinAndOnlyBitesOneWay() {
        Fixture f = new Fixture();
        long day = 8_000;
        // Friend first, then the quest-minted grudge (holder = the guard): hostile wins.
        f.relationships.addSymmetric(f.guard.id(), f.listener.id(), RelationshipKind.FRIEND);
        assertEquals("greet.watch.friend.day", f.greet(day).tableKey());
        f.relationships.addDirected(f.guard.id(), f.listener.id(), RelationshipKind.GRUDGE);
        assertEquals("greet.watch.hostile.day", f.greet(day).tableKey(),
                "a grudge outweighs old friendship (Sprint 3 quest endings)");

        // Directedness: the OBJECT of the grudge greets normally (its friend edge still reads).
        Job listenerJob = f.jobs().get(f.listener.jobOrdinal());
        BarkSelector.BarkChoice reverse = BarkSelector.select(SEED, day, f.listener, listenerJob,
                f.guard.identity().presentedId(), f.standings, f.relationships);
        assertEquals("greet.serf.friend.day", reverse.tableKey(),
                "only speaker→listener grudges bite — the object holds no grudge");

        // Kin forgive: a HOUSEHOLD tie outranks even the grudge.
        f.relationships.addSymmetric(f.guard.id(), f.listener.id(), RelationshipKind.HOUSEHOLD);
        assertEquals("greet.watch.kin.day", f.greet(day).tableKey(),
                "HOUSEHOLD > GRUDGE (kin forgive)");
    }

    @Test
    void aDisguisedVillainSpeaksAsItsCoverFamily() {
        Fixture f = new Fixture();
        Actor cutpurse = f.registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                PackedPos.pack(13, 10, 11));
        cutpurse.setJobOrdinal((short) f.jobs().ordinalOf(Job.Villain.Cutpurse.ID));
        Job trueJob = f.jobs().get(cutpurse.jobOrdinal());
        BarkSelector.BarkChoice choice = BarkSelector.select(SEED, 8_000, cutpurse, trueJob,
                f.listener.identity().presentedId(), f.standings, f.relationships);
        assertEquals("greet.wastrel.neutral.day", choice.tableKey(),
                "the villain's voice is its COVER's voice — the secret does not leak");
    }

    @Test
    void selectionIsDeterministicAndTheDrawNeverTouchesSimCounters() {
        Fixture f = new Fixture();
        BarkSelector.BarkChoice first = f.greet(8_000);
        BarkSelector.BarkChoice again = f.greet(8_000);
        assertEquals(first, again, "same (seed, tick, speaker, state) -> same choice");
        // The draw is addressed (seed, tick, ACTOR_BARK, speaker, PRESENTATION_DRAW_INDEX)
        // and never touches a ctx counter (BarkSelector never sees the context), so asking
        // for a bark cannot perturb a running sim — reconstructable attribution:
        assertEquals(NamedDraws.draw(ActorRngStream.ACTOR_BARK, SEED, 8_000, f.guard.id(),
                BarkSelector.PRESENTATION_DRAW_INDEX), first.rowDraw());
        BarkSelector.BarkChoice nextTick = f.greet(8_001);
        assertEquals(first.tableKey(), nextTick.tableKey(),
                "the key is state-driven, not draw-driven");
    }

    @Test
    void resolveWalksTheSparseAuthoringFallbackChain() {
        BarkTableRegistry tables = BarkTableRegistry.of(List.of(
                new BarkTableRegistry.BarkTable("greet.watch",
                        List.of("The Watch sees you.")),
                new BarkTableRegistry.BarkTable("greet.watch.hostile.day",
                        List.of("You. Stay where you are.", "Hands where I can see them."))));
        BarkSelector.BarkChoice specific =
                new BarkSelector.BarkChoice("greet.watch.hostile.day", 7);
        assertEquals("Hands where I can see them.", specific.resolve(tables),
                "an authored exact key resolves its own rows (draw 7 % 2 = 1)");
        BarkSelector.BarkChoice fallback =
                new BarkSelector.BarkChoice("greet.watch.cold.night", 7);
        assertEquals("The Watch sees you.", fallback.resolve(tables),
                "an unauthored specific key walks up to greet.watch");
        BarkSelector.BarkChoice miss = new BarkSelector.BarkChoice("greet.serf.warm.day", 7);
        assertNull(miss.resolve(tables), "nothing authored anywhere on the chain -> silent");
    }
}
