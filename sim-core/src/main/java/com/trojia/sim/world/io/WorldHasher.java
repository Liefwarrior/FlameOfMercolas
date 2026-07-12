package com.trojia.sim.world.io;

import com.trojia.sim.engine.SystemId;
import com.trojia.sim.random.RandomSource;
import com.trojia.sim.world.ChunkView;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.LaneId;
import com.trojia.sim.world.LaneRegistry;
import com.trojia.sim.world.OverlayId;
import com.trojia.sim.world.World;

import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * The one world hasher (ARCHITECTURE.md §1.1 #9): the engine owns the
 * {@link Sink} protocol and collects per-system sub-hashes; the world
 * contributes its canonical logical content via {@link #hashWorld}. Per-system
 * sub-hashes let a golden-master divergence name the first divergent system
 * and tick instead of "some byte differs".
 *
 * <p>Canonical world content: decoded (uncompressed) lane values, chunks in
 * ascending chunkIndex, lanes in registry order, overlays sorted by localIdx;
 * chunk lifecycle state (frozen/active) is excluded — the logical world hashes
 * identically whether a chunk happens to be frozen. Pure integer math; the
 * hash function is pinned before the first bless.
 *
 * <p><b>Pinned hash spec:</b> every sink is a little-endian byte stream folded
 * 8 bytes at a time through {@code h = mix64(h ^ block)}, seeded with
 * {@code mix64(SECTION_SEED ^ salt)}; the sub-hash finalizer folds the partial
 * tail block (tagged with its byte count) and the total byte count. The
 * combined hash folds {@code (salt, subHash)} pairs in ascending-salt order —
 * so it is invariant to the order sinks were created or fed.
 */
public final class WorldHasher {

    /**
     * The reserved identity under which {@link #hashWorld} accumulates the
     * world's (WRLD) sub-hash. The engine's boot-time salt collision check
     * must treat this name as taken.
     */
    public static final SystemId WORLD_SECTION = SystemId.of("world");

    /** Pinned sub-hash seed constant ("TROJSAV1"). */
    private static final long SECTION_SEED = 0x54524F4A53415631L;
    /** Pinned combined-hash seed constant ("FLAMEV01"). */
    private static final long COMBINE_SEED = 0x464C414D45563031L;

    private static final int CELLS = Coords.TILES_PER_CHUNK;
    private static final OverlayId[] OVERLAYS = OverlayId.values();

    /**
     * The engine-owned hashing protocol systems and the world feed their
     * canonical state into. Widths are explicit — callers never rely on
     * platform int promotion; multi-byte values are folded little-endian.
     */
    public interface Sink {

        /** Folds the low 8 bits of {@code v}. */
        void putByte(int v);

        /** Folds the low 16 bits of {@code v}. */
        void putShort(int v);

        /** Folds all 32 bits of {@code v}. */
        void putInt(int v);

        /** Folds all 64 bits of {@code v}. */
        void putLong(long v);

        /** Folds {@code length} raw bytes from {@code bytes} starting at {@code offset}. */
        void putBytes(byte[] bytes, int offset, int length);
    }

    /** Per-section streaming state; salt-seeded so equal content under different ids differs. */
    private static final class HashSink implements Sink {

        private long h;
        private long buf;
        private int bufBits;
        private long totalBytes;

        HashSink(long salt) {
            this.h = RandomSource.mix64(SECTION_SEED ^ salt);
        }

        private void fold(int b) {
            buf |= (long) (b & 0xFF) << bufBits;
            bufBits += 8;
            totalBytes++;
            if (bufBits == 64) {
                h = RandomSource.mix64(h ^ buf);
                buf = 0;
                bufBits = 0;
            }
        }

        @Override
        public void putByte(int v) {
            fold(v);
        }

        @Override
        public void putShort(int v) {
            fold(v);
            fold(v >>> 8);
        }

        @Override
        public void putInt(int v) {
            fold(v);
            fold(v >>> 8);
            fold(v >>> 16);
            fold(v >>> 24);
        }

        @Override
        public void putLong(long v) {
            putInt((int) v);
            putInt((int) (v >>> 32));
        }

        @Override
        public void putBytes(byte[] bytes, int offset, int length) {
            for (int i = 0; i < length; i++) {
                fold(bytes[offset + i]);
            }
        }

        /**
         * The sub-hash of everything folded so far. Pure — does not disturb
         * the stream state, so it may be read repeatedly.
         */
        long finished() {
            long r = h;
            if (bufBits != 0) {
                // Tail bits live in the low bufBits; the byte count tag in the
                // (always unused) top byte disambiguates short tails from zeros.
                r = RandomSource.mix64(r ^ (buf | ((long) (bufBits >>> 3) << 56)));
            }
            return RandomSource.mix64(r ^ totalBytes);
        }
    }

    /** Sub-hash states keyed by system salt: TreeMap iteration IS the canonical combine order. */
    private final TreeMap<Long, HashSink> sections = new TreeMap<>();

    /** Creates a hasher for one tick's hash; not reusable across ticks. */
    public WorldHasher() {
    }

    /**
     * The sink accumulating {@code system}'s sub-hash. Each system feeds its
     * canonical state exactly once per hash; sub-hash accumulation order does
     * not affect the combined hash (sub-hashes are combined in sorted
     * {@link SystemId#salt()} order).
     */
    public Sink sectionSink(SystemId system) {
        return sections.computeIfAbsent(system.salt(), HashSink::new);
    }

    /**
     * Contributes {@code world}'s canonical logical content (the WRLD
     * sub-hash) in the canonical order documented on this class: every chunk
     * ascending by chunkIndex, lanes in registry order, overlay cells (read
     * through {@code ChunkView.overlay}) in ascending localIdx.
     */
    public void hashWorld(World world) {
        Sink sink = sectionSink(WORLD_SECTION);
        LaneRegistry lanes = world.lanes();
        LaneId[] order = new LaneId[lanes.count()];
        for (int i = 0; i < order.length; i++) {
            order[i] = lanes.byIndex(i);
        }
        ViewOverlays overlays = new ViewOverlays();
        int chunkCount = world.coords().chunkCount();
        for (int chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++) {
            ChunkView chunk = world.chunk(chunkIndex);
            hashChunkInto(sink, chunk, order, overlays.bind(chunk));
        }
    }

    /**
     * Contributes one chunk's canonical content to the WRLD sub-hash: the
     * per-chunk unit of {@link #hashWorld}, exposed for tests and tools that
     * carry a lane layout without a live world. Callers must feed chunks in
     * ascending chunkIndex order to match the canonical world hash.
     *
     * @param lanesInOrder the lane layout in registry order (index {@code i}
     *                     holds the lane with registration ordinal {@code i})
     * @param overlays     the chunk's sparse overlay cells, ascending localIdx
     */
    public void hashChunk(ChunkView chunk, List<LaneId> lanesInOrder, ChunkOverlays overlays) {
        LaneId[] order = lanesInOrder.toArray(new LaneId[0]);
        for (int i = 0; i < order.length; i++) {
            if (order[i].index() != i) {
                throw new IllegalArgumentException(
                        "lane order must be dense ascending registry order; position " + i
                                + " holds lane index " + order[i].index());
            }
        }
        hashChunkInto(sectionSink(WORLD_SECTION), chunk, order, overlays);
    }

    private static void hashChunkInto(Sink sink, ChunkView chunk, LaneId[] laneOrder,
                                      ChunkOverlays overlays) {
        sink.putInt(chunk.chunkIndex());
        for (LaneId lane : laneOrder) {
            sink.putByte(lane.index());
            sink.putByte(lane.bytesPerTile());
            if (lane.bytesPerTile() == 2) {
                short[] values = chunk.shortLane(lane);
                for (int i = 0; i < CELLS; i++) {
                    sink.putShort(values[i]);
                }
            } else {
                sink.putBytes(chunk.byteLane(lane), 0, CELLS);
            }
        }
        for (OverlayId overlay : OVERLAYS) {
            sink.putByte(overlay.ordinal());
            int cells = overlays.size(overlay);
            sink.putInt(cells);
            int prev = -1;
            for (int i = 0; i < cells; i++) {
                int localIdx = overlays.localIdxAt(overlay, i);
                if (localIdx <= prev || localIdx >= CELLS) {
                    throw new IllegalArgumentException("overlay " + overlay
                            + " cells must be ascending localIdx: " + localIdx + " after " + prev);
                }
                sink.putShort(localIdx);
                sink.putShort(overlays.valueAt(overlay, i));
                prev = localIdx;
            }
        }
    }

    /** The finished sub-hash of {@code system}; legal after its sink is fully fed. */
    public long sectionHash(SystemId system) {
        HashSink sink = sections.get(system.salt());
        if (sink == null) {
            throw new IllegalStateException("no section was fed for system '" + system.name() + "'");
        }
        return sink.finished();
    }

    /**
     * The combined tick hash: all sub-hashes (world + systems) folded in
     * sorted salt order. This is the value chained tick-over-tick by the
     * golden-master harness.
     */
    public long combinedHash() {
        long h = RandomSource.mix64(COMBINE_SEED);
        for (Map.Entry<Long, HashSink> entry : sections.entrySet()) {
            h = RandomSource.mix64(h ^ entry.getKey());
            h = RandomSource.mix64(h + entry.getValue().finished());
        }
        return h;
    }
}
