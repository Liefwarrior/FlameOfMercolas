package com.trojia.sim.world.io;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * Little-endian primitives over {@link DataOutput}/{@link DataInput} for the
 * TROJSAV family of formats (ARCHITECTURE.md §9: the container and everything
 * inside it is little-endian). {@code java.io.Data*Stream} is big-endian by
 * contract, so every multi-byte value in this package goes through these
 * helpers instead — byte order is then explicit and platform-independent.
 *
 * <p>Pure static functions; no state.
 */
final class LittleEndian {

    private LittleEndian() {
    }

    /** Writes the low 16 bits of {@code v}, least-significant byte first. */
    static void writeShort(DataOutput out, int v) throws IOException {
        out.write(v & 0xFF);
        out.write((v >>> 8) & 0xFF);
    }

    /** Writes all 32 bits of {@code v}, least-significant byte first. */
    static void writeInt(DataOutput out, int v) throws IOException {
        out.write(v & 0xFF);
        out.write((v >>> 8) & 0xFF);
        out.write((v >>> 16) & 0xFF);
        out.write((v >>> 24) & 0xFF);
    }

    /** Writes all 64 bits of {@code v}, least-significant byte first. */
    static void writeLong(DataOutput out, long v) throws IOException {
        writeInt(out, (int) v);
        writeInt(out, (int) (v >>> 32));
    }

    /** Reads an unsigned little-endian 16-bit value (0..65535). */
    static int readUShort(DataInput in) throws IOException {
        int b0 = in.readUnsignedByte();
        int b1 = in.readUnsignedByte();
        return b0 | (b1 << 8);
    }

    /** Reads a little-endian 32-bit value. */
    static int readInt(DataInput in) throws IOException {
        int b0 = in.readUnsignedByte();
        int b1 = in.readUnsignedByte();
        int b2 = in.readUnsignedByte();
        int b3 = in.readUnsignedByte();
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    /** Reads a little-endian 64-bit value. */
    static long readLong(DataInput in) throws IOException {
        long lo = readInt(in) & 0xFFFFFFFFL;
        long hi = readInt(in) & 0xFFFFFFFFL;
        return lo | (hi << 32);
    }
}
