package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.inspect.CheckLineFormatter;
import com.trojia.client.inspect.NameplateText;
import com.trojia.client.inspect.TalkText;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.RelationshipEdge;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.bark.BarkRawsLoader;
import com.trojia.sim.bark.BarkTableRegistry;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-2 TALK surface DoD over the FORGED docks bake (GL-free): a notable greets the
 * played actor by name, portrait identity and World-authored text; the disposition tag is
 * the sim selector's own bucket; a disguised listener is greeted as the face it wears; a
 * mood override (custody) preempts the greeting; the exchange is deterministic; and the
 * nameplate standing-attitude (the S2 tint) speaks the same vocabulary. Also prints the
 * sprint report's full-exchange mock. Headless, no GL.
 */
class DocksTalkTest {

    private static final long TICK_MORNING = 3_000L;

    private static long worldSeed;
    private static DocksPopulation population;
    private static IdentityRegistry identity;
    private static BarkTableRegistry barks;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        worldSeed = loaded.worldSeed();
        population = DocksPopulation.build(worldSeed, loaded.world());
        identity = population.identity();
        barks = BarkRawsLoader.load(RepoPaths.locate("content", "raws"));
        assertTrue(barks.size() > 0, "the World team's bark tables are committed content");
    }

    private static TalkText.Exchange greet(int speakerId, int listenerId, long tick) {
        return TalkText.greet(worldSeed, tick, speakerId, listenerId, population.registry(),
                population.jobs(), identity, population.system().factionStandings(),
                population.relationships(), barks);
    }

    private static int actorNamed(String fullName) {
        for (int i = 0; i < identity.size(); i++) {
            if (identity.get(i).fullName().equals(fullName)) {
                return i;
            }
        }
        throw new AssertionError("no soul named " + fullName + " in the bake");
    }

    private static Actor firstOfType(String typeKey) {
        for (int i = 0; i < population.registry().size(); i++) {
            if (population.registry().get(i).typeId().key().equals(typeKey)) {
                return population.registry().get(i);
            }
        }
        throw new AssertionError("no actor of type " + typeKey);
    }

    @Test
    void aNotableGreetsByNameJobAndAuthoredText() {
        int vess = actorNamed("Sergeant Vess");
        Actor listener = firstOfType("serf");
        TalkText.Exchange exchange = greet(vess, listener.id(), TICK_MORNING);

        assertTrue(exchange.nameLine().contains("Sergeant Vess"), exchange.nameLine());
        assertTrue(exchange.jobLine().startsWith("watch"),
                "a sergeant speaks under a watch job id: " + exchange.jobLine());
        assertEquals("[neutral]", exchange.contextLine(),
                "an untouched serf is greeted neutral");
        assertFalse(exchange.barkLine().isBlank(), "coverage: the greeting is authored");
        assertNotEquals(TalkText.SAYS_NOTHING, exchange.barkLine(),
                "the fallback chain must never go silent over the committed tables");
    }

    @Test
    void theExchangeIsDeterministic() {
        int vess = actorNamed("Sergeant Vess");
        Actor listener = firstOfType("serf");
        assertEquals(greet(vess, listener.id(), TICK_MORNING),
                greet(vess, listener.id(), TICK_MORNING),
                "same (seed, tick, speaker, listener) -> byte-identical exchange");
    }

    @Test
    void aColdStandingChangesTheGreeting() {
        // The reactivity payoff: harden a listener's Watch standing and the same sergeant
        // greets the same face cold — bucket AND text lane both move.
        FactionStandings standings = population.system().factionStandings();
        int watchFaction = standings.factions().rawId("watch");
        int vess = actorNamed("Sergeant Vess");
        Actor listener = firstOfType("wastrel");
        try {
            standings.adjust(listener.id(), watchFaction, -30);
            TalkText.Exchange exchange = greet(vess, listener.id(), TICK_MORNING);
            assertEquals("[cold]", exchange.contextLine(), exchange.toString());
            assertNotEquals(TalkText.SAYS_NOTHING, exchange.barkLine());
        } finally {
            standings.adjust(listener.id(), watchFaction, 30);
        }
    }

    @Test
    void aDisguisedListenerIsGreetedAsTheFaceItWears() {
        // The Persona rule made audible: B wears C's face; C's record is cold with the
        // Watch; the sergeant greets B cold — and greets B's own face neutral again the
        // moment the disguise drops.
        FactionStandings standings = population.system().factionStandings();
        int watchFaction = standings.factions().rawId("watch");
        int vess = actorNamed("Sergeant Vess");
        Actor listener = firstOfType("serf");
        int coldFace = actorNamed("Gullet Mag");
        try {
            standings.adjust(coldFace, watchFaction, -30);
            listener.setActAs(coldFace);
            assertEquals("[cold]", greet(vess, listener.id(), TICK_MORNING).contextLine(),
                    "the sergeant reads the PRESENTED face's record");
            listener.setActAs(listener.id());
            assertEquals("[neutral]", greet(vess, listener.id(), TICK_MORNING).contextLine(),
                    "dropping the disguise restores the listener's own record");
        } finally {
            listener.setActAs(listener.id());
            standings.adjust(coldFace, watchFaction, 30);
        }
    }

    @Test
    void aMoodOverridePreemptsTheGreeting() {
        int vess = actorNamed("Sergeant Vess");
        Actor speaker = firstOfType("wastrel");
        try {
            speaker.setStatus(StatusBit.HELD, true);
            TalkText.Exchange exchange = greet(speaker.id(), vess, TICK_MORNING);
            assertEquals("[held]", exchange.contextLine(),
                    "custody speaks before any greeting: " + exchange);
            assertNotEquals(TalkText.SAYS_NOTHING, exchange.barkLine(),
                    "mood.held is authored");
        } finally {
            speaker.setStatus(StatusBit.HELD, false);
        }
    }

    @Test
    void nameplateAttitudeSpeaksTheSelectorsVocabulary() {
        // The S2 standing tint's GL-free core: the attitude token parsed off the sim's own
        // selected key — kin for a household pair, friend for an authored FRIEND edge,
        // null (no tint) with no viewer and under a mood override.
        FactionStandings standings = population.system().factionStandings();
        var registry = population.registry();
        var jobs = population.jobs();
        var relationships = population.relationships();

        int[] householdPair = firstPairOfKind(RelationshipKind.HOUSEHOLD);
        assertEquals("kin", NameplateText.attitudeToward(householdPair[0], householdPair[1],
                registry, jobs, standings, relationships));

        int[] friendPair = firstPureFriendPair();
        assertEquals("friend", NameplateText.attitudeToward(friendPair[0], friendPair[1],
                registry, jobs, standings, relationships));

        assertNull(NameplateText.attitudeToward(householdPair[0], Actor.NONE, registry, jobs,
                standings, relationships), "no viewer -> no tint");

        Actor held = firstOfType("wastrel");
        try {
            held.setStatus(StatusBit.HELD, true);
            assertNull(NameplateText.attitudeToward(held.id(), householdPair[1], registry,
                    jobs, standings, relationships), "a mood override is not a standing");
        } finally {
            held.setStatus(StatusBit.HELD, false);
        }
    }

    @Test
    void printTheReportsFullExchangeMock() {
        // The sprint report's text mock: a full talk exchange with a notable, plus the
        // pickpocket check feedback line as the panel would stack them.
        int vess = actorNamed("Sergeant Vess");
        Actor listener = firstOfType("serf");
        TalkText.Exchange exchange = greet(vess, listener.id(), TICK_MORNING);
        String check = CheckLineFormatter.pickpocketLine(population.system().skillTracks(),
                listener.id(), vess, "Sergeant Vess", false);
        System.out.println("==== TALK PANEL (played: "
                + identity.get(listener.id()).fullName() + ") ====");
        for (String line : exchange.panelLines()) {
            System.out.println(line);
        }
        System.out.println(check);
        System.out.println("(T greet again  ·  G pickpocket  ·  ESC close)");
        assertTrue(check.startsWith("[Skyrunning "), check);
    }

    /** The first edge of {@code kind}, as {@code {fromId, toId}}. */
    private static int[] firstPairOfKind(RelationshipKind kind) {
        var relationships = population.relationships();
        for (int i = 0; i < relationships.size(); i++) {
            RelationshipEdge edge = relationships.get(i);
            if (edge.kind() == kind) {
                return new int[] {edge.fromId(), edge.toId()};
            }
        }
        throw new AssertionError("no edge of kind " + kind + " in the bake");
    }

    /** The first FRIEND pair with no HOUSEHOLD edge between them (household wins ties). */
    private static int[] firstPureFriendPair() {
        var relationships = population.relationships();
        for (int i = 0; i < relationships.size(); i++) {
            RelationshipEdge edge = relationships.get(i);
            if (edge.kind() != RelationshipKind.FRIEND) {
                continue;
            }
            boolean household = false;
            for (int j = 0; j < relationships.size(); j++) {
                RelationshipEdge other = relationships.get(j);
                if (other.kind() == RelationshipKind.HOUSEHOLD
                        && ((other.fromId() == edge.fromId() && other.toId() == edge.toId())
                        || (other.fromId() == edge.toId() && other.toId() == edge.fromId()))) {
                    household = true;
                    break;
                }
            }
            if (!household) {
                return new int[] {edge.fromId(), edge.toId()};
            }
        }
        throw new AssertionError("no pure FRIEND pair in the bake");
    }
}
