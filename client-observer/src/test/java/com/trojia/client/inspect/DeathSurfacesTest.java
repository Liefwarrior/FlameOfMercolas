package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.DeathLog;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.StatusBit;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 6 death, every client surface (Eli's bug 7 — "we need people to die", and the
 * ward needs to SEE it): the by-name feed line off the {@link DeathLog}, the deceased
 * sheet, the {@code (dead)} nameplate, the status-words decode, the verb-target guards,
 * and the one-death-one-line contract with {@link EventLogTracker}. Headless, crafted
 * stamps over the real compound population.
 */
class DeathSurfacesTest {

    private final CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);

    private void clearDeath(Actor actor) {
        actor.setStatus(StatusBit.DEAD, false);
        actor.setStatus(StatusBit.EXECUTED, false);
    }

    @Test
    void statusBitsDecodeToWordsNotHex() {
        assertEquals("(none)", DeathPresentation.statusWords((short) 0));
        assertEquals("HELD", DeathPresentation.statusWords(StatusBit.HELD));
        assertEquals("EXECUTED, DEAD", DeathPresentation.statusWords(
                (short) (StatusBit.EXECUTED | StatusBit.DEAD)));
        assertEquals("ON_FIRE, WET", DeathPresentation.statusWords(
                (short) (StatusBit.ON_FIRE | StatusBit.WET)));
    }

    @Test
    void deathFeedNamesTheDeadWithTheirCause() {
        DeathLog log = new DeathLog(16);
        EventLog eventLog = new EventLog(16);
        ToastQueue toasts = new ToastQueue();
        DeathFeedTracker tracker = new DeathFeedTracker(log, p.registry(),
                IdentityRegistry.EMPTY, eventLog, toasts, () -> Actor.NONE);

        log.record(40L, 2, ReasonCode.STARVED_TO_DEATH);
        log.record(41L, 5, ReasonCode.EXECUTED_SECOND_OFFENSE);
        tracker.afterTick(41L);

        List<EventLog.Entry> entries = eventLog.recentNewestFirst(10);
        assertEquals(2, entries.size());
        assertEquals(PersonNames.fullNameOf(5, p.registry(), IdentityRegistry.EMPTY)
                + " has died -- hanged", entries.get(0).text());
        assertEquals(EventLog.Channel.CRIME, entries.get(0).channel());
        assertEquals(PersonNames.fullNameOf(2, p.registry(), IdentityRegistry.EMPTY)
                + " has died -- starvation", entries.get(1).text());
        assertEquals(EventLog.Channel.GENERAL, entries.get(1).channel());
        assertTrue(toasts.visible().isEmpty(), "nobody played: no toast");

        // Cursor discipline: nothing new, nothing re-narrated.
        tracker.afterTick(42L);
        assertEquals(2, eventLog.recentNewestFirst(10).size());
    }

    @Test
    void thePlayedSoulsOwnDeathToasts() {
        DeathLog log = new DeathLog(4);
        EventLog eventLog = new EventLog(4);
        ToastQueue toasts = new ToastQueue();
        DeathFeedTracker tracker = new DeathFeedTracker(log, p.registry(),
                IdentityRegistry.EMPTY, eventLog, toasts, () -> 7);
        log.record(9L, 7, ReasonCode.STARVED_TO_DEATH);
        tracker.afterTick(9L);
        assertEquals(List.of(DeathFeedTracker.PLAYED_DEATH_TOAST),
                toasts.visible().stream().map(ToastQueue.Toast::text).toList());
    }

    @Test
    void rowsAlreadyInTheLogAtConstructionAreHistoryNotNews() {
        DeathLog log = new DeathLog(4);
        log.record(1L, 3, ReasonCode.STARVED_TO_DEATH);
        EventLog eventLog = new EventLog(4);
        DeathFeedTracker tracker = new DeathFeedTracker(log, p.registry(),
                IdentityRegistry.EMPTY, eventLog, new ToastQueue(), () -> Actor.NONE);
        tracker.afterTick(2L);
        assertTrue(eventLog.recentNewestFirst(10).isEmpty());
    }

    @Test
    void aDeadSoulsSheetSaysDeceasedNotBusy() {
        Actor actor = p.registry().get(2);
        try {
            actor.setStatus(StatusBit.DEAD, true);
            List<String> lines = CharacterSheetText.describe(2, p.registry(), p.homes(),
                    p.relationships(), p.jobs(), p.items(), IdentityRegistry.EMPTY,
                    com.trojia.sim.actor.SkillTrackRegistry.UNWIRED,
                    com.trojia.sim.actor.FactionStandings.UNWIRED);
            assertTrue(lines.contains(CharacterSheetText.DECEASED_BANNER), lines.toString());
            assertTrue(lines.contains(CharacterSheetText.DEAD_NEEDS_LINE), lines.toString());
            assertTrue(lines.stream().noneMatch(l -> l.startsWith("goal:")),
                    "a corpse keeps no purposes: " + lines);
            assertTrue(lines.stream().noneMatch(l -> l.startsWith("reason:")), lines.toString());
            assertTrue(lines.stream().anyMatch(l -> l.contains("status: DEAD")),
                    "status must read as words: " + lines);
        } finally {
            clearDeath(actor);
        }
    }

    @Test
    void aLivingSheetKeepsGoalReasonAndWordStatuses() {
        List<String> lines = CharacterSheetText.describe(2, p.registry(), p.homes(),
                p.relationships(), p.jobs(), p.items(), IdentityRegistry.EMPTY,
                com.trojia.sim.actor.SkillTrackRegistry.UNWIRED,
                com.trojia.sim.actor.FactionStandings.UNWIRED);
        assertFalse(lines.contains(CharacterSheetText.DECEASED_BANNER));
        assertTrue(lines.stream().anyMatch(l -> l.startsWith("goal:")));
        assertTrue(lines.stream().anyMatch(l -> l.startsWith("reason:")));
        assertTrue(lines.stream().anyMatch(l -> l.contains("status: (none)")),
                "the hex print is gone for the living too: " + lines);
    }

    @Test
    void deadNameplateCarriesTheMarker() {
        Actor actor = p.registry().get(2);
        try {
            actor.setStatus(StatusBit.DEAD, true);
            assertEquals("Serf #2 -- serf.laborer" + NameplateText.DEAD_MARKER,
                    NameplateText.labelFor(2, p.registry(), p.jobs(), IdentityRegistry.EMPTY));
        } finally {
            clearDeath(actor);
        }
        assertEquals("Serf #2 -- serf.laborer",
                NameplateText.labelFor(2, p.registry(), p.jobs(), IdentityRegistry.EMPTY));
    }

    @Test
    void aStarvedCorpseIsNoVerbTargetButTheGibbetStillTalks() {
        // Two adjacent souls in the spawn layout: find any pair within reach 1, same z.
        int selfId = -1;
        int otherId = -1;
        outer:
        for (int i = 0; i < p.registry().size(); i++) {
            for (int j = 0; j < p.registry().size(); j++) {
                if (i != j && com.trojia.sim.actor.ActorGeometry.chebyshev(
                        p.registry().get(i).cell(), p.registry().get(j).cell()) <= 1
                        && com.trojia.sim.world.PackedPos.z(p.registry().get(i).cell())
                                == com.trojia.sim.world.PackedPos.z(p.registry().get(j).cell())) {
                    selfId = i;
                    otherId = j;
                    break outer;
                }
            }
        }
        assertTrue(selfId >= 0, "no adjacent pair in the spawn layout");
        Actor other = p.registry().get(otherId);
        try {
            assertEquals(otherId,
                    AdjacentTargets.lowestIdAdjacent(p.registry(), selfId, true));

            other.setStatus(StatusBit.DEAD, true); // starved: gone from every verb
            assertFalse(AdjacentTargets.lowestIdAdjacent(p.registry(), selfId, true) == otherId,
                    "dead flesh doesn't speak");
            assertFalse(AdjacentTargets.lowestIdAdjacent(p.registry(), selfId, false) == otherId);

            other.setStatus(StatusBit.EXECUTED, true); // the gibbet: talk-only
            assertEquals(otherId,
                    AdjacentTargets.lowestIdAdjacent(p.registry(), selfId, true));
            assertFalse(AdjacentTargets.lowestIdAdjacent(p.registry(), selfId, false) == otherId);
        } finally {
            clearDeath(other);
        }
    }

    @Test
    void oneDeathLandsOneFeedLineNeverAlsoTheDebugShape() {
        EventLog eventLog = new EventLog(64);
        EventLogTracker tracker = new EventLogTracker(p.registry(), p.homes(), eventLog);
        Actor actor = p.registry().get(2);
        ReasonCode before = actor.lastReasonCode();
        try {
            actor.setLastReasonCode(ReasonCode.STARVED_TO_DEATH);
            tracker.afterTick(1L);
            actor.setLastReasonCode(ReasonCode.EXECUTED_SECOND_OFFENSE);
            tracker.afterTick(2L);
            assertTrue(eventLog.recentNewestFirst(20).stream()
                            .noneMatch(e -> e.text().contains("STARVED_TO_DEATH")
                                    || e.text().contains("EXECUTED_SECOND_OFFENSE")),
                    "DeathFeedTracker owns death narration");

            actor.setLastReasonCode(ReasonCode.MAIMED_FIRST_OFFENSE);
            tracker.afterTick(3L);
            assertTrue(eventLog.recentNewestFirst(20).stream()
                            .anyMatch(e -> e.text().equals(
                                    "Serf #2 lost a hand to the ward's justice")),
                    "the maiming gets its authored sentence");
        } finally {
            actor.setLastReasonCode(before);
            tracker.afterTick(4L);
        }
    }
}
