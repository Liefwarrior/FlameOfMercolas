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
 * CLI contract of {@link ToolsLauncher}: {@code check-map} / {@code check-raws} /
 * {@code gen-palette} dispatch with exit codes 0 (success, warnings allowed),
 * 1 (command failed), 2 (usage error). {@code gen-palette} writes exactly the
 * {@link RawsPaletteGenerator} document (UTF-8, byte-identical across reruns)
 * and never leaves partial output on failure.
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

    private static Path mapsDir() {
        Path rel = Path.of("content", "maps", "src");
        for (Path base = Path.of("").toAbsolutePath(); base != null; base = base.getParent()) {
            if (Files.isRegularFile(base.resolve(rel).resolve("tavern_fixture.tmx"))) {
                return base.resolve(rel);
            }
        }
        throw new IllegalStateException("cannot locate content/maps/src");
    }

    private static Path rawsDir() {
        Path rel = Path.of("content", "raws");
        for (Path base = Path.of("").toAbsolutePath(); base != null; base = base.getParent()) {
            if (Files.isDirectory(base.resolve(rel).resolve("materials"))) {
                return base.resolve(rel);
            }
        }
        throw new IllegalStateException("cannot locate content/raws");
    }

    // ------------------------------------------------------------ check-map

    @Test
    void checkMapPassesOnTavernFixture() {
        int code = run("check-map", mapsDir().resolve("tavern_fixture.tmx").toString());
        assertEquals(0, code, () -> outBytes + " / " + errBytes);
        assertTrue(outBytes.toString(StandardCharsets.UTF_8).contains("OK"));
    }

    @Test
    void checkMapFailsOnBrokenMap() throws Exception {
        Files.writeString(tempDir.resolve("test.tsx"), """
                <?xml version="1.0" encoding="UTF-8"?>
                <tileset version="1.10" name="test" tilewidth="16" tileheight="16" tilecount="1" columns="0">
                 <tile id="0"><properties>
                  <property name="material" value="granite"/><property name="form" value="WALL"/>
                 </properties></tile>
                </tileset>
                """);
        Files.writeString(tempDir.resolve("broken.tmx"), """
                <?xml version="1.0" encoding="UTF-8"?>
                <map version="1.10" orientation="orthogonal" width="4" height="4" tilewidth="16" tileheight="16">
                 <tileset firstgid="1" source="test.tsx"/>
                 <group name="cellar">
                  <layer name="terrain" width="4" height="4"><data encoding="csv">
                   1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1</data></layer>
                  <layer name="floor" width="4" height="4"><data encoding="csv">
                   0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0</data></layer>
                  <objectgroup name="markers"/>
                 </group>
                </map>
                """);
        int code = run("check-map", tempDir.resolve("broken.tmx").toString(),
                "--raws", rawsDir().toString());
        assertEquals(1, code, () -> outBytes + " / " + errBytes);
        String out = outBytes.toString(StandardCharsets.UTF_8);
        assertTrue(out.contains("[z-groups]"), out);
        assertTrue(out.contains("FAIL"), out);
    }

    @Test
    void missingMapFileExitsOne() {
        assertEquals(1, run("check-map", tempDir.resolve("nope.tmx").toString()));
        assertTrue(errBytes.toString(StandardCharsets.UTF_8).contains("no such map file"));
    }

    // ----------------------------------------------------------- check-raws

    @Test
    void checkRawsPassesOnCommittedRaws() {
        int code = run("check-raws", rawsDir().toString());
        assertEquals(0, code, () -> outBytes + " / " + errBytes);
        assertTrue(outBytes.toString(StandardCharsets.UTF_8).contains("OK"));
    }

    // ---------------------------------------------------------- gen-palette

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

    // ---------------------------------------------------------- usage errors

    @Test
    void usageErrorsExitTwo() {
        assertEquals(2, run());
        assertEquals(2, run("no-such-command"));
        assertEquals(2, run("gen-palette", "only-one-arg"));
        assertEquals(2, run("check-map"));
    }

    @Test
    void noArgumentsPrintsUsageListingAllCommands() {
        assertEquals(2, run());
        String usage = errBytes.toString(StandardCharsets.UTF_8);
        assertTrue(usage.contains("check-map"), usage);
        assertTrue(usage.contains("check-raws"), usage);
        assertTrue(usage.contains("gen-palette"), usage);
    }
}
