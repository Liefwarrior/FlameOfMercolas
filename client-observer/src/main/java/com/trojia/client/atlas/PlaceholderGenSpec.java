package com.trojia.client.atlas;

import com.badlogic.gdx.utils.JsonReader;
import com.badlogic.gdx.utils.JsonValue;
import com.trojia.client.art.ArtMappingException;

import java.io.Reader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.NavigableMap;
import java.util.NavigableSet;
import java.util.TreeMap;

/**
 * The parsed {@code placeholderGen} block of {@code art-mapping.json} — input to the
 * placeholder atlas generator ONLY (TILE-ART-SPEC sections 6 and 7.1;
 * {@code JsonTileArtResolver} ignores the block entirely).
 *
 * <p>Carries the sheet geometry ({@code atlasSizePx}, {@code cellPx}), the Q8 style
 * constants ({@code outlineScaleQ8}, {@code floorScaleQ8}, {@code floorGlyphBlendQ8}),
 * one {@link Entry} per listed material or fluid id (fill color(s), glyph, glyph
 * color), and the {@code missing} checker style. Ids <em>not</em> listed fall to the
 * FNV-1a-64 HSL mint of {@link PlaceholderColorMath} — that decision belongs to the
 * raster, not here; this class is pure parsed data.
 *
 * <p>Chromatis is the {@code bucketColors} case (BLESSING-QUEUE ruling 5): the array
 * is the fill ramp silver &rarr; pale gold &rarr; gold indexed by appearance bucket
 * (clamp-high); the separate {@code heatGlowTint} discharge overlay is the resolver's
 * business and is ignored here under the unknown-fields convention, as are
 * {@code layout}, {@code glyphFont}, {@code hashRule}, {@code colorSource},
 * {@code provenance}, and {@code notes}.
 *
 * <p>GL-free (libGDX {@link JsonReader} needs no graphics context), immutable after
 * construction, deterministic iteration ({@link #entryIds()} is sorted). Validation
 * aggregates every defect into one {@link ArtMappingException} (boot fails), matching
 * {@code JsonTileArtResolver}.
 */
public final class PlaceholderGenSpec {

    /** Widest legal {@code bucketColors} array (buckets 0..3, TILE-ART-SPEC section 2). */
    public static final int MAX_BUCKETS = 4;

    private final int atlasSizePx;
    private final int cellPx;
    private final int outlineScaleQ8;
    private final int floorScaleQ8;
    private final int floorGlyphBlendQ8;
    private final NavigableMap<String, Entry> entries;
    private final int missingCheckerColorA;
    private final int missingCheckerColorB;
    private final int missingCheckerPx;

    private PlaceholderGenSpec(int atlasSizePx, int cellPx, int outlineScaleQ8,
                               int floorScaleQ8, int floorGlyphBlendQ8,
                               NavigableMap<String, Entry> entries,
                               int missingCheckerColorA, int missingCheckerColorB,
                               int missingCheckerPx) {
        this.atlasSizePx = atlasSizePx;
        this.cellPx = cellPx;
        this.outlineScaleQ8 = outlineScaleQ8;
        this.floorScaleQ8 = floorScaleQ8;
        this.floorGlyphBlendQ8 = floorGlyphBlendQ8;
        this.entries = entries;
        this.missingCheckerColorA = missingCheckerColorA;
        this.missingCheckerColorB = missingCheckerColorB;
        this.missingCheckerPx = missingCheckerPx;
    }

    /**
     * Parses the {@code placeholderGen} block out of a full mapping document.
     *
     * @param mappingJson the complete {@code art-mapping.json} text
     * @return the immutable spec
     * @throws ArtMappingException listing every validation failure (one per line),
     *                             wrapping the parser error for malformed JSON, or if
     *                             the document has no {@code placeholderGen} object
     */
    public static PlaceholderGenSpec parse(String mappingJson) {
        JsonValue root;
        try {
            root = new JsonReader().parse(mappingJson);
        } catch (RuntimeException e) {
            throw new ArtMappingException("art-mapping: malformed JSON: " + e.getMessage(), e);
        }
        return fromRoot(root);
    }

    /**
     * Parses the {@code placeholderGen} block from a character stream.
     *
     * @param reader the complete {@code art-mapping.json} content; not closed here
     * @return the immutable spec
     * @throws ArtMappingException as for {@link #parse(String)}
     */
    public static PlaceholderGenSpec parse(Reader reader) {
        JsonValue root;
        try {
            root = new JsonReader().parse(reader);
        } catch (RuntimeException e) {
            throw new ArtMappingException("art-mapping: malformed JSON: " + e.getMessage(), e);
        }
        return fromRoot(root);
    }

    private static PlaceholderGenSpec fromRoot(JsonValue root) {
        if (root == null || !root.isObject()) {
            throw new ArtMappingException("art-mapping: document is empty or not a JSON object");
        }
        JsonValue gen = root.get("placeholderGen");
        if (gen == null || !gen.isObject()) {
            throw new ArtMappingException(
                    "placeholderGen: missing or not an object (generator input required)");
        }
        List<String> errors = new ArrayList<>();

        int cellPx = readInt(gen, "cellPx", errors);
        int atlasSizePx = readInt(gen, "atlasSizePx", errors);
        if (cellPx != Integer.MIN_VALUE && cellPx < Glyph8x8Font.HEIGHT) {
            errors.add("placeholderGen.cellPx: " + cellPx + " smaller than the "
                    + Glyph8x8Font.WIDTH + "x" + Glyph8x8Font.HEIGHT + " glyph box");
        }
        if (atlasSizePx != Integer.MIN_VALUE && cellPx != Integer.MIN_VALUE
                && cellPx >= Glyph8x8Font.HEIGHT
                && (atlasSizePx <= 0 || atlasSizePx % cellPx != 0)) {
            errors.add("placeholderGen.atlasSizePx: " + atlasSizePx
                    + " must be a positive multiple of cellPx " + cellPx);
        }
        int outlineScaleQ8 = readQ8(gen, "outlineScaleQ8", errors);
        int floorScaleQ8 = readQ8(gen, "floorScaleQ8", errors);
        int floorGlyphBlendQ8 = readQ8(gen, "floorGlyphBlendQ8", errors);

        NavigableMap<String, Entry> entries = new TreeMap<>();
        parseEntries(gen.get("materials"), "placeholderGen.materials", false, entries, errors);
        parseEntries(gen.get("fluids"), "placeholderGen.fluids", true, entries, errors);

        int checkerA = 0;
        int checkerB = 0;
        int checkerPx = 0;
        JsonValue missing = gen.get("missing");
        if (missing == null || !missing.isObject()) {
            errors.add("placeholderGen.missing: missing or not an object");
        } else {
            JsonValue colors = missing.get("checkerColors");
            if (colors == null || !colors.isArray() || colors.size != 2) {
                errors.add("placeholderGen.missing.checkerColors: must be an array of"
                        + " exactly 2 \"#RRGGBB\" colors");
            } else {
                checkerA = readColor(colors.get(0),
                        "placeholderGen.missing.checkerColors[0]", errors);
                checkerB = readColor(colors.get(1),
                        "placeholderGen.missing.checkerColors[1]", errors);
            }
            JsonValue px = missing.get("checkerPx");
            if (px == null || !px.isNumber() || px.asInt() <= 0
                    || (cellPx > 0 && px.asInt() > cellPx)) {
                errors.add("placeholderGen.missing.checkerPx: must be a positive int"
                        + " no larger than cellPx");
            } else {
                checkerPx = px.asInt();
            }
        }

        if (!errors.isEmpty()) {
            throw new ArtMappingException(String.join("\n", errors));
        }
        return new PlaceholderGenSpec(atlasSizePx, cellPx, outlineScaleQ8, floorScaleQ8,
                floorGlyphBlendQ8, entries, checkerA, checkerB, checkerPx);
    }

    private static void parseEntries(JsonValue node, String where, boolean fluid,
                                     NavigableMap<String, Entry> out, List<String> errors) {
        if (node == null) {
            return; // both sections optional; an all-mint sheet is legal
        }
        if (!node.isObject()) {
            errors.add(where + ": must be an object");
            return;
        }
        for (JsonValue child = node.child; child != null; child = child.next) {
            Entry entry = parseEntry(child, where + "." + child.name, fluid, errors);
            if (entry != null && !out.containsKey(child.name)) {
                // materials parse first and win on (never-expected) id collisions
                out.put(child.name, entry);
            }
        }
    }

    private static Entry parseEntry(JsonValue node, String where, boolean fluid,
                                    List<String> errors) {
        if (!node.isObject()) {
            errors.add(where + ": must be an object");
            return null;
        }
        JsonValue colorNode = node.get("color");
        JsonValue bucketsNode = node.get("bucketColors");
        int[] fills = null;
        if (colorNode != null && bucketsNode != null) {
            errors.add(where + ": has both color and bucketColors (exactly one required)");
        } else if (colorNode != null) {
            fills = new int[]{readColor(colorNode, where + ".color", errors)};
        } else if (bucketsNode != null) {
            if (!bucketsNode.isArray() || bucketsNode.size < 1 || bucketsNode.size > MAX_BUCKETS) {
                errors.add(where + ".bucketColors: must be an array of 1.." + MAX_BUCKETS
                        + " colors");
            } else {
                fills = new int[bucketsNode.size];
                int i = 0;
                for (JsonValue c = bucketsNode.child; c != null; c = c.next, i++) {
                    fills[i] = readColor(c, where + ".bucketColors[" + i + "]", errors);
                }
            }
        } else {
            errors.add(where + ": needs color or bucketColors");
        }

        char glyph = 0;
        JsonValue glyphNode = node.get("glyph");
        if (glyphNode == null || !glyphNode.isString() || glyphNode.asString().length() != 1
                || glyphNode.asString().charAt(0) < Glyph8x8Font.FIRST
                || glyphNode.asString().charAt(0) > Glyph8x8Font.LAST) {
            errors.add(where + ".glyph: must be one printable-ASCII character");
        } else {
            glyph = glyphNode.asString().charAt(0);
        }
        int glyphRgb = readColor(node.get("glyphColor"), where + ".glyphColor", errors);

        if (fills == null || glyph == 0) {
            return null;
        }
        return new Entry(fills, glyph, glyphRgb, fluid);
    }

    private static int readInt(JsonValue obj, String field, List<String> errors) {
        JsonValue v = obj.get(field);
        if (v == null || !v.isNumber() || v.asInt() <= 0) {
            errors.add("placeholderGen." + field + ": must be a positive int (found "
                    + (v == null ? "nothing" : v.toString()) + ")");
            return Integer.MIN_VALUE;
        }
        return v.asInt();
    }

    private static int readQ8(JsonValue obj, String field, List<String> errors) {
        JsonValue v = obj.get(field);
        if (v == null || !v.isNumber() || v.asInt() < 0
                || v.asInt() > PlaceholderColorMath.UNIT_Q8) {
            errors.add("placeholderGen." + field + ": must be an int 0.."
                    + PlaceholderColorMath.UNIT_Q8 + " (found "
                    + (v == null ? "nothing" : v.toString()) + ")");
            return 0;
        }
        return v.asInt();
    }

    private static int readColor(JsonValue v, String where, List<String> errors) {
        if (v != null && v.isString()) {
            try {
                return PlaceholderColorMath.parseRgb(v.asString());
            } catch (IllegalArgumentException ignored) {
                // falls through to the error below
            }
        }
        errors.add(where + ": must be \"#RRGGBB\" (found "
                + (v == null ? "nothing" : v.toString()) + ")");
        return 0;
    }

    // ------------------------------------------------------------------ queries

    /** Sheet edge length in pixels ({@code atlasSizePx}, v0: 128). */
    public int atlasSizePx() {
        return atlasSizePx;
    }

    /** Cell edge length in pixels ({@code cellPx}, v0: 16). */
    public int cellPx() {
        return cellPx;
    }

    /** Q8 factor deriving the 1 px outline color from the fill (v0: 128). */
    public int outlineScaleQ8() {
        return outlineScaleQ8;
    }

    /** Q8 factor scaling fill and outline for the floor variant (v0: 184). */
    public int floorScaleQ8() {
        return floorScaleQ8;
    }

    /** Q8 amount the floor glyph blends toward the floor fill (v0: 128). */
    public int floorGlyphBlendQ8() {
        return floorGlyphBlendQ8;
    }

    /**
     * The listed style for a material or fluid id.
     *
     * @param id canonical raws id, verbatim
     * @return the entry, or {@code null} for unlisted ids (the raster then applies
     *         the {@link PlaceholderColorMath} mint)
     * @throws IllegalArgumentException if {@code id} is null or blank
     */
    public Entry entry(String id) {
        if (id == null || id.isBlank()) {
            throw new IllegalArgumentException("id must be non-blank");
        }
        return entries.get(id);
    }

    /** All listed ids (materials and fluids) in ascending order; unmodifiable. */
    public NavigableSet<String> entryIds() {
        return Collections.unmodifiableNavigableSet(entries.navigableKeySet());
    }

    /** First checker color of the {@code missing} cell, packed {@code 0xRRGGBB}. */
    public int missingCheckerColorA() {
        return missingCheckerColorA;
    }

    /** Second checker color of the {@code missing} cell, packed {@code 0xRRGGBB}. */
    public int missingCheckerColorB() {
        return missingCheckerColorB;
    }

    /** Checker square edge length in pixels (v0: 8). */
    public int missingCheckerPx() {
        return missingCheckerPx;
    }

    /**
     * One listed {@code placeholderGen} style: the fill ramp (a single color or up to
     * {@value PlaceholderGenSpec#MAX_BUCKETS} {@code bucketColors}), the glyph, and
     * the glyph color. Immutable.
     */
    public static final class Entry {
        private final int[] fillByBucket;
        private final char glyph;
        private final int glyphRgb;
        private final boolean fluid;

        private Entry(int[] fillByBucket, char glyph, int glyphRgb, boolean fluid) {
            this.fillByBucket = fillByBucket;
            this.glyph = glyph;
            this.glyphRgb = glyphRgb;
            this.fluid = fluid;
        }

        /**
         * The fill color for an appearance bucket, clamped high (TILE-ART-SPEC
         * section 2: an over-charged material saturates, never wraps).
         *
         * @param bucket appearance bucket ordinal, {@code >= 0}
         * @return packed {@code 0xRRGGBB}
         * @throws IllegalArgumentException if {@code bucket < 0}
         */
        public int fillRgb(int bucket) {
            if (bucket < 0) {
                throw new IllegalArgumentException("bucket " + bucket + " < 0");
            }
            return fillByBucket[Math.min(bucket, fillByBucket.length - 1)];
        }

        /** Number of distinct fill colors (1 for single-{@code color} entries). */
        public int bucketCount() {
            return fillByBucket.length;
        }

        /** The 8x8 glyph character. */
        public char glyph() {
            return glyph;
        }

        /** The glyph color, packed {@code 0xRRGGBB}. */
        public int glyphRgb() {
            return glyphRgb;
        }

        /**
         * Whether the id came from {@code placeholderGen.fluids}: fluid cells render
         * fill + glyph with no outline (TILE-ART-SPEC section 6 lists water as
         * "solid fill, glyph" — the outline is a block/floor tile-grid affordance).
         */
        public boolean fluid() {
            return fluid;
        }
    }
}
