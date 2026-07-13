package com.trojia.tools.palette;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.trojia.tools.tmx.TmxPropertyType;
import com.trojia.tools.tmx.TmxTileset;
import com.trojia.tools.tmx.TmxTilesetTile;
import com.trojia.tools.tmx.TsxReader;

import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import org.junit.jupiter.api.Test;

/**
 * {@link RawsPaletteGenerator} contract tests against the committed
 * {@code content/raws}: determinism (regenerating twice is byte-identical), canonical
 * ordering (materials by id, forms in enum order, fluids by id with descending
 * depths), and round-trip parseability with the production {@link TsxReader}.
 */
class RawsPaletteGeneratorTest {

    private static final Path RAWS = PaletteTestPaths.rawsDir();

    @Test
    void regeneratingTwiceIsByteIdentical() {
        String first = new RawsPaletteGenerator().generate(RAWS);
        String second = new RawsPaletteGenerator().generate(RAWS);
        assertEquals(first, second, "generate() must be a pure function of the raws");
        assertArrayEquals(first.getBytes(StandardCharsets.UTF_8),
                second.getBytes(StandardCharsets.UTF_8),
                "encoded output must be byte-identical across regeneration");
        assertEquals(new RawsPaletteGenerator().plan(RAWS), new RawsPaletteGenerator().plan(RAWS),
                "plan() must return the identical tile sequence");
    }

    @Test
    void tileSequenceIsCanonicallyOrdered() {
        List<PaletteTile> tiles = new RawsPaletteGenerator().plan(RAWS);

        int firstFluid = tiles.size();
        for (int i = 0; i < tiles.size(); i++) {
            if (tiles.get(i) instanceof PaletteTile.FluidTile) {
                firstFluid = i;
                break;
            }
        }
        List<PaletteTile> materialTiles = tiles.subList(0, firstFluid);
        List<PaletteTile> fluidTiles = tiles.subList(firstFluid, tiles.size());
        assertTrue(fluidTiles.stream().allMatch(t -> t instanceof PaletteTile.FluidTile),
                "all material tiles must precede all fluid tiles");

        String prevMaterial = "";
        int prevOrdinal = -1;
        for (PaletteTile tile : materialTiles) {
            PaletteTile.MaterialTile m = (PaletteTile.MaterialTile) tile;
            int cmp = m.materialId().compareTo(prevMaterial);
            assertTrue(cmp >= 0, "materials must be sorted by id: " + m.materialId()
                    + " after " + prevMaterial);
            if (cmp > 0) {
                prevOrdinal = -1;
            }
            assertTrue(m.form().ordinal() > prevOrdinal,
                    "forms must follow enum order within " + m.materialId() + ": " + m.form());
            prevMaterial = m.materialId();
            prevOrdinal = m.form().ordinal();
        }

        String prevFluid = "";
        List<Integer> depths = new ArrayList<>();
        for (PaletteTile tile : fluidTiles) {
            PaletteTile.FluidTile f = (PaletteTile.FluidTile) tile;
            if (!f.fluidId().equals(prevFluid)) {
                assertTrue(f.fluidId().compareTo(prevFluid) > 0, "fluids must be sorted by id");
                depths.clear();
            }
            depths.add(f.depth());
            prevFluid = f.fluidId();
        }
        assertEquals(List.of(7, 4, 2), depths, "depth variants must be 7, 4, 2 per fluid");
    }

    @Test
    void includesTreatmentMintedMaterialAndNeverOpen() {
        List<PaletteTile> tiles = new RawsPaletteGenerator().plan(RAWS);
        assertTrue(tiles.stream().anyMatch(t -> t instanceof PaletteTile.MaterialTile m
                        && m.materialId().equals("trudgeon_wood@getilia_soak")),
                "treatment-minted material must get palette tiles");
        assertTrue(tiles.stream().noneMatch(t -> t instanceof PaletteTile.MaterialTile m
                        && m.form() == PaletteForm.OPEN),
                "OPEN is authored by leaving the cell empty; it must never be a tile");
    }

    @Test
    void outputParsesWithTsxReader() {
        String document = new RawsPaletteGenerator().generate(RAWS);
        int planned = new RawsPaletteGenerator().plan(RAWS).size();

        List<String> warnings = new ArrayList<>();
        TmxTileset tileset = new TsxReader(warnings::add).read(new StringReader(document));

        assertTrue(warnings.isEmpty(), "generated palette must parse without warnings: " + warnings);
        assertEquals("materials", tileset.name());
        assertEquals(16, tileset.tileWidth());
        assertEquals(16, tileset.tileHeight());
        assertEquals(planned, tileset.tileCount());
        assertEquals(planned, tileset.tiles().size());

        for (int i = 0; i < tileset.tiles().size(); i++) {
            TmxTilesetTile tile = tileset.tiles().get(i);
            assertEquals(i, tile.localId(), "tile ids must be contiguous 0..n-1");
            boolean material = tile.properties().find("material").isPresent();
            boolean fluid = tile.properties().find("fluid").isPresent();
            assertTrue(material ^ fluid,
                    "tile " + i + " must carry exactly one of material/fluid");
            if (material) {
                assertEquals(TmxPropertyType.STRING,
                        tile.properties().find("material").orElseThrow().type());
                assertEquals(TmxPropertyType.STRING,
                        tile.properties().find("form").orElseThrow().type());
            } else {
                assertEquals(TmxPropertyType.STRING,
                        tile.properties().find("fluid").orElseThrow().type());
                int depth = tile.properties().find("depth").orElseThrow().asInt();
                assertTrue(depth >= 1 && depth <= 7, "depth must be 1..7, was " + depth);
            }
        }
    }

    @Test
    void missingRawsDirectoryFails() {
        RawsPaletteGenerator generator = new RawsPaletteGenerator();
        Path missing = RAWS.resolve("no-such-dir");
        assertThrows(PaletteGenerationException.class, () -> generator.generate(missing));
    }
}
