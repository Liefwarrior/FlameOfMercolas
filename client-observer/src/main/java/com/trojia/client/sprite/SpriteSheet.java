package com.trojia.client.sprite;

import com.badlogic.gdx.files.FileHandle;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.TextureRegion;

import java.util.NavigableMap;
import java.util.TreeMap;

/**
 * The GL half of the sprite index (unified art spec §2.4) — pattern-copied from
 * {@link com.trojia.client.atlas.SheetTileAtlas}: owns one {@link Texture} loaded from
 * {@code content/art/sprites/sprites.png} and interns a {@link TextureRegion} per
 * {@link SpriteRef} of a GL-free {@link SpriteIndex} at boot.
 *
 * <p>Sprites are full-colour-on-transparent, pre-colored in MERCOLAS-24 — the renderer draws
 * them with a white batch color, never a multiply tint (spec §3.3). {@code Nearest} filtering
 * keeps the 16px pixel art crisp. Requires a live GL context to construct;
 * {@link #dispose()} when the screen goes away. Immutable after construction (texture
 * disposal aside).
 */
public final class SpriteSheet {

    private final Texture texture;
    private final NavigableMap<String, TextureRegion> regions;

    private SpriteSheet(Texture texture, NavigableMap<String, TextureRegion> regions) {
        this.texture = texture;
        this.regions = regions;
    }

    /**
     * Loads {@code sheetFile} and slices one region per index entry (a {@code w x h}
     * multi-cell entry becomes one {@code 16w x 16h} region).
     *
     * <p>Must run on the render thread with a live GL context (boot / {@code create()}).
     *
     * @param index     the validated GL-free index
     * @param sheetFile the sheet image on disk, sized to cover every cell in {@code index}
     * @return the sheet; caller owns disposal
     * @throws IllegalArgumentException if either argument is null
     */
    public static SpriteSheet create(SpriteIndex index, FileHandle sheetFile) {
        if (index == null) {
            throw new IllegalArgumentException("index must be non-null");
        }
        if (sheetFile == null) {
            throw new IllegalArgumentException("sheetFile must be non-null");
        }
        Texture texture = new Texture(sheetFile);
        texture.setFilter(Texture.TextureFilter.Nearest, Texture.TextureFilter.Nearest);
        int px = index.tilePx();
        NavigableMap<String, TextureRegion> built = new TreeMap<>();
        for (SpriteRef ref : index.all()) {
            built.put(ref.id(), new TextureRegion(texture, ref.col() * px, ref.row() * px,
                    ref.cellsW() * px, ref.cellsH() * px));
        }
        return new SpriteSheet(texture, built);
    }

    /**
     * The region for {@code ref} (matched by id).
     *
     * @throws IllegalArgumentException if {@code ref} is not from the index this sheet was
     *                                  sliced from
     */
    public TextureRegion region(SpriteRef ref) {
        TextureRegion region = ref == null ? null : regions.get(ref.id());
        if (region == null) {
            throw new IllegalArgumentException("unknown sprite \""
                    + (ref == null ? null : ref.id()) + "\" (have " + regions.size() + ")");
        }
        return region;
    }

    /** The owned sheet texture (regions reference it). */
    public Texture texture() {
        return texture;
    }

    /** Disposes the owned texture; every handed-out region dangles afterwards. */
    public void dispose() {
        texture.dispose();
    }
}
