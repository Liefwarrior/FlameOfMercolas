package com.trojia.tools.validate;

import java.util.List;
import java.util.Objects;
import java.util.Optional;

import com.trojia.tools.tmx.TmxMap;
import com.trojia.tools.tmx.TmxTilesetTile;

/**
 * Immutable input of one {@link TiledValidator} run: the parsed map, its resolved
 * external tilesets, the non-fatal reader diagnostics collected while parsing, and
 * the raws id universe to resolve against.
 *
 * @param mapName        display name of the map (file name), never {@code null} or blank
 * @param map            the parsed map model, never {@code null}
 * @param tilesets       resolved tilesets in map document order; immutable
 * @param readerWarnings warnings emitted by the tmx/tsx readers in document order
 *                       (e.g. masked flip bits); immutable
 * @param raws           raws id index, never {@code null}
 */
public record MapCheckContext(String mapName, TmxMap map, List<ResolvedTileset> tilesets,
                              List<String> readerWarnings, RawsIndex raws) {

    /**
     * @throws NullPointerException     if any component or list element is {@code null}
     * @throws IllegalArgumentException if {@code mapName} is blank
     */
    public MapCheckContext {
        Objects.requireNonNull(mapName, "mapName");
        Objects.requireNonNull(map, "map");
        Objects.requireNonNull(raws, "raws");
        if (mapName.isBlank()) {
            throw new IllegalArgumentException("mapName must not be blank");
        }
        tilesets = List.copyOf(tilesets);
        readerWarnings = List.copyOf(readerWarnings);
    }

    /**
     * Resolves a gid to its owning tileset by Tiled semantics: the reference with the
     * greatest {@code firstGid <= gid} whose declared range contains the gid.
     *
     * @param gid a decoded global tile id, {@code > 0}
     * @return the owning tileset, or empty when no declared range covers {@code gid}
     */
    public Optional<ResolvedTileset> tilesetFor(int gid) {
        ResolvedTileset best = null;
        for (ResolvedTileset t : tilesets) {
            if (t.firstGid() <= gid && (best == null || t.firstGid() > best.firstGid())) {
                best = t;
            }
        }
        return best != null && best.contains(gid) ? Optional.of(best) : Optional.empty();
    }

    /**
     * @param gid a decoded global tile id, {@code > 0}
     * @return the tileset metadata tile for {@code gid}, if the gid resolves and the
     *         tileset declared metadata for it
     */
    public Optional<TmxTilesetTile> tileFor(int gid) {
        return tilesetFor(gid).flatMap(t -> t.tile(gid));
    }
}
