package com.trojia.client.atlas;

import com.badlogic.gdx.files.FileHandle;
import com.badlogic.gdx.graphics.Pixmap;
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
 *
 * <p><b>Depth-blur pyramid</b> (the air-depth renderer, Eli 2026-07-15). At load this owns
 * not one texture but {@link #BLUR_LEVELS}: level 0 is the sharp sheet as before, and each
 * higher level is the whole sheet re-uploaded with every referenced cell gaussian-blurred
 * <em>in isolation</em> (edge-clamped, no cross-tile bleed — see {@link TileGaussianBlur}) at
 * an increasing {@link #SIGMAS} sigma. The world renderer draws a tile seen from {@code d}
 * z-levels above through empty air from a deeper level, so it reads softer the further down it
 * lives. The blur is done once here on the CPU at boot, never per frame and never in a shader;
 * lookups stay a plain region fetch. {@code region(name, variant, 0)} is byte-identical to the
 * old sharp path, so the top (non-air) render layer is unchanged.
 */
public final class SheetTileAtlas implements TileAtlas {

    /**
     * Depth-blur pyramid depth including the sharp level 0. Level {@code k} blurs at
     * {@link #SIGMAS}{@code [k]}; the renderer picks {@code clamp(d-1, 0, BLUR_LEVELS-1)} for a
     * tile {@code d} z-levels below empty air (deeper ⇒ blurrier), so the nearest look-down
     * ({@code d==1}) still uses the sharp cell and only its depth-dim marks it as recessed.
     */
    static final int BLUR_LEVELS = 5;

    /**
     * Per-level gaussian sigma, in sheet pixels, indexed by blur level. Index 0 is unused (the
     * sharp source texture is reused verbatim); the ramp stays gentle — even the deepest level
     * blurs with a radius (~{@code ceil(3*sigma)}) well under the 16 px tile — so the effect
     * reads as haze/recession rather than a smear.
     */
    static final float[] SIGMAS = {0f, 0.6f, 1.1f, 1.7f, 2.4f};

    /** One texture per blur level; index 0 is the sharp sheet. */
    private final List<Texture> textures;
    /** Per-blur-level region interning (index-aligned with {@link #textures}). */
    private final List<NavigableMap<String, List<TextureRegion>>> regionsByLevel;
    private final SheetAtlasSpec spec;

    private SheetTileAtlas(List<Texture> textures,
                          List<NavigableMap<String, List<TextureRegion>>> regionsByLevel,
                          SheetAtlasSpec spec) {
        this.textures = textures;
        this.regionsByLevel = regionsByLevel;
        this.spec = spec;
    }

    /**
     * Loads {@code sheetFile}, precomputes the {@link #BLUR_LEVELS}-deep depth-blur pyramid,
     * and slices every variant cell of every region in {@code spec} at each level.
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
        // Load the sheet CPU-side once so the higher blur levels can be convolved without a
        // GPU read-back; level 0 uploads it verbatim (identical to the old new Texture(file)).
        Pixmap sheet = new Pixmap(sheetFile);
        List<Texture> textures = new ArrayList<>(BLUR_LEVELS);
        try {
            textures.add(upload(sheet));
            for (int level = 1; level < BLUR_LEVELS; level++) {
                Pixmap blurred = TileGaussianBlur.blurSheet(sheet, spec, SIGMAS[level]);
                try {
                    textures.add(upload(blurred));
                } finally {
                    blurred.dispose();
                }
            }
        } finally {
            sheet.dispose();
        }
        List<NavigableMap<String, List<TextureRegion>>> regionsByLevel =
                new ArrayList<>(BLUR_LEVELS);
        for (Texture texture : textures) {
            regionsByLevel.add(slice(texture, spec));
        }
        return new SheetTileAtlas(textures, regionsByLevel, spec);
    }

    /** Uploads a pixmap as a Nearest-filtered texture (crisp 16 px pixel-art). */
    private static Texture upload(Pixmap pixmap) {
        Texture texture = new Texture(pixmap);
        texture.setFilter(Texture.TextureFilter.Nearest, Texture.TextureFilter.Nearest);
        return texture;
    }

    /** Interns a region-name -> variant-cell list map against one level's texture. */
    private static NavigableMap<String, List<TextureRegion>> slice(Texture texture,
            SheetAtlasSpec spec) {
        NavigableMap<String, List<TextureRegion>> built = new TreeMap<>();
        for (String name : spec.regionNames()) {
            List<TextureRegion> variants = new ArrayList<>();
            for (AtlasCellRect rect : spec.cellRects(name)) {
                variants.add(new TextureRegion(texture, rect.x(), rect.y(),
                        rect.width(), rect.height()));
            }
            built.put(name, List.copyOf(variants));
        }
        return built;
    }

    @Override
    public TextureRegion region(String regionName) {
        return region(regionName, 0, 0);
    }

    @Override
    public TextureRegion region(String regionName, int variantIndex) {
        return region(regionName, variantIndex, 0);
    }

    @Override
    public TextureRegion region(String regionName, int variantIndex, int blurLevel) {
        int level = clampLevel(blurLevel);
        List<TextureRegion> variants =
                regionName == null ? null : regionsByLevel.get(level).get(regionName);
        if (variants == null) {
            throw new IllegalArgumentException(
                    "unknown region \"" + regionName + "\" (have "
                            + regionsByLevel.get(level).size() + ")");
        }
        // Defensive fold so any (even negative) index resolves to a real cell.
        return variants.get(Math.floorMod(variantIndex, variants.size()));
    }

    @Override
    public int blurLevelCount() {
        return textures.size();
    }

    private int clampLevel(int blurLevel) {
        if (blurLevel < 0) {
            return 0;
        }
        int last = textures.size() - 1;
        return blurLevel > last ? last : blurLevel;
    }

    @Override
    public int variantCount(String regionName) {
        List<TextureRegion> variants =
                regionName == null ? null : regionsByLevel.get(0).get(regionName);
        return variants == null ? 0 : variants.size();
    }

    @Override
    public VariantPattern variantPattern(String regionName) {
        return spec.variantPattern(regionName);
    }

    @Override
    public boolean contains(String regionName) {
        return regionName != null && regionsByLevel.get(0).containsKey(regionName);
    }

    /** All region names in ascending ASCII order; unmodifiable. */
    public NavigableSet<String> regionNames() {
        return regionsByLevel.get(0).navigableKeySet();
    }

    /** The owned sharp (level 0) sheet texture (regions reference it). */
    public Texture texture() {
        return textures.get(0);
    }

    /** Disposes every blur-level texture; all handed-out regions dangle afterwards. */
    @Override
    public void dispose() {
        for (Texture texture : textures) {
            texture.dispose();
        }
    }
}
