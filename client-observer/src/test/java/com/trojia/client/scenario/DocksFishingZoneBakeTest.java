package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.sim.actor.FishingZoneTable;
import com.trojia.sim.actor.ZLinkTable;
import com.trojia.sim.actor.ZReachability;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.Walkability;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * S6 fishing-zone bake audit (Eli's bug 6): the World-authored
 * {@link DocksPopulation#fishingZoneTable()} is re-verified against the baked world on
 * every run — every zone is 2-3 CONTIGUOUS open-water cells (deep fluid, not walkable) on
 * the z:+10 water surface, and every cast cell is real walkable ground a fisher can
 * actually stand on and route to (adjacent to the zone's water, same-z strand for the
 * small inshore zones, one z above for the pier-deck casts). This is the authoring gate:
 * a regenerated map that silts a zone up, walls a deck, or strands a cast cell fails here
 * loudly instead of silently starving the fishing loop.
 */
class DocksFishingZoneBakeTest {

    /** Fluid depth at/above which the FLUID lane reads as real water (Walkability's bar). */
    private static final int DEEP_FLUID = 4;

    private static FixtureWorldLoader.Loaded loaded;
    private static FishingZoneTable zones;
    private static ZReachability reach;

    @BeforeAll
    static void bake() {
        loaded = FixtureWorldLoader.loadDocksSurface();
        zones = DocksPopulation.fishingZoneTable();
        // The mid-Tarwalk sidewalk hub (96,33): known inhabited street ground — the S6
        // observer survey's own flood seed.
        int hub = PackedPos.pack(Coords.CHUNK_SIZE_X + 96, Coords.CHUNK_SIZE_Y + 33,
                Coords.CHUNK_SIZE_Z + 11);
        reach = ZReachability.flood(loaded.world(), ZLinkTable.extract(loaded.world()), hub);
    }

    @Test
    void theTableShapeIsTheAuthoredSurvey() {
        assertEquals(26, zones.zoneCount(), "10 small + 10 medium + 6 large survey zones");
        int[] byClass = new int[FishingZoneTable.SIZE_CLASSES];
        for (int z = 0; z < zones.zoneCount(); z++) {
            byClass[zones.sizeClassOf(z)]++;
            int waters = zones.waterCellCount(z);
            assertTrue(waters >= 2 && waters <= 3,
                    "zone " + z + " must span 2-3 water cells, has " + waters);
        }
        assertEquals(10, byClass[FishingZoneTable.SMALL]);
        assertEquals(10, byClass[FishingZoneTable.MEDIUM]);
        assertEquals(6, byClass[FishingZoneTable.LARGE]);
    }

    @Test
    void everyWaterCellIsOpenDeepWaterOnTheWaterSurface() {
        TileCursor cursor = loaded.world().cursor();
        for (int z = 0; z < zones.zoneCount(); z++) {
            for (int j = 0; j < zones.waterCellCount(z); j++) {
                int cell = zones.waterCellAt(z, j);
                assertEquals(Coords.CHUNK_SIZE_Z + 10, PackedPos.z(cell),
                        "zone " + z + " water cell " + j + " must sit on the z:+10 surface");
                var tile = cursor.moveTo(cell);
                assertTrue((tile.fluidBits() & 0x7) >= DEEP_FLUID,
                        "zone " + z + " cell " + j + " at " + at(cell) + " is not deep water");
                assertFalse(Walkability.isWalkable(tile),
                        "zone " + z + " cell " + j + " at " + at(cell)
                                + " is walkable - a spot must surface on open water");
            }
        }
    }

    @Test
    void everyZonesWaterCellsAreContiguous() {
        for (int z = 0; z < zones.zoneCount(); z++) {
            int n = zones.waterCellCount(z);
            boolean[] joined = new boolean[n];
            joined[0] = true;
            for (int grown = 1; grown < n; ) {
                boolean progressed = false;
                for (int j = 0; j < n; j++) {
                    if (joined[j]) {
                        continue;
                    }
                    for (int k = 0; k < n; k++) {
                        if (joined[k] && chebyshev(zones.waterCellAt(z, j),
                                zones.waterCellAt(z, k)) <= 1) {
                            joined[j] = true;
                            grown++;
                            progressed = true;
                            break;
                        }
                    }
                }
                assertTrue(progressed, "zone " + z + " water cells are not contiguous");
            }
        }
    }

    @Test
    void everyCastCellIsWalkableWaterAdjacentAndReachable() {
        TileCursor cursor = loaded.world().cursor();
        for (int z = 0; z < zones.zoneCount(); z++) {
            int cast = zones.castCellOf(z);
            assertTrue(Walkability.isWalkable(cursor.moveTo(cast)),
                    "zone " + z + " cast cell " + at(cast) + " is not walkable");
            assertTrue(reach.reachable(cast),
                    "zone " + z + " cast cell " + at(cast)
                            + " is not flood-reachable from the Tarwalk hub");
            boolean adjacent = false;
            for (int j = 0; j < zones.waterCellCount(z) && !adjacent; j++) {
                int water = zones.waterCellAt(z, j);
                int dz = Math.abs(PackedPos.z(cast) - PackedPos.z(water));
                adjacent = dz <= 1 && chebyshev(cast, water) <= 1;
            }
            assertTrue(adjacent, "zone " + z + " cast cell " + at(cast)
                    + " does not adjoin its own water (chebyshev 1, dz <= 1)");
            int expectedCastZ = zones.sizeClassOf(z) == FishingZoneTable.SMALL ? 10 : 11;
            assertEquals(Coords.CHUNK_SIZE_Z + expectedCastZ, PackedPos.z(cast),
                    "zone " + z + ": small zones cast from the strand, larger from the decks");
        }
    }

    private static int chebyshev(int a, int b) {
        return Math.max(Math.abs(PackedPos.x(a) - PackedPos.x(b)),
                Math.abs(PackedPos.y(a) - PackedPos.y(b)));
    }

    private static String at(int cell) {
        return "(" + (PackedPos.x(cell) - Coords.CHUNK_SIZE_X) + ","
                + (PackedPos.y(cell) - Coords.CHUNK_SIZE_Y) + ",z+"
                + (PackedPos.z(cell) - Coords.CHUNK_SIZE_Z) + ")";
    }
}
