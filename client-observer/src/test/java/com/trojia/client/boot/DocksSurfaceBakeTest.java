package com.trojia.client.boot;

import com.trojia.sim.material.MaterialRawsLoader;
import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.material.RawsBundle;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.WorldConfig;
import com.trojia.sim.world.io.TrojSav;
import com.trojia.sim.world.io.WorldLoader;
import com.trojia.tools.importer.TiledWorldImporter;
import com.trojia.tools.tmx.TmxLayer;
import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxMap;
import com.trojia.tools.tmx.TmxReader;
import com.trojia.tools.tmx.TmxTileset;
import com.trojia.tools.tmx.TmxTilesetRef;
import com.trojia.tools.tmx.TsxReader;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * The Docks-ward bake step, the twin of {@link CompoundBlockBakeTest}: regenerates the
 * committed {@code content/maps/baked/docks_surface.trojsav} from the generated
 * {@code content/maps/src/docks_surface.tmx} on every test run via the same {@code tools}
 * importer path (parse map + tileset, load raws, {@link TiledWorldImporter#importWorld},
 * {@link TiledWorldImporter#toTrojSav}, write).
 *
 * <p>{@link TrojSav#writeTo} is byte-deterministic for identical map/raws content, so
 * rerunning this test is a no-op diff unless the fixture or the material raws changed —
 * the intended way to keep the baked file fresh. Only {@link FixtureWorldLoader} reads the
 * output at runtime, and it never depends on {@code tools} (test-only dependency).
 *
 * <p>Also the docks population stage's bake check: the reloaded world's dimensions are
 * asserted against values derived independently from the map itself (footprint + z-group
 * count), not hardcoded.
 */
class DocksSurfaceBakeTest {

    @Test
    void bakesTheDocksWardAndReloadsWithMapDerivedDimensions() throws IOException {
        Path mapsDir = RepoPaths.locate("content", "maps", "src");
        List<String> warnings = new ArrayList<>();

        TmxMap map = new TmxReader(warnings::add).read(mapsDir.resolve("docks_surface.tmx"));
        TmxTilesetRef ref = map.tilesets().get(0);
        TmxTileset tileset = new TsxReader(warnings::add).read(mapsDir.resolve(ref.source()));

        RawsBundle raws = MaterialRawsLoader.load(RepoPaths.locate("content", "raws"));
        MaterialRegistry registry = raws.materials();

        TiledWorldImporter importer = new TiledWorldImporter();
        TickableWorld baked = importer.importWorld(map, tileset, registry, raws.fluids());

        Path outFile = RepoPaths.locate("content", "maps")
                .resolve("baked").resolve(FixtureWorldLoader.DOCKS_FILE);
        importer.toTrojSav(baked, registry.fingerprint()).writeTo(outFile);

        TickableWorld reloaded = new WorldLoader().load(TrojSav.read(outFile));

        int interiorChunksX = ceilDiv(map.width(), Coords.CHUNK_SIZE_X);
        int interiorChunksY = ceilDiv(map.height(), Coords.CHUNK_SIZE_Y);
        int zLevelCount = countZGroups(map);
        int interiorChunksZ = ceilDiv(zLevelCount, Coords.CHUNK_SIZE_Z);
        WorldConfig expected = new WorldConfig(
                interiorChunksX + 2, interiorChunksY + 2, interiorChunksZ + 2);

        assertEquals(expected, reloaded.config(),
                "baked world dimensions must match the authored map's footprint and z-group count");
        assertEquals(16, zLevelCount,
                "content/maps/README.md documents docks_surface.tmx as sixteen z-levels (+0..+15)");
    }

    private static int countZGroups(TmxMap map) {
        int count = 0;
        for (TmxLayer layer : map.layers()) {
            if (layer instanceof TmxLayerGroup group && group.name().matches("z:[+-]\\d+")) {
                count++;
            }
        }
        return count;
    }

    private static int ceilDiv(int a, int b) {
        return (a + b - 1) / b;
    }
}
