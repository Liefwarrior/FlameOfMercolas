package com.trojia.client.atlas;

import com.badlogic.gdx.files.FileHandle;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.TextureRegion;

import java.util.ArrayList;
import java.util.List;
import java.util.NavigableMap;
import java.util.NavigableSet;
import java.util.TreeMap;

/**
 * The GL half of a real sprite-sheet pack: owns one {@link Texture} loaded from disk (the
 * Kenney 1-bit {@code colored-transparent_packed} sheet, DECISIONS.md art register, Eli
 * 2026-07-13) and interns a {@link TextureRegion} per region name from a GL-free
 * {@link SheetAtlasSpec}. The shipped-pack analogue of {@link PlaceholderAtlas}, and like it
 * a {@link TileAtlas} the world renderer draws through unchanged.
 *
 * <p>The sheet is full-colour-on-transparent; each region's cells already carry their own
 * baked colour, so most materials draw them as authored with no per-material tint (a
 * minority still multiply a secondary tint over the baked colour — see
 * {@code TileArtResolver#materialTintRgb}). A region name may back <b>several
 * interchangeable cells</b> (cosmetic variants, TILE-ART-SPEC section 12); the renderer picks
 * one per tile by world position via {@link #region(String, int)} /
 * {@link #variantCount(String)}, so a large surface does not repeat one sprite.
 * {@code Nearest} filtering keeps the 16 px pixel-art crisp (TILE-ART-SPEC section 4).
 * Requires a live GL context to construct; {@link #dispose() dispose} when the screen goes
 * away. Immutable after construction (texture disposal aside).
 */
public final class SheetTileAtlas implements TileAtlas {

    private final Texture texture;
    private final NavigableMap<String, List<TextureRegion>> regions;
    private final SheetAtlasSpec spec;

    private SheetTileAtlas(Texture texture, NavigableMap<String, List<TextureRegion>> regions,
                          SheetAtlasSpec spec) {
        this.texture = texture;
        this.regions = regions;
        this.spec = spec;
    }

    /**
     * Loads {@code sheetFile} and slices every variant cell of every region in {@code spec}.
     *
     * <p>Must run on the render thread with a live GL context (boot / {@code create()}).
     *
     * @param spec      the GL-free layout (variant cell rects per region name)
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
        NavigableMap<String, List<TextureRegion>> built = new TreeMap<>();
        for (String name : spec.regionNames()) {
            List<TextureRegion> variants = new ArrayList<>();
            for (AtlasCellRect rect : spec.cellRects(name)) {
                variants.add(new TextureRegion(texture, rect.x(), rect.y(),
                        rect.width(), rect.height()));
            }
            built.put(name, List.copyOf(variants));
        }
        return new SheetTileAtlas(texture, built, spec);
    }

    @Override
    public TextureRegion region(String regionName) {
        return region(regionName, 0);
    }

    @Override
    public TextureRegion region(String regionName, int variantIndex) {
        List<TextureRegion> variants = regionName == null ? null : regions.get(regionName);
        if (variants == null) {
            throw new IllegalArgumentException(
                    "unknown region \"" + regionName + "\" (have " + regions.size() + ")");
        }
        // Defensive fold so any (even negative) index resolves to a real cell.
        return variants.get(Math.floorMod(variantIndex, variants.size()));
    }

    @Override
    public int variantCount(String regionName) {
        List<TextureRegion> variants = regionName == null ? null : regions.get(regionName);
        return variants == null ? 0 : variants.size();
    }

    @Override
    public VariantPattern variantPattern(String regionName) {
        return spec.variantPattern(regionName);
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
