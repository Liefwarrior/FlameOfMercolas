package com.trojia.sim.actor;

import com.trojia.sim.actor.faction.FactionRawsLoader;
import com.trojia.sim.actor.faction.FactionRegistry;
import com.trojia.sim.world.io.WorldHasher;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/**
 * The per-actor per-faction standing ledger (Sprint 1): deterministic clamped event deltas
 * (arrest / fine / house-arrest push Watch down + Skyrunners up; purchases push Merchants
 * up), the UNWIRED no-op contract, and the persisted triad with its frame guard.
 */
final class FactionStandingsTest {

    private static final FactionRegistry FACTIONS = FactionRawsLoader.load(committedRawsRoot());

    private static Path committedRawsRoot() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws not found above " + Path.of("").toAbsolutePath());
    }

    @Test
    void justiceEventsMoveWatchDownAndSkyrunnersUp() {
        FactionStandings standings = new FactionStandings(FACTIONS);
        int actor = 42;
        assertEquals(0, standings.watchStanding(actor), "neutral until an event lands");

        standings.onArrest(actor);
        assertEquals(FactionStandings.ARREST_WATCH_DELTA, standings.watchStanding(actor));
        assertEquals(FactionStandings.ARREST_SKYRUNNERS_DELTA,
                standings.standingOf(actor, FACTIONS.rawId("skyrunners")));

        standings.onFine(actor);
        standings.onHouseArrest(actor);
        assertEquals(FactionStandings.ARREST_WATCH_DELTA + FactionStandings.FINE_WATCH_DELTA
                        + FactionStandings.HOUSE_ARREST_WATCH_DELTA,
                standings.watchStanding(actor));

        standings.onPurchase(actor);
        assertEquals(FactionStandings.PURCHASE_MERCHANTS_DELTA,
                standings.standingOf(actor, FACTIONS.rawId("merchants")));
        assertEquals(0, standings.standingOf(actor, FACTIONS.rawId("temple")),
                "no event moves Temple standing this sprint");
        assertEquals(0, standings.watchStanding(7), "other actors untouched");
    }

    @Test
    void standingsClampAtTheBounds() {
        FactionStandings standings = new FactionStandings(FACTIONS);
        for (int i = 0; i < 20; i++) {
            standings.onArrest(3); // -20 Watch each: would be -400 unclamped
        }
        assertEquals(FactionStandings.MIN_STANDING, standings.watchStanding(3));
        int merchants = FACTIONS.rawId("merchants");
        standings.adjust(3, merchants, 500);
        assertEquals(FactionStandings.MAX_STANDING, standings.standingOf(3, merchants));
    }

    @Test
    void unwiredNoOpsEverythingAndReadsNeutral() {
        FactionStandings unwired = FactionStandings.UNWIRED;
        unwired.onArrest(5);
        unwired.onPurchase(5);
        assertEquals(0, unwired.watchStanding(5));
        assertEquals(0, unwired.factionCount());
    }

    @Test
    void theTriadRoundTripsByteIdentically() throws IOException {
        FactionStandings source = new FactionStandings(FACTIONS);
        source.onArrest(2);
        source.onPurchase(9);
        source.onFine(2);
        source.onHouseArrest(17);

        byte[] first = serialize(source);
        FactionStandings reloaded = new FactionStandings(FACTIONS);
        reloaded.load(new DataInputStream(new ByteArrayInputStream(first)));
        byte[] second = serialize(reloaded);

        assertArrayEquals(first, second, "serialize -> load -> serialize must be byte-identical");
        assertEquals(hash(source), hash(reloaded), "hashInto must match after load");
        assertEquals(source.watchStanding(2), reloaded.watchStanding(2));

        // Frame guard: a wired save never loads into an unwired ledger.
        assertThrows(IOException.class, () -> FactionStandings.UNWIRED
                .load(new DataInputStream(new ByteArrayInputStream(first))));
    }

    private static byte[] serialize(FactionStandings standings) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        standings.serialize(new DataOutputStream(bytes));
        return bytes.toByteArray();
    }

    private static long hash(FactionStandings standings) {
        WorldHasher hasher = new WorldHasher();
        var id = com.trojia.sim.engine.SystemId.of("test", "TEST");
        standings.hashInto(hasher.sectionSink(id));
        return hasher.sectionHash(id);
    }
}
