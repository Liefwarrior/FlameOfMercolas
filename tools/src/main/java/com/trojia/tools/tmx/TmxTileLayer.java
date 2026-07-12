package com.trojia.tools.tmx;

import java.util.Arrays;
import java.util.Objects;

/**
 * A tile layer with its fully decoded gid grid.
 *
 * <p>The grid is row-major: index {@code y * width + x}, row 0 at the top, exactly as
 * the CSV data appears in the document. Gid {@code 0} means "no tile". The three Tiled
 * flip bits have already been masked off by {@link TmxReader}, so every value is a
 * plain non-negative global tile id.</p>
 *
 * <p>Immutable: the array is defensively copied in and out. Use {@link #gidAt(int, int)}
 * for allocation-free access.</p>
 *
 * @param id         Tiled layer id ({@code 0} if absent)
 * @param name       layer name, never {@code null}
 * @param width      grid width in tiles, {@code > 0}
 * @param height     grid height in tiles, {@code > 0}
 * @param gids       row-major decoded gid grid of length {@code width * height}
 * @param properties custom properties, never {@code null}
 */
public record TmxTileLayer(int id, String name, int width, int height, int[] gids, TmxProperties properties)
        implements TmxLayer {

    /**
     * @throws NullPointerException     if {@code name}, {@code gids} or {@code properties} is {@code null}
     * @throws IllegalArgumentException if dimensions are non-positive, the array length
     *                                  is not {@code width * height}, or any gid is negative
     */
    public TmxTileLayer {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(gids, "gids");
        Objects.requireNonNull(properties, "properties");
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("layer dimensions must be positive: " + width + "x" + height);
        }
        if (gids.length != width * height) {
            throw new IllegalArgumentException(
                    "gid grid length " + gids.length + " != width*height " + (width * height));
        }
        gids = gids.clone();
        for (int gid : gids) {
            if (gid < 0) {
                throw new IllegalArgumentException("negative gid " + gid + " (flip bits must be masked)");
            }
        }
    }

    /** @return a defensive copy of the row-major gid grid */
    @Override
    public int[] gids() {
        return gids.clone();
    }

    /**
     * Allocation-free grid access.
     *
     * @param x column, {@code 0 <= x < width}
     * @param y row, {@code 0 <= y < height} (row 0 is the top row)
     * @return the decoded gid at (x, y); {@code 0} means no tile
     * @throws IndexOutOfBoundsException if (x, y) is outside the grid
     */
    public int gidAt(int x, int y) {
        Objects.checkIndex(x, width);
        Objects.checkIndex(y, height);
        return gids[y * width + x];
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof TmxTileLayer other
                && id == other.id
                && width == other.width
                && height == other.height
                && name.equals(other.name)
                && properties.equals(other.properties)
                && Arrays.equals(gids, other.gids);
    }

    @Override
    public int hashCode() {
        int h = Objects.hash(id, name, width, height, properties);
        return 31 * h + Arrays.hashCode(gids);
    }

    @Override
    public String toString() {
        return "TmxTileLayer[id=" + id + ", name=" + name + ", " + width + "x" + height
                + ", properties=" + properties + "]";
    }
}
