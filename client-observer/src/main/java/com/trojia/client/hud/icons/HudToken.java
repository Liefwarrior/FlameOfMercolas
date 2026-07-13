package com.trojia.client.hud.icons;

/**
 * One GL-free piece of a HUD/inspector line: either a literal text run or a reference to an
 * {@link IconKey} glyph. Lets {@code HudText}/{@code InspectorText} describe a keybinding
 * legend that interleaves icon glyphs with words while staying entirely GL-free and
 * unit-testable (content only — no font, no texture); {@link IconTextLine} is the GL-side
 * class that turns a list of these into actual icon quads and {@code font.draw} calls.
 */
public sealed interface HudToken {

    /** A literal text run, drawn with the caller's current font/color. */
    record Text(String value) implements HudToken {
    }

    /** One icon glyph, drawn at the font's line height from {@link IconAtlas}. */
    record Icon(IconKey key) implements HudToken {
    }

    static HudToken text(String value) {
        return new Text(value);
    }

    static HudToken icon(IconKey key) {
        return new Icon(key);
    }
}
