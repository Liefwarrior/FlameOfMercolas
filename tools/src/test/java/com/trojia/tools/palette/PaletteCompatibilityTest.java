package com.trojia.tools.palette;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.trojia.tools.tmx.TmxTileset;
import com.trojia.tools.tmx.TmxTilesetTile;
import com.trojia.tools.tmx.TsxReader;

import java.io.StringReader;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * Structural comparison of the generated palette against the committed hand-authored
 * {@code content/maps/src/materials.tsx} (the compatibility target).
 *
 * <p><strong>Contract checked:</strong> every hand-authored tile — keyed by its
 * {@code (material, form)} or {@code (fluid, depth)} identity — must exist in the
 * generated output with an identical property block (same names, declared types, and
 * values in the same order) and the identical {@code material/FORM} class label. Tile
 * <em>ids</em> are deliberately not compared: the generator's canonical order
 * (materials sorted by id) differs from the hand-authored historical order, and maps
 * are re-imported alongside a regenerated palette.</p>
 *
 * <p>Extra generated tiles (superset vocabulary, e.g. {@code oak/RAMP} or materials
 * added to the raws after the palette was hand-authored) are <em>reported</em> on
 * stdout, never a failure.</p>
 */
class PaletteCompatibilityTest {

    private static Map<String, TmxTilesetTile> handTiles;
    private static Map<String, TmxTilesetTile> generatedTiles;

    @BeforeAll
    static void parseBothPalettes() {
        List<String> warnings = new ArrayList<>();
        TsxReader reader = new TsxReader(warnings::add);

        TmxTileset hand = reader.read(PaletteTestPaths.handAuthoredTsx());
        String document = new RawsPaletteGenerator().generate(PaletteTestPaths.rawsDir());
        TmxTileset generated = reader.read(new StringReader(document));

        assertTrue(warnings.isEmpty(), "both palettes must parse without warnings: " + warnings);
        handTiles = byIdentity(hand, "hand-authored");
        generatedTiles = byIdentity(generated, "generated");
    }

    @Test
    void everyHandAuthoredTileExistsWithIdenticalProperties() {
        List<String> missing = new ArrayList<>();
        for (Map.Entry<String, TmxTilesetTile> entry : handTiles.entrySet()) {
            TmxTilesetTile generated = generatedTiles.get(entry.getKey());
            if (generated == null) {
                missing.add(entry.getKey());
                continue;
            }
            TmxTilesetTile hand = entry.getValue();
            assertEquals(hand.properties(), generated.properties(),
                    "property block must be identical for " + entry.getKey());
            assertEquals(hand.typeName(), generated.typeName(),
                    "class label must be identical for " + entry.getKey());
        }
        assertTrue(missing.isEmpty(),
                "hand-authored tiles missing from generated palette: " + missing);
    }

    @Test
    void extraGeneratedTilesAreReportedNotFailed() {
        List<String> extras = new ArrayList<>();
        for (String key : generatedTiles.keySet()) {
            if (!handTiles.containsKey(key)) {
                extras.add(key);
            }
        }
        // Report-only by design: extras are legal vocabulary the fixtures don't use yet.
        System.out.println("palette-compat: " + extras.size()
                + " generated tile(s) beyond the hand-authored set: " + extras);
        assertNotNull(extras);
    }

    /**
     * Indexes tiles by semantic identity ({@code material|form} or {@code fluid|depth}),
     * failing on tiles that carry neither contract or on duplicate identities.
     */
    private static Map<String, TmxTilesetTile> byIdentity(TmxTileset tileset, String label) {
        Map<String, TmxTilesetTile> byKey = new LinkedHashMap<>();
        for (TmxTilesetTile tile : tileset.tiles()) {
            String key;
            var material = tile.properties().find("material");
            var fluid = tile.properties().find("fluid");
            if (material.isPresent()) {
                key = material.orElseThrow().value() + "|"
                        + tile.properties().find("form").orElseThrow().value();
            } else if (fluid.isPresent()) {
                key = fluid.orElseThrow().value() + "|depth"
                        + tile.properties().find("depth").orElseThrow().asInt();
            } else {
                throw new AssertionError(label + " tile " + tile.localId()
                        + " carries neither material nor fluid properties");
            }
            TmxTilesetTile previous = byKey.put(key, tile);
            if (previous != null) {
                throw new AssertionError(label + " palette has duplicate identity " + key);
            }
        }
        return byKey;
    }
}
