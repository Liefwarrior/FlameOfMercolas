package com.trojia.client.atlas;

import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.badlogic.gdx.utils.Disposable;

/**
 * The GL-side tile-atlas seam the world renderer draws through: region name &rarr;
 * {@link TextureRegion}. Two implementations exist behind it — {@link PlaceholderAtlas}
 * (the deterministic in-memory procedural pack, kept as a headless-testable fallback) and
 * {@link SheetTileAtlas} (a real uniform-grid sprite sheet loaded from disk, e.g. the
 * Kenney 1-bit pack). Both are built once at boot on the render thread and own a GPU
 * texture, so both are {@link Disposable}.
 *
 * <p>The renderer only needs the two lookup methods; keeping the surface this small lets
 * either pack drive {@code WorldRenderer} unchanged (TILE-ART-SPEC section 8: an art swap
 * is a pack + mapping change, not a renderer change).
 */
public interface TileAtlas extends Disposable {

    /**
     * The texture region for a region name.
     *
     * @param regionName the name from the art mapping
     * @return the region; never null
     * @throws IllegalArgumentException if the name has no cell — callers that need a soft
     *                                  check use {@link #contains(String)}
     */
    TextureRegion region(String regionName);

    /** Whether {@code regionName} has a cell in this atlas. */
    boolean contains(String regionName);
}
