package com.trojia.tools.tmx;

import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * A parsed external tileset ({@code .tsx}) with its tileset-level and per-tile custom
 * properties. Produced by {@link TsxReader}.
 *
 * <p><strong>Determinism contract:</strong> {@link #tiles()} preserves document order
 * (Tiled writes ascending local ids, but the model does not re-sort). Local ids are
 * unique; duplicates are rejected at parse time.</p>
 *
 * @param name       tileset name, never {@code null}
 * @param tileWidth  tile width in pixels, {@code > 0}
 * @param tileHeight tile height in pixels, {@code > 0}
 * @param tileCount  declared tile count ({@code 0} if the document omitted it)
 * @param columns    declared column count ({@code 0} if the document omitted it)
 * @param properties tileset-level custom properties, never {@code null}
 * @param tiles      tiles carrying metadata, document order; immutable
 */
public record TmxTileset(String name, int tileWidth, int tileHeight, int tileCount, int columns,
                         TmxProperties properties, List<TmxTilesetTile> tiles) {

    /**
     * @throws NullPointerException     if any reference component or list element is {@code null}
     * @throws IllegalArgumentException if tile dimensions are non-positive, counts are
     *                                  negative, or two tiles share a local id
     */
    public TmxTileset {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(properties, "properties");
        if (tileWidth <= 0 || tileHeight <= 0) {
            throw new IllegalArgumentException(
                    "tile dimensions must be positive: " + tileWidth + "x" + tileHeight);
        }
        if (tileCount < 0 || columns < 0) {
            throw new IllegalArgumentException("tileCount/columns must be >= 0");
        }
        tiles = List.copyOf(tiles);
        for (int i = 0; i < tiles.size(); i++) {
            for (int j = i + 1; j < tiles.size(); j++) {
                if (tiles.get(i).localId() == tiles.get(j).localId()) {
                    throw new IllegalArgumentException("duplicate tile local id " + tiles.get(i).localId());
                }
            }
        }
    }

    /**
     * @param localId tile id local to this tileset
     * @return the metadata tile with that id, if the document declared one
     */
    public Optional<TmxTilesetTile> tile(int localId) {
        for (TmxTilesetTile t : tiles) {
            if (t.localId() == localId) {
                return Optional.of(t);
            }
        }
        return Optional.empty();
    }
}
