package com.trojia.sim.world;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sparse CHARGE overlay storage through the public write/read surface:
 * canonical ascending-localIdx iteration regardless of insertion order,
 * replace/remove semantics, growth past the initial capacity, and the shared
 * empty view.
 */
final class SparseOverlayTest {

    @Test
    void cellsIterateInAscendingLocalIdxOrderRegardlessOfInsertionOrder() {
        WorldFixture fixture = WorldFixture.minimal();
        // 40 tiles of the single interior chunk, written in descending order:
        // exercises growth (initial capacity 8) and front insertion.
        int[] valueByLocalIdx = new int[Coords.TILES_PER_CHUNK];
        for (int i = 39; i >= 0; i--) {
            int pos = PackedPos.pack(32 + (i & 31), 40 + (i >> 5), 10);
            assertEquals(ChunkWriter.APPLIED,
                    fixture.writer.setOverlay(pos, OverlayId.CHARGE, 1000 + i));
            valueByLocalIdx[fixture.coords.localIdx(pos)] = 1000 + i;
        }
        OverlayView overlay =
                fixture.world.chunk(fixture.interiorChunk()).overlay(OverlayId.CHARGE);
        assertEquals(40, overlay.size());
        int previous = -1;
        for (int i = 0; i < overlay.size(); i++) {
            int localIdx = overlay.localIdxAt(i);
            assertTrue(localIdx > previous, "localIdx order must be strictly ascending");
            previous = localIdx;
            assertEquals(valueByLocalIdx[localIdx], overlay.valueAt(i));
        }
    }

    @Test
    void putReplacesAndRemoveShifts() {
        WorldFixture fixture = WorldFixture.minimal();
        int a = PackedPos.pack(33, 40, 10);
        int b = PackedPos.pack(34, 40, 10);
        int c = PackedPos.pack(35, 40, 10);
        fixture.writer.setOverlay(a, OverlayId.CHARGE, 1);
        fixture.writer.setOverlay(b, OverlayId.CHARGE, 2);
        fixture.writer.setOverlay(c, OverlayId.CHARGE, 3);
        fixture.writer.setOverlay(b, OverlayId.CHARGE, 22); // replace, no new cell
        OverlayView overlay =
                fixture.world.chunk(fixture.interiorChunk()).overlay(OverlayId.CHARGE);
        assertEquals(3, overlay.size());
        assertEquals(22, overlay.get(fixture.coords.localIdx(b), -1));

        fixture.writer.clearOverlay(b, OverlayId.CHARGE);
        assertEquals(2, overlay.size());
        assertEquals(fixture.coords.localIdx(a), overlay.localIdxAt(0));
        assertEquals(fixture.coords.localIdx(c), overlay.localIdxAt(1));
        assertEquals(1, overlay.valueAt(0));
        assertEquals(3, overlay.valueAt(1));
        // Clearing an absent cell is an accepted no-op.
        assertEquals(ChunkWriter.APPLIED, fixture.writer.clearOverlay(b, OverlayId.CHARGE));
        assertEquals(2, overlay.size());
    }

    @Test
    void unsignedSixteenBitRangeSurvives() {
        WorldFixture fixture = WorldFixture.minimal();
        int pos = PackedPos.pack(50, 50, 12);
        fixture.writer.setOverlay(pos, OverlayId.CHARGE, 65535);
        OverlayView overlay =
                fixture.world.chunk(fixture.interiorChunk()).overlay(OverlayId.CHARGE);
        assertEquals(65535, overlay.get(fixture.coords.localIdx(pos), -1));
    }

    @Test
    void emptyViewBehavesAndOutOfRangeIndicesThrow() {
        WorldFixture fixture = WorldFixture.minimal();
        OverlayView empty =
                fixture.world.chunk(fixture.interiorChunk()).overlay(OverlayId.CHARGE);
        assertEquals(0, empty.size());
        assertFalse(empty.contains(0));
        assertEquals(7, empty.get(123, 7));
        assertThrows(IndexOutOfBoundsException.class, () -> empty.localIdxAt(0));
        assertThrows(IndexOutOfBoundsException.class, () -> empty.valueAt(0));

        fixture.writer.setOverlay(PackedPos.pack(40, 40, 10), OverlayId.CHARGE, 5);
        OverlayView one =
                fixture.world.chunk(fixture.interiorChunk()).overlay(OverlayId.CHARGE);
        assertEquals(1, one.size());
        assertThrows(IndexOutOfBoundsException.class, () -> one.localIdxAt(1));
        assertThrows(IndexOutOfBoundsException.class, () -> one.valueAt(-1));
    }
}
