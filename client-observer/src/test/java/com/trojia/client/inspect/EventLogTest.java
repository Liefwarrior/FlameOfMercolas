package com.trojia.client.inspect;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** {@link EventLog} rolling-buffer capacity + ordering — pure, headless. */
class EventLogTest {

    @Test
    void rejectsNonPositiveCapacity() {
        assertThrows(IllegalArgumentException.class, () -> new EventLog(0));
    }

    @Test
    void evictsOldestBeyondCapacity() {
        EventLog log = new EventLog(3);
        log.add(1, "a");
        log.add(2, "b");
        log.add(3, "c");
        log.add(4, "d"); // evicts "a"

        assertEquals(3, log.size());
        List<EventLog.Entry> recent = log.recentNewestFirst(10);
        assertEquals(List.of("d", "c", "b"),
                recent.stream().map(EventLog.Entry::text).toList(),
                () -> "expected newest-first with oldest evicted, got " + recent);
    }

    @Test
    void recentNewestFirstIsCappedByLimit() {
        EventLog log = new EventLog(30);
        for (int i = 0; i < 10; i++) {
            log.add(i, "e" + i);
        }
        List<EventLog.Entry> recent = log.recentNewestFirst(4);
        assertEquals(4, recent.size());
        assertEquals("e9", recent.get(0).text());
        assertEquals(9, recent.get(0).tick());
        assertEquals("e6", recent.get(3).text());
    }

    @Test
    void tagsEntriesWithTheirTick() {
        EventLog log = new EventLog(5);
        log.add(42, "something happened");
        EventLog.Entry only = log.recentNewestFirst(1).get(0);
        assertEquals(42, only.tick());
        assertTrue(only.text().contains("something"));
    }

    @Test
    void untaggedEntriesRideTheGeneralChannel() {
        EventLog log = new EventLog(5);
        log.add(1, "plain");
        assertEquals(EventLog.Channel.GENERAL, log.recentNewestFirst(1).get(0).channel());
    }

    @Test
    void channelFilterShowsOnlyItsLaneNewestFirst() {
        EventLog log = new EventLog(10);
        log.add(1, EventLog.Channel.CRIME, "a theft");
        log.add(2, EventLog.Channel.GROWTH, "a level");
        log.add(3, EventLog.Channel.QUEST, "a stage");
        log.add(4, EventLog.Channel.GROWTH, "another level");

        assertEquals(List.of("another level", "a level"),
                log.recentNewestFirst(10, EventLog.Channel.GROWTH).stream()
                        .map(EventLog.Entry::text).toList());
        assertEquals(List.of("a theft"),
                log.recentNewestFirst(10, EventLog.Channel.CRIME).stream()
                        .map(EventLog.Entry::text).toList());
        assertEquals(4, log.recentNewestFirst(10, null).size(),
                "null filter = every channel");
    }

    @Test
    void evictionIsCapacityGlobalAcrossChannels() {
        EventLog log = new EventLog(2);
        log.add(1, EventLog.Channel.CRIME, "old theft");
        log.add(2, EventLog.Channel.GROWTH, "level a");
        log.add(3, EventLog.Channel.GROWTH, "level b"); // evicts the crime line
        assertTrue(log.recentNewestFirst(10, EventLog.Channel.CRIME).isEmpty(),
                "a filtered view sees only what the rolling buffer still holds");
    }

    @Test
    void feedFilterCyclesAllLanesAndWraps() {
        assertEquals(FeedFilter.GROWTH, FeedFilter.ALL.next());
        assertEquals(FeedFilter.CRIME, FeedFilter.GROWTH.next());
        assertEquals(FeedFilter.QUESTS, FeedFilter.CRIME.next());
        assertEquals(FeedFilter.ALL, FeedFilter.QUESTS.next());
        assertEquals(EventLog.Channel.QUEST, FeedFilter.QUESTS.only());
        assertEquals(null, FeedFilter.ALL.only());
    }
}
