package com.trojia.sim.actor;

import com.trojia.sim.world.io.WorldHasher;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-2 crime ring buffer: oldest-first ring semantics (the ShoveLog template),
 * the witnessed/served flag lifecycle, and the byte-faithful persisted triad — a save
 * mid-crime-window must resume with the exact rows (and served state) the continuous run
 * carries.
 */
final class CrimeLogTest {

    @Test
    void recordsOldestFirstAndOverwritesTheOldestWhenFull() {
        CrimeLog log = new CrimeLog(3);
        log.record(10, 100, 1, 1, 2, true);
        log.record(11, 101, 3, 3, 4, false);
        log.record(12, 102, 5, 5, 6, true);
        assertEquals(3, log.size());
        assertEquals(10, log.tickAt(0));
        assertEquals(12, log.tickAt(2));

        log.record(13, 103, 7, 8, 9, true); // overwrites the tick-10 row
        assertEquals(3, log.size());
        assertEquals(4, log.totalRecorded());
        assertEquals(11, log.tickAt(0));
        assertEquals(13, log.tickAt(2));
        assertEquals(7, log.thiefIdAt(2));
        assertEquals(8, log.presentedIdAt(2));
        assertEquals(9, log.victimIdAt(2));
        assertTrue(log.witnessedAt(2));
    }

    @Test
    void markServedFlagsOnlyTheCulpritsWitnessedRows() {
        CrimeLog log = new CrimeLog(8);
        log.record(10, 100, 1, 1, 2, true);   // culprit 1, witnessed
        log.record(11, 101, 1, 1, 3, false);  // culprit 1, clean lift (never served)
        log.record(12, 102, 5, 5, 6, true);   // culprit 5, witnessed
        log.record(13, 103, 1, 1, 4, true);   // culprit 1, witnessed again

        log.markServed(1);

        assertTrue(log.servedAt(0), "culprit 1's witnessed row is served");
        assertFalse(log.servedAt(1), "an unwitnessed lift is never 'served'");
        assertFalse(log.servedAt(2), "culprit 5's row is untouched");
        assertTrue(log.servedAt(3), "one correction answers the whole spree");
    }

    @Test
    void theTriadRoundTripsByteIdenticallyWithServedFlagsAboard() throws IOException {
        CrimeLog log = new CrimeLog(4);
        for (int i = 0; i < 6; i++) { // wraps the ring: slot arithmetic must survive the trip
            log.record(100 + i, 200 + i, i, i + 50, i + 100, i % 2 == 0);
        }
        log.markServed(4);

        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        log.serialize(new DataOutputStream(bytes));
        byte[] first = bytes.toByteArray();

        CrimeLog reloaded = new CrimeLog(4);
        reloaded.load(new DataInputStream(new ByteArrayInputStream(first)));
        assertEquals(log.totalRecorded(), reloaded.totalRecorded());
        assertTrue(reloaded.servedAt(reloaded.size() - 2) || reloaded.witnessedAt(0),
                "flag bytes ride the trip");

        ByteArrayOutputStream second = new ByteArrayOutputStream();
        reloaded.serialize(new DataOutputStream(second));
        assertArrayEquals(first, second.toByteArray(),
                "serialize -> load -> serialize must be byte-identical");
        assertEquals(hash(log), hash(reloaded), "hashInto must match after load");

        // The reloaded ring must keep overwriting on the exact slots the original would.
        log.record(900, 901, 9, 9, 9, true);
        reloaded.record(900, 901, 9, 9, 9, true);
        assertEquals(hash(log), hash(reloaded), "post-load ring arithmetic must stay in step");
    }

    private static long hash(CrimeLog log) {
        WorldHasher hasher = new WorldHasher();
        var id = com.trojia.sim.engine.SystemId.of("crimetest", "CRIM");
        log.hashInto(hasher.sectionSink(id));
        return hasher.sectionHash(id);
    }
}
