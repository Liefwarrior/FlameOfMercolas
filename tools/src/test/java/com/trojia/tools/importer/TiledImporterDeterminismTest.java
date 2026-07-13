package com.trojia.tools.importer;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.world.TickableWorld;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/**
 * DoD2 — byte-determinism (M1 acceptance). Importing the same fixture twice
 * yields byte-identical TROJSAV output; the whole engine's determinism
 * guarantee rests on this.
 */
class TiledImporterDeterminismTest {

    @Test
    void twoImportsAreByteIdentical(@TempDir Path tmp) throws IOException {
        ImporterTestSupport.Fixture fixture = ImporterTestSupport.tavernFixture();
        MaterialRegistry registry = ImporterTestSupport.raws().materials();
        TiledWorldImporter importer = new TiledWorldImporter();

        Path first = tmp.resolve("first.trojsav");
        Path second = tmp.resolve("second.trojsav");

        TickableWorld worldA = importer.importWorld(fixture.map(), fixture.tileset(), registry);
        importer.toTrojSav(worldA, registry.fingerprint()).writeTo(first);

        TickableWorld worldB = importer.importWorld(fixture.map(), fixture.tileset(), registry);
        importer.toTrojSav(worldB, registry.fingerprint()).writeTo(second);

        byte[] bytesA = Files.readAllBytes(first);
        byte[] bytesB = Files.readAllBytes(second);
        assertTrue(bytesA.length > 0, "import produced no bytes");
        assertArrayEquals(bytesA, bytesB,
                "importing the same map twice must yield byte-identical TROJSAV output");
    }
}
