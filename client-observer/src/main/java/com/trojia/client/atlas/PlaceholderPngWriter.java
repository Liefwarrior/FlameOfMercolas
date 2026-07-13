package com.trojia.client.atlas;

import java.io.ByteArrayOutputStream;
import java.util.zip.Adler32;
import java.util.zip.CRC32;

/**
 * Minimal byte-deterministic PNG encoder for the placeholder debug dump
 * (TILE-ART-SPEC section 3: "byte-deterministic — no timestamps, fixed PNG encoder
 * settings"). Zero third-party deps, no AWT, no GL.
 *
 * <p>Fixed output shape, always: 8-bit RGBA (color type 6), no interlace, filter 0 on
 * every scanline, exactly three chunks (IHDR, one IDAT, IEND), and a zlib stream made
 * of <em>stored</em> (uncompressed) deflate blocks. Stored blocks sidestep the only
 * nondeterminism risk of a real compressor (library/level/version-dependent bit
 * streams): the bytes are a pure function of the pixels on every JVM. A 128&times;128
 * sheet costs ~66 KB instead of a few KB — irrelevant for a debug artifact.
 *
 * <p>Checksums (CRC-32, Adler-32) are algorithmically fixed by the PNG/zlib specs.
 */
public final class PlaceholderPngWriter {

    private static final byte[] SIGNATURE =
            {(byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    /** Largest stored-deflate block payload we emit (the format caps LEN at 65535). */
    private static final int STORED_BLOCK_MAX = 32768;

    private PlaceholderPngWriter() {
    }

    /**
     * Encodes a row-major {@code 0xAARRGGBB} buffer as a PNG.
     *
     * @param pixelsArgb pixels, index {@code y * width + x}, origin top-left — the
     *                   exact convention of {@link PlaceholderSheetRaster#pixelsArgb()}
     * @param width      image width in pixels, positive
     * @param height     image height in pixels, positive
     * @return the complete PNG file bytes; identical for identical input on every run
     * @throws IllegalArgumentException if {@code pixelsArgb} is null, a dimension is
     *                                  not positive, or the buffer length is not
     *                                  {@code width * height}
     */
    public static byte[] encodeArgb(int[] pixelsArgb, int width, int height) {
        if (pixelsArgb == null) {
            throw new IllegalArgumentException("pixelsArgb must be non-null");
        }
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("size " + width + "x" + height + " not positive");
        }
        if (pixelsArgb.length != width * height) {
            throw new IllegalArgumentException("buffer length " + pixelsArgb.length
                    + " != " + width + " * " + height);
        }

        // Raw image data: per scanline, filter byte 0 then RGBA bytes.
        byte[] raw = new byte[height * (1 + width * 4)];
        int at = 0;
        for (int y = 0; y < height; y++) {
            raw[at++] = 0; // filter: None
            for (int x = 0; x < width; x++) {
                int argb = pixelsArgb[y * width + x];
                raw[at++] = (byte) (argb >> 16); // R
                raw[at++] = (byte) (argb >> 8);  // G
                raw[at++] = (byte) argb;         // B
                raw[at++] = (byte) (argb >>> 24); // A
            }
        }

        ByteArrayOutputStream png = new ByteArrayOutputStream(raw.length + 1024);
        png.writeBytes(SIGNATURE);

        byte[] ihdr = new byte[13];
        putIntBE(ihdr, 0, width);
        putIntBE(ihdr, 4, height);
        ihdr[8] = 8;  // bit depth
        ihdr[9] = 6;  // color type: truecolor with alpha
        ihdr[10] = 0; // compression: deflate
        ihdr[11] = 0; // filter method
        ihdr[12] = 0; // interlace: none
        writeChunk(png, "IHDR", ihdr);
        writeChunk(png, "IDAT", zlibStored(raw));
        writeChunk(png, "IEND", new byte[0]);
        return png.toByteArray();
    }

    /** Wraps raw bytes in a zlib stream of stored deflate blocks. */
    private static byte[] zlibStored(byte[] raw) {
        int blocks = (raw.length + STORED_BLOCK_MAX - 1) / STORED_BLOCK_MAX;
        ByteArrayOutputStream out = new ByteArrayOutputStream(raw.length + blocks * 5 + 6);
        out.write(0x78); // CMF: deflate, 32K window
        out.write(0x01); // FLG: no dict, fastest; (0x7801 % 31 == 0 holds)
        int offset = 0;
        while (offset < raw.length) {
            int len = Math.min(STORED_BLOCK_MAX, raw.length - offset);
            boolean last = offset + len == raw.length;
            out.write(last ? 1 : 0);       // BFINAL + BTYPE 00 (stored)
            out.write(len & 0xFF);          // LEN, little-endian
            out.write((len >> 8) & 0xFF);
            out.write(~len & 0xFF);         // NLEN = one's complement of LEN
            out.write((~len >> 8) & 0xFF);
            out.write(raw, offset, len);
            offset += len;
        }
        Adler32 adler = new Adler32();
        adler.update(raw, 0, raw.length);
        long checksum = adler.getValue();
        out.write((int) (checksum >>> 24) & 0xFF);
        out.write((int) (checksum >>> 16) & 0xFF);
        out.write((int) (checksum >>> 8) & 0xFF);
        out.write((int) checksum & 0xFF);
        return out.toByteArray();
    }

    private static void writeChunk(ByteArrayOutputStream png, String type, byte[] data) {
        byte[] head = new byte[4];
        putIntBE(head, 0, data.length);
        png.writeBytes(head);
        byte[] typeBytes = new byte[]{(byte) type.charAt(0), (byte) type.charAt(1),
                (byte) type.charAt(2), (byte) type.charAt(3)};
        png.writeBytes(typeBytes);
        png.writeBytes(data);
        CRC32 crc = new CRC32();
        crc.update(typeBytes);
        crc.update(data);
        byte[] tail = new byte[4];
        putIntBE(tail, 0, (int) crc.getValue());
        png.writeBytes(tail);
    }

    private static void putIntBE(byte[] buffer, int offset, int value) {
        buffer[offset] = (byte) (value >>> 24);
        buffer[offset + 1] = (byte) (value >>> 16);
        buffer[offset + 2] = (byte) (value >>> 8);
        buffer[offset + 3] = (byte) value;
    }
}
