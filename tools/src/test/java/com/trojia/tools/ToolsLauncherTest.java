package com.trojia.tools;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.trojia.tools.palette.RawsPaletteGenerator;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/**
 * {@link ToolsLauncher} dispatch tests: the {@code gen-palette} subcommand writes
 * exactly the {@link RawsPaletteGenerator} document (UTF-8, byte-identical across
 * reruns), and argument/raws failures map to the documented exit codes without
 * touching the output file.
 */
class ToolsLauncherTest {

    @TempDir
    Path tempDir;

    private final ByteArrayOutputStream outBytes = new ByteArrayOutputStream();
    private final ByteArrayOutputStream errBytes = new ByteArrayOutputStream();

    private int run(String... args) {
        try (PrintStream out = new PrintStream(outBytes, true, StandardCharsets.UTF_8);
             PrintStream err = new PrintStream(errBytes, true, StandardCharsets.UTF_8)) {
            return ToolsLauncher.run(args, out, err);
        }
    }

    /** Resolves content/raws the same way the palette tests do. */
    private static Path rawsDir() {
        Path marker = Path.of("content", "raws", "materials");
        for (Path base = Path.of("").toAbsolutePath(); base != null; base = base.getParent()) {
            if (Files.isDirectory(base.resolve(marker))) {
                return base.resolve(Path.of("content", "raws"));
            }
        }
        throw new IllegalStateException("cannot locate content/raws");
    }

    @Test
    void genPaletteWritesGeneratorOutputVerbatim() throws Exception {
        Path out = tempDir.resolve("generated").resolve("materials.tsx");

        int exit = run("gen-palette", rawsDir().toString(), out.toString());

        assertEquals(0, exit, errBytes.toString(StandardCharsets.UTF_8));
        String expected = new RawsPaletteGenerator().generate(rawsDir());
        assertArrayEquals(expected.getBytes(StandardCharsets.UTF_8), Files.readAllBytes(out),
                "file content must be the generator document, byte for byte");
        assertTrue(outBytes.toString(StandardCharsets.UTF_8).contains("materials.tsx"));
    }

    @Test
    void rerunningOverwritesByteIdentically() throws Exception {
        Path out = tempDir.resolve("materials.tsx");
        assertEquals(0, run("gen-palette", rawsDir().toString(), out.toString()));
        byte[] first = Files.readAllBytes(out);
        assertEquals(0, run("gen-palette", rawsDir().toString(), out.toString()));
        assertArrayEquals(first, Files.readAllBytes(out),
                "regeneration must be byte-identical (stable tile ids)");
    }

    @Test
    void missingRawsDirectoryFailsWithExitOne() {
        Path out = tempDir.resolve("materials.tsx");
        int exit = run("gen-palette", tempDir.resolve("no-such-raws").toString(), out.toString());
        assertEquals(1, exit);
        assertFalse(Files.exists(out), "no output may be written on failure");
        assertTrue(errBytes.toString(StandardCharsets.UTF_8).contains("gen-palette"));
    }

    @Test
    void usageErrorsExitTwo() {
        assertEquals(2, run());
        assertEquals(2, run("no-such-command"));
        assertEquals(2, run("gen-palette", "only-one-arg"));
    }
}
