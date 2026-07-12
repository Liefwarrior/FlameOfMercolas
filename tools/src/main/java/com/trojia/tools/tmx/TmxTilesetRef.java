package com.trojia.tools.tmx;

import java.util.Objects;

/**
 * Reference from a map to an external tileset: {@code <tileset firstgid=".." source="...tsx"/>}.
 *
 * <p>v0 requires all tilesets to be external; {@link TmxReader} rejects embedded ones.
 * Resolve {@code source} against the map file's directory and parse it with
 * {@link TsxReader}. A gid {@code g} belongs to this tileset when
 * {@code firstGid <= g < firstGid + tileCount}; the local tile id is {@code g - firstGid}.</p>
 *
 * @param firstGid first global tile id mapped to this tileset, {@code >= 1}
 * @param source   path to the {@code .tsx} file as written in the document, never {@code null}
 */
public record TmxTilesetRef(int firstGid, String source) {

    /**
     * @throws NullPointerException     if {@code source} is {@code null}
     * @throws IllegalArgumentException if {@code firstGid < 1} or {@code source} is blank
     */
    public TmxTilesetRef {
        Objects.requireNonNull(source, "source");
        if (firstGid < 1) {
            throw new IllegalArgumentException("firstGid must be >= 1, was " + firstGid);
        }
        if (source.isBlank()) {
            throw new IllegalArgumentException("tileset source must not be blank");
        }
    }
}
