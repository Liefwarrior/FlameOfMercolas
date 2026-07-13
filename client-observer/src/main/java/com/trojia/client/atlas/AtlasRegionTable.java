package com.trojia.client.atlas;

import java.util.Collection;
import java.util.Collections;
import java.util.NavigableMap;
import java.util.NavigableSet;
import java.util.TreeMap;

/**
 * GL-free region-name &rarr; atlas-cell lookup for the generated placeholder atlas:
 * the deterministic packing rule of TILE-ART-SPEC section 3 — cells laid out
 * <b>row-major in ascending ASCII byte order of region name</b> on a fixed square
 * sheet of uniform cells (v0: 128&times;128 px, 16 px cells, 8 columns).
 *
 * <p>Region names are restricted to printable ASCII without spaces
 * ({@code 0x21..0x7E}), which makes ascending byte order identical to
 * {@link String#compareTo} order — the layout cannot depend on locale or charset.
 *
 * <p>The layout is a pure function of the name set: insertion order never matters,
 * and two tables built from the same names are equal cell for cell. Immutable after
 * construction; iteration ({@link #regionNames()}) is in canonical sorted order.
 *
 * <p>This is the GL-free half of the atlas-table seam: the GL side
 * ({@code PlaceholderAtlas}) pairs these rectangles with a {@code Texture} to make
 * {@code TextureRegion}s at boot.
 */
public final class AtlasRegionTable {

    private final int atlasSizePx;
    private final int cellPx;
    private final NavigableMap<String, AtlasCellRect> cells;

    /**
     * Lays out {@code regionNames} row-major in ascending ASCII order.
     *
     * @param atlasSizePx sheet edge length in pixels; positive multiple of
     *                    {@code cellPx}
     * @param cellPx      cell edge length in pixels, positive
     * @param regionNames the names to place; duplicates collapse (set semantics);
     *                    each must be non-blank printable ASCII ({@code 0x21..0x7E})
     * @throws IllegalArgumentException if the geometry is invalid, any name is null,
     *                                  blank, or contains a character outside
     *                                  {@code 0x21..0x7E}, or the distinct names do
     *                                  not fit the sheet
     */
    public AtlasRegionTable(int atlasSizePx, int cellPx, Collection<String> regionNames) {
        if (cellPx <= 0) {
            throw new IllegalArgumentException("cellPx " + cellPx + " must be positive");
        }
        if (atlasSizePx <= 0 || atlasSizePx % cellPx != 0) {
            throw new IllegalArgumentException("atlasSizePx " + atlasSizePx
                    + " must be a positive multiple of cellPx " + cellPx);
        }
        if (regionNames == null) {
            throw new IllegalArgumentException("regionNames must be non-null");
        }
        NavigableMap<String, AtlasCellRect> layout = new TreeMap<>();
        for (String name : regionNames) {
            checkName(name);
            layout.put(name, null); // sorted first; rects assigned below
        }
        int columns = atlasSizePx / cellPx;
        int capacity = columns * columns;
        if (layout.size() > capacity) {
            throw new IllegalArgumentException(layout.size() + " regions exceed the "
                    + capacity + "-cell capacity of a " + atlasSizePx + "x" + atlasSizePx
                    + " sheet with " + cellPx + " px cells");
        }
        int index = 0;
        for (String name : layout.keySet()) {
            int cellX = (index % columns) * cellPx;
            int cellY = (index / columns) * cellPx;
            layout.put(name, new AtlasCellRect(cellX, cellY, cellPx, cellPx));
            index++;
        }
        this.atlasSizePx = atlasSizePx;
        this.cellPx = cellPx;
        this.cells = layout;
    }

    private static void checkName(String name) {
        if (name == null || name.isEmpty()) {
            throw new IllegalArgumentException("region name must be non-empty");
        }
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if (c <= 0x20 || c > 0x7E) {
                throw new IllegalArgumentException("region name \"" + name
                        + "\" contains non-printable-ASCII char at index " + i
                        + " (layout order is ascending ASCII byte order)");
            }
        }
    }

    /** Sheet edge length in pixels. */
    public int atlasSizePx() {
        return atlasSizePx;
    }

    /** Cell edge length in pixels. */
    public int cellPx() {
        return cellPx;
    }

    /** Cells per row (and rows per sheet — the sheet is square). */
    public int columns() {
        return atlasSizePx / cellPx;
    }

    /** Number of placed regions. */
    public int size() {
        return cells.size();
    }

    /** Whether {@code regionName} has a cell. */
    public boolean contains(String regionName) {
        return regionName != null && cells.containsKey(regionName);
    }

    /**
     * The cell rectangle of a region.
     *
     * @param regionName the region name
     * @return its cell, origin top-left
     * @throws IllegalArgumentException if the name is unknown — callers that need a
     *                                  soft check use {@link #contains(String)}
     */
    public AtlasCellRect cellRect(String regionName) {
        AtlasCellRect rect = regionName == null ? null : cells.get(regionName);
        if (rect == null) {
            throw new IllegalArgumentException(
                    "unknown region \"" + regionName + "\" (have " + cells.size() + ")");
        }
        return rect;
    }

    /** All placed region names in ascending ASCII order; unmodifiable. */
    public NavigableSet<String> regionNames() {
        return Collections.unmodifiableNavigableSet(cells.navigableKeySet());
    }
}
