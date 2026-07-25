package com.trojia.sim.actor;

import com.trojia.sim.progression.SkillRawsLoader;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 6 slice 5 (Eli's bug 6): the fishing-spot registry. Spots surface and sink on the
 * fixed cadence with named draws — deterministic (twin registries stay byte-identical),
 * capped per size class, never two spots in one zone, lifetimes by size. Perception is a
 * STABLE pure function per (spot, soul) — no flicker — whose threshold deepens with the
 * FISHING skill. The slot state rides a frame-guarded persisted triad.
 */
final class FishingSpotsTest {

    private static final long SEED = 4242L;
    private static final int Z_WATER = 18;

    private static int cell(int x, int y, int z) {
        return com.trojia.sim.world.PackedPos.pack(x, y, z);
    }

    private static FishingZoneTable zones() {
        return new FishingZoneTable(
                new int[] {FishingZoneTable.SMALL, FishingZoneTable.SMALL,
                        FishingZoneTable.MEDIUM, FishingZoneTable.LARGE},
                new int[] {cell(2, 3, Z_WATER), cell(8, 3, Z_WATER),
                        cell(14, 3, Z_WATER + 1), cell(20, 3, Z_WATER + 1)},
                new int[][] {
                        {cell(2, 1, Z_WATER), cell(3, 1, Z_WATER)},
                        {cell(8, 1, Z_WATER), cell(9, 1, Z_WATER), cell(10, 1, Z_WATER)},
                        {cell(14, 1, Z_WATER), cell(15, 1, Z_WATER)},
                        {cell(20, 1, Z_WATER), cell(21, 1, Z_WATER), cell(22, 1, Z_WATER)}});
    }

    @Test
    void spawnsWithinCapsInAuthoredZonesWithSizedLifetimes() {
        FishingSpots spots = new FishingSpots(zones());
        boolean everLive = false;
        for (long t = FishingSpots.SPAWN_PERIOD_TICKS; t <= 300_000;
                t += FishingSpots.SPAWN_PERIOD_TICKS) {
            spots.tick(SEED, t);
            int[] liveOfClass = new int[FishingZoneTable.SIZE_CLASSES];
            boolean[] zoneLive = new boolean[zones().zoneCount()];
            for (int s = 0; s < spots.slotCapacity(); s++) {
                if (!spots.isLive(s)) {
                    continue;
                }
                everLive = true;
                int zone = spots.zoneAt(s);
                assertTrue(zone >= 0 && zone < zones().zoneCount(), "spot in an authored zone");
                assertTrue(!zoneLive[zone], "never two live spots in one zone");
                zoneLive[zone] = true;
                liveOfClass[spots.sizeClassAt(s)]++;
                assertEquals(FishingSpots.LIFETIME_TICKS[spots.sizeClassAt(s)],
                        spots.expiryTickAt(s) - spots.spawnTickAt(s),
                        "lifetime is sized by class");
                assertTrue(spots.expiryTickAt(s) > t, "expired spots are cleared on cadence");
            }
            for (int c = 0; c < FishingZoneTable.SIZE_CLASSES; c++) {
                assertTrue(liveOfClass[c] <= FishingSpots.capOf(c),
                        "class " + c + " within its live cap");
            }
        }
        assertTrue(everLive, "over 200 cadences the water must surface SOMETHING");
    }

    @Test
    void twinRegistriesTickIdentically() {
        FishingSpots a = new FishingSpots(zones());
        FishingSpots b = new FishingSpots(zones());
        for (long t = FishingSpots.SPAWN_PERIOD_TICKS; t <= 120_000;
                t += FishingSpots.SPAWN_PERIOD_TICKS) {
            a.tick(SEED, t);
            b.tick(SEED, t);
        }
        for (int s = 0; s < a.slotCapacity(); s++) {
            assertEquals(a.zoneAt(s), b.zoneAt(s), "slot " + s + " zone");
            assertEquals(a.spawnTickAt(s), b.spawnTickAt(s), "slot " + s + " spawnTick");
            assertEquals(a.expiryTickAt(s), b.expiryTickAt(s), "slot " + s + " expiry");
        }
    }

    @Test
    void roundTripsThroughTheTriadAndFrameGuardsTheZoneCount() throws IOException {
        FishingSpots spots = new FishingSpots(zones());
        for (long t = FishingSpots.SPAWN_PERIOD_TICKS; t <= 60_000;
                t += FishingSpots.SPAWN_PERIOD_TICKS) {
            spots.tick(SEED, t);
        }
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        spots.serialize(new DataOutputStream(bytes));

        FishingSpots loaded = new FishingSpots(zones());
        loaded.load(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));
        for (int s = 0; s < spots.slotCapacity(); s++) {
            assertEquals(spots.zoneAt(s), loaded.zoneAt(s));
            assertEquals(spots.spawnTickAt(s), loaded.spawnTickAt(s));
            assertEquals(spots.expiryTickAt(s), loaded.expiryTickAt(s));
        }

        FishingSpots mismatched = new FishingSpots(FishingZoneTable.EMPTY);
        assertThrows(IOException.class, () -> mismatched.load(
                new DataInputStream(new ByteArrayInputStream(bytes.toByteArray()))),
                "a zone-count mismatch must fail the load loudly (frame guard)");
    }

    @Test
    void perceptionIsStablePerSpotAndDeepensWithTheFishingSkill() {
        FishingSpots spots = new FishingSpots(zones());
        long t = FishingSpots.SPAWN_PERIOD_TICKS;
        while (spots.liveCount() == 0) {
            spots.tick(SEED, t);
            t += FishingSpots.SPAWN_PERIOD_TICKS;
            assertTrue(t < 1_000_000, "a spot must surface");
        }
        int slot = 0;
        while (!spots.isLive(slot)) {
            slot++;
        }
        // Stability: the same (spot, soul) pair answers identically forever — no flicker.
        boolean first = spots.visibleTo(slot, SEED, 7, SkillTrackRegistry.UNWIRED);
        for (int i = 0; i < 5; i++) {
            assertEquals(first, spots.visibleTo(slot, SEED, 7, SkillTrackRegistry.UNWIRED));
        }
        // Skill monotonicity: the pass threshold only grows with FISHING level, so any spot
        // an untrained eye sees, a trained one sees too — and a trained eye sees MORE souls'
        // worth of water (counted across many observer ids).
        SkillTrackRegistry tracks = new SkillTrackRegistry(
                SkillRawsLoader.load(locateRawsDir()));
        int untrained = 0;
        int trained = 0;
        for (int actorId = 0; actorId < 400; actorId++) {
            tracks.seedLevel(actorId, tracks.fishingRaw(), 0);
            if (spots.visibleTo(slot, SEED, actorId, tracks)) {
                untrained++;
            }
            tracks.seedLevel(actorId, tracks.fishingRaw(), 50);
            if (spots.visibleTo(slot, SEED, actorId, tracks)) {
                trained++;
            }
        }
        assertTrue(trained > untrained,
                "FISHING deepens perception: trained " + trained + " vs untrained " + untrained);
        assertTrue(untrained > 0, "the untrained still see some water (base chance)");
        assertEquals(SkillChecks.FISH_SIGHT_BASE_PERMILLE,
                SkillChecks.fishSightPermille(SkillTrackRegistry.UNWIRED, 0),
                "unwired tracks degrade to the base sight chance");
    }

    static Path locateRawsDir() {
        Path dir = Path.of("").toAbsolutePath();
        for (int i = 0; i < 6 && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException("content/raws not found");
    }
}
