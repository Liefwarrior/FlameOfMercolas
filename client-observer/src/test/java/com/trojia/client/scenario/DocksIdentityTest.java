package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.inspect.JobDisplay;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.RelationshipEdge;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The S1 identity-table DoD gates (bake-side, no ticking): twin-bake byte-identity of the
 * whole table, full-roster coverage (every citizen named + bio'd, keeper-owned beasts kennel-
 * named, ferals/mice/cats deliberately nameless), full-name uniqueness among the named,
 * household-surname sharing off the HOUSEHOLD components, and the Forty Notables' bindings —
 * every authored identity pinned to (name, group), with site/job spot checks proving the
 * spawn-site selectors grabbed the right souls (the Skyrunner's cover above all).
 */
class DocksIdentityTest {

    /** The Forty Notables pin table: notable id -> (authored name, actor-type key). */
    private static final Map<String, String[]> FORTY = fortyPinTable();

    private static Map<String, String[]> fortyPinTable() {
        Map<String, String[]> pins = new LinkedHashMap<>();
        pins.put("crell", new String[] {"Ottavan Crell", "shopkeeper"});
        pins.put("vess", new String[] {"Sergeant Vess", "militia_watch"});
        pins.put("sethra", new String[] {"Mother Sethra", "shopkeeper"});
        pins.put("maell", new String[] {"Father Maell", "priest_of_the_flame"});
        pins.put("onna", new String[] {"Onna", "disciple_of_the_flame"});
        pins.put("cobb", new String[] {"Old Cobb", "animal_keeper"});
        pins.put("jek", new String[] {"Tarry Jek", "wastrel"});
        pins.put("dagny", new String[] {"Dagny", "shopkeeper"});
        pins.put("harl", new String[] {"Harl", "shopkeeper"});
        pins.put("brann", new String[] {"Brann", "shopkeeper"});
        pins.put("venn", new String[] {"Master Venn", "shopkeeper"});
        pins.put("fenner", new String[] {"Fenner", "shopkeeper"});
        pins.put("merle", new String[] {"Merle", "shopkeeper"});
        pins.put("squall", new String[] {"Squall", "shopkeeper"});
        pins.put("wake", new String[] {"Captain Ivo Wake", "serf"});
        pins.put("bregga", new String[] {"Captain Wull Bregga", "serf"});
        pins.put("vane", new String[] {"Captain Sorrel Vane", "serf"});
        pins.put("quayward", new String[] {"Ceffa Quayward", "shopkeeper"});
        pins.put("netter", new String[] {"Widow Annis Netter", "shopkeeper"});
        pins.put("saltgate", new String[] {"Goodman Tarl Saltgate", "shopkeeper"});
        pins.put("gilt", new String[] {"Master Ondrey Gilt", "shopkeeper"});
        pins.put("finch", new String[] {"Finch", "wastrel"});
        pins.put("redda", new String[] {"Redda", "shopkeeper"});
        pins.put("hemp", new String[] {"Foreman Cathal Hemp", "shopkeeper"});
        pins.put("ulwer", new String[] {"Pitch-Master Ulwer", "shopkeeper"});
        pins.put("salla", new String[] {"Salla", "shopkeeper"});
        pins.put("grieve", new String[] {"Bondsman Grieve", "shopkeeper"});
        pins.put("vetch", new String[] {"Keeper Vetch", "shopkeeper"});
        pins.put("withy", new String[] {"Grandmother Withy", "shopkeeper"});
        pins.put("stave", new String[] {"Cooper Aldous Stave", "shopkeeper"});
        pins.put("luff", new String[] {"Sailmaker Luff", "shopkeeper"});
        pins.put("crumb", new String[] {"Baker Hobb Crumb", "shopkeeper"});
        pins.put("neddry", new String[] {"Slops Neddry", "shopkeeper"});
        pins.put("dray", new String[] {"Foreman Dray", "shopkeeper"});
        pins.put("cull", new String[] {"Watchman Cull", "animal_keeper"});
        pins.put("herdis", new String[] {"Herdis the Goatwife", "animal_keeper"});
        pins.put("weyland", new String[] {"Carter Weyland", "animal_keeper"});
        pins.put("tolley", new String[] {"Mad Tolley", "wastrel"});
        pins.put("mag", new String[] {"Gullet Mag", "wastrel"});
        pins.put("brakk", new String[] {"Sergeant Brakk", "militia_watch"});
        return pins;
    }

    private static final Set<String> HUMAN_GROUPS = Set.of("serf", "wastrel", "shopkeeper",
            "militia_watch", "priest_of_the_flame", "disciple_of_the_flame", "animal_keeper");

    private static DocksPopulation population;
    private static IdentityRegistry identity;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        identity = population.identity();
    }

    @Test
    void twinBakesForgeByteIdenticalIdentityTables() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation twin = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        assertEquals(identity.canonicalTable(), twin.identity().canonicalTable(),
                "twin bakes must forge byte-identical identity tables");
    }

    @Test
    void everySoulIsCoveredAndTheNamelessAreDeliberate() {
        ActorRegistry registry = population.registry();
        assertEquals(registry.size(), identity.size(), "one identity row per actor");
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            String type = actor.typeId().key();
            IdentityRegistry.Identity who = identity.get(i);
            assertEquals(i, who.actorId());
            assertFalse(who.fullName().isBlank(), "actor#" + i + " has a blank display name");
            if (HUMAN_GROUPS.contains(type)) {
                assertTrue(who.named(), "citizen actor#" + i + " [" + type + "] must be named");
                assertFalse(who.bio().isBlank(), "citizen actor#" + i + " must carry a bio");
                if (who.notableId() == null) {
                    assertFalse(who.givenName().isBlank(),
                            "forged citizen actor#" + i + " must have a given name");
                    assertFalse(who.surname().isBlank(),
                            "forged citizen actor#" + i + " must have a surname");
                    assertEquals(who.givenName() + " " + who.surname(), who.fullName());
                }
            } else if (type.equals("animal") && actor.ownerId() != Actor.NONE) {
                assertTrue(who.named(), "keeper-owned beast actor#" + i + " gets a kennel name");
                assertFalse(who.bio().isBlank(), "owned beast actor#" + i + " must carry a bio");
            } else {
                assertFalse(who.named(),
                        "ferals/mice/cats stay nameless by design (actor#" + i + ")");
            }
        }
    }

    @Test
    void namedFullNamesAreUniqueAcrossTheWholeRoster() {
        Set<String> seen = new HashSet<>();
        int named = 0;
        for (int i = 0; i < identity.size(); i++) {
            IdentityRegistry.Identity who = identity.get(i);
            if (!who.named()) {
                continue;
            }
            named++;
            assertTrue(seen.add(who.fullName()),
                    "duplicate full name \"" + who.fullName() + "\" at actor#" + i);
        }
        assertEquals(seen.size(), named);
        assertTrue(named >= 638, "638 citizens + 11 kenneled beasts expected named, saw " + named);
    }

    @Test
    void householdMembersShareTheirComponentSurname() {
        ActorRegistry registry = population.registry();
        int[] root = independentHouseholdComponents(registry.size(),
                population.relationships());
        Map<Integer, String> surnameByRoot = new HashMap<>();
        Map<Integer, Integer> membersByRoot = new HashMap<>();
        int multiMemberComponents = 0;
        for (int i = 0; i < registry.size(); i++) {
            IdentityRegistry.Identity who = identity.get(i);
            if (!who.named() || who.surname().isBlank()) {
                continue; // notables without an authored family name, and the nameless
            }
            if (membersByRoot.merge(root[i], 1, Integer::sum) == 2) {
                multiMemberComponents++;
            }
            String previous = surnameByRoot.putIfAbsent(root[i], who.surname());
            if (previous != null) {
                assertEquals(previous, who.surname(),
                        "actor#" + i + " does not share its household's surname");
            }
        }
        assertTrue(multiMemberComponents >= 80,
                "the bake forms many multi-member households; saw " + multiMemberComponents);
        // The three authored compound family names landed on their whole households.
        for (String family : new String[] {"Quayward", "Netter", "Saltgate"}) {
            int carriers = 0;
            for (int i = 0; i < identity.size(); i++) {
                if (identity.get(i).surname().equals(family)) {
                    carriers++;
                }
            }
            assertTrue(carriers >= 2,
                    "family \"" + family + "\" must cover its mansion household, saw " + carriers);
        }
    }

    @Test
    void theFortyNotablesBindByNameGroupAndSite() {
        ActorRegistry registry = population.registry();
        Map<String, Integer> actorByNotable = new LinkedHashMap<>();
        for (int i = 0; i < identity.size(); i++) {
            String notableId = identity.get(i).notableId();
            if (notableId != null) {
                assertEquals(null, actorByNotable.put(notableId, i),
                        "notable \"" + notableId + "\" bound twice");
            }
        }
        assertEquals(40, actorByNotable.size(), "the Forty Notables are exactly forty");
        for (Map.Entry<String, String[]> pin : FORTY.entrySet()) {
            Integer actorId = actorByNotable.get(pin.getKey());
            assertTrue(actorId != null, "notable \"" + pin.getKey() + "\" is unbound");
            IdentityRegistry.Identity who = identity.get(actorId);
            assertEquals(pin.getValue()[0], who.fullName(),
                    "notable \"" + pin.getKey() + "\" name");
            assertEquals(pin.getValue()[1], registry.get(actorId).typeId().key(),
                    "notable \"" + pin.getKey() + "\" group");
            assertFalse(who.bio().isBlank());
        }

        Map<String, Integer> sites = DocksPopulation.notableSpawnSites();
        // Site spot checks: the selectors grabbed the right souls at the right anchors.
        assertHomeAt(actorByNotable.get("crell"), sites.get("K01_WEIGHHOUSE"), 2);
        assertHomeAt(actorByNotable.get("vess"), sites.get("WATCHPOST_K21"), 2);
        assertHomeAt(actorByNotable.get("maell"), sites.get("MISSION_BUNKS"), 3);
        assertHomeAt(actorByNotable.get("onna"), sites.get("MISSION_BUNKS"), 3);
        assertHomeAt(actorByNotable.get("cobb"), sites.get("K25_KENNEL_ROW"), 2);
        assertHomeAt(actorByNotable.get("dagny"), sites.get("K14_WRACKHOUSE"), 2);
        assertHomeAt(actorByNotable.get("harl"), sites.get("K06_HARLS_YARD"), 2);
        assertHomeAt(actorByNotable.get("quayward"), sites.get("C1_MANSION"), 2);
        assertHomeAt(actorByNotable.get("netter"), sites.get("C2_MANSION"), 2);
        assertHomeAt(actorByNotable.get("saltgate"), sites.get("C3_MANSION"), 2);
        assertHomeAt(actorByNotable.get("gilt"), sites.get("K36_BANK_COUNTER"), 2);
        assertHomeAt(actorByNotable.get("brakk"), sites.get("K34_GUARDHOUSE"), 2);
        assertHomeAt(actorByNotable.get("jek"), sites.get("STRAND_JEK"), 1);
        assertEquals((int) sites.get("SHIP_K30_KESTREL"),
                registry.get(actorByNotable.get("wake")).anchorCell(), "the Kestrel's captain");
        assertEquals((int) sites.get("SHIP_K31_BREGGAS_PROMISE"),
                registry.get(actorByNotable.get("bregga")).anchorCell(),
                "Bregga's Promise's captain");
        assertEquals((int) sites.get("SHIP_K32_DEEPKEEL"),
                registry.get(actorByNotable.get("vane")).anchorCell(), "the Deep Keel's captain");

        // The Skyrunner's cover: the authored name is the COVER; the true job is the secret.
        int finch = actorByNotable.get("finch");
        Job finchJob = population.jobs().get(registry.get(finch).jobOrdinal());
        assertEquals("villain.skyrunner", JobDisplay.trueJobId(finchJob),
                "\"Finch\" must bind the K35-anchored Skyrunner");
        assertEquals("wastrel.streetlife", JobDisplay.presentedJobId(finchJob),
                "\"Finch\" presents as ordinary streetlife — the cover convention");
        assertEquals((int) sites.get("LAIR_SKYRUNNER"), registry.get(finch).anchorCell());
        assertFalse(identity.get(finch).bio().contains("Skyrunner"),
                "the authored cover bio must not name the Skyrunner");
    }

    private static void assertHomeAt(Integer actorId, Integer siteCell, int radius) {
        assertTrue(actorId != null && siteCell != null);
        Actor actor = population.registry().get(actorId);
        int home = population.homes().get(actor.homeId()).homeCell();
        assertEquals(PackedPos.z(siteCell), PackedPos.z(home),
                "actor#" + actorId + " home band");
        int distance = Math.max(Math.abs(PackedPos.x(home) - PackedPos.x(siteCell)),
                Math.abs(PackedPos.y(home) - PackedPos.y(siteCell)));
        assertTrue(distance <= radius,
                "actor#" + actorId + " home sits " + distance + " from its notable site");
    }

    /** An independent union-find over HOUSEHOLD edges (not NameForge's own code path). */
    private static int[] independentHouseholdComponents(int n,
            RelationshipRegistry relationships) {
        int[] parent = new int[n];
        for (int i = 0; i < n; i++) {
            parent[i] = i;
        }
        for (int e = 0; e < relationships.size(); e++) {
            RelationshipEdge edge = relationships.get(e);
            if (edge.kind() != RelationshipKind.HOUSEHOLD) {
                continue;
            }
            int a = edge.fromId();
            int b = edge.toId();
            while (parent[a] != a) {
                a = parent[a];
            }
            while (parent[b] != b) {
                b = parent[b];
            }
            if (a != b) {
                parent[Math.max(a, b)] = Math.min(a, b);
            }
        }
        int[] root = new int[n];
        for (int i = 0; i < n; i++) {
            int r = i;
            while (parent[r] != r) {
                r = parent[r];
            }
            root[i] = r;
        }
        return root;
    }
}
