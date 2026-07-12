package com.trojia.tools.tmx;

import java.util.List;
import java.util.Objects;

/**
 * Root of a parsed {@code .tmx} document.
 *
 * <p><strong>Determinism contract:</strong> {@link #tilesets()} and {@link #layers()}
 * preserve document order; the layer list holds only top-level layers, with grouped
 * layers reachable through their {@link TmxLayerGroup}.</p>
 *
 * @param width       map width in tiles, {@code > 0}
 * @param height      map height in tiles, {@code > 0}
 * @param tileWidth   tile width in pixels, {@code > 0}
 * @param tileHeight  tile height in pixels, {@code > 0}
 * @param orientation Tiled orientation attribute (v0 maps are {@code "orthogonal"}), never {@code null}
 * @param renderOrder Tiled render order (default {@code "right-down"}), never {@code null}
 * @param tilesets    external tileset references in document order; immutable
 * @param layers      top-level layers in document order; immutable
 * @param properties  map-level custom properties, never {@code null}
 */
public record TmxMap(int width, int height, int tileWidth, int tileHeight,
                     String orientation, String renderOrder,
                     List<TmxTilesetRef> tilesets, List<TmxLayer> layers,
                     TmxProperties properties) {

    /**
     * @throws NullPointerException     if any reference component or list element is {@code null}
     * @throws IllegalArgumentException if any dimension is non-positive
     */
    public TmxMap {
        Objects.requireNonNull(orientation, "orientation");
        Objects.requireNonNull(renderOrder, "renderOrder");
        Objects.requireNonNull(properties, "properties");
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("map dimensions must be positive: " + width + "x" + height);
        }
        if (tileWidth <= 0 || tileHeight <= 0) {
            throw new IllegalArgumentException(
                    "tile dimensions must be positive: " + tileWidth + "x" + tileHeight);
        }
        tilesets = List.copyOf(tilesets);
        layers = List.copyOf(layers);
    }
}
