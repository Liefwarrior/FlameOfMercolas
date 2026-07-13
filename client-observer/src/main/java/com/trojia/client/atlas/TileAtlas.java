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
 * <p>The renderer only needs the lookup methods; keeping the surface this small lets
 * either pack drive {@code WorldRenderer} unchanged (TILE-ART-SPEC section 8: an art swap
 * is a pack + mapping change, not a renderer change).
 *
 * <p><b>Cosmetic variants (TILE-ART-SPEC section 12).</b> A region name may back
 * <em>several</em> interchangeable sheet cells — different rough-stone or plank looks under
 * one logical name — so a large wall or floor does not repeat one sprite. The renderer picks
 * a variant per tile with {@link #variantCount(String)} + {@link #region(String, int)}, keyed
 * on the tile's world position (a pure, deterministic function — never RNG or mutable state).
 * This is orthogonal to the appearance bucket of {@code TileArtResolver} (the gameplay
 * charge-stop axis): variety is a presentation concern resolved GL-side, not a keying input.
 * A pack that ships one cell per name (the procedural placeholder) reports {@code variantCount
 * == 1} and every {@code region(name, v)} returns that single cell.
 */
public interface TileAtlas extends Disposable {

    /**
     * The first (variant 0) texture region for a region name — the single-cell accessor
     * used where variety does not apply (e.g. the fluid overlay).
     *
     * @param regionName the name from the art mapping
     * @return the region; never null
     * @throws IllegalArgumentException if the name has no cell — callers that need a soft
     *                                  check use {@link #contains(String)}
     */
    TextureRegion region(String regionName);

    /**
     * The {@code variantIndex}-th cosmetic variant of a region name. Implementations reduce
     * {@code variantIndex} modulo {@link #variantCount(String)} defensively, so any
     * non-negative or negative index resolves to a real cell.
     *
     * @param regionName   the name from the art mapping
     * @param variantIndex which interchangeable cell to draw (typically a position hash)
     * @return the region; never null
     * @throws IllegalArgumentException if the name has no cell
     */
    TextureRegion region(String regionName, int variantIndex);

    /**
     * How many interchangeable cells back {@code regionName}: {@code >= 1} for a known name
     * (one for a single-cell pack), {@code 0} for an unknown name.
     */
    int variantCount(String regionName);

    /** Whether {@code regionName} has a cell in this atlas. */
    boolean contains(String regionName);
}
