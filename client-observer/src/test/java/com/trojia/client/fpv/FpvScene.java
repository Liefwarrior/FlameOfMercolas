package com.trojia.client.fpv;

import com.trojia.client.art.JsonTileArtResolver;
import com.trojia.client.atlas.RegionCatalog;
import com.trojia.client.atlas.VariantPattern;
import com.trojia.sim.fluid.FluidDefinition;
import com.trojia.sim.fluid.FluidRegistry;
import com.trojia.sim.material.Material;
import com.trojia.sim.material.MaterialPhase;
import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.world.TileForm;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * A hand-authored ward for the planner tests: a bounded box of tile lanes with no world, no
 * chunks, no cursor and no GL. The same trick {@code DepthSightTest} plays with a synthetic
 * column, one dimension up — the planner only ever asks a {@link CellSight} questions, so a
 * {@link HashMap} is a perfectly good ward.
 *
 * <p>Unset cells are {@link TileForm#OPEN} with no fluid, which is what the real map is too
 * over most of its volume: OPEN means "nothing was authored here", not "air in a room".
 */
final class FpvScene implements CellSight {

    static final int SIZE_XY = 64;
    static final int SIZE_Z = 32;

    /** Material lane ids, assigned by MaterialRegistry in sorted key order. */
    static final int GRANITE = 0;
    static final int OAK = 1;

    private final Map<Integer, int[]> cells = new HashMap<>();
    private int[] current = OPEN_CELL;

    private static final int[] OPEN_CELL = {TileForm.OPEN.ordinal(), 0, 0};

    private static int key(int x, int y, int z) {
        return (z * SIZE_XY + y) * SIZE_XY + x;
    }

    FpvScene put(int x, int y, int z, TileForm form, int materialLane) {
        cells.put(key(x, y, z), new int[] {form.ordinal(), 0, materialLane});
        return this;
    }

    /** A cell with pooled fluid: {@code fluidId} 0 is the only fluid this scene registers. */
    FpvScene fluid(int x, int y, int z, int depth) {
        cells.put(key(x, y, z), new int[] {TileForm.OPEN.ordinal(), depth, 0});
        return this;
    }

    FpvScene fill(int x0, int x1, int y0, int y1, int z, TileForm form, int materialLane) {
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                put(x, y, z, form, materialLane);
            }
        }
        return this;
    }

    /** The form actually authored at a cell — the test's independent read of the same ward. */
    TileForm formAt(int x, int y, int z) {
        int[] cell = cells.get(key(x, y, z));
        return TileForm.ofOrdinal(cell == null ? TileForm.OPEN.ordinal() : cell[0]);
    }

    int fluidDepthAt(int x, int y, int z) {
        int[] cell = cells.get(key(x, y, z));
        return cell == null ? 0 : cell[1];
    }

    int materialAt(int x, int y, int z) {
        int[] cell = cells.get(key(x, y, z));
        return cell == null ? 0 : cell[2];
    }

    @Override
    public boolean moveTo(int x, int y, int z) {
        if (x < 0 || x >= SIZE_XY || y < 0 || y >= SIZE_XY || z < 0 || z >= SIZE_Z) {
            return false;
        }
        int[] cell = cells.get(key(x, y, z));
        current = cell == null ? OPEN_CELL : cell;
        return true;
    }

    @Override
    public int form() {
        return current[0];
    }

    @Override
    public int fluidBits() {
        return current[1]; // depth in bits 0-2, fluid id 0
    }

    @Override
    public int materialId() {
        return current[2];
    }

    // ------------------------------------------------------------------ art fixtures

    /** Every region exists, single-cell, hash-patterned: art identity is not what is on trial
     * here, and a stub keeps the planner's answers reproducible by hand. */
    static RegionCatalog catalog() {
        return new RegionCatalog() {
            @Override
            public boolean contains(String regionName) {
                return true;
            }

            @Override
            public int variantCount(String regionName) {
                return 3;
            }

            @Override
            public VariantPattern variantPattern(String regionName) {
                return VariantPattern.HASH;
            }
        };
    }

    static MaterialRegistry materials() {
        return MaterialRegistry.of(List.of(material("granite"), material("oak")), List.of());
    }

    private static Material material(String key) {
        return new Material(key, key, MaterialPhase.SOLID, 1000, 5, 0, Material.NONE,
                Material.NONE, null, 0, Material.NONE, null, 128, 64, 0, null, 1,
                List.of(), 31, List.of());
    }

    static FluidRegistry fluids() {
        return FluidRegistry.of(List.of(new FluidDefinition("water", "water", 1, 0, 12,
                FluidDefinition.NONE, null, 0, FluidDefinition.NONE, null, 0,
                FluidDefinition.NONE, 0, List.of())));
    }

    static JsonTileArtResolver resolver() {
        StringBuilder tint = new StringBuilder();
        for (int level = 0; level < 32; level++) {
            if (level > 0) {
                tint.append(',');
            }
            tint.append(36 + 220 * level * level / 961);
        }
        return JsonTileArtResolver.parse("""
                {
                  "schemaVersion": 1,
                  "atlas": "art/test/test.atlas",
                  "tilePx": 16,
                  "missingRegion": "missing",
                  "voidColor": "#000000",
                  "lightTintQ8": [%s],
                  "zPeekDimQ8": [256, 168, 112, 76],
                  "materials": {
                    "granite": {
                      "forms": {
                        "block": { "byAppearance": ["granite_block"] },
                        "floor": { "byAppearance": ["granite_floor"] },
                        "wall":  { "byAppearance": ["granite_wall"] }
                      }
                    },
                    "oak": {
                      "forms": {
                        "block": { "byAppearance": ["oak_block"] },
                        "floor": { "byAppearance": ["oak_floor"] },
                        "wall":  { "byAppearance": ["oak_wall"] }
                      }
                    }
                  },
                  "fluids": {
                    "water": { "region": "water", "depthAlphaQ8": [0, 96, 120, 144, 168, 192, 216, 240] }
                  }
                }
                """.formatted(tint));
    }

    static FirstPersonPlanner planner() {
        return new FirstPersonPlanner(materials(), fluids(), resolver(), catalog());
    }
}
