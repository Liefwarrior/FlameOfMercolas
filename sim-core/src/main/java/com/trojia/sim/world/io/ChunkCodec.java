package com.trojia.sim.world.io;

import com.trojia.sim.world.ChunkView;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.LaneId;
import com.trojia.sim.world.LaneRegistry;
import com.trojia.sim.world.OverlayId;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.util.List;

/**
 * Lane-wise RLE codec for one chunk's dense lanes + sparse overlays, used by
 * the WRLD section, freeze snapshots and the thaw path. Versioned
 * independently of the container so lane layout can migrate in place.
 *
 * <p>Deterministic: identical chunk content encodes to identical bytes. Lanes
 * are encoded in registry order; overlays sorted by localIdx. The FLUID and
 * LIGHT lanes and all overlays survive freeze verbatim through this codec.
 *
 * <p><b>Format v1</b> (little-endian): {@code u8 codecVersion · u8 laneCount ·
 * per lane [u8 bytesPerTile · u8 mode · payload] · u8 overlayCount · per
 * overlay [u8 ordinal · u16 cellCount · cellCount × (u16 localIdx, u16
 * value)]}. Lane payload mode 0 (RLE) is {@code u16 runCount · runCount ×
 * (u16 runLength, u8|u16 value)} with maximal runs covering all 8192 cells;
 * mode 1 (RAW) is 8192 raw values. The smaller encoding is chosen per lane —
 * a pure function of content, so the choice is deterministic.
 */
public final class ChunkCodec {

    /** Codec version written by this build. */
    public static final int CODEC_VERSION = 1;

    private static final int MODE_RLE = 0;
    private static final int MODE_RAW = 1;
    private static final int CELLS = Coords.TILES_PER_CHUNK;
    private static final OverlayId[] OVERLAYS = OverlayId.values();

    private final LaneId[] laneOrder;

    /** A codec bound to one world's sealed lane registry. */
    public ChunkCodec(LaneRegistry lanes) {
        this(snapshot(lanes));
    }

    private ChunkCodec(LaneId[] laneOrder) {
        if (laneOrder.length == 0 || laneOrder.length > 255) {
            throw new IllegalArgumentException("lane count must be in [1, 255]: " + laneOrder.length);
        }
        for (int i = 0; i < laneOrder.length; i++) {
            LaneId lane = laneOrder[i];
            if (lane.index() != i) {
                throw new IllegalArgumentException(
                        "lane order must be dense ascending registry order; position " + i
                                + " holds lane index " + lane.index() + " (" + lane.name() + ")");
            }
        }
        this.laneOrder = laneOrder;
    }

    /**
     * A codec over an explicit lane layout in registry order (index {@code i}
     * of the list must be the lane with registration ordinal {@code i}). For
     * tests and tools that carry a lane layout without a live
     * {@link LaneRegistry}; behavior is identical to the registry constructor.
     */
    public static ChunkCodec forLanes(List<LaneId> lanesInOrder) {
        return new ChunkCodec(lanesInOrder.toArray(new LaneId[0]));
    }

    private static LaneId[] snapshot(LaneRegistry lanes) {
        LaneId[] order = new LaneId[lanes.count()];
        for (int i = 0; i < order.length; i++) {
            order[i] = lanes.byIndex(i);
        }
        return order;
    }

    /** Encodes {@code chunk}'s lanes to {@code out} with no overlay cells. Pure read. */
    public void encode(ChunkView chunk, DataOutput out) throws IOException {
        encode(chunk, ChunkOverlays.EMPTY, out);
    }

    /**
     * Encodes {@code chunk}'s lanes and {@code overlays}' cells to {@code out}.
     * Pure read: neither the chunk nor the overlay seam is mutated. The seam
     * must present cells in ascending localIdx order; violations throw
     * {@code IllegalArgumentException} (a programming error, not a format error).
     */
    public void encode(ChunkView chunk, ChunkOverlays overlays, DataOutput out) throws IOException {
        out.write(CODEC_VERSION);
        out.write(laneOrder.length);
        for (LaneId lane : laneOrder) {
            out.write(lane.bytesPerTile());
            if (lane.bytesPerTile() == 2) {
                encodeShortLane(chunk.shortLane(lane), out);
            } else {
                encodeByteLane(chunk.byteLane(lane), out);
            }
        }
        out.write(OVERLAYS.length);
        for (OverlayId overlay : OVERLAYS) {
            out.write(overlay.ordinal());
            int cells = overlays.size(overlay);
            if (cells < 0 || cells > CELLS) {
                throw new IllegalArgumentException(
                        "overlay " + overlay + " cell count out of range: " + cells);
            }
            LittleEndian.writeShort(out, cells);
            int prev = -1;
            for (int i = 0; i < cells; i++) {
                int localIdx = overlays.localIdxAt(overlay, i);
                int value = overlays.valueAt(overlay, i);
                if (localIdx <= prev || localIdx >= CELLS) {
                    throw new IllegalArgumentException("overlay " + overlay
                            + " cells must be ascending localIdx in [0, " + CELLS + "): "
                            + localIdx + " after " + prev);
                }
                if (value < 0 || value > 0xFFFF) {
                    throw new IllegalArgumentException(
                            "overlay " + overlay + " value must be unsigned 16-bit: " + value);
                }
                LittleEndian.writeShort(out, localIdx);
                LittleEndian.writeShort(out, value);
                prev = localIdx;
            }
        }
    }

    /**
     * Decodes one chunk from {@code in} into {@code target}'s backing arrays,
     * rejecting streams that carry overlay cells (no seam to deliver them to).
     * Loader/thaw-pipeline only — the single sanctioned bulk write that
     * bypasses ChunkWriter (no change logs fire; callers rebuild frontiers
     * from ChunkThawed / bulk-init passes).
     *
     * @throws IOException on codec version mismatch this build cannot migrate
     */
    public void decode(DataInput in, ChunkView target) throws IOException {
        decode(in, target, ChunkOverlays.EMPTY);
    }

    /**
     * Decodes one chunk from {@code in} into {@code target}'s backing arrays
     * and delivers overlay cells to {@code overlays} in ascending localIdx
     * order. Same sanctioned-bulk-write caveats as {@link #decode(DataInput,
     * ChunkView)}.
     *
     * @throws IOException on codec version mismatch, lane-layout mismatch, or
     *                     malformed run/overlay data
     */
    public void decode(DataInput in, ChunkView target, ChunkOverlays overlays) throws IOException {
        int version = in.readUnsignedByte();
        if (version != CODEC_VERSION) {
            throw new IOException("chunk codec version " + version
                    + " cannot be migrated by this build (supports " + CODEC_VERSION + ")");
        }
        int laneCount = in.readUnsignedByte();
        if (laneCount != laneOrder.length) {
            throw new IOException("lane-set mismatch: stream has " + laneCount
                    + " lanes, world has " + laneOrder.length);
        }
        for (LaneId lane : laneOrder) {
            int width = in.readUnsignedByte();
            if (width != lane.bytesPerTile()) {
                throw new IOException("lane '" + lane.name() + "' width mismatch: stream "
                        + width + ", world " + lane.bytesPerTile());
            }
            if (width == 2) {
                decodeShortLane(in, target.shortLane(lane), lane);
            } else {
                decodeByteLane(in, target.byteLane(lane), lane);
            }
        }
        int overlayCount = in.readUnsignedByte();
        int prevOrdinal = -1;
        for (int k = 0; k < overlayCount; k++) {
            int ordinal = in.readUnsignedByte();
            if (ordinal <= prevOrdinal || ordinal >= OVERLAYS.length) {
                throw new IOException("unknown or out-of-order overlay ordinal " + ordinal);
            }
            OverlayId overlay = OVERLAYS[ordinal];
            int cells = LittleEndian.readUShort(in);
            if (cells > CELLS) {
                throw new IOException("overlay " + overlay + " cell count out of range: " + cells);
            }
            if (cells > 0 && overlays == ChunkOverlays.EMPTY) {
                throw new IOException("stream carries " + cells + " " + overlay
                        + " overlay cells but the caller supplied no overlay seam");
            }
            int prev = -1;
            for (int i = 0; i < cells; i++) {
                int localIdx = LittleEndian.readUShort(in);
                int value = LittleEndian.readUShort(in);
                if (localIdx <= prev || localIdx >= CELLS) {
                    throw new IOException("overlay " + overlay
                            + " cells not ascending localIdx: " + localIdx + " after " + prev);
                }
                overlays.put(overlay, localIdx, value);
                prev = localIdx;
            }
            prevOrdinal = ordinal;
        }
    }

    private static void encodeShortLane(short[] lane, DataOutput out) throws IOException {
        int runs = countRunsShort(lane);
        int rleBytes = 2 + runs * 4;
        if (rleBytes < CELLS * 2) {
            out.write(MODE_RLE);
            LittleEndian.writeShort(out, runs);
            int i = 0;
            while (i < CELLS) {
                short v = lane[i];
                int j = i + 1;
                while (j < CELLS && lane[j] == v) {
                    j++;
                }
                LittleEndian.writeShort(out, j - i);
                LittleEndian.writeShort(out, v & 0xFFFF);
                i = j;
            }
        } else {
            out.write(MODE_RAW);
            for (int i = 0; i < CELLS; i++) {
                LittleEndian.writeShort(out, lane[i] & 0xFFFF);
            }
        }
    }

    private static void encodeByteLane(byte[] lane, DataOutput out) throws IOException {
        int runs = countRunsByte(lane);
        int rleBytes = 2 + runs * 3;
        if (rleBytes < CELLS) {
            out.write(MODE_RLE);
            LittleEndian.writeShort(out, runs);
            int i = 0;
            while (i < CELLS) {
                byte v = lane[i];
                int j = i + 1;
                while (j < CELLS && lane[j] == v) {
                    j++;
                }
                LittleEndian.writeShort(out, j - i);
                out.write(v & 0xFF);
                i = j;
            }
        } else {
            out.write(MODE_RAW);
            for (int i = 0; i < CELLS; i++) {
                out.write(lane[i] & 0xFF);
            }
        }
    }

    private static void decodeShortLane(DataInput in, short[] lane, LaneId id) throws IOException {
        int mode = in.readUnsignedByte();
        if (mode == MODE_RAW) {
            for (int i = 0; i < CELLS; i++) {
                lane[i] = (short) LittleEndian.readUShort(in);
            }
        } else if (mode == MODE_RLE) {
            int runs = LittleEndian.readUShort(in);
            int pos = 0;
            for (int r = 0; r < runs; r++) {
                int len = LittleEndian.readUShort(in);
                short v = (short) LittleEndian.readUShort(in);
                if (len == 0 || pos + len > CELLS) {
                    throw new IOException("lane '" + id.name() + "' malformed run at cell " + pos);
                }
                for (int i = 0; i < len; i++) {
                    lane[pos + i] = v;
                }
                pos += len;
            }
            if (pos != CELLS) {
                throw new IOException("lane '" + id.name() + "' runs cover " + pos
                        + " of " + CELLS + " cells");
            }
        } else {
            throw new IOException("lane '" + id.name() + "' unknown encoding mode " + mode);
        }
    }

    private static void decodeByteLane(DataInput in, byte[] lane, LaneId id) throws IOException {
        int mode = in.readUnsignedByte();
        if (mode == MODE_RAW) {
            in.readFully(lane, 0, CELLS);
        } else if (mode == MODE_RLE) {
            int runs = LittleEndian.readUShort(in);
            int pos = 0;
            for (int r = 0; r < runs; r++) {
                int len = LittleEndian.readUShort(in);
                byte v = in.readByte();
                if (len == 0 || pos + len > CELLS) {
                    throw new IOException("lane '" + id.name() + "' malformed run at cell " + pos);
                }
                for (int i = 0; i < len; i++) {
                    lane[pos + i] = v;
                }
                pos += len;
            }
            if (pos != CELLS) {
                throw new IOException("lane '" + id.name() + "' runs cover " + pos
                        + " of " + CELLS + " cells");
            }
        } else {
            throw new IOException("lane '" + id.name() + "' unknown encoding mode " + mode);
        }
    }

    private static int countRunsShort(short[] lane) {
        int runs = 1;
        for (int i = 1; i < CELLS; i++) {
            if (lane[i] != lane[i - 1]) {
                runs++;
            }
        }
        return runs;
    }

    private static int countRunsByte(byte[] lane) {
        int runs = 1;
        for (int i = 1; i < CELLS; i++) {
            if (lane[i] != lane[i - 1]) {
                runs++;
            }
        }
        return runs;
    }
}
