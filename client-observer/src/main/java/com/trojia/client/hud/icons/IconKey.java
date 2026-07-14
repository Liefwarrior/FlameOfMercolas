package com.trojia.client.hud.icons;

/**
 * GL-free identifier + source filename for one Kenney "Input Prompts" key/button glyph used
 * by the observer's HUD/inspector overlays. Mirrors the spec/loader seam used elsewhere in
 * this client (e.g. {@code atlas.SheetAtlasSpec} / {@code atlas.SheetTileAtlas}) but with far
 * less ceremony: this pack ships one already-cropped 64&times;64 PNG per glyph rather than a
 * packed sheet, so the "spec" half of that seam is just a filename, not grid math — see
 * {@link IconAtlas} for the GL-loading half.
 *
 * <p>Source: {@code content/art/kenney-input-prompts/Keyboard & Mouse/Default/*.png}
 * (Kenney's CC0 "Input Prompts" pack, the {@code Keyboard & Mouse} device family, {@code
 * Default} = 64px raster variant). Every key/button the observer's shipped keybindings
 * actually reference has an entry here; add more only when a new keybinding needs one.
 */
public enum IconKey {
    SPACE("keyboard_space.png"),
    W("keyboard_w.png"),
    A("keyboard_a.png"),
    S("keyboard_s.png"),
    D("keyboard_d.png"),
    ARROW_UP("keyboard_arrow_up.png"),
    ARROW_DOWN("keyboard_arrow_down.png"),
    ARROW_LEFT("keyboard_arrow_left.png"),
    ARROW_RIGHT("keyboard_arrow_right.png"),
    BRACKET_OPEN("keyboard_bracket_open.png"),
    BRACKET_CLOSE("keyboard_bracket_close.png"),
    ESCAPE("keyboard_escape.png"),
    F("keyboard_f.png"),
    PERIOD("keyboard_period.png"),
    C("keyboard_c.png"),
    MOUSE_LEFT_CLICK("mouse_left.png");

    private final String fileName;

    IconKey(String fileName) {
        this.fileName = fileName;
    }

    /** The PNG filename within the pack's {@code Keyboard & Mouse/Default/} folder. */
    public String fileName() {
        return fileName;
    }
}
