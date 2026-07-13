package com.trojia.tools.validate;

import java.util.Objects;
import java.util.Optional;

import com.trojia.tools.tmx.TmxTileset;
import com.trojia.tools.tmx.TmxTilesetRef;
import com.trojia.tools.tmx.TmxTilesetTile;

/**
 * A map's external tileset reference paired with its parsed {@code .tsx} document,
 * giving gid-space accessors ({@code firstGid <= gid < firstGid + tileCount}).
 *
 * @param ref     the {@code <tileset firstgid source>} reference from the map, never {@code null}
 * @param tileset the parsed external tileset, never {@code null}
 */
public record ResolvedTileset(TmxTilesetRef ref, TmxTileset tileset) {

    /** @throws NullPointerException if either component is {@code null} */
    public ResolvedTileset {
        Objects.requireNonNull(ref, "ref");
        Objects.requireNonNull(tileset, "tileset");
    }

    /** @return the first global tile id mapped to this tileset */
    public int firstGid() {
        return ref.firstGid();
    }

    /**
     * @param gid a decoded global tile id
     * @return {@code true} iff {@code gid} falls inside this tileset's declared range;
     *         always {@code false} when the tileset omitted {@code tilecount}
     */
    public boolean contains(int gid) {
        return tileset.tileCount() > 0
                && gid >= ref.firstGid()
                && gid < ref.firstGid() + tileset.tileCount();
    }

    /**
     * @param gid a decoded global tile id inside this tileset's range
     * @return the metadata tile for {@code gid}, if the document declared one
     */
    public Optional<TmxTilesetTile> tile(int gid) {
        return tileset.tile(gid - ref.firstGid());
    }
}
