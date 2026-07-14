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
 * The compound-block bake step, the twin of {@link TavernFixtureBakeTest}: regenerates the
 * committed {@code content/maps/baked/compound_block.trojsav} from the hand-authored
 * {@code content/maps/src/compound_block.tmx} on every test run via the same {@code tools}
 * importer path (parse map + tileset, load raws, {@link TiledWorldImporter#importWorld},
 * {@link TiledWorldImporter#toTrojSav}, write).
 *
 * <p>{@link TrojSav#writeTo} is byte-deterministic for identical map/raws content, so
 * rerunning this test is a no-op diff unless the fixture or the material raws changed —
 * the intended way to keep the baked file fresh. Only {@link FixtureWorldLoader} reads the
 * output at runtime, and it never depends on {@code tools} (test-only dependency).
 *
 * <p>Also the compound population stage's bake check: the reloaded world's dimensions are
 * asserted against values derived independently from the map itself (footprint + z-group
 * count), not hardcoded.
 */
class CompoundBlockBakeTest {

    @Test
    void bakesTheCompoundBlockAndReloadsWithMapDerivedDimensions() throws IOException {
        Path mapsDir = RepoPaths.locate("content", "maps", "src");
        List<String> warnings = new ArrayList<>();

        TmxMap map = new TmxReader(warnings::add).read(mapsDir.resolve("compound_block.tmx"));
        TmxTilesetRef ref = map.tilesets().get(0);
        TmxTileset tileset = new TsxReader(warnings::add).read(mapsDir.resolve(ref.source()));

        RawsBundle raws = MaterialRawsLoader.load(RepoPaths.locate("content", "raws"));
        MaterialRegistry registry = raws.materials();

        TiledWorldImporter importer = new TiledWorldImporter();
        TickableWorld baked = importer.importWorld(map, tileset, registry, raws.fluids());

        Path outFile = RepoPaths.locate("content", "maps")
                .resolve("baked").resolve(FixtureWorldLoader.COMPOUND_FILE);
        importer.toTrojSav(baked, registry.fingerprint()).writeTo(outFile);

        TickableWorld reloaded = new WorldLoader().load(TrojSav.read(outFile));

        int interiorChunksX = ceilDiv(map.width(), Coords.CHUNK_SIZE_X);
        int interiorChunksY = ceilDiv(map.height(), Coords.CHUNK_SIZE_Y);
        int zLevelCount = countZGroups(map);
        int interiorChunksZ = ceilDiv(zSpan(map), Coords.CHUNK_SIZE_Z);
        WorldConfig expected = new WorldConfig(
                interiorChunksX + 2, interiorChunksY + 2, interiorChunksZ + 2);

        assertEquals(expected, reloaded.config(),
                "baked world dimensions must match the authored map's footprint and z-group span");
        assertEquals(3, zLevelCount,
                "content/maps/README.md documents compound_block.tmx as three z-levels (+0/+1/+2)");
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

    /**
     * {@code maxZ - minZ + 1} over every z-group's parsed signed value — the same span
     * {@link TiledWorldImporter#importWorld} itself derives {@code interiorChunksZ} from.
     * Deliberately NOT a raw z-group count ({@link #countZGroups}): the importer never
     * requires z-groups to be contiguous, so a count and a min/max span only agree when the
     * authored z-groups happen to have no gaps. Computing the same span here (instead of a
     * count) keeps this test's "independently derived" dimension check honest even if a
     * future fixture ever authors a gap.
     */
    private static int zSpan(TmxMap map) {
        int min = Integer.MAX_VALUE;
        int max = Integer.MIN_VALUE;
        for (TmxLayer layer : map.layers()) {
            if (layer instanceof TmxLayerGroup group && group.name().matches("z:[+-]\\d+")) {
                int z = Integer.parseInt(group.name().substring("z:".length()));
                min = Math.min(min, z);
                max = Math.max(max, z);
            }
        }
        return max - min + 1;
    }

    private static int ceilDiv(int a, int b) {
        return (a + b - 1) / b;
    }
}
