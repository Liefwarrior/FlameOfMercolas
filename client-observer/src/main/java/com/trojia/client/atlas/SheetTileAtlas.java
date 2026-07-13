package com.trojia.client.atlas;

import com.badlogic.gdx.files.FileHandle;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.TextureRegion;

import java.util.NavigableMap;
import java.util.NavigableSet;
import java.util.TreeMap;

/**
 * The GL half of a real sprite-sheet pack: owns one {@link Texture} loaded from disk (the
 * Kenney 1-bit {@code monochrome-transparent_packed} sheet) and interns a
 * {@link TextureRegion} per region name from a GL-free {@link SheetAtlasSpec}. The
 * shipped-pack analogue of {@link PlaceholderAtlas}, and like it a {@link TileAtlas} the
 * world renderer draws through unchanged.
 *
 * <p>The sheet is white-on-transparent grayscale; the black luminous-on-black base shows
 * through the transparent texels and the renderer multiplies each material's tint into the
 * white ones (DECISIONS.md art register), so one grayscale wall/floor sprite serves many
 * materials. {@code Nearest} filtering keeps the 16 px pixel-art crisp (TILE-ART-SPEC
 * section 4). Requires a live GL context to construct; {@link #dispose() dispose} when the
 * screen goes away. Immutable after construction (texture disposal aside).
 */
public final class SheetTileAtlas implements TileAtlas {

    private final Texture texture;
    private final NavigableMap<String, TextureRegion> regions;

    private SheetTileAtlas(Texture texture, NavigableMap<String, TextureRegion> regions) {
        this.texture = texture;
        this.regions = regions;
    }

    /**
     * Loads {@code sheetFile} and slices one region per entry in {@code spec}.
     *
     * <p>Must run on the render thread with a live GL context (boot / {@code create()}).
     *
     * @param spec      the GL-free layout (cell rects per region name)
     * @param sheetFile the sheet image on disk, sized to cover every cell in {@code spec}
     * @return the atlas; caller owns disposal
     * @throws IllegalArgumentException if either argument is null
     */
    public static SheetTileAtlas create(SheetAtlasSpec spec, FileHandle sheetFile) {
        if (spec == null) {
            throw new IllegalArgumentException("spec must be non-null");
        }
        if (sheetFile == null) {
            throw new IllegalArgumentException("sheetFile must be non-null");
        }
        Texture texture = new Texture(sheetFile);
        texture.setFilter(Texture.TextureFilter.Nearest, Texture.TextureFilter.Nearest);
        NavigableMap<String, TextureRegion> built = new TreeMap<>();
        for (String name : spec.regionNames()) {
            AtlasCellRect rect = spec.cellRect(name);
            built.put(name, new TextureRegion(texture, rect.x(), rect.y(),
                    rect.width(), rect.height()));
        }
        return new SheetTileAtlas(texture, built);
    }

    @Override
    public TextureRegion region(String regionName) {
        TextureRegion region = regionName == null ? null : regions.get(regionName);
        if (region == null) {
            throw new IllegalArgumentException(
                    "unknown region \"" + regionName + "\" (have " + regions.size() + ")");
        }
        return region;
    }

    @Override
    public boolean contains(String regionName) {
        return regionName != null && regions.containsKey(regionName);
    }

    /** All region names in ascending ASCII order; unmodifiable. */
    public NavigableSet<String> regionNames() {
        return regions.navigableKeySet();
    }

    /** The owned sheet texture (regions reference it). */
    public Texture texture() {
        return texture;
    }

    /** Disposes the owned texture; every handed-out region dangles afterwards. */
    @Override
    public void dispose() {
        texture.dispose();
    }
}
