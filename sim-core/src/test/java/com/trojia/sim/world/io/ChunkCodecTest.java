package com.trojia.sim.world.io;

import com.trojia.sim.world.Coords;
import com.trojia.sim.world.LaneId;
import com.trojia.sim.world.OverlayId;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.List;
import java.util.Random;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Round-trip and format-guard tests for {@link ChunkCodec} against
 * array-backed {@link FakeChunkView}s — no live world required.
 */
final class ChunkCodecTest {

    private static final List<LaneId> LANES = FakeChunkView.coreLanes();
    private static final ChunkCodec CODEC = ChunkCodec.forLanes(LANES);

    private static byte[] encode(FakeChunkView chunk) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        CODEC.encode(chunk, new DataOutputStream(bytes));
        return bytes.toByteArray();
    }

    private static byte[] encode(FakeChunkView chunk, ChunkOverlays overlays) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        CODEC.encode(chunk, overlays, new DataOutputStream(bytes));
        return bytes.toByteArray();
    }

    private static DataInputStream input(byte[] bytes) {
        return new DataInputStream(new ByteArrayInputStream(bytes));
    }

    private static void assertLanesEqual(FakeChunkView expected, FakeChunkView actual) {
        for (LaneId lane : LANES) {
            if (lane.bytesPerTile() == 2) {
                assertArrayEquals(expected.shortLane(lane), actual.shortLane(lane),
                        "lane " + lane.name());
            } else {
                assertArrayEquals(expected.byteLane(lane), actual.byteLane(lane),
                        "lane " + lane.name());
            }
        }
    }

    @Test
    void roundTripsMixedRunAndNoiseContent() throws IOException {
        FakeChunkView original = new FakeChunkView(7, LANES);
        original.fillRandom(0xC0FFEE);
        FakeChunkView decoded = new FakeChunkView(7, LANES);

        CODEC.decode(input(encode(original)), decoded);

        assertLanesEqual(original, decoded);
    }

    @Test
    void roundTripsAllZeroAndAllMaxLanes() throws IOException {
        FakeChunkView original = new FakeChunkView(0, LANES);
        short[] material = original.shortLane(LANES.get(0));
        java.util.Arrays.fill(material, (short) 0xFFFF);
        FakeChunkView decoded = new FakeChunkView(0, LANES);

        CODEC.decode(input(encode(original)), decoded);

        assertLanesEqual(original, decoded);
    }

    @Test
    void encodingIsByteDeterministic() throws IOException {
        FakeChunkView chunk = new FakeChunkView(3, LANES);
        chunk.fillRandom(42);

        assertArrayEquals(encode(chunk), encode(chunk));
    }

    @Test
    void uniformContentEncodesFarSmallerThanNoise() throws IOException {
        FakeChunkView uniform = new FakeChunkView(0, LANES);
        FakeChunkView noise = new FakeChunkView(0, LANES);
        Random random = new Random(1);
        for (LaneId lane : LANES) {
            if (lane.bytesPerTile() == 2) {
                short[] values = noise.shortLane(lane);
                for (int i = 0; i < values.length; i++) {
                    values[i] = (short) random.nextInt(0x10000);
                }
            } else {
                random.nextBytes(noise.byteLane(lane));
            }
        }

        int uniformSize = encode(uniform).length;
        int noiseSize = encode(noise).length;

        assertTrue(uniformSize < 100, "uniform chunk should RLE to a few bytes: " + uniformSize);
        // Noise falls back to RAW: 11 bytes/tile of lanes plus small framing.
        assertTrue(noiseSize >= 11 * Coords.TILES_PER_CHUNK, "noise should be RAW: " + noiseSize);
        FakeChunkView decoded = new FakeChunkView(0, LANES);
        CODEC.decode(input(encode(noise)), decoded);
        assertLanesEqual(noise, decoded);
    }

    @Test
    void roundTripsOverlayCellsInAscendingLocalIdxOrder() throws IOException {
        FakeChunkView chunk = new FakeChunkView(1, LANES);
        FakeChunkOverlays overlays = new FakeChunkOverlays();
        // Deliberately inserted out of order; the seam canonicalizes.
        overlays.put(OverlayId.CHARGE, 5000, 60000);
        overlays.put(OverlayId.CHARGE, 12, 0);
        overlays.put(OverlayId.CHARGE, 8191, 12345);

        FakeChunkOverlays decoded = new FakeChunkOverlays();
        CODEC.decode(input(encode(chunk, overlays)), new FakeChunkView(1, LANES), decoded);

        assertEquals(3, decoded.size(OverlayId.CHARGE));
        assertEquals(12, decoded.localIdxAt(OverlayId.CHARGE, 0));
        assertEquals(0, decoded.valueAt(OverlayId.CHARGE, 0));
        assertEquals(5000, decoded.localIdxAt(OverlayId.CHARGE, 1));
        assertEquals(60000, decoded.valueAt(OverlayId.CHARGE, 1));
        assertEquals(8191, decoded.localIdxAt(OverlayId.CHARGE, 2));
        assertEquals(12345, decoded.valueAt(OverlayId.CHARGE, 2));
    }

    @Test
    void decodingOverlayCellsWithoutASinkIsAFormatError() throws IOException {
        FakeChunkView chunk = new FakeChunkView(1, LANES);
        FakeChunkOverlays overlays = new FakeChunkOverlays();
        overlays.put(OverlayId.CHARGE, 100, 42);
        byte[] encoded = encode(chunk, overlays);

        assertThrows(IOException.class,
                () -> CODEC.decode(input(encoded), new FakeChunkView(1, LANES)));
    }

    @Test
    void versionMismatchIsAHardFail() throws IOException {
        FakeChunkView chunk = new FakeChunkView(0, LANES);
        byte[] encoded = encode(chunk);
        encoded[0] = (byte) (ChunkCodec.CODEC_VERSION + 1);

        assertThrows(IOException.class,
                () -> CODEC.decode(input(encoded), new FakeChunkView(0, LANES)));
    }

    @Test
    void laneLayoutMismatchIsAHardFail() throws IOException {
        FakeChunkView chunk = new FakeChunkView(0, LANES);
        byte[] encoded = encode(chunk);

        List<LaneId> narrower = List.of(new LaneId(0, "material", 2));
        ChunkCodec other = ChunkCodec.forLanes(narrower);

        assertThrows(IOException.class,
                () -> other.decode(input(encoded), new FakeChunkView(0, narrower)));
    }

    @Test
    void truncatedStreamIsAHardFail() throws IOException {
        FakeChunkView chunk = new FakeChunkView(0, LANES);
        chunk.fillRandom(9);
        byte[] encoded = encode(chunk);
        byte[] truncated = java.util.Arrays.copyOf(encoded, encoded.length / 2);

        assertThrows(IOException.class,
                () -> CODEC.decode(input(truncated), new FakeChunkView(0, LANES)));
    }

    @Test
    void laneOrderMustBeDenseAscending() {
        assertThrows(IllegalArgumentException.class,
                () -> ChunkCodec.forLanes(List.of(new LaneId(1, "misplaced", 2))));
    }
}
