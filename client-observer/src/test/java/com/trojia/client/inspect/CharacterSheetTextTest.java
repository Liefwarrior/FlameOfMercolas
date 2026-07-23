package com.trojia.client.inspect;

import com.trojia.client.hud.icons.HudToken;
import com.trojia.client.hud.icons.IconKey;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobId;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link CharacterSheetText} sheet-content contract (Sprint 1 "Click a person, meet a
 * person" — supersedes {@code InspectorTextTest}, carrying its ACTORS-SPEC.md §7.2
 * legibility pins forward onto the sheet layout) against the real compound population —
 * the un-forged fixture, so this class pins the DEGRADED modes end to end: "#id"-style
 * name fallback, "-> #id" ties fallback, "(unschooled)" skills placeholder. The named
 * surfaces are pinned by {@code DocksCharacterSheetTest} against the forged docks bake.
 * Headless: reads committed raws, no GL.
 */
class CharacterSheetTextTest {

    private static CompoundBlockPopulation build() {
        return CompoundBlockPopulation.build(1234L);
    }

    private static String join(List<String> lines) {
        return String.join("\n", lines);
    }

    private static String describe(CompoundBlockPopulation p, int selectedId) {
        return join(CharacterSheetText.describe(selectedId, p.registry(), p.homes(),
                p.relationships(), p.jobs(), p.items(), IdentityRegistry.EMPTY,
                p.system().skillTracks()));
    }

    @Test
    void nothingSelectedShowsAHint() {
        CompoundBlockPopulation p = build();
        assertTrue(describe(p, Actor.NONE).toLowerCase().contains("click"));
    }

    @Test
    void ordinaryActorSheetCarriesHeaderSectionsHomeAndTies() {
        CompoundBlockPopulation p = build();
        // Actor #2 is a mansion Serf (home #0), household-tied to ids 0/1/3.
        String text = describe(p, 2);

        // Un-forged fixture: the header degrades to the "Type #id" style, never fails.
        assertTrue(text.startsWith("Serf #2"), text);
        assertTrue(text.contains("-- IDENTITY --"), text);
        assertTrue(text.contains("-- NEEDS --"), text);
        assertTrue(text.contains("-- SKILLS --"), text);
        assertTrue(text.contains("-- TIES --"), text);
        assertTrue(text.contains("id:     #2  serf"), text);
        assertTrue(text.contains("serf.laborer"), text);
        assertTrue(text.contains("home:") && text.contains("#0"), text);
        assertTrue(text.contains("HOUSEHOLD -> #"),
                "un-forged ties keep the debug fallback: " + text);
        assertTrue(text.contains("carries:"), text);
        assertFalse(text.contains("(secret)"), "an ordinary serf has no cover: " + text);
    }

    @Test
    void needLabelsCoverAllFiveNeedsInOrder() {
        assertEquals(List.of("HUNGER", "REST", "COIN", "SAFETY", "DUTY"),
                List.of(CharacterSheetText.NEED_LABELS));
    }

    @Test
    void villainSheetShowsPresentedCoverAndSecretMarker() {
        CompoundBlockPopulation p = build();
        // A rooftop-slum Wastrel secretly runs villain.skyrunner under a wastrel.streetlife
        // cover; locate it by its TRUE job so the test survives population re-balancing (ids
        // are not a stable contract).
        int skyrunnerId = firstWithTrueJob(p, Job.Villain.Skyrunner.ID);
        String text = describe(p, skyrunnerId);

        assertTrue(text.contains("villain.skyrunner"), text);
        assertTrue(text.contains("presents: wastrel.streetlife"), text);
        assertTrue(text.contains("(secret)"), () -> "villain must be flagged secret: " + text);
        // Its tell rides the inventory (a lockpick), on the compact carries line.
        assertTrue(text.contains("kind 5 x1"), () -> "expected the lockpick item: " + text);
    }

    @Test
    void ordinaryActorShowsSelfInThePresentsLine() {
        CompoundBlockPopulation p = build();
        assertTrue(describe(p, 2).contains("presents: (self)"));
    }

    @Test
    void disguisedActorHeaderFollowsThePresentedActorButIdentityStaysTrue() {
        // PLAY-MODE-SPEC.md §5.3: setActAs is the Persona seam. The header (name line) must
        // become the PRESENTED actor's identity while the IDENTITY section keeps the truth.
        CompoundBlockPopulation p = build();
        Actor serf = p.registry().get(2);
        Actor watch = firstOfType(p, "militia_watch");
        assertFalse(serf.identity().isDisguised(), "sanity: not disguised yet");

        serf.setActAs(watch.id());
        String text = describe(p, 2);
        assertTrue(text.startsWith("Militia Watch #" + watch.id()),
                "header must be the presented soul: " + text);
        assertTrue(text.contains("presents: militia_watch"), text);
        assertTrue(text.contains("id:     #2  serf"), "true identity must be unaffected: " + text);

        serf.setActAs(serf.id()); // drop the disguise
        text = describe(p, 2);
        assertTrue(text.startsWith("Serf #2"), text);
        assertTrue(text.contains("presents: (self)"), text);
    }

    @Test
    void unwiredSkillRegistryRendersTheUnschooledPlaceholder() {
        CompoundBlockPopulation p = build();
        CharacterSheetText.Section skills =
                CharacterSheetText.skillsSection(2, p.system().skillTracks());
        assertEquals("SKILLS", skills.title());
        assertEquals(List.of("(unschooled)"), skills.lines());
        assertFalse(SkillTrackRegistry.UNWIRED.isWired(), "compound ships unwired");
    }

    @Test
    void selectionHintTokensCarryTheMouseAndCIcons() {
        List<HudToken> tokens = CharacterSheetText.selectionHintTokens();
        assertTrue(tokens.contains(HudToken.icon(IconKey.MOUSE_LEFT_CLICK)),
                () -> "expected a mouse-left-click icon: " + tokens);
        assertTrue(tokens.contains(HudToken.icon(IconKey.C)),
                () -> "expected a C key icon: " + tokens);
    }

    @Test
    void followBadgeTokensCarryTheCIcon() {
        List<HudToken> tokens = CharacterSheetText.followBadgeTokens();
        assertTrue(tokens.contains(HudToken.icon(IconKey.C)),
                () -> "expected a C key icon: " + tokens);
    }

    private static Actor firstOfType(CompoundBlockPopulation p, String typeKey) {
        for (int i = 0; i < p.registry().size(); i++) {
            if (p.registry().get(i).typeId().key().equals(typeKey)) {
                return p.registry().get(i);
            }
        }
        throw new AssertionError("no actor of type " + typeKey + " in the population");
    }

    /** The lowest-id actor whose TRUE (not presented) job is {@code jobId}. */
    private static int firstWithTrueJob(CompoundBlockPopulation p, JobId jobId) {
        for (int i = 0; i < p.registry().size(); i++) {
            Actor a = p.registry().get(i);
            if (a.jobOrdinal() >= 0 && p.jobs().get(a.jobOrdinal()).id().equals(jobId)) {
                return a.id();
            }
        }
        throw new AssertionError("no actor with true job " + jobId + " in the population");
    }
}
