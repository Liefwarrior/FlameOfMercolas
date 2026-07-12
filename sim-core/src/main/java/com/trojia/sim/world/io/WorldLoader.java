package com.trojia.sim.world.io;

import com.trojia.sim.world.ChunkWriter;
import com.trojia.sim.world.LaneId;
import com.trojia.sim.world.LaneRegistry;
import com.trojia.sim.world.OverlayId;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.WorldBuilder;
import com.trojia.sim.world.WorldConfig;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * Rebuilds a world from a TROJSAV's META + WRLD sections: reconstructs the
 * WorldBuilder configuration and lane registry from META (extension lanes must
 * re-register identically — a lane-set mismatch is a hard fail), then decodes
 * chunks through {@link ChunkCodec}. System sections are loaded separately by
 * the engine via each system's {@code load}.
 *
 * <p>No relight, no frontier rebuild here — systems rebuild their own derived
 * state from their sections and bulk-init passes.
 */
public final class WorldLoader {

    /** Creates a loader. */
    public WorldLoader() {
    }

    /**
     * Builds the world described by {@code save}'s META section and populates
     * it from the WRLD section (layouts documented on {@link WorldSaver}).
     *
     * @throws IOException on missing/corrupt sections or lane-set mismatch
     */
    public TickableWorld load(TrojSav save) throws IOException {
        DataInputStream meta = new DataInputStream(
                new ByteArrayInputStream(save.section(TrojSav.META)));
        int metaVersion = meta.readUnsignedByte();
        if (metaVersion != WorldSaver.META_VERSION) {
            throw new IOException("unsupported META layout version " + metaVersion
                    + " (this build reads " + WorldSaver.META_VERSION + ")");
        }
        int chunksX = LittleEndian.readInt(meta);
        int chunksY = LittleEndian.readInt(meta);
        int chunksZ = LittleEndian.readInt(meta);
        WorldBuilder builder;
        try {
            builder = WorldBuilder.create(new WorldConfig(chunksX, chunksY, chunksZ));
        } catch (IllegalArgumentException e) {
            throw new IOException("META carries invalid world dimensions: " + e.getMessage(), e);
        }
        registerLanes(meta, builder.lanes());
        int siteCount = LittleEndian.readInt(meta);
        if (siteCount != 0) {
            throw new IOException("META carries " + siteCount
                    + " site defs; site loading is not implemented in this build");
        }

        TickableWorld world = builder.build();
        ChunkCodec codec = new ChunkCodec(world.lanes());
        DataInputStream wrld = new DataInputStream(
                new ByteArrayInputStream(save.section(TrojSav.WRLD)));
        int chunkCount = LittleEndian.readInt(wrld);
        int worldChunks = world.coords().chunkCount();
        WriterOverlays overlays = new WriterOverlays(world);
        int prevIndex = -1;
        for (int k = 0; k < chunkCount; k++) {
            int chunkIndex = LittleEndian.readInt(wrld);
            int byteLen = LittleEndian.readInt(wrld);
            if (chunkIndex <= prevIndex || chunkIndex >= worldChunks) {
                throw new IOException("WRLD chunk frames not ascending in-range chunkIndex: "
                        + chunkIndex + " after " + prevIndex);
            }
            if (byteLen < 0) {
                throw new IOException("WRLD chunk " + chunkIndex + " has negative frame length");
            }
            byte[] frame = new byte[byteLen];
            wrld.readFully(frame);
            overlays.bind(chunkIndex);
            codec.decode(new DataInputStream(new ByteArrayInputStream(frame)),
                    world.chunk(chunkIndex), overlays);
            prevIndex = chunkIndex;
        }
        return world;
    }

    /**
     * Load-side {@link ChunkOverlays} seam: delivers decoded overlay cells
     * into the world through {@code ChunkWriter.setOverlay} — the sanctioned
     * overlay write path (no change logs by ruling §1.2 b; the load-time
     * revision marks are published at the first commit and carry no hash
     * weight). Read methods report empty: the codec only reads the seam when
     * encoding.
     */
    private static final class WriterOverlays implements ChunkOverlays {

        private final TickableWorld world;
        private int chunkIndex;

        WriterOverlays(TickableWorld world) {
            this.world = world;
        }

        /** Targets subsequent {@link #put}s at {@code chunkIndex}. */
        void bind(int chunkIndex) {
            this.chunkIndex = chunkIndex;
        }

        @Override
        public int size(OverlayId overlay) {
            return 0;
        }

        @Override
        public int localIdxAt(OverlayId overlay, int i) {
            throw new IndexOutOfBoundsException("load-side overlay seam has no readable cells");
        }

        @Override
        public int valueAt(OverlayId overlay, int i) {
            throw new IndexOutOfBoundsException("load-side overlay seam has no readable cells");
        }

        @Override
        public void put(OverlayId overlay, int localIdx, int value) {
            int packedPos = world.coords().packedPos(chunkIndex, localIdx);
            int status = world.writer().setOverlay(packedPos, overlay, value);
            if (status != ChunkWriter.APPLIED) {
                throw new IllegalStateException("overlay cell in save targets non-concrete chunk "
                        + chunkIndex + " (writer status " + status + ")");
            }
        }
    }

    /**
     * Replays META's lane list against the builder's registry: the lanes the
     * builder pre-registered (the core set) must match by name and width, and
     * every later entry is registered as an extension lane.
     */
    private static void registerLanes(DataInputStream meta, LaneRegistry lanes)
            throws IOException {
        int laneCount = meta.readUnsignedByte();
        int preRegistered = lanes.count();
        if (laneCount < preRegistered) {
            throw new IOException("lane-set mismatch: save has " + laneCount
                    + " lanes, world pre-registers " + preRegistered);
        }
        for (int i = 0; i < laneCount; i++) {
            int nameLen = meta.readUnsignedByte();
            byte[] nameBytes = new byte[nameLen];
            meta.readFully(nameBytes);
            String name = new String(nameBytes, StandardCharsets.US_ASCII);
            int bytesPerTile = meta.readUnsignedByte();
            if (i < preRegistered) {
                LaneId lane = lanes.byIndex(i);
                if (!lane.name().equals(name) || lane.bytesPerTile() != bytesPerTile) {
                    throw new IOException("lane-set mismatch at index " + i + ": save has '"
                            + name + "'/" + bytesPerTile + ", world has '"
                            + lane.name() + "'/" + lane.bytesPerTile());
                }
            } else {
                lanes.register(name, bytesPerTile);
            }
        }
    }
}
