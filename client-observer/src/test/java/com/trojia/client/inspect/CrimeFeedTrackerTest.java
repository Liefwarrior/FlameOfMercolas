package com.trojia.client.inspect;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.CrimeLog;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link CrimeFeedTracker} narration contract (Sprint 2): crime rows become named feed
 * lines — the thief named by the face the ward SAW (the disguised case) — and the played
 * actor's own attempts toast with the {@link CheckLineFormatter} line either way, landing
 * it on an open talk panel. Rows are fed to a private {@link CrimeLog} directly (the
 * tracker only ever READS the sim's log), against the forged docks bake for real names.
 * Headless, no GL.
 */
class CrimeFeedTrackerTest {

    private static DocksPopulation population;
    private static IdentityRegistry identity;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        identity = population.identity();
    }

    private static int actorNamed(String fullName) {
        for (int i = 0; i < identity.size(); i++) {
            if (identity.get(i).fullName().equals(fullName)) {
                return i;
            }
        }
        throw new AssertionError("no soul named " + fullName);
    }

    @Test
    void aWitnessedRowNamesBothPartiesAndAnUnwitnessedRowStaysQuietProse() {
        int jek = actorNamed("Tarry Jek");
        int onna = actorNamed("Onna");
        CrimeLog log = new CrimeLog(8);
        EventLog feed = new EventLog(10);
        ToastQueue toasts = new ToastQueue();
        CrimeFeedTracker tracker = new CrimeFeedTracker(log,
                population.system().skillTracks(), population.registry(), identity, feed,
                toasts, () -> Actor.NONE, null);

        log.record(7L, 0, jek, jek, onna, false);
        log.record(8L, 0, jek, jek, onna, true);
        tracker.afterTick(8L);

        List<EventLog.Entry> lines = feed.recentNewestFirst(10);
        assertEquals(2, lines.size());
        assertEquals("Tarry Jek picked Onna's pocket", lines.get(1).text());
        assertEquals(7L, lines.get(1).tick());
        assertEquals("Tarry Jek was caught with a hand in Onna's pocket", lines.get(0).text());
        assertTrue(toasts.visible().isEmpty(), "ambient thieves never toast");
    }

    @Test
    void aDisguisedThiefIsNamedByTheFaceTheWardSaw() {
        // The row carries (trueId, presentedId): the feed must print the PRESENTED name —
        // a lift under a stolen face stains the cover, exactly like its standings.
        int thief = actorNamed("Tarry Jek");
        int cover = actorNamed("Ottavan Crell");
        int victim = actorNamed("Onna");
        CrimeLog log = new CrimeLog(8);
        EventLog feed = new EventLog(10);
        CrimeFeedTracker tracker = new CrimeFeedTracker(log,
                population.system().skillTracks(), population.registry(), identity, feed,
                new ToastQueue(), () -> Actor.NONE, null);

        log.record(9L, 0, thief, cover, victim, true);
        tracker.afterTick(9L);

        assertEquals("Ottavan Crell was caught with a hand in Onna's pocket",
                feed.recentNewestFirst(1).get(0).text());
    }

    @Test
    void thePlayedActorsAttemptToastsWithTheCheckLineAndLandsOnTheOpenPanel() {
        int played = actorNamed("Tarry Jek");
        int victim = actorNamed("Onna");
        CrimeLog log = new CrimeLog(8);
        ToastQueue toasts = new ToastQueue();
        TalkState talk = new TalkState();
        talk.open(new TalkText.Exchange(victim, victim, 1L, "Onna", "clergy.lay", "[neutral]",
                "The pot is on."));
        CrimeFeedTracker tracker = new CrimeFeedTracker(log,
                population.system().skillTracks(), population.registry(), identity,
                new EventLog(10), toasts, () -> played, talk);

        log.record(11L, 0, played, played, victim, true);
        tracker.afterTick(11L);

        List<ToastQueue.Toast> visible = toasts.visible();
        assertEquals(1, visible.size());
        assertTrue(visible.get(0).text().startsWith("Caught! Onna seizes your wrist."),
                visible.get(0).text());
        assertTrue(visible.get(0).text().contains("[Skyrunning "), visible.get(0).text());
        assertTrue(visible.get(0).text().endsWith("-- CAUGHT]"), visible.get(0).text());
        assertEquals(talk.checkLine(), visible.get(0).text()
                        .substring(visible.get(0).text().indexOf("[Skyrunning ")),
                "the open panel shows the same check line the toast carries");
    }

    @Test
    void rowsAlreadyInTheLogAtConstructionAreHistoryNotNews() {
        int jek = actorNamed("Tarry Jek");
        int onna = actorNamed("Onna");
        CrimeLog log = new CrimeLog(8);
        log.record(1L, 0, jek, jek, onna, false);
        EventLog feed = new EventLog(10);
        CrimeFeedTracker tracker = new CrimeFeedTracker(log,
                population.system().skillTracks(), population.registry(), identity, feed,
                new ToastQueue(), () -> Actor.NONE, null);

        tracker.afterTick(2L);
        assertEquals(0, feed.size(), "the baseline convention: pre-existing rows are history");
        assertNull(new TalkState().checkLine());
    }
}
