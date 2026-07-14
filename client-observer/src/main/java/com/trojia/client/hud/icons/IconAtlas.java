package com.trojia.client.hud.icons;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.Pixmap;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.badlogic.gdx.utils.Disposable;

import java.nio.file.Path;
import java.util.EnumMap;
import java.util.Map;

/**
 * The GL half of the icon lookup: one {@link Texture} per {@link IconKey}, loaded from the
 * Kenney "Input Prompts" pack's {@code Keyboard & Mouse/Default} folder. Unlike
 * {@code atlas.SheetTileAtlas}, there is no packed sheet to slice — the pack ships one
 * already-cropped 64&times;64 PNG per glyph — and the full icon set this observer ever needs
 * is a small, fixed handful of keys, so one texture per icon keeps this simple rather than
 * packing a runtime atlas for no real benefit.
 *
 * <p>Requires a live GL context to construct (boot / {@code create()}); {@link #dispose()}
 * when the screen goes away, mirroring every other GL-owning class in this client.
 */
public final class IconAtlas implements Disposable {

    private final Map<IconKey, Texture> textures;
    private final Map<IconKey, TextureRegion> regions;
    private final Texture whitePixelTexture;
    private final TextureRegion whitePixel;

    private IconAtlas(Map<IconKey, Texture> textures, Map<IconKey, TextureRegion> regions,
            Texture whitePixelTexture, TextureRegion whitePixel) {
        this.textures = textures;
        this.regions = regions;
        this.whitePixelTexture = whitePixelTexture;
        this.whitePixel = whitePixel;
    }

    /**
     * Loads every {@link IconKey}'s PNG from {@code iconDir} (the pack's
     * {@code Keyboard & Mouse/Default} directory on disk).
     *
     * <p>Must run on the render thread with a live GL context.
     *
     * @param iconDir directory containing one {@code *.png} file per {@link IconKey#fileName()}
     * @return the atlas; caller owns disposal
     */
    public static IconAtlas load(Path iconDir) {
        Map<IconKey, Texture> textures = new EnumMap<>(IconKey.class);
        Map<IconKey, TextureRegion> regions = new EnumMap<>(IconKey.class);
        for (IconKey key : IconKey.values()) {
            Path file = iconDir.resolve(key.fileName());
            Texture texture = new Texture(Gdx.files.absolute(file.toAbsolutePath().toString()));
            // These are smooth vector-drawn glyphs (not 16px pixel art), so linear filtering
            // renders them cleanly when the HUD scales them to the font's line height.
            texture.setFilter(Texture.TextureFilter.Linear, Texture.TextureFilter.Linear);
            textures.put(key, texture);
            regions.put(key, new TextureRegion(texture));
        }
        Pixmap pixmap = new Pixmap(1, 1, Pixmap.Format.RGBA8888);
        pixmap.setColor(Color.WHITE);
        pixmap.fill();
        Texture whitePixelTexture = new Texture(pixmap);
        pixmap.dispose();
        TextureRegion whitePixel = new TextureRegion(whitePixelTexture);
        return new IconAtlas(textures, regions, whitePixelTexture, whitePixel);
    }

    /** The icon glyph for {@code key}; every {@link IconKey} is loaded, so never null. */
    public TextureRegion region(IconKey key) {
        return regions.get(key);
    }

    /** A 1&times;1 opaque white texture region — {@code com.trojia.client.hud.HudPanel}'s
     * solid-color fill primitive, stretched to any rectangle via {@code SpriteBatch}'s own
     * tint/scale (no new GL resource needs threading through every draw call site since this
     * atlas is already everywhere). */
    public TextureRegion whitePixel() {
        return whitePixel;
    }

    /** Disposes every owned texture (including the synthesized white pixel); every handed-out
     * region dangles afterward. */
    @Override
    public void dispose() {
        textures.values().forEach(Texture::dispose);
        whitePixelTexture.dispose();
    }
}
