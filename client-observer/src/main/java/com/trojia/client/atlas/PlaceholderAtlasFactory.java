package com.trojia.client.atlas;

import com.badlogic.gdx.graphics.Pixmap;
import com.badlogic.gdx.graphics.Texture;
import com.trojia.client.art.JsonTileArtResolver;

/**
 * Builds the deterministic in-memory placeholder atlas at boot (TILE-ART-SPEC
 * section 6): {@code art-mapping.json} &rarr; {@link PlaceholderSheetRaster}
 * (GL-free) &rarr; {@link Pixmap} &rarr; {@link Texture} &rarr;
 * {@link PlaceholderAtlas} (GL). No file is written and nothing is packed —
 * the sheet exists only in memory (the debug dump is
 * {@link PlaceholderAtlasDump}'s job).
 *
 * <p>{@link #buildRaster} is GL-free and headless-testable; {@link #create} needs a
 * live GL context ({@code Pixmap} uses the gdx natives, {@code Texture} uploads) and
 * is a thin, logic-free wrapper by design — every color, layout, and raster decision
 * already happened in the buffer it copies.
 */
public final class PlaceholderAtlasFactory {

    private PlaceholderAtlasFactory() {
    }

    /**
     * GL-free front half: parses the mapping document and rasters one cell per
     * region name that {@link JsonTileArtResolver#referencedRegionNames()} reports.
     *
     * @param mappingJson the complete {@code art-mapping.json} text
     * @return the rastered sheet, byte-deterministic for identical input
     * @throws com.trojia.client.art.ArtMappingException if the document fails either
     *                                                   parse (resolver schema or
     *                                                   {@code placeholderGen} block)
     */
    public static PlaceholderSheetRaster buildRaster(String mappingJson) {
        JsonTileArtResolver resolver = JsonTileArtResolver.parse(mappingJson);
        PlaceholderGenSpec spec = PlaceholderGenSpec.parse(mappingJson);
        return new PlaceholderSheetRaster(spec, resolver.missingRegionName(),
                resolver.referencedRegionNames());
    }

    /**
     * GL back half: uploads a rastered sheet as a {@code Nearest}-filtered texture
     * (pixel-snapped 16 px grid, TILE-ART-SPEC section 4) and interns every cell as a
     * {@link com.badlogic.gdx.graphics.g2d.TextureRegion}.
     *
     * <p>Must run on the render thread with a live GL context (boot / {@code create()}).
     *
     * @param raster the GL-free sheet from {@link #buildRaster}
     * @return the atlas; caller owns disposal
     * @throws IllegalArgumentException if {@code raster} is null
     */
    public static PlaceholderAtlas create(PlaceholderSheetRaster raster) {
        if (raster == null) {
            throw new IllegalArgumentException("raster must be non-null");
        }
        Pixmap pixmap = toPixmap(raster);
        try {
            Texture texture = new Texture(pixmap);
            texture.setFilter(Texture.TextureFilter.Nearest, Texture.TextureFilter.Nearest);
            return new PlaceholderAtlas(texture, raster.regionTable());
        } finally {
            pixmap.dispose();
        }
    }

    /**
     * Copies the raster's ARGB buffer into an RGBA8888 {@link Pixmap}, blending off
     * (a verbatim copy, including the fully transparent unused cells). Caller owns
     * disposal.
     */
    private static Pixmap toPixmap(PlaceholderSheetRaster raster) {
        int size = raster.atlasSizePx();
        Pixmap pixmap = new Pixmap(size, size, Pixmap.Format.RGBA8888);
        pixmap.setBlending(Pixmap.Blending.None);
        int[] argb = raster.pixelsArgb();
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                int pixel = argb[y * size + x];
                pixmap.drawPixel(x, y, (pixel << 8) | (pixel >>> 24)); // ARGB -> RGBA
            }
        }
        return pixmap;
    }
}
