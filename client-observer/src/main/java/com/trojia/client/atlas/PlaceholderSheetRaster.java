package com.trojia.client.atlas;

import com.trojia.client.art.RegionNameGrammar;

import java.util.Collection;

/**
 * The GL-free heart of the placeholder atlas generator: rasters one 16 px cell per
 * region name into a single {@code int[]} ARGB sheet buffer, laid out by
 * {@link AtlasRegionTable} (row-major, ascending ASCII — TILE-ART-SPEC section 3),
 * styled per TILE-ART-SPEC section 6:
 *
 * <ul>
 *   <li><b>Block</b> (default form): solid fill of the material color, a 1 px outline
 *       of {@code fill * outlineScaleQ8 >> 8} per channel, and the material's 8&times;8
 *       glyph centered (v0: at (4,4)) in the glyph color.</li>
 *   <li><b>Floor:</b> block fill and block outline each scaled by
 *       {@code floorScaleQ8/256} (in that order — scale the block colors, do not
 *       re-derive the outline from the scaled fill); glyph color blended
 *       {@code floorGlyphBlendQ8/256} toward the floor fill.</li>
 *   <li><b>Fluids</b> ({@code placeholderGen.fluids}): solid fill + glyph, no outline,
 *       full alpha (depth alpha is a render-time multiply, section 5.3).</li>
 *   <li><b>missing:</b> checker of the two {@code checkerColors}, no glyph.</li>
 *   <li><b>Unlisted ids:</b> FNV-1a-64 HSL mint ({@link PlaceholderColorMath}); the
 *       glyph is the id's first character (fallback box outside printable ASCII),
 *       glyph color by the WCAG luminance rule.</li>
 * </ul>
 *
 * <p>Appearance buckets index a listed entry's fill ramp with the clamp-high rule —
 * {@code chromatis}/{@code chromatis.a1}/{@code chromatis.a2} take
 * {@code bucketColors[0..2]} (the silver &rarr; pale-gold &rarr; gold ramp of
 * BLESSING-QUEUE ruling 5).
 *
 * <p>Byte-deterministic: the pixel buffer and {@link #atlasText} are pure functions
 * of the spec and the region-name <em>set</em> (input order never matters). Painted
 * texels are opaque ({@code 0xFF} alpha); cells without a region stay fully
 * transparent ({@code 0x00000000}). Immutable after construction; headless (no GL,
 * no I/O).
 */
public final class PlaceholderSheetRaster {

    /** The form token that triggers the floor variant (TILE-ART-SPEC section 6). */
    public static final String FLOOR_FORM = "floor";

    private final PlaceholderGenSpec spec;
    private final AtlasRegionTable table;
    private final int[] pixelsArgb;

    /**
     * Rasters the full sheet.
     *
     * @param spec              the parsed {@code placeholderGen} block
     * @param missingRegionName the mapping's universal fallback region name (v0:
     *                          {@code "missing"}); rendered as the checker cell when
     *                          present in {@code regionNames}
     * @param regionNames       every region to generate — in v0 exactly
     *                          {@code JsonTileArtResolver.referencedRegionNames()};
     *                          duplicates collapse, order is irrelevant
     * @throws IllegalArgumentException if any argument is null, the missing name is
     *                                  blank, a region name violates
     *                                  {@link AtlasRegionTable}'s ASCII rule or the
     *                                  grammar of TILE-ART-SPEC section 3, or the
     *                                  names do not fit the sheet
     */
    public PlaceholderSheetRaster(PlaceholderGenSpec spec, String missingRegionName,
                                  Collection<String> regionNames) {
        if (spec == null) {
            throw new IllegalArgumentException("spec must be non-null");
        }
        if (missingRegionName == null || missingRegionName.isBlank()) {
            throw new IllegalArgumentException("missingRegionName must be non-blank");
        }
        this.spec = spec;
        this.table = new AtlasRegionTable(spec.atlasSizePx(), spec.cellPx(), regionNames);
        this.pixelsArgb = new int[spec.atlasSizePx() * spec.atlasSizePx()];
        for (String name : table.regionNames()) {
            paintCell(name, missingRegionName, table.cellRect(name));
        }
    }

    private void paintCell(String name, String missingRegionName, AtlasCellRect rect) {
        if (name.equals(missingRegionName)) {
            paintChecker(rect);
            return;
        }
        PlaceholderRegionKey key = PlaceholderRegionKey.parse(name);
        PlaceholderGenSpec.Entry entry = spec.entry(key.materialId());

        int fill;
        char glyph;
        int glyphRgb;
        boolean outlined;
        if (entry != null) {
            fill = entry.fillRgb(key.bucket());
            glyph = entry.glyph();
            glyphRgb = entry.glyphRgb();
            outlined = !entry.fluid();
        } else {
            fill = PlaceholderColorMath.mintFillRgb(key.materialId());
            glyph = key.materialId().charAt(0);
            glyphRgb = PlaceholderColorMath.mintGlyphRgb(fill);
            outlined = true;
        }

        int outline = PlaceholderColorMath.scaleRgbQ8(fill, spec.outlineScaleQ8());
        if (FLOOR_FORM.equals(key.form())) {
            fill = PlaceholderColorMath.scaleRgbQ8(fill, spec.floorScaleQ8());
            outline = PlaceholderColorMath.scaleRgbQ8(outline, spec.floorScaleQ8());
            glyphRgb = PlaceholderColorMath.blendRgbQ8(glyphRgb, fill, spec.floorGlyphBlendQ8());
        }
        paintTile(rect, fill, outlined ? outline : fill, glyph, glyphRgb);
    }

    private void paintTile(AtlasCellRect rect, int fillRgb, int outlineRgb,
                           char glyph, int glyphRgb) {
        int cell = rect.width();
        int fillArgb = opaque(fillRgb);
        int outlineArgb = opaque(outlineRgb);
        int glyphArgb = opaque(glyphRgb);
        int glyphOffset = (cell - Glyph8x8Font.WIDTH) / 2; // v0: (16-8)/2 = 4
        for (int y = 0; y < cell; y++) {
            for (int x = 0; x < cell; x++) {
                int argb;
                if (x == 0 || y == 0 || x == cell - 1 || y == cell - 1) {
                    argb = outlineArgb;
                } else {
                    argb = fillArgb;
                }
                int gx = x - glyphOffset;
                int gy = y - glyphOffset;
                if (gx >= 0 && gx < Glyph8x8Font.WIDTH && gy >= 0 && gy < Glyph8x8Font.HEIGHT
                        && Glyph8x8Font.isInk(glyph, gx, gy)) {
                    argb = glyphArgb;
                }
                set(rect.x() + x, rect.y() + y, argb);
            }
        }
    }

    private void paintChecker(AtlasCellRect rect) {
        int checkerPx = spec.missingCheckerPx();
        int a = opaque(spec.missingCheckerColorA());
        int b = opaque(spec.missingCheckerColorB());
        for (int y = 0; y < rect.height(); y++) {
            for (int x = 0; x < rect.width(); x++) {
                boolean even = ((x / checkerPx) + (y / checkerPx)) % 2 == 0;
                set(rect.x() + x, rect.y() + y, even ? a : b);
            }
        }
    }

    private static int opaque(int rgb888) {
        return 0xFF000000 | (rgb888 & 0xFFFFFF);
    }

    private void set(int x, int y, int argb) {
        pixelsArgb[y * spec.atlasSizePx() + x] = argb;
    }

    // ------------------------------------------------------------------ queries

    /** Sheet edge length in pixels. */
    public int atlasSizePx() {
        return spec.atlasSizePx();
    }

    /** Cell edge length in pixels. */
    public int cellPx() {
        return spec.cellPx();
    }

    /** The cell layout (row-major, ascending ASCII). */
    public AtlasRegionTable regionTable() {
        return table;
    }

    /**
     * The full sheet as row-major {@code 0xAARRGGBB} pixels, origin top-left
     * (index {@code y * atlasSizePx() + x}). Painted texels are opaque; unused cells
     * are {@code 0x00000000}.
     *
     * @return a defensive copy — byte-identical across runs for identical inputs
     */
    public int[] pixelsArgb() {
        return pixelsArgb.clone();
    }

    /**
     * One sheet pixel, {@code 0xAARRGGBB}.
     *
     * @param x column, 0..{@code atlasSizePx()-1}
     * @param y row (top-down), 0..{@code atlasSizePx()-1}
     * @return the pixel value
     * @throws IllegalArgumentException if either coordinate is out of bounds
     */
    public int pixelArgb(int x, int y) {
        int size = spec.atlasSizePx();
        if (x < 0 || x >= size || y < 0 || y >= size) {
            throw new IllegalArgumentException(
                    "(" + x + ", " + y + ") outside the " + size + "x" + size + " sheet");
        }
        return pixelsArgb[y * size + x];
    }

    /**
     * The {@code placeholder.atlas} text: standard libGDX atlas format, one page,
     * {@code Nearest} filtering (pixel-snapped 16 px grid, TILE-ART-SPEC section 4),
     * regions in ascending ASCII order with their layout coordinates. Line separator
     * is {@code "\n"} and there are no timestamps — byte-deterministic (section 3).
     *
     * @param pngFileName the page image name to reference (v0: {@code "placeholder.png"})
     * @return the full file text, trailing newline included
     * @throws IllegalArgumentException if {@code pngFileName} is null or blank
     */
    public String atlasText(String pngFileName) {
        if (pngFileName == null || pngFileName.isBlank()) {
            throw new IllegalArgumentException("pngFileName must be non-blank");
        }
        int size = spec.atlasSizePx();
        int cell = spec.cellPx();
        StringBuilder text = new StringBuilder();
        text.append(pngFileName).append('\n');
        text.append("size: ").append(size).append(", ").append(size).append('\n');
        text.append("format: RGBA8888").append('\n');
        text.append("filter: Nearest, Nearest").append('\n');
        text.append("repeat: none").append('\n');
        for (String name : table.regionNames()) {
            AtlasCellRect rect = table.cellRect(name);
            text.append(name).append('\n');
            text.append("  rotate: false").append('\n');
            text.append("  xy: ").append(rect.x()).append(", ").append(rect.y()).append('\n');
            text.append("  size: ").append(cell).append(", ").append(cell).append('\n');
            text.append("  orig: ").append(cell).append(", ").append(cell).append('\n');
            text.append("  offset: 0, 0").append('\n');
            text.append("  index: -1").append('\n');
        }
        return text.toString();
    }

    /**
     * Convenience for callers composing names for a lookup: delegates to
     * {@link RegionNameGrammar#compose} then {@link AtlasRegionTable#cellRect}.
     *
     * @param materialId canonical raws id
     * @param form       lowercase form token ({@code "block"} omits the segment)
     * @param bucket     appearance bucket 0..3
     * @return the cell of the composed region name
     * @throws IllegalArgumentException if the composed name has no cell or any
     *                                  component is invalid
     */
    public AtlasCellRect cellOf(String materialId, String form, int bucket) {
        return table.cellRect(RegionNameGrammar.compose(materialId, form, bucket));
    }
}
