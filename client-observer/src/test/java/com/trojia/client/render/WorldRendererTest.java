package com.trojia.client.render;

import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.trojia.client.art.JsonTileArtResolver;
import com.trojia.client.art.TileArtResolver;
import com.trojia.client.atlas.TileAtlas;
import com.trojia.client.render.WorldRenderer.FluidOverlay;
import com.trojia.sim.fluid.FluidDefinition;
import com.trojia.sim.fluid.FluidRegistry;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for {@link WorldRenderer}'s GL-free fluid-overlay plan
 * ({@link WorldRenderer#fluidOverlay}) — the pure (fluidBits, position, registries,
 * resolver, atlas) &rarr; draw-plan function behind the water overlay pass
 * (TILE-ART-SPEC section 5.3; GRANADAD art spec section 5). The GL half of the pass is
 * one {@code batch.draw} of the returned plan and is exercised by the observer smoke run.
 */
class WorldRendererTest {

    /** The pinned depthAlphaQ8 curve (TILE-ART-SPEC section 5.3). */
    private static final int[] WATER_ALPHA = {0, 96, 120, 144, 168, 192, 216, 240};

    // ------------------------------------------------------------------ fixtures

    /**
     * Two-fluid registry. Ids are assigned in sorted-key order (FluidRegistry contract):
     * {@code brine} = 0, {@code water} = 1 — deliberately not registration order, so a
     * test that resolves the wrong lane bits picks the wrong fluid and fails.
     */
    private static FluidRegistry registry() {
        return FluidRegistry.of(List.of(fluid("water"), fluid("brine")));
    }

    private static FluidDefinition fluid(String key) {
        return new FluidDefinition(key, key, 1, 0, 12, FluidDefinition.NONE, null, 0,
                FluidDefinition.NONE, null, 0, FluidDefinition.NONE, 0, List.of());
    }

    /**
     * Minimal valid mapping (same skeleton as {@code JsonTileArtResolverTest}): maps
     * {@code water} (pre-colored, no tint — the shipped packs' shape) and {@code brine}
     * (with a secondary tint, exercising the tint passthrough); {@code oil} is
     * deliberately unmapped.
     */
    private static JsonTileArtResolver resolver() {
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
                  "voidColor": "#0E0E12",
                  "lightTintQ8": [%s],
                  "zPeekDimQ8": [256, 168, 112, 76],
                  "materials": {
                    "granite": { "forms": { "block": { "byAppearance": ["granite"] } } }
                  },
                  "fluids": {
                    "water": { "region": "water", "depthAlphaQ8": [0, 96, 120, 144, 168, 192, 216, 240] },
                    "brine": { "region": "brine", "depthAlphaQ8": [0, 10, 20, 30, 40, 50, 60, 70], "tint": "#336699" }
                  }
                }
                """.formatted(tint));
    }

    /** GL-free stand-in atlas: region name &rarr; variant count. */
    private static final class FakeAtlas implements TileAtlas {
        private final Map<String, Integer> variants;

        FakeAtlas(Map<String, Integer> variants) {
            this.variants = variants;
        }

        @Override
        public TextureRegion region(String regionName) {
            if (!contains(regionName)) {
                throw new IllegalArgumentException("no region " + regionName);
            }
            return new TextureRegion();
        }

        @Override
        public TextureRegion region(String regionName, int variantIndex) {
            return region(regionName);
        }

        @Override
        public int variantCount(String regionName) {
            return variants.getOrDefault(regionName, 0);
        }

        @Override
        public boolean contains(String regionName) {
            return variants.containsKey(regionName);
        }

        @Override
        public void dispose() {
        }
    }

    private static TileAtlas atlas() {
        return new FakeAtlas(Map.of("water", 4, "brine", 1, "granite", 1, "missing", 1));
    }

    /** Packs FLUID-lane bits per Tile.java: depth 0–2, fluidId 3–5, SETTLED 6. */
    private static int bits(int depth, int fluidId, boolean settled) {
        return depth | (fluidId << 3) | (settled ? 0x40 : 0);
    }

    private static final int WATER_ID = 1; // sorted-key order: brine 0, water 1

    private static FluidOverlay plan(int fluidBits) {
        return WorldRenderer.fluidOverlay(fluidBits, 12, 34, 5, registry(), resolver(), atlas());
    }

    // ------------------------------------------------------------------ tests

    @Test
    void depthZeroDrawsNothing() {
        assertNull(plan(bits(0, WATER_ID, false)));
        assertNull(plan(bits(0, WATER_ID, true)));
        assertNull(plan(0));
    }

    @Test
    void everyPooledDepthDrawsWaterAtThePinnedAlpha() {
        for (int depth = 1; depth <= 7; depth++) {
            FluidOverlay overlay = plan(bits(depth, WATER_ID, false));
            assertNotNull(overlay, "depth " + depth);
            assertEquals("water", overlay.regionName(), "depth " + depth);
            assertEquals(WATER_ALPHA[depth], overlay.alphaQ8(), "depth " + depth);
            assertEquals(TileArtResolver.NO_TINT, overlay.tintRgb(),
                    "shipped-shape water lists no tint; the region draws as authored");
        }
    }

    @Test
    void fluidIdBitsSelectTheFluidNotRegistrationOrder() {
        FluidOverlay overlay = plan(bits(3, 0, false)); // raw id 0 = brine (sorted keys)
        assertNotNull(overlay);
        assertEquals("brine", overlay.regionName());
        assertEquals(30, overlay.alphaQ8()); // brine's curve at depth 3
    }

    @Test
    void fluidTintPassesThroughAsSecondaryAdjustment() {
        FluidOverlay overlay = plan(bits(2, 0, false));
        assertNotNull(overlay);
        assertEquals(0x336699, overlay.tintRgb());
    }

    @Test
    void laneGarbageFluidIdDrawsNothing() {
        // Registry has 2 fluids; the 3-bit field can carry up to 7.
        assertNull(plan(bits(4, 5, false)));
        assertNull(plan(bits(4, 7, true)));
    }

    @Test
    void fluidUnmappedByThePackDrawsNothing() {
        // A registry whose raw id 0 is a fluid the pack maps no entry for: the resolver
        // reports alpha 0 at every depth, the pack's opt-out signal.
        FluidRegistry withOil = FluidRegistry.of(List.of(fluid("water"), fluid("oil")));
        FluidOverlay overlay = WorldRenderer.fluidOverlay(bits(7, 0, false), 1, 2, 3,
                withOil, resolver(), atlas());
        assertNull(overlay);
    }

    @Test
    void settledBitDoesNotChangeThePlan() {
        FluidOverlay flowing = plan(bits(5, WATER_ID, false));
        FluidOverlay settled = plan(bits(5, WATER_ID, true));
        assertEquals(flowing, settled);
    }

    @Test
    void variantIsDeterministicAndInRange() {
        FluidOverlay first = plan(bits(7, WATER_ID, false));
        FluidOverlay second = plan(bits(7, WATER_ID, false));
        assertNotNull(first);
        assertEquals(first, second, "pure function: same inputs, same plan, every call");
        assertTrue(first.variant() >= 0 && first.variant() < 4,
                "variant " + first.variant() + " outside the region's 4 cells");
    }

    @Test
    void variantVariesAcrossPositions() {
        // Not a strict per-pair guarantee (it is a hash), but across a 16x16 patch the
        // 4-variant water region must show more than one variant or the surface tiles
        // visibly repeat — the whole point of the cosmetic-variant axis.
        boolean[] seen = new boolean[4];
        int distinct = 0;
        for (int ty = 0; ty < 16; ty++) {
            for (int tx = 0; tx < 16; tx++) {
                FluidOverlay overlay = WorldRenderer.fluidOverlay(bits(7, WATER_ID, false),
                        tx, ty, 5, registry(), resolver(), atlas());
                assertNotNull(overlay);
                if (!seen[overlay.variant()]) {
                    seen[overlay.variant()] = true;
                    distinct++;
                }
            }
        }
        assertTrue(distinct >= 2, "only " + distinct + " distinct variants across 256 cells");
    }

    @Test
    void singleVariantRegionAlwaysPicksZero() {
        FluidOverlay overlay = plan(bits(3, 0, false)); // brine: variantCount 1
        assertNotNull(overlay);
        assertEquals(0, overlay.variant());
    }

    @Test
    void regionAbsentFromTheAtlasFallsBackToMissing() {
        TileAtlas noWaterCell = new FakeAtlas(Map.of("granite", 1, "missing", 1, "brine", 1));
        FluidOverlay overlay = WorldRenderer.fluidOverlay(bits(6, WATER_ID, false), 1, 2, 3,
                registry(), resolver(), noWaterCell);
        assertNotNull(overlay);
        assertEquals("missing", overlay.regionName());
        assertEquals(WATER_ALPHA[6], overlay.alphaQ8());
    }

    @Test
    void fluidFormSaltIsPinnedOutOfBandOfEveryRealForm() {
        // GRANADAD art spec section 5: one past TileForm.STAIR.ordinal() == 5, so water
        // variants never share a hash stream with any base-tile form.
        assertEquals(6, WorldRenderer.FLUID_FORM_SALT);
        assertTrue(WorldRenderer.FLUID_FORM_SALT
                        > com.trojia.sim.world.TileForm.STAIR.ordinal(),
                "salt must stay out-of-band if TileForm ever grows");
    }
}
