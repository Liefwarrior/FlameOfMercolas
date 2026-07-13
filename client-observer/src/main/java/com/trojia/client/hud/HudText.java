package com.trojia.client.hud;

/**
 * Pure formatting for the observer's on-screen readout (M1 Behavior 4, DoD9): GL-free so
 * the text content is unit-testable independent of font rendering.
 */
public final class HudText {

    private HudText() {
    }

    /**
     * The single HUD line for the given navigation state.
     *
     * @param z    the currently drawn world z-level
     * @param zoom the current integer zoom ({@link com.trojia.client.camera.MapCamera#zoom()})
     * @return a legible one-line status string, key-binding reminder included
     */
    public static String describe(int z, int zoom) {
        return String.format(
                "z=%d  zoom=%dx   |  WASD/Arrows pan   [ ] zoom   PgUp/PgDn z-level   ESC quit",
                z, zoom);
    }
}
