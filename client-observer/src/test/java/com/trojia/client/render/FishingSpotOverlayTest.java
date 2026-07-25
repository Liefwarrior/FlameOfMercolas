package com.trojia.client.render;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.FishingSpots;
import com.trojia.sim.actor.FishingZoneTable;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link FishingSpotOverlay}: the GL-free spot-marker planner — live slots only, water
 * cells culled to the camera box, the depth-vision rule (same z plans at depth 0; a lower
 * z plans only where the look-down column resolves to it; occluded/above plans nothing),
 * and the visibility split (omniscient {@code Actor.NONE} vs the played soul's
 * sim-confirmed perception read). Headless — the registry state is authored through the
 * persisted triad's own {@code load}, the depth column is a synthetic lambda.
 */
class FishingSpotOverlayTest {

    private static final long SEED = 9021L;
    private static final int WATER_Z = 10;
    private static final int VIEW_Z = 12;

    /** Three zones (small/medium/large), two water cells each, on the z=10 water plane. */
    private static FishingZoneTable zones() {
        return new FishingZoneTable(
                new int[] {FishingZoneTable.SMALL, FishingZoneTable.MEDIUM,
                        FishingZoneTable.LARGE},
                new int[] {PackedPos.pack(10, 21, 11), PackedPos.pack(30, 21, 11),
                        PackedPos.pack(50, 21, 11)},
                new int[][] {
                        {PackedPos.pack(10, 20, WATER_Z), PackedPos.pack(11, 20, WATER_Z)},
                        {PackedPos.pack(30, 20, WATER_Z), PackedPos.pack(31, 20, WATER_Z)},
                        {PackedPos.pack(50, 20, WATER_Z), PackedPos.pack(51, 20, WATER_Z)},
                });
    }

    /**
     * A registry with the given zones LIVE, authored through the persisted triad's own
     * {@code load} (slots are otherwise only reachable through the spawn cadence).
     */
    private static FishingSpots liveSpots(FishingZoneTable zones, int... liveZones) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(bytes);
            out.writeInt(zones.zoneCount());
            for (int s = 0; s < FishingSpots.SLOT_CAPACITY; s++) {
                if (s < liveZones.length) {
                    out.writeInt(liveZones[s]);
                    out.writeLong(1_500L);   // spawnTick
                    out.writeLong(90_000L);  // expiryTick (far future)
                } else {
                    out.writeInt(Actor.NONE);
                    out.writeLong(0L);
                    out.writeLong(0L);
                }
            }
            FishingSpots spots = new FishingSpots(zones);
            spots.load(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));
            return spots;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /** Every column over water resolves to the water plane (nothing occludes). */
    private static final DepthSight OPEN_AIR = (viewZ, x, y) -> WATER_Z;

    @Test
    void omniscientViewPlansEveryLiveSpotWaterCellAtItsLookdownDepth() {
        FishingSpots spots = liveSpots(zones(), 0, 1, 2);
        List<FishingSpotOverlay.Marker> markers = FishingSpotOverlay.plan(spots, VIEW_Z,
                0, 200, 0, 200, OPEN_AIR, Actor.NONE, SEED, SkillTrackRegistry.UNWIRED);
        assertEquals(6, markers.size(), "3 live zones x 2 water cells");
        for (FishingSpotOverlay.Marker marker : markers) {
            assertEquals(VIEW_Z - WATER_Z, marker.depth());
        }
        // Ascending slot order carries the size classes small, medium, large.
        assertEquals(FishingZoneTable.SMALL, markers.get(0).sizeClass());
        assertEquals(FishingZoneTable.MEDIUM, markers.get(2).sizeClass());
        assertEquals(FishingZoneTable.LARGE, markers.get(4).sizeClass());
    }

    @Test
    void deadSlotsAndOutOfViewCellsPlanNothing() {
        FishingSpots spots = liveSpots(zones(), 1); // only the medium zone is live
        List<FishingSpotOverlay.Marker> markers = FishingSpotOverlay.plan(spots, VIEW_Z,
                0, 200, 0, 200, OPEN_AIR, Actor.NONE, SEED, SkillTrackRegistry.UNWIRED);
        assertEquals(2, markers.size());
        assertEquals(30, markers.get(0).tileX());
        assertEquals(31, markers.get(1).tileX());

        // Camera box excluding x=31 keeps only the first water cell.
        assertEquals(1, FishingSpotOverlay.plan(spots, VIEW_Z, 0, 30, 0, 200,
                OPEN_AIR, Actor.NONE, SEED, SkillTrackRegistry.UNWIRED).size());
    }

    @Test
    void sameZPlansAtDepthZeroAndOccludedColumnsPlanNothing() {
        FishingSpots spots = liveSpots(zones(), 0, 1, 2);
        // Viewing the water plane itself: depth 0, no look-down consulted.
        List<FishingSpotOverlay.Marker> onPlane = FishingSpotOverlay.plan(spots, WATER_Z,
                0, 200, 0, 200, (viewZ, x, y) -> DepthSight.NONE,
                Actor.NONE, SEED, SkillTrackRegistry.UNWIRED);
        assertEquals(6, onPlane.size());
        for (FishingSpotOverlay.Marker marker : onPlane) {
            assertEquals(0, marker.depth());
        }
        // From above with every column occluded (a pier deck): nothing plans.
        assertTrue(FishingSpotOverlay.plan(spots, VIEW_Z, 0, 200, 0, 200,
                (viewZ, x, y) -> DepthSight.NONE, Actor.NONE, SEED,
                SkillTrackRegistry.UNWIRED).isEmpty());
        // From BELOW the water (a diver's-eye z) nothing plans either.
        assertTrue(FishingSpotOverlay.plan(spots, WATER_Z - 1, 0, 200, 0, 200,
                OPEN_AIR, Actor.NONE, SEED, SkillTrackRegistry.UNWIRED).isEmpty());
    }

    @Test
    void playedViewerPlansExactlyTheSimsOwnPerceptionRead() {
        FishingZoneTable zones = zones();
        FishingSpots spots = liveSpots(zones, 0, 1, 2);
        // The expectation IS the sim's own visibleTo read — the client must never roll
        // its own visibility. Scan a few viewer ids; the untrained base sight is 300
        // permille, so across ids and 3 spots both outcomes occur (fixed seed, stable).
        boolean sawFiltered = false;
        boolean sawShown = false;
        for (int viewer = 0; viewer < 12; viewer++) {
            List<FishingSpotOverlay.Marker> markers = FishingSpotOverlay.plan(spots,
                    VIEW_Z, 0, 200, 0, 200, OPEN_AIR, viewer, SEED,
                    SkillTrackRegistry.UNWIRED);
            int expectedCells = 0;
            for (int slot = 0; slot < spots.slotCapacity(); slot++) {
                if (spots.isLive(slot)
                        && spots.visibleTo(slot, SEED, viewer, SkillTrackRegistry.UNWIRED)) {
                    expectedCells += zones.waterCellCount(spots.zoneAt(slot));
                }
            }
            assertEquals(expectedCells, markers.size(),
                    "viewer " + viewer + " must see exactly the sim-perceived spots");
            sawFiltered |= expectedCells < 6;
            sawShown |= expectedCells > 0;
        }
        assertTrue(sawFiltered, "some viewer must fail some perception check");
        assertTrue(sawShown, "some viewer must pass some perception check");
    }
}
