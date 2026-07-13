package com.trojia.client.inspect;

import com.trojia.client.hud.icons.HudToken;
import com.trojia.client.hud.icons.IconKey;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobId;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link InspectorText} panel-content contract (ACTORS-SPEC.md §7.2) against the real
 * compound population — the legibility acceptance surface must reconstruct WHO/WHY/WHERE
 * from an actor's live state alone. Headless: reads committed raws, no GL.
 */
class InspectorTextTest {

    private static CompoundBlockPopulation build() {
        return CompoundBlockPopulation.build(1234L);
    }

    private static String join(List<String> lines) {
        return String.join("\n", lines);
    }

    @Test
    void nothingSelectedShowsAHint() {
        CompoundBlockPopulation p = build();
        List<String> lines = InspectorText.describe(Actor.NONE, p.registry(), p.homes(),
                p.relationships(), p.jobs(), p.items());
        assertTrue(join(lines).toLowerCase().contains("click"),
                () -> "expected a select hint, got: " + lines);
    }

    @Test
    void ordinaryActorPanelCarriesIdentityNeedsHomeAndRelationships() {
        CompoundBlockPopulation p = build();
        // Actor #2 is a mansion Serf (home #0), household-tied to ids 0/1/3.
        String text = join(InspectorText.describe(2, p.registry(), p.homes(),
                p.relationships(), p.jobs(), p.items()));

        assertTrue(text.contains("ACTOR #2"), text);
        assertTrue(text.contains("serf"), text);
        assertTrue(text.contains("serf.laborer"), text);
        assertTrue(text.contains("HUNGER") && text.contains("REST") && text.contains("COIN")
                && text.contains("SAFETY") && text.contains("DUTY"), text);
        assertTrue(text.contains("home:") && text.contains("#0"), text);
        assertTrue(text.contains("HOUSEHOLD -> #"), text);
        assertTrue(text.contains("inventory ("), text);
        assertFalse(text.contains("(secret)"), "an ordinary serf has no cover: " + text);
    }

    @Test
    void villainPanelShowsPresentedCoverAndSecretMarker() {
        CompoundBlockPopulation p = build();
        // A rooftop-slum Wastrel secretly runs villain.skyrunner under a wastrel.streetlife
        // cover; locate it by its TRUE job so the test survives population re-balancing (ids
        // are not a stable contract).
        int skyrunnerId = firstWithTrueJob(p, Job.Villain.Skyrunner.ID);
        String text = join(InspectorText.describe(skyrunnerId, p.registry(), p.homes(),
                p.relationships(), p.jobs(), p.items()));

        assertTrue(text.contains("villain.skyrunner"), text);
        assertTrue(text.contains("presents: wastrel.streetlife"), text);
        assertTrue(text.contains("(secret)"), () -> "villain must be flagged secret: " + text);
        // Its tell rides the inventory (a lockpick), resolved via ItemsLite kind + quantity.
        assertTrue(text.contains("kind 5 x1"), () -> "expected the lockpick item: " + text);
    }

    @Test
    void selectionHintTokensCarryTheMouseAndCIcons() {
        List<HudToken> tokens = InspectorText.selectionHintTokens();
        assertTrue(tokens.contains(HudToken.icon(IconKey.MOUSE_LEFT_CLICK)),
                () -> "expected a mouse-left-click icon: " + tokens);
        assertTrue(tokens.contains(HudToken.icon(IconKey.C)),
                () -> "expected a C key icon: " + tokens);
    }

    @Test
    void followBadgeTokensCarryTheCIcon() {
        List<HudToken> tokens = InspectorText.followBadgeTokens();
        assertTrue(tokens.contains(HudToken.icon(IconKey.C)),
                () -> "expected a C key icon: " + tokens);
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
