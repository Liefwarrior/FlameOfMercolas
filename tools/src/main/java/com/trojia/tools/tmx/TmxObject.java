package com.trojia.tools.tmx;

import java.util.Objects;

/**
 * One object from an object layer (annotation rectangles, points, site markers, ...).
 *
 * <p>Coordinates are Tiled pixel coordinates as authored; the importer converts them to
 * tile space. {@code typeName} carries the {@code class} attribute (Tiled 1.9+) or the
 * legacy {@code type} attribute — whichever the document used. For tile objects the
 * {@code gid} has its flip bits masked by {@link TmxReader}; {@code 0} means the object
 * carries no tile.</p>
 *
 * @param id         Tiled object id ({@code 0} if absent)
 * @param name       object name, never {@code null} (may be empty)
 * @param typeName   object class/type, never {@code null} (may be empty)
 * @param x          x position in pixels
 * @param y          y position in pixels
 * @param width      width in pixels ({@code 0} for points)
 * @param height     height in pixels ({@code 0} for points)
 * @param gid        decoded global tile id for tile objects, {@code 0} when absent
 * @param properties custom properties, never {@code null}
 */
public record TmxObject(int id, String name, String typeName, double x, double y,
                        double width, double height, int gid, TmxProperties properties) {

    /**
     * @throws NullPointerException     if {@code name}, {@code typeName} or {@code properties} is {@code null}
     * @throws IllegalArgumentException if {@code gid} is negative
     */
    public TmxObject {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(typeName, "typeName");
        Objects.requireNonNull(properties, "properties");
        if (gid < 0) {
            throw new IllegalArgumentException("negative gid " + gid + " (flip bits must be masked)");
        }
    }
}
