package com.trojia.sim.world.io;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Random;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Container round-trip, determinism, atomicity and corruption-detection tests
 * for {@link TrojSav}.
 */
final class TrojSavTest {

    @TempDir
    Path dir;

    private static TrojSav.Header header() {
        return new TrojSav.Header(TrojSav.FORMAT_VERSION, 0xDEADBEEFL, 1234L, 0xFEEDFACEL);
    }

    private static byte[] randomBytes(long seed, int length) {
        byte[] bytes = new byte[length];
        new Random(seed).nextBytes(bytes);
        return bytes;
    }

    @Test
    void roundTripsHeaderAndSections() throws IOException {
        TrojSav out = TrojSav.create(header());
        byte[] meta = randomBytes(1, 300);
        byte[] wrld = randomBytes(2, 200_000);
        out.putSection(TrojSav.META, meta);
        out.putSection(TrojSav.WRLD, wrld);
        out.putSection(TrojSav.AETH, new byte[0]); // reserved-empty section
        Path file = dir.resolve("world.trojsav");
        out.writeTo(file);

        TrojSav in = TrojSav.read(file);

        assertEquals(TrojSav.FORMAT_VERSION, in.header().formatVersion());
        assertEquals(0xDEADBEEFL, in.header().worldSeed());
        assertEquals(1234L, in.header().tick());
        assertEquals(0xFEEDFACEL, in.header().rawsFingerprint());
        assertTrue(in.hasSection(TrojSav.META));
        assertTrue(in.hasSection(TrojSav.AETH));
        assertFalse(in.hasSection(TrojSav.ECON));
        assertArrayEquals(meta, in.section(TrojSav.META));
        assertArrayEquals(wrld, in.section(TrojSav.WRLD));
        assertEquals(0, in.section(TrojSav.AETH).length);
    }

    @Test
    void writeIsByteDeterministicAndPutOrderIndependent() throws IOException {
        TrojSav a = TrojSav.create(header());
        a.putSection(TrojSav.META, randomBytes(1, 500));
        a.putSection(TrojSav.WRLD, randomBytes(2, 5000));
        a.putSection(TrojSav.EVNT, randomBytes(3, 50));

        TrojSav b = TrojSav.create(header());
        b.putSection(TrojSav.EVNT, randomBytes(3, 50));
        b.putSection(TrojSav.WRLD, randomBytes(2, 5000));
        b.putSection(TrojSav.META, randomBytes(1, 500));

        Path fileA = dir.resolve("a.trojsav");
        Path fileB = dir.resolve("b.trojsav");
        a.writeTo(fileA);
        b.writeTo(fileB);

        assertArrayEquals(Files.readAllBytes(fileA), Files.readAllBytes(fileB));
    }

    @Test
    void missingSectionIsAnIoError() throws IOException {
        TrojSav out = TrojSav.create(header());
        out.putSection(TrojSav.META, randomBytes(1, 10));
        Path file = dir.resolve("world.trojsav");
        out.writeTo(file);

        TrojSav in = TrojSav.read(file);

        assertThrows(IOException.class, () -> in.section(TrojSav.WRLD));
    }

    @Test
    void corruptedSectionBytesFailTheCrcCheck() throws IOException {
        TrojSav out = TrojSav.create(header());
        out.putSection(TrojSav.WRLD, randomBytes(7, 10_000));
        Path file = dir.resolve("world.trojsav");
        out.writeTo(file);

        byte[] bytes = Files.readAllBytes(file);
        bytes[bytes.length - 1] ^= 0x55; // flip a bit inside the last section blob
        Files.write(file, bytes);

        TrojSav in = TrojSav.read(file); // header + TOC still parse
        assertThrows(IOException.class, () -> in.section(TrojSav.WRLD));
    }

    @Test
    void badMagicIsAHardFailAtRead() throws IOException {
        TrojSav out = TrojSav.create(header());
        out.putSection(TrojSav.META, randomBytes(1, 10));
        Path file = dir.resolve("world.trojsav");
        out.writeTo(file);

        byte[] bytes = Files.readAllBytes(file);
        bytes[0] ^= 0x01;
        Files.write(file, bytes);

        assertThrows(IOException.class, () -> TrojSav.read(file));
    }

    @Test
    void unsupportedFormatVersionIsAHardFailAtRead() throws IOException {
        TrojSav out = TrojSav.create(header());
        out.putSection(TrojSav.META, randomBytes(1, 10));
        Path file = dir.resolve("world.trojsav");
        out.writeTo(file);

        byte[] bytes = Files.readAllBytes(file);
        bytes[4] = (byte) (TrojSav.FORMAT_VERSION + 1); // little-endian version field
        Files.write(file, bytes);

        assertThrows(IOException.class, () -> TrojSav.read(file));
    }

    @Test
    void putSectionReplacesEarlierContent() throws IOException {
        TrojSav out = TrojSav.create(header());
        out.putSection(TrojSav.META, randomBytes(1, 100));
        byte[] latest = randomBytes(2, 60);
        out.putSection(TrojSav.META, latest);
        Path file = dir.resolve("world.trojsav");
        out.writeTo(file);

        assertArrayEquals(latest, TrojSav.read(file).section(TrojSav.META));
    }

    @Test
    void readModifyWritePreservesUntouchedSections() throws IOException {
        TrojSav out = TrojSav.create(header());
        byte[] wrld = randomBytes(11, 40_000);
        out.putSection(TrojSav.WRLD, wrld);
        out.putSection(TrojSav.META, randomBytes(12, 80));
        Path file = dir.resolve("world.trojsav");
        out.writeTo(file);

        TrojSav reread = TrojSav.read(file);
        byte[] newMeta = randomBytes(13, 90);
        reread.putSection(TrojSav.META, newMeta); // WRLD never touched (stays compressed)
        Path file2 = dir.resolve("world2.trojsav");
        reread.writeTo(file2);

        TrojSav result = TrojSav.read(file2);
        assertArrayEquals(wrld, result.section(TrojSav.WRLD));
        assertArrayEquals(newMeta, result.section(TrojSav.META));
    }

    @Test
    void writeLeavesNoTmpFileBehindAndReplacesExisting() throws IOException {
        Path file = dir.resolve("world.trojsav");
        TrojSav first = TrojSav.create(header());
        first.putSection(TrojSav.META, randomBytes(1, 10));
        first.writeTo(file);

        TrojSav second = TrojSav.create(header());
        byte[] latest = randomBytes(2, 20);
        second.putSection(TrojSav.META, latest);
        second.writeTo(file); // atomic replace of an existing save

        assertFalse(Files.exists(dir.resolve("world.trojsav.tmp")));
        assertArrayEquals(latest, TrojSav.read(file).section(TrojSav.META));
    }

    @Test
    void createRejectsForeignFormatVersionsAndBadIds() {
        assertThrows(IllegalArgumentException.class,
                () -> TrojSav.create(new TrojSav.Header(99, 0, 0, 0)));
        TrojSav sav = TrojSav.create(header());
        assertThrows(IllegalArgumentException.class, () -> sav.putSection("TOOLONG", new byte[0]));
        assertThrows(IllegalArgumentException.class, () -> sav.putSection(null, new byte[0]));
        assertThrows(IllegalArgumentException.class, () -> sav.putSection(TrojSav.META, null));
    }
}
