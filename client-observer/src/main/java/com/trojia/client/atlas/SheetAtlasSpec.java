package com.trojia.client.atlas;

import com.badlogic.gdx.utils.JsonReader;
import com.badlogic.gdx.utils.JsonValue;
import com.trojia.client.art.ArtMappingException;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.NavigableMap;
import java.util.NavigableSet;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * GL-free parse of a real uniform-grid sprite sheet's atlas layout from an
 * {@code art-mapping.json} — the loader-side companion to {@link PlaceholderGenSpec} for
 * a shipped pack instead of the generated placeholder one. Reads three fields the
 * procedural loader ignores:
 *
 * <ul>
 *   <li>{@code atlas} — the sheet image path relative to the {@code content/} root
 *       (a {@code .png}, not a {@code .atlas} descriptor: this pack is a single packed
 *       sheet with no spacing, sliced by grid math rather than a TexturePacker file).</li>
 *   <li>{@code sheet.columns} / {@code sheet.rows} — the tile grid dimensions, so cell
 *       coordinates can be bounds-checked at boot.</li>
 *   <li>{@code regions} — a map of region name &rarr; either one {@code [col, row]} tile
 *       coordinate <em>or</em> an array of them, {@code [[col,row], [col,row], …]}. The
 *       array form declares several interchangeable <b>cosmetic variants</b> of one logical
 *       region (different rough-stone or plank looks), which the renderer picks between per
 *       tile by world position so a large surface does not repeat one sprite (TILE-ART-SPEC
 *       section 12). The single-pair form is the one-variant shorthand. Multiple materials
 *       point at one shared region; the per-material colour comes from the resolver's tint,
 *       not the pixels.</li>
 * </ul>
 *
 * <p>Cells are {@code tilePx}&times;{@code tilePx} (16 in v0) with no inter-tile spacing
 * (the {@code *_packed} Kenney sheets), so a {@code [col, row]} pair occupies pixel rect
 * {@code (col*tilePx, row*tilePx, tilePx, tilePx)} — the same top-left origin as
 * {@link AtlasCellRect} and the raster/PNG/atlas-text formats. Validation aggregates every
 * defect into one {@link ArtMappingException} (same "boot fails" rule as
 * {@link com.trojia.client.art.JsonTileArtResolver}). Immutable after construction.
 */
public final class SheetAtlasSpec {

    private final String sheetPath;
    private final int tilePx;
    private final int columns;
    private final int rows;
    private final NavigableMap<String, List<AtlasCellRect>> cells;

    private SheetAtlasSpec(String sheetPath, int tilePx, int columns, int rows,
                           NavigableMap<String, List<AtlasCellRect>> cells) {
        this.sheetPath = sheetPath;
        this.tilePx = tilePx;
        this.columns = columns;
        this.rows = rows;
        this.cells = cells;
    }

    /**
     * Parses the sheet-atlas layout from a mapping document.
     *
     * @param mappingJson the complete {@code art-mapping.json} text
     * @return the immutable spec
     * @throws ArtMappingException listing every defect, or wrapping a parser error
     */
    public static SheetAtlasSpec parse(String mappingJson) {
        JsonValue root;
        try {
            root = new JsonReader().parse(mappingJson);
        } catch (RuntimeException e) {
            throw new ArtMappingException("sheet-atlas: malformed JSON: " + e.getMessage(), e);
        }
        if (root == null || !root.isObject()) {
            throw new ArtMappingException("sheet-atlas: document is empty or not a JSON object");
        }
        List<String> errors = new ArrayList<>();

        String sheetPath = readString(root, "atlas", errors);

        int tilePx = readPositiveInt(root, "tilePx", errors);

        int columns = 0;
        int rows = 0;
        JsonValue sheet = root.get("sheet");
        if (sheet == null || !sheet.isObject()) {
            errors.add("sheet: missing or not an object (need { columns, rows })");
        } else {
            columns = readPositiveInt(sheet, "columns", errors);
            rows = readPositiveInt(sheet, "rows", errors);
        }

        NavigableMap<String, List<AtlasCellRect>> cells = new TreeMap<>();
        JsonValue regions = root.get("regions");
        if (regions == null || !regions.isObject() || regions.child == null) {
            errors.add("regions: missing or empty (need at least one name -> [col, row])");
        } else {
            for (JsonValue region = regions.child; region != null; region = region.next) {
                List<AtlasCellRect> rects = parseCells(region, tilePx, columns, rows, errors);
                if (rects != null) {
                    cells.put(region.name, rects);
                }
            }
        }

        if (!errors.isEmpty()) {
            throw new ArtMappingException(String.join("\n", errors));
        }
        return new SheetAtlasSpec(sheetPath, tilePx, columns, rows, cells);
    }

    /**
     * Parses a region's cell(s): a bare {@code [col, row]} pair (one variant) or an array of
     * such pairs {@code [[col,row], …]} (several variants, TILE-ART-SPEC section 12). Returns
     * {@code null} if any variant is malformed or out of bounds (with the defect recorded).
     */
    private static List<AtlasCellRect> parseCells(JsonValue region, int tilePx, int columns,
                                                  int rows, List<String> errors) {
        String where = "regions." + region.name;
        if (!region.isArray() || region.child == null) {
            errors.add(where + ": must be a [col, row] pair or an array of [col, row] pairs");
            return null;
        }
        // Single-pair shorthand: the first child is a number (e.g. [10, 17]).
        if (region.child.isNumber()) {
            AtlasCellRect rect = parsePair(region, where, tilePx, columns, rows, errors);
            return rect == null ? null : List.of(rect);
        }
        // Variant list: each child is itself a [col, row] pair (e.g. [[10,17],[11,18]]).
        List<AtlasCellRect> variants = new ArrayList<>();
        boolean ok = true;
        int i = 0;
        for (JsonValue pair = region.child; pair != null; pair = pair.next, i++) {
            AtlasCellRect rect = parsePair(pair, where + "[" + i + "]", tilePx, columns, rows, errors);
            if (rect == null) {
                ok = false;
            } else {
                variants.add(rect);
            }
        }
        if (variants.isEmpty()) {
            errors.add(where + ": variant list has no valid [col, row] pairs");
            return null;
        }
        return ok ? List.copyOf(variants) : null;
    }

    private static AtlasCellRect parsePair(JsonValue pair, String where, int tilePx, int columns,
                                           int rows, List<String> errors) {
        if (!pair.isArray() || pair.size != 2
                || !pair.child.isNumber() || !pair.child.next.isNumber()) {
            errors.add(where + ": must be a [col, row] pair of integers");
            return null;
        }
        int col = pair.child.asInt();
        int row = pair.child.next.asInt();
        boolean ok = true;
        if (col < 0 || (columns > 0 && col >= columns)) {
            errors.add(where + ": col " + col + " outside 0.." + (columns - 1));
            ok = false;
        }
        if (row < 0 || (rows > 0 && row >= rows)) {
            errors.add(where + ": row " + row + " outside 0.." + (rows - 1));
            ok = false;
        }
        if (!ok || tilePx <= 0) {
            return null;
        }
        return new AtlasCellRect(col * tilePx, row * tilePx, tilePx, tilePx);
    }

    private static String readString(JsonValue obj, String field, List<String> errors) {
        JsonValue v = obj.get(field);
        if (v == null || !v.isString() || v.asString().isBlank()) {
            errors.add(field + ": must be a non-blank string");
            return null;
        }
        return v.asString();
    }

    private static int readPositiveInt(JsonValue obj, String field, List<String> errors) {
        JsonValue v = obj.get(field);
        if (v == null || !v.isNumber() || v.asInt() <= 0) {
            errors.add(field + ": must be a positive integer"
                    + " (found " + (v == null ? "nothing" : v.toString()) + ")");
            return 0;
        }
        return v.asInt();
    }

    /**
     * Asserts every name in {@code referenced} has a cell, so an art mapping cannot point
     * {@code byAppearance}/fluid regions at a name this sheet does not carry (TILE-ART-SPEC
     * section 7.2 — the GL-side region-existence check). Reports the full missing list.
     *
     * @throws ArtMappingException if any referenced name is absent
     */
    public void validateReferenced(Collection<String> referenced) {
        NavigableSet<String> missing = new TreeSet<>();
        for (String name : referenced) {
            if (!cells.containsKey(name)) {
                missing.add(name);
            }
        }
        if (!missing.isEmpty()) {
            throw new ArtMappingException(
                    "sheet-atlas: mapping references region names with no cell in 'regions': "
                            + String.join(", ", missing));
        }
    }

    /** The sheet image path, relative to the {@code content/} root. */
    public String sheetPath() {
        return sheetPath;
    }

    /** Tile edge length in sheet pixels. */
    public int tilePx() {
        return tilePx;
    }

    /** Grid width in tiles. */
    public int columns() {
        return columns;
    }

    /** Grid height in tiles. */
    public int rows() {
        return rows;
    }

    /** Whether {@code regionName} has at least one cell. */
    public boolean contains(String regionName) {
        return regionName != null && cells.containsKey(regionName);
    }

    /**
     * The pixel cells of a region — one entry per cosmetic variant, in declared order
     * (TILE-ART-SPEC section 12). Never empty for a known name.
     *
     * @throws IllegalArgumentException if the name is unknown
     */
    public List<AtlasCellRect> cellRects(String regionName) {
        List<AtlasCellRect> rects = regionName == null ? null : cells.get(regionName);
        if (rects == null) {
            throw new IllegalArgumentException(
                    "unknown region \"" + regionName + "\" (have " + cells.size() + ")");
        }
        return rects;
    }

    /**
     * How many cosmetic variants back {@code regionName}: {@code >= 1} for a known name,
     * {@code 0} for an unknown one.
     */
    public int variantCount(String regionName) {
        List<AtlasCellRect> rects = regionName == null ? null : cells.get(regionName);
        return rects == null ? 0 : rects.size();
    }

    /** All region names in ascending ASCII order. */
    public NavigableSet<String> regionNames() {
        return cells.navigableKeySet();
    }
}
