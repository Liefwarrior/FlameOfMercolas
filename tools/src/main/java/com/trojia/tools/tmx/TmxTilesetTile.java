package com.trojia.tools.tmx;

import java.util.Objects;

/**
 * Per-tile metadata inside a tileset: {@code <tile id=".." class="..">} with its custom
 * properties. Only tiles that carry metadata appear in the document (and thus in the
 * model); plain image tiles are implicit.
 *
 * @param localId    tile id local to the tileset ({@code gid - firstGid}), {@code >= 0}
 * @param typeName   tile class/type, never {@code null} (may be empty)
 * @param properties custom properties (e.g. the material binding key), never {@code null}
 */
public record TmxTilesetTile(int localId, String typeName, TmxProperties properties) {

    /**
     * @throws NullPointerException     if {@code typeName} or {@code properties} is {@code null}
     * @throws IllegalArgumentException if {@code localId} is negative
     */
    public TmxTilesetTile {
        Objects.requireNonNull(typeName, "typeName");
        Objects.requireNonNull(properties, "properties");
        if (localId < 0) {
            throw new IllegalArgumentException("localId must be >= 0, was " + localId);
        }
    }
}
