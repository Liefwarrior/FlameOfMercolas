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
}
