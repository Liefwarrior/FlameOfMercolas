package com.trojia.sim.world.io;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.TreeMap;
import java.util.zip.CRC32C;
import java.util.zip.DataFormatException;
import java.util.zip.Deflater;
import java.util.zip.Inflater;

/**
 * The one save container (ARCHITECTURE.md §9): little-endian header (magic,
 * format version, worldSeed, tick, rawsFingerprint) + TOC + Deflate-1
 * compressed sections with CRC32C. Saves are legal only at TICK_END; writes
 * are atomic (tmp + rename). The Tiled importer emits this format at tick 0.
 *
 * <p>Load rules: header rawsFingerprint mismatch is a hard fail; a missing
 * section is system-default init only if the section was declared optional,
 * otherwise a hard fail. Section content is opaque bytes here — each owner
 * (world codec, systems via {@code serialize}/{@code load}) defines its own
 * layout and versioning.
 *
 * <p><b>File layout v1</b> (all little-endian): 36-byte header {@code [u32
 * magic · u32 formatVersion · u64 worldSeed · u64 tick · u64 rawsFingerprint ·
 * u32 sectionCount]}, then {@code sectionCount} 32-byte TOC entries {@code
 * [4×u8 id · u64 offset · u64 compressedLen · u64 uncompressedLen · u32
 * crc32c]} sorted ascending by id, then the section blobs in TOC order. The
 * CRC32C covers the <em>uncompressed</em> content and is verified lazily on
 * {@link #section} access. Sorting sections by id makes {@link #writeTo}
 * byte-deterministic regardless of {@link #putSection} order.
 */
public final class TrojSav {

    /** File magic, little-endian "TROJ". */
    public static final int MAGIC = 0x4A4F5254;

    /** Container format version written by this build. */
    public static final int FORMAT_VERSION = 1;

    /** Section: world metadata (dimensions, lane registry, site defs). */
    public static final String META = "META";
    /** Section: the input log (drained commands + scripted actions). */
    public static final String INPT = "INPT";
    /** Section: event-bus carry-over lap. */
    public static final String EVNT = "EVNT";
    /** Section: world lanes + overlays via {@link ChunkCodec}, canonical chunk order. */
    public static final String WRLD = "WRLD";
    /** Section: change-log carry-over (unconsumed entry tails + reader cursors), engine-owned. */
    public static final String CHNG = "CHNG";
    /**
     * Section: fluid system state (frontiers, quiet counters, ledger). Not
     * name-derivable from {@code "fluids"} — the owning system must pin it via
     * {@code SystemId.of("fluids", TrojSav.FLUD)}.
     */
    public static final String FLUD = "FLUD";
    /**
     * Section: thermal/fire state (frontiers, residual carry, burn table). Not
     * name-derivable from {@code "thermal"} — the owning system must pin it via
     * {@code SystemId.of("thermal", TrojSav.THRM)}.
     */
    public static final String THRM = "THRM";
    /** Section: reaction state (wear counters, charge buffers). */
    public static final String REAC = "REAC";
    /**
     * Section: light state (in-flight queues as packed longs, source registry).
     * Not name-derivable from {@code "light"} — the owning system must pin it
     * via {@code SystemId.of("light", TrojSav.LGHT)}.
     */
    public static final String LGHT = "LGHT";
    /**
     * Section: bubble state (tickets, summaries, journals, grace deadlines).
     * Not name-derivable from {@code "bubble"} — the owning system must pin it
     * via {@code SystemId.of("bubble", TrojSav.BUBL)}.
     */
    public static final String BUBL = "BUBL";
    /** Section: macro economy state (ledgers, scheduler wheel, incidents). */
    public static final String ECON = "ECON";
    /** Section: reserved-empty for the aether field. */
    public static final String AETH = "AETH";

    private static final int HEADER_BYTES = 4 + 4 + 8 + 8 + 8 + 4;
    private static final int TOC_ENTRY_BYTES = 4 + 8 + 8 + 8 + 4;
    private static final int DEFLATE_LEVEL = 1;

    /**
     * The fixed-size container header. {@code rawsFingerprint} is the hash of
     * the loaded raws set; a mismatch on load is a hard fail (goldens are
     * meaningless across raws changes).
     *
     * @param formatVersion   container format version
     * @param worldSeed       the world seed — the only persisted RNG state
     * @param tick            the tick the save was taken at (TICK_END boundary)
     * @param rawsFingerprint fingerprint of the raws content the save was made with
     */
    public record Header(int formatVersion, long worldSeed, long tick, long rawsFingerprint) {
    }

    /** A section read from disk, still compressed; inflated + CRC-checked on first access. */
    private record RawSection(byte[] compressed, long uncompressedLen, int crc32c) {
    }

    private final Header header;
    /** Uncompressed section content: everything put by callers or already inflated. */
    private final TreeMap<String, byte[]> plain = new TreeMap<>();
    /** Sections read from disk and not yet inflated. Disjoint from {@link #plain}. */
    private final TreeMap<String, RawSection> raw = new TreeMap<>();

    private TrojSav(Header header) {
        this.header = header;
    }

    /** An empty container for writing, carrying {@code header}. */
    public static TrojSav create(Header header) {
        if (header == null) {
            throw new IllegalArgumentException("header must be non-null");
        }
        if (header.formatVersion() != FORMAT_VERSION) {
            throw new IllegalArgumentException("this build writes format version "
                    + FORMAT_VERSION + ", not " + header.formatVersion());
        }
        return new TrojSav(header);
    }

    /**
     * Reads a container: validates magic, format version and per-section
     * CRC32C lazily on {@link #section} access.
     */
    public static TrojSav read(Path file) throws IOException {
        byte[] bytes = Files.readAllBytes(file);
        if (bytes.length < HEADER_BYTES) {
            throw new IOException("truncated TROJSAV header: " + file);
        }
        ByteBuffer buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        int magic = buf.getInt();
        if (magic != MAGIC) {
            throw new IOException(String.format("not a TROJSAV (magic 0x%08X): %s", magic, file));
        }
        int formatVersion = buf.getInt();
        if (formatVersion != FORMAT_VERSION) {
            throw new IOException("unsupported TROJSAV format version " + formatVersion
                    + " (this build reads " + FORMAT_VERSION + "): " + file);
        }
        long worldSeed = buf.getLong();
        long tick = buf.getLong();
        long rawsFingerprint = buf.getLong();
        int sectionCount = buf.getInt();
        if (sectionCount < 0
                || HEADER_BYTES + (long) sectionCount * TOC_ENTRY_BYTES > bytes.length) {
            throw new IOException("corrupt TROJSAV TOC (" + sectionCount + " sections): " + file);
        }
        TrojSav sav = new TrojSav(new Header(formatVersion, worldSeed, tick, rawsFingerprint));
        byte[] idBytes = new byte[4];
        for (int i = 0; i < sectionCount; i++) {
            buf.get(idBytes);
            String id = new String(idBytes, StandardCharsets.US_ASCII);
            requireValidId(id);
            long offset = buf.getLong();
            long compressedLen = buf.getLong();
            long uncompressedLen = buf.getLong();
            int crc = buf.getInt();
            if (offset < 0 || compressedLen < 0 || uncompressedLen < 0
                    || offset + compressedLen > bytes.length) {
                throw new IOException("section '" + id + "' extends past end of file: " + file);
            }
            byte[] compressed = Arrays.copyOfRange(bytes, (int) offset, (int) (offset + compressedLen));
            sav.raw.put(id, new RawSection(compressed, uncompressedLen, crc));
        }
        return sav;
    }

    /** This container's header. */
    public Header header() {
        return header;
    }

    /** Whether a section with 4-char id {@code sectionId} is present. */
    public boolean hasSection(String sectionId) {
        return plain.containsKey(sectionId) || raw.containsKey(sectionId);
    }

    /**
     * The decompressed, CRC-verified content of {@code sectionId}. The
     * returned array is the container's cached copy — borrowed, do not mutate.
     *
     * @throws IOException on CRC mismatch or absent section
     */
    public byte[] section(String sectionId) throws IOException {
        byte[] content = plain.get(sectionId);
        if (content != null) {
            return content;
        }
        RawSection pending = raw.get(sectionId);
        if (pending == null) {
            throw new IOException("TROJSAV section '" + sectionId + "' is absent");
        }
        content = inflate(pending, sectionId);
        plain.put(sectionId, content);
        raw.remove(sectionId);
        return content;
    }

    /** Adds/replaces a section (content is compressed at {@link #writeTo} time). */
    public void putSection(String sectionId, byte[] content) {
        requireValidId(sectionId);
        if (content == null) {
            throw new IllegalArgumentException("section content must be non-null (may be empty)");
        }
        plain.put(sectionId, content.clone());
        raw.remove(sectionId);
    }

    /** Writes the container atomically (tmp + rename). Byte-deterministic. */
    public void writeTo(Path file) throws IOException {
        // Inflate any still-compressed sections so every section takes the
        // same deterministic content -> Deflate-1 path.
        for (String id : List.copyOf(raw.keySet())) {
            section(id);
        }
        int count = plain.size();
        List<String> ids = new ArrayList<>(plain.keySet());
        byte[][] blobs = new byte[count][];
        int[] crcs = new int[count];
        long totalBlobBytes = 0;
        for (int i = 0; i < count; i++) {
            byte[] content = plain.get(ids.get(i));
            CRC32C crc = new CRC32C();
            crc.update(content, 0, content.length);
            crcs[i] = (int) crc.getValue();
            blobs[i] = deflate(content);
            totalBlobBytes += blobs[i].length;
        }
        long total = HEADER_BYTES + (long) count * TOC_ENTRY_BYTES + totalBlobBytes;
        if (total > Integer.MAX_VALUE) {
            throw new IOException("TROJSAV too large: " + total + " bytes");
        }
        ByteBuffer out = ByteBuffer.allocate((int) total).order(ByteOrder.LITTLE_ENDIAN);
        out.putInt(MAGIC);
        out.putInt(header.formatVersion());
        out.putLong(header.worldSeed());
        out.putLong(header.tick());
        out.putLong(header.rawsFingerprint());
        out.putInt(count);
        long offset = HEADER_BYTES + (long) count * TOC_ENTRY_BYTES;
        for (int i = 0; i < count; i++) {
            out.put(ids.get(i).getBytes(StandardCharsets.US_ASCII));
            out.putLong(offset);
            out.putLong(blobs[i].length);
            out.putLong(plain.get(ids.get(i)).length);
            out.putInt(crcs[i]);
            offset += blobs[i].length;
        }
        for (int i = 0; i < count; i++) {
            out.put(blobs[i]);
        }
        Path parent = file.toAbsolutePath().getParent();
        if (parent != null) {
            Files.createDirectories(parent);
        }
        Path tmp = file.toAbsolutePath().resolveSibling(file.getFileName() + ".tmp");
        Files.write(tmp, out.array());
        try {
            Files.move(tmp, file.toAbsolutePath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (AtomicMoveNotSupportedException e) {
            Files.move(tmp, file.toAbsolutePath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private static byte[] deflate(byte[] content) {
        Deflater deflater = new Deflater(DEFLATE_LEVEL);
        try {
            deflater.setInput(content);
            deflater.finish();
            ByteArrayOutputStream out = new ByteArrayOutputStream(Math.max(64, content.length / 4));
            byte[] chunk = new byte[8192];
            while (!deflater.finished()) {
                int n = deflater.deflate(chunk);
                out.write(chunk, 0, n);
            }
            return out.toByteArray();
        } finally {
            deflater.end();
        }
    }

    private static byte[] inflate(RawSection section, String id) throws IOException {
        if (section.uncompressedLen() > Integer.MAX_VALUE) {
            throw new IOException("section '" + id + "' too large: " + section.uncompressedLen());
        }
        byte[] out = new byte[(int) section.uncompressedLen()];
        Inflater inflater = new Inflater();
        try {
            inflater.setInput(section.compressed());
            int off = 0;
            while (off < out.length) {
                int n = inflater.inflate(out, off, out.length - off);
                if (n == 0) {
                    throw new IOException("section '" + id + "' is truncated or corrupt");
                }
                off += n;
            }
            if (!inflater.finished() && inflater.inflate(new byte[1], 0, 1) != 0) {
                throw new IOException("section '" + id + "' has trailing data");
            }
        } catch (DataFormatException e) {
            throw new IOException("section '" + id + "' is corrupt: " + e.getMessage(), e);
        } finally {
            inflater.end();
        }
        CRC32C crc = new CRC32C();
        crc.update(out, 0, out.length);
        if ((int) crc.getValue() != section.crc32c()) {
            throw new IOException("section '" + id + "' CRC32C mismatch");
        }
        return out;
    }

    private static void requireValidId(String sectionId) {
        if (sectionId == null || sectionId.length() != 4) {
            throw new IllegalArgumentException("section id must be exactly 4 chars: " + sectionId);
        }
        for (int i = 0; i < 4; i++) {
            char c = sectionId.charAt(i);
            if (c < 0x20 || c > 0x7E) {
                throw new IllegalArgumentException("section id must be printable ASCII: " + sectionId);
            }
        }
    }
}
