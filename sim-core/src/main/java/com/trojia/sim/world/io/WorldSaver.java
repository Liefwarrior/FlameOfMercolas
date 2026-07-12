package com.trojia.sim.world.io;

import com.trojia.sim.world.ChunkView;
import com.trojia.sim.world.LaneId;
import com.trojia.sim.world.LaneRegistry;
import com.trojia.sim.world.World;
import com.trojia.sim.world.WorldConfig;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * Produces the world-owned TROJSAV sections: META (dimensions, lane registry,
 * site defs) and WRLD (all resident chunks through {@link ChunkCodec} in
 * ascending chunkIndex; frozen-compressed blobs pass through verbatim).
 * Container assembly and system sections belong to the engine's save path —
 * this class never touches system state.
 *
 * <p>Pure reads; legal only between ticks (after commitTick, before the next
 * beginTick). Byte-deterministic: the same logical world always yields the
 * same section bytes.
 *
 * <p><b>META layout v1</b> (little-endian): {@code u8 metaVersion · u32
 * chunksX · u32 chunksY · u32 chunksZ · u8 laneCount · per lane [u8 nameLen ·
 * ASCII name · u8 bytesPerTile] · u32 siteCount} (site defs land with the
 * site-index implementation; v1 always writes 0). <b>WRLD layout v1:</b>
 * {@code u32 chunkCount · per chunk [u32 chunkIndex · u32 byteLen · byteLen
 * codec bytes]}, ascending chunkIndex.
 */
public final class WorldSaver {

    /** META section layout version written by this build. */
    public static final int META_VERSION = 1;

    private final ChunkCodec codec;

    /** A saver writing with {@code codec}. */
    public WorldSaver(ChunkCodec codec) {
        if (codec == null) {
            throw new IllegalArgumentException("codec must be non-null");
        }
        this.codec = codec;
    }

    /** Renders {@code world}'s META section content. */
    public byte[] writeMetaSection(World world) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream(256);
        DataOutputStream out = new DataOutputStream(bytes);
        out.write(META_VERSION);
        WorldConfig config = world.config();
        LittleEndian.writeInt(out, config.chunksX());
        LittleEndian.writeInt(out, config.chunksY());
        LittleEndian.writeInt(out, config.chunksZ());
        LaneRegistry lanes = world.lanes();
        out.write(lanes.count());
        for (int i = 0; i < lanes.count(); i++) {
            LaneId lane = lanes.byIndex(i);
            byte[] name = lane.name().getBytes(StandardCharsets.US_ASCII);
            if (name.length > 255) {
                throw new IOException("lane name too long for META: " + lane.name());
            }
            out.write(name.length);
            out.write(name);
            out.write(lane.bytesPerTile());
        }
        LittleEndian.writeInt(out, 0); // site defs: none serialized in v1.
        return bytes.toByteArray();
    }

    /** Renders {@code world}'s WRLD section content in canonical chunk order. */
    public byte[] writeWorldSection(World world) throws IOException {
        int chunkCount = world.coords().chunkCount();
        ByteArrayOutputStream bytes = new ByteArrayOutputStream(chunkCount * 64);
        DataOutputStream out = new DataOutputStream(bytes);
        LittleEndian.writeInt(out, chunkCount);
        ByteArrayOutputStream chunkBytes = new ByteArrayOutputStream(96 * 1024);
        ViewOverlays overlays = new ViewOverlays();
        for (int chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++) {
            chunkBytes.reset();
            ChunkView chunk = world.chunk(chunkIndex);
            codec.encode(chunk, overlays.bind(chunk), new DataOutputStream(chunkBytes));
            LittleEndian.writeInt(out, chunkIndex);
            LittleEndian.writeInt(out, chunkBytes.size());
            chunkBytes.writeTo(out);
        }
        return bytes.toByteArray();
    }
}
