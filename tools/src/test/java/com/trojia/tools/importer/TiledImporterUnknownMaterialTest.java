package com.trojia.tools.importer;

import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.material.RawsBundle;
import com.trojia.tools.tmx.TmxLayer;
import com.trojia.tools.tmx.TmxMap;
import com.trojia.tools.tmx.TmxProperties;
import com.trojia.tools.tmx.TmxProperty;
import com.trojia.tools.tmx.TmxPropertyType;
import com.trojia.tools.tmx.TmxTileLayer;
import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxTileset;
import com.trojia.tools.tmx.TmxTilesetRef;
import com.trojia.tools.tmx.TmxTilesetTile;

import java.util.List;

import org.junit.jupiter.api.Test;

/**
 * DoD3 — unknown material fails loudly. A synthetic map/tileset referencing a
 * material id that is not in the registry must abort the import with a
 * {@link TiledImportException} whose message names the offending id and offers
 * a nearest-name suggestion. Built entirely in-test — the real fixtures are
 * never corrupted.
 */
class TiledImporterUnknownMaterialTest {

    @Test
    void unknownMaterialThrowsWithSuggestion() {
        RawsBundle raws = ImporterTestSupport.raws();
        MaterialRegistry registry = raws.materials();
        TmxTileset tileset = tilesetWithMaterial("granitte"); // one edit from "granite"
        TmxMap map = singleTerrainCellMap();

        TiledImportException failure = assertThrows(TiledImportException.class,
                () -> new TiledWorldImporter().importWorld(map, tileset, registry, raws.fluids()));

        String message = failure.getMessage();
        assertTrue(message.contains("granitte"),
                () -> "message must name the offending id: " + message);
        assertTrue(message.contains("granite"),
                () -> "message must suggest the nearest real id \"granite\": " + message);
    }

    private static TmxTileset tilesetWithMaterial(String materialId) {
        TmxTilesetTile tile = new TmxTilesetTile(0, materialId + "/WALL", TmxProperties.of(List.of(
                new TmxProperty("material", TmxPropertyType.STRING, materialId),
                new TmxProperty("form", TmxPropertyType.STRING, "WALL"))));
        return new TmxTileset("synthetic", 16, 16, 1, 1, TmxProperties.empty(), List.of(tile));
    }

    private static TmxMap singleTerrainCellMap() {
        TmxTileLayer terrain = new TmxTileLayer(1, "terrain", 1, 1, new int[] {1}, TmxProperties.empty());
        TmxTileLayer floor = new TmxTileLayer(2, "floor", 1, 1, new int[] {0}, TmxProperties.empty());
        TmxLayerGroup group = new TmxLayerGroup(3, "z:+0", List.of(terrain, floor), TmxProperties.empty());
        return new TmxMap(1, 1, 16, 16, "orthogonal", "right-down",
                List.of(new TmxTilesetRef(1, "materials.tsx")),
                List.<TmxLayer>of(group), TmxProperties.empty());
    }
}
