package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.inspect.CharacterSheetText;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.progression.SkillRegistry;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-5 authored-mastery DoD gates ("the masters of the ward"): every authored
 * {@code notables.json} skill grant lands on the bound actor's live track at bake, the
 * levels are visible on the character sheet (masters ARE masters on day one), ordinary
 * souls stay level 0 and earn it, and twin bakes seed byte-identical sheets.
 */
class DocksNotableMasteryTest {

    private static DocksPopulation population;
    private static Map<String, Integer> notableActor;
    private static List<NotableRaws.Notable> notables;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        IdentityRegistry identity = population.identity();
        notableActor = new HashMap<>();
        for (int i = 0; i < identity.size(); i++) {
            if (identity.get(i).notableId() != null) {
                notableActor.put(identity.get(i).notableId(), i);
            }
        }
        notables = NotableRaws.load(
                RepoPaths.locate("content", "raws").resolve("names").resolve("notables.json"));
    }

    /** Every authored grant is live on the bound actor's track — no silent zeroes. */
    @Test
    void everyAuthoredGrantLandsOnTheBoundActorsTrack() {
        SkillTrackRegistry tracks = population.system().skillTracks();
        SkillRegistry skills = tracks.skills();
        int granted = 0;
        for (NotableRaws.Notable notable : notables) {
            for (NotableRaws.SkillGrant grant : notable.skills()) {
                int actorId = notableActor.get(notable.id());
                assertEquals(grant.level(),
                        tracks.level(actorId, skills.id(grant.skillKey()).raw()),
                        notable.id() + " " + grant.skillKey());
                granted++;
            }
        }
        assertTrue(granted >= 35, "the ward should have a substantial authored-mastery "
                + "roster, saw " + granted + " grants");
    }

    /** The ruling's named masters, pinned so a raws edit cannot silently demote them. */
    @Test
    void theNamedMastersAreMastersOnTheirSheets() {
        SkillTrackRegistry tracks = population.system().skillTracks();
        assertSheetShows("withy", "Kit-Keeping 50");
        assertSheetShows("vane", "Seacraft 50");
        assertSheetShows("cobb", "Fieldcraft 40");
        assertSheetShows("vess", "Streetwise 35");
        assertSheetShows("harl", "Kit-Keeping 45");
        // The secret skyrunner's sheet finally matches his night work — on the TRUE body.
        assertEquals(50, tracks.level(notableActor.get("finch"),
                tracks.skyrunningRaw()), "finch skyrunning");
    }

    /** Ordinary souls (and the deliberately unseeded notables) start at 0 and earn it. */
    @Test
    void ordinarySoulsStayUnschooled() {
        SkillTrackRegistry tracks = population.system().skillTracks();
        for (String id : List.of("squall", "crumb", "neddry", "tolley")) {
            assertEquals(List.of("(unschooled)"),
                    CharacterSheetText.skillsSection(notableActor.get(id), tracks).lines(),
                    id + " is authored unseeded");
        }
        // A non-notable soul: the roster is 692; notables bind ~41 — spot-check that some
        // ordinary actor holds no seeded level in any skill.
        int ordinary = firstNonNotableActor();
        assertEquals(List.of("(unschooled)"),
                CharacterSheetText.skillsSection(ordinary, tracks).lines(),
                "actor#" + ordinary + " (non-notable) must start unschooled");
    }

    /** Twin bakes seed identical levels — the bake stays a pure function of the raws. */
    @Test
    void twinBakesSeedIdenticalMastery() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation twin = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        SkillTrackRegistry a = population.system().skillTracks();
        SkillTrackRegistry b = twin.system().skillTracks();
        SkillRegistry skills = a.skills();
        for (NotableRaws.Notable notable : notables) {
            int actorId = notableActor.get(notable.id());
            for (int raw = 0; raw < skills.size(); raw++) {
                assertEquals(a.level(actorId, raw), b.level(actorId, raw),
                        notable.id() + " skill raw " + raw);
            }
        }
    }

    private static void assertSheetShows(String notableId, String line) {
        List<String> lines = CharacterSheetText.skillsSection(notableActor.get(notableId),
                population.system().skillTracks()).lines();
        assertTrue(lines.contains(line),
                notableId + " sheet must show \"" + line + "\", saw " + lines);
    }

    private static int firstNonNotableActor() {
        IdentityRegistry identity = population.identity();
        for (int i = 0; i < identity.size(); i++) {
            if (identity.get(i).notableId() == null) {
                return i;
            }
        }
        throw new AssertionError("every actor is a notable?");
    }
}
