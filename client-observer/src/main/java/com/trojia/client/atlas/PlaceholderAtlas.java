package com.trojia.client.atlas;

import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.TextureRegion;

import java.util.NavigableMap;
import java.util.NavigableSet;
import java.util.TreeMap;

/**
 * The GL half of the placeholder atlas: owns the generated {@link Texture} and the
 * boot-time interned region lookup (TILE-ART-SPEC section 1 — {@code AtlasRegionTable}
 * side of the seam pairs names with rectangles; this class pairs the rectangles with
 * the texture to make {@link TextureRegion}s).
 *
 * <p>Built by {@link PlaceholderAtlasFactory#create}; requires a live GL context to
 * construct and must be {@link #dispose() disposed} when the screen goes away. All
 * regions are created eagerly at construction in canonical sorted order, so lookups
 * never allocate.
 *
 * <p>Immutable after construction (texture disposal aside).
 */
public final class PlaceholderAtlas implements TileAtlas {

    private final Texture texture;
    private final AtlasRegionTable table;
    private final NavigableMap<String, TextureRegion> regions;

    /**
     * Wraps a texture with the placeholder cell layout.
     *
     * @param texture the uploaded sheet; ownership transfers to this atlas
     * @param table   the GL-free layout the sheet was rastered with
     * @throws IllegalArgumentException if either argument is null
     */
    PlaceholderAtlas(Texture texture, AtlasRegionTable table) {
        if (texture == null) {
            throw new IllegalArgumentException("texture must be non-null");
        }
        if (table == null) {
            throw new IllegalArgumentException("table must be non-null");
        }
        this.texture = texture;
        this.table = table;
        NavigableMap<String, TextureRegion> built = new TreeMap<>();
        for (String name : table.regionNames()) {
            AtlasCellRect rect = table.cellRect(name);
            built.put(name, new TextureRegion(texture, rect.x(), rect.y(),
                    rect.width(), rect.height()));
        }
        this.regions = built;
    }

    /**
     * The texture region of a placeholder cell.
     *
     * @param regionName the region name (TILE-ART-SPEC section 3 grammar)
     * @return the region; never null
     * @throws IllegalArgumentException if the name has no cell — callers that need a
     *                                  soft check use {@link #contains(String)}
     */
    public TextureRegion region(String regionName) {
        TextureRegion region = regionName == null ? null : regions.get(regionName);
        if (region == null) {
            throw new IllegalArgumentException(
                    "unknown region \"" + regionName + "\" (have " + regions.size() + ")");
        }
        return region;
    }

    /**
     * The placeholder pack rasters exactly one cell per name, so every variant index maps
     * to that single region (TILE-ART-SPEC section 12 — the single-cell pack).
     */
    @Override
    public TextureRegion region(String regionName, int variantIndex) {
        return region(regionName);
    }

    /** Always {@code 1} for a known name, {@code 0} otherwise: one cell per placeholder name. */
    @Override
    public int variantCount(String regionName) {
        return contains(regionName) ? 1 : 0;
    }

    /** Whether {@code regionName} has a cell. */
    public boolean contains(String regionName) {
        return regionName != null && regions.containsKey(regionName);
    }

    /** All region names in ascending ASCII order; unmodifiable. */
    public NavigableSet<String> regionNames() {
        return table.regionNames();
    }

    /** The GL-free cell layout this atlas was built from. */
    public AtlasRegionTable regionTable() {
        return table;
    }

    /** The owned sheet texture (for batch flush diagnostics; regions reference it). */
    public Texture texture() {
        return texture;
    }

    /** Disposes the owned texture; every handed-out region dangles afterwards. */
    @Override
    public void dispose() {
        texture.dispose();
    }
}
