package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.inspect.CharacterSheetText;
import com.trojia.client.inspect.EventLog;
import com.trojia.client.inspect.NameplateText;
import com.trojia.client.inspect.SkillUpTracker;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.SkillTrackRegistry;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-1 "meet a PERSON" DoD gates on the FORGED docks bake: the character sheet
 * headed by a real name + bio, ties rendered as people, the disguised header following the
 * presented soul, nameplates always showing the PRESENTED identity, the skills section
 * reading the Sim team's live table, and the skill-up feed narrating named people. The
 * un-forged degraded modes are pinned by {@code CharacterSheetTextTest} — this class is
 * where the sprint's emotional payoff is verified against real names. Headless, no GL.
 */
class DocksCharacterSheetTest {

    private static DocksPopulation population;
    private static IdentityRegistry identity;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        identity = population.identity();
    }

    private static String describe(int selectedId) {
        return String.join("\n", CharacterSheetText.describe(selectedId,
                population.registry(), population.homes(), population.relationships(),
                population.jobs(), population.items(), identity,
                population.system().skillTracks()));
    }

    private static int actorNamed(String fullName) {
        for (int i = 0; i < identity.size(); i++) {
            if (identity.get(i).fullName().equals(fullName)) {
                return i;
            }
        }
        throw new AssertionError("no soul named " + fullName + " in the bake");
    }

    @Test
    void crellsSheetIsHeadedByTheAuthoredNameAndBio() {
        int crell = actorNamed("Ottavan Crell");
        String nameLine = CharacterSheetText.nameLine(crell, population.registry(), identity);
        assertTrue(nameLine.contains("Ottavan Crell"), nameLine);

        String bio = CharacterSheetText.bioLine(crell, population.registry(), identity);
        assertFalse(bio.isBlank(), "a notable carries an authored bio");
        assertEquals(identity.get(crell).bio(), bio);

        String sheet = describe(crell);
        assertTrue(sheet.startsWith(nameLine), sheet);
        assertTrue(sheet.contains("-- IDENTITY --") && sheet.contains("-- NEEDS --")
                && sheet.contains("-- SKILLS --") && sheet.contains("-- TIES --"), sheet);
        // The demo mock for the sprint report (and a human eyeball surface in test stdout).
        System.out.println("==== CHARACTER SHEET: Ottavan Crell ====");
        System.out.println(sheet);
    }

    @Test
    void tiesRenderAsPeopleNotIds() {
        // Find any citizen with a tie to a NAMED other soul; its TIES section must speak of
        // the person ("Household -- Ceffa Quayward (...)"), never the "-> #id" debug style.
        for (int i = 0; i < identity.size(); i++) {
            if (!identity.get(i).named()) {
                continue;
            }
            List<String> ties = CharacterSheetText.tiesSection(i, population.registry(),
                    population.relationships(), population.jobs(), identity).lines();
            if (ties.equals(List.of("(no ties)"))) {
                continue;
            }
            for (String line : ties) {
                assertFalse(line.contains("-> #"),
                        "forged fixture must render ties as people: " + line);
                assertTrue(line.contains(" -- "), line);
            }
            // One is enough to also pin the exact shape against the identity table.
            List<RelView> views = viewsOf(i);
            assertEquals(views.size(), ties.size());
            assertTrue(ties.get(0).endsWith(")") || !ties.get(0).contains("("),
                    "job suffix is parenthesized when present: " + ties.get(0));
            return;
        }
        throw new AssertionError("no named soul with ties found");
    }

    private record RelView(int otherId) {
    }

    private static List<RelView> viewsOf(int actorId) {
        return population.relationships().relationshipsOf(actorId).stream()
                .map(v -> new RelView(v.otherId())).toList();
    }

    @Test
    void disguisedSheetHeaderAndNameplateBecomeThePresentedSoul() {
        int crell = actorNamed("Ottavan Crell");
        Actor serf = firstOfType("serf");
        try {
            serf.setActAs(crell);
            // Sheet header: the ward's-eye identity (name + bio follow the disguise) ...
            String nameLine = CharacterSheetText.nameLine(serf.id(), population.registry(),
                    identity);
            assertTrue(nameLine.contains("Ottavan Crell"), nameLine);
            assertEquals(identity.get(crell).bio(),
                    CharacterSheetText.bioLine(serf.id(), population.registry(), identity));
            // ... while the IDENTITY section keeps the omniscient truth.
            String sheet = describe(serf.id());
            assertTrue(sheet.contains("id:     #" + serf.id() + "  serf"), sheet);
            assertTrue(sheet.contains("presents: shopkeeper"), sheet);
            // Nameplate: byte-identical to the impersonated soul's own label.
            assertEquals(
                    NameplateText.labelFor(crell, population.registry(), population.jobs(),
                            identity),
                    NameplateText.labelFor(serf.id(), population.registry(), population.jobs(),
                            identity));
        } finally {
            serf.setActAs(serf.id());
        }
    }

    @Test
    void theSkyrunnersNameplateReadsAsItsCoverEverywhere() {
        // "Finch" is the Skyrunner's authored cover (bound by the K35 lair anchor): the
        // nameplate must show the cover name and the PRESENTED wastrel job — the true
        // villain.skyrunner job must never leak onto a social surface.
        int finch = actorNamed("Finch");
        String label = NameplateText.labelFor(finch, population.registry(), population.jobs(),
                identity);
        assertTrue(label.contains("Finch"), label);
        assertFalse(label.contains("villain"), "the cover must hold on hover: " + label);
        // The omniscient sheet IDENTITY section still tells the observer the truth.
        String sheet = describe(finch);
        assertTrue(sheet.contains("villain.skyrunner"), sheet);
        assertTrue(sheet.contains("(secret)"), sheet);
    }

    @Test
    void skillsSectionReadsTheLiveSimTable() {
        SkillTrackRegistry tracks = population.system().skillTracks();
        assertTrue(tracks.isWired(), "the docks bake wires the committed skills raws");
        Actor wastrel = firstOfType("wastrel");
        assertEquals(List.of("(unschooled)"),
                CharacterSheetText.skillsSection(wastrel.id(), tracks).lines(),
                "no ticks have run: every soul is unschooled");

        // One tier-0 FAVORED level's worth of streetwise (75 cp = 1500 grains = threshold 0).
        tracks.award(wastrel.id(), tracks.streetwiseRaw(), 75, 7L, 1L);
        assertEquals(List.of("Streetwise 1"),
                CharacterSheetText.skillsSection(wastrel.id(), tracks).lines(),
                "the sheet reads levels live off the Sim team's table");
    }

    @Test
    void populationLevelUpsNarrateNamedPeopleIntoTheFeed() {
        SkillTrackRegistry tracks = population.system().skillTracks();
        int crell = actorNamed("Ottavan Crell");
        EventLog feed = new EventLog(30);
        ToastQueue toasts = new ToastQueue();
        SkillUpTracker tracker = new SkillUpTracker(tracks, population.registry(), identity,
                feed, toasts, () -> Actor.NONE);

        tracks.award(crell, tracks.gritRaw(), 100, 3L, 20L); // TRAINED: 2000 grains = level 1
        tracker.afterTick(20L);

        assertEquals(1, feed.size());
        assertEquals("Ottavan Crell is now Grit 1", feed.recentNewestFirst(1).get(0).text());
        assertTrue(toasts.visible().isEmpty());
    }

    private static Actor firstOfType(String typeKey) {
        for (int i = 0; i < population.registry().size(); i++) {
            if (population.registry().get(i).typeId().key().equals(typeKey)) {
                return population.registry().get(i);
            }
        }
        throw new AssertionError("no actor of type " + typeKey);
    }
}
