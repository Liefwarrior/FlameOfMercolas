package com.trojia.client.hud;

import com.trojia.client.hud.icons.HudToken;
import com.trojia.client.hud.icons.IconKey;
import com.trojia.sim.actor.DailyRhythm;

import java.util.ArrayList;
import java.util.List;

/**
 * Pure formatting for the observer's on-screen readout (M1 Behavior 4, DoD9): GL-free so
 * the text content is unit-testable independent of font rendering. {@link #describe} and
 * {@link #describeTime} stay plain strings covering the status portion of each line (still
 * exactly what {@code HudTextTest} exercises); the keybinding-reminder portion of each line
 * is now a {@link HudToken} list ({@link #navKeybindingTokens}/{@link #timeKeybindingTokens},
 * combined with the status text by {@link #describeTokens}/{@link #describeTimeTokens}) so
 * the draw call sites can render real icon glyphs for the key names instead of spelling them
 * out — {@link HudToken}/{@link IconKey} are themselves GL-free (just an enum and two record
 * types), so this class stays free of any font/texture dependency.
 */
public final class HudText {

    private HudText() {
    }

    /**
     * The status portion of the navigation HUD line (no keybinding reminder — see
     * {@link #navKeybindingTokens}).
     *
     * @param z    the currently drawn world z-level
     * @param zoom the current integer zoom ({@link com.trojia.client.camera.MapCamera#zoom()})
     * @return a legible one-line status string
     */
    public static String describe(int z, int zoom) {
        return String.format("z=%d  zoom=%dx", z, zoom);
    }

    /**
     * The status portion of the time-control HUD line: a {@code Day N, HH:MM:SS} clock derived
     * straight from {@code tick} (one tick is one simulated second — {@code TickClock.
     * MILLIS_PER_TICK}), plus the active speed setting and the raw tick count. The day/time
     * split uses {@link DailyRhythm#DAY} (24,000 simulated seconds); at 1 tick = 1 second that
     * is a real, honest ~6h40m day, not a literal 24-hour one — {@code DailyRhythm.DAY} is left
     * unchanged because every job/actor-type rhythm window in {@code content/raws} is tuned
     * against that scale. No keybinding reminder — see {@link #timeKeybindingTokens}.
     *
     * @param tick       the engine's current tick ({@code SimulationEngine#currentTick()})
     * @param speedLabel the active {@code SpeedSetting}'s name (e.g. {@code "PAUSED"})
     */
    public static String describeTime(long tick, String speedLabel) {
        long day = tick / DailyRhythm.DAY;
        long secondsOfDay = tick % DailyRhythm.DAY;
        long hours = secondsOfDay / 3600;
        long minutes = (secondsOfDay % 3600) / 60;
        long seconds = secondsOfDay % 60;
        return String.format("Day %d, %02d:%02d:%02d  speed=%-6s  tick=%d",
                day, hours, minutes, seconds, speedLabel, tick);
    }

    /**
     * The navigation keybinding legend: WASD/left-right-arrow icons for pan, bracket icons for
     * zoom, up/down-arrow icons for z-level, an Escape icon to quit — the icon-augmented
     * replacement for the old {@code "WASD/Arrows pan   [ ] zoom   PgUp/PgDn z-level   ESC
     * quit"} text. Up/Down arrows are z-level ONLY (Dwarf-Fortress-style level scrub); they are
     * deliberately absent from the pan group since {@link com.trojia.client.input.CameraInput}
     * no longer binds them to panning.
     */
    public static List<HudToken> navKeybindingTokens() {
        return List.of(
                HudToken.text("   |  "),
                HudToken.icon(IconKey.W), HudToken.icon(IconKey.A),
                HudToken.icon(IconKey.S), HudToken.icon(IconKey.D),
                HudToken.text(" / "),
                HudToken.icon(IconKey.ARROW_LEFT), HudToken.icon(IconKey.ARROW_RIGHT),
                HudToken.text(" pan   "),
                HudToken.icon(IconKey.BRACKET_OPEN), HudToken.icon(IconKey.BRACKET_CLOSE),
                HudToken.text(" zoom   "),
                HudToken.icon(IconKey.ARROW_UP), HudToken.icon(IconKey.ARROW_DOWN),
                HudToken.text(" z-level   "),
                HudToken.icon(IconKey.ESCAPE),
                HudToken.text(" quit"));
    }

    /**
     * The time-control keybinding legend: Space/F/period icons — the icon-augmented
     * replacement for the old {@code "SPACE play/pause   F fast/normal   . step (while
     * paused)"} text.
     */
    public static List<HudToken> timeKeybindingTokens() {
        return List.of(
                HudToken.text("   |  "),
                HudToken.icon(IconKey.SPACE),
                HudToken.text(" play/pause   "),
                HudToken.icon(IconKey.F),
                HudToken.text(" fast/normal   "),
                HudToken.icon(IconKey.PERIOD),
                HudToken.text(" step (while paused)"));
    }

    /** {@link #describe}'s status text followed by {@link #navKeybindingTokens} — the full
     * navigation HUD line, ready to hand to {@code IconTextLine.draw}. */
    public static List<HudToken> describeTokens(int z, int zoom) {
        List<HudToken> tokens = new ArrayList<>();
        tokens.add(HudToken.text(describe(z, zoom)));
        tokens.addAll(navKeybindingTokens());
        return tokens;
    }

    /** {@link #describeTime}'s status text followed by {@link #timeKeybindingTokens} — the
     * full time-control HUD line, ready to hand to {@code IconTextLine.draw}. */
    public static List<HudToken> describeTimeTokens(long tick, String speedLabel) {
        List<HudToken> tokens = new ArrayList<>();
        tokens.add(HudToken.text(describeTime(tick, speedLabel)));
        tokens.addAll(timeKeybindingTokens());
        return tokens;
    }
}
