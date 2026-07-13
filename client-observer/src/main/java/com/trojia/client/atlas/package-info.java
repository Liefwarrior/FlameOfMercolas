/**
 * Placeholder atlas generation (TILE-ART-SPEC sections 3 and 6): a deterministic
 * in-memory texture atlas built at boot from the {@code placeholderGen} block of
 * {@code content/art/placeholder/art-mapping.json} — one 16 px flat-color-plus-glyph
 * cell per region name that {@code JsonTileArtResolver.referencedRegionNames()} reports.
 *
 * <p>The package is split across the GL-free / GL boundary of TILE-ART-SPEC section 1:
 *
 * <ul>
 *   <li><b>GL-free</b> (headless-testable, pure integer/deterministic math):
 *       {@link com.trojia.client.atlas.PlaceholderColorMath} (FNV-1a-64 HSL color mint,
 *       Q8 channel ops), {@link com.trojia.client.atlas.Glyph8x8Font} (built-in 8&times;8
 *       ASCII bitmap font), {@link com.trojia.client.atlas.PlaceholderRegionKey} (region
 *       name decomposition), {@link com.trojia.client.atlas.PlaceholderGenSpec} (parsed
 *       {@code placeholderGen} block), {@link com.trojia.client.atlas.AtlasRegionTable}
 *       (row-major ascending-ASCII cell layout),
 *       {@link com.trojia.client.atlas.PlaceholderSheetRaster} (the full sheet rastered
 *       into an {@code int[]} ARGB buffer),
 *       {@link com.trojia.client.atlas.PlaceholderPngWriter} and
 *       {@link com.trojia.client.atlas.PlaceholderAtlasDump} (byte-deterministic debug
 *       dump of {@code placeholder.png} + {@code placeholder.atlas}).</li>
 *   <li><b>GL</b> (thin wrappers, boot-time only):
 *       {@link com.trojia.client.atlas.PlaceholderAtlasFactory} (raster &rarr;
 *       {@code Pixmap} &rarr; {@code Texture}) and
 *       {@link com.trojia.client.atlas.PlaceholderAtlas} (owns the texture and the
 *       region lookup).</li>
 * </ul>
 *
 * <p>Everything GL-free is immutable after construction and byte-deterministic: the
 * same mapping file yields the same pixel buffer, the same atlas text, and the same
 * PNG bytes on every run and platform (no timestamps, fixed encoder settings — same
 * rule as the Tiled importer, ARCHITECTURE section 9).
 */
package com.trojia.client.atlas;
