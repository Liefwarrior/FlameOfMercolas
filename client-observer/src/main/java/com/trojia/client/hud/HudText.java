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

    /**
     * The time-control HUD line: current tick, active speed setting and total simulated
     * elapsed time as an {@code HH:MM:SS} clock derived cleanly from
     * {@code tick * TickClock.MILLIS_PER_TICK} — no ticks-per-day constant exists yet in
     * sim-core, so this reports a raw elapsed clock rather than a guessed calendar day count.
     *
     * @param tick               the engine's current tick ({@code SimulationEngine#currentTick()})
     * @param speedLabel         the active {@code SpeedSetting}'s name (e.g. {@code "PAUSED"})
     * @param simElapsedSeconds  total simulated elapsed time in seconds
     */
    public static String describeTime(long tick, String speedLabel, long simElapsedSeconds) {
        long hours = simElapsedSeconds / 3600;
        long minutes = (simElapsedSeconds % 3600) / 60;
        long seconds = simElapsedSeconds % 60;
        return String.format(
                "tick=%d  speed=%-6s  elapsed=%02d:%02d:%02d   |  "
                        + "SPACE play/pause   F fast/normal   . step (while paused)",
                tick, speedLabel, hours, minutes, seconds);
    }
}
