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
     * The first-person orientation readout: which band the eye is standing on, and where it is
     * looking. Absolute world z, never authored-relative — the docks' authored {@code z:+11}
     * quayside is band 19 — because a readout that is eight bands out looks perfectly plausible
     * and is completely wrong.
     *
     * @param band    the absolute world z the driven actor is standing on
     * @param point   the eight-point compass label
     * @param bearing compass degrees, 0 north
     */
    public static String eyeLine(int band, String point, int bearing) {
        return String.format("eye z=%d  facing %-2s %03d", band, point, bearing);
    }

    /** Displayed minutes in a day: the {@link DailyRhythm#DAY}-tick sim day reads as 24h. */
    private static final long MINUTES_PER_DAY = 24 * 60;

    /**
     * The time-of-day clock: {@code Day N, HH:MM Phase}. The {@link DailyRhythm#DAY}-tick sim
     * day (24,000 ticks) is mapped onto a 24-hour readout — {@code minutesOfDay = tickOfDay *
     * 1440 / 24000}, so 1,000 ticks read as one displayed hour and the digits line up with the
     * rhythm anchors the sim actually keys off ({@code tick % DAY}): dawn 00:00, noon 06:00,
     * dusk 12:00, midnight 18:00. Days are 1-based ({@code Day 1} at boot). The trailing
     * {@link DayPhase} tag ({@code Dawn}/{@code Day}/{@code Dusk}/{@code Night}) is derived
     * from the same tick and padded to a fixed width so the columns after it never jitter.
     * Pure presentation — {@code DailyRhythm.DAY} itself is untouched.
     *
     * @param tick the engine's current tick ({@code SimulationEngine#currentTick()})
     */
    public static String clock(long tick) {
        long day = tick / DailyRhythm.DAY + 1;
        long minutesOfDay = DailyRhythm.tickOfDay(tick) * MINUTES_PER_DAY / DailyRhythm.DAY;
        return String.format("Day %d, %02d:%02d %-5s",
                day, minutesOfDay / 60, minutesOfDay % 60, DayPhase.of(tick).label());
    }

    /**
     * The full status portion of the time-control HUD line: the {@link #clock} readout, the
     * active speed setting, and the raw tick count (kept for dev use; rendered as a dim suffix
     * by {@link #describeTimeTokens}). No keybinding reminder — see
     * {@link #timeKeybindingTokens}.
     *
     * @param tick       the engine's current tick ({@code SimulationEngine#currentTick()})
     * @param speedLabel the active {@code SpeedSetting}'s name (e.g. {@code "PAUSED"})
     */
    public static String describeTime(long tick, String speedLabel) {
        return clockAndSpeed(tick, speedLabel) + tickSuffix(tick);
    }

    private static String clockAndSpeed(long tick, String speedLabel) {
        return clock(tick) + String.format("  speed=%-6s", speedLabel);
    }

    private static String tickSuffix(long tick) {
        return "  tick=" + tick;
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

    /**
     * The PLAY-MODE verb legend (Sprint 4, the playtest's top defect: the entire social
     * verb surface was undiscoverable — no key appeared on any screen): shown as the HUD's
     * third line while an actor is being driven. Movement is deliberately absent
     * ({@code WASD} already reads as movement from the nav line's pan group being
     * suppressed); this line carries the VERBS.
     *
     * <p><b>This is the line the anti-stale guard checks.</b> Every key a play-mode input class
     * binds and can actually be pressed while the tile view is up has to be here — see
     * {@code HudTextTest}, and see {@link #firstPersonKeybindingTokens} for the one narrow
     * category that is allowed to live on the other line instead.
     */
    public static List<HudToken> playModeKeybindingTokens() {
        return List.of(
                HudToken.icon(IconKey.T), HudToken.text(" talk   "),
                HudToken.icon(IconKey.G), HudToken.text(" pickpocket   "),
                HudToken.icon(IconKey.E), HudToken.text(" eat   "),
                HudToken.icon(IconKey.R), HudToken.text(" fish   "),
                // Sprint 8 (the scalp loop): the two verbs the sprint shipped. They were on no
                // screen at all until this line carried them — the S4 playtest's #1 fix,
                // re-opened; HudTextTest now reads the input package itself so a verb bound
                // without a legend entry fails the build rather than shipping invisible.
                HudToken.icon(IconKey.K), HudToken.text(" cull   "),
                HudToken.icon(IconKey.B), HudToken.text(" sell   "),
                HudToken.icon(IconKey.ARROW_UP), HudToken.icon(IconKey.ARROW_DOWN),
                HudToken.text(" climb   "),
                // The turn keys are LIVE ON THE MAP (FirstPersonInput.pollTurn runs from the
                // tile view too, so the facing wedge is something you aim before you press V).
                // They shipped in round 2 on no screen while driving, and the anti-stale guard
                // was widened to let that through instead of the legend being corrected. They
                // belong here, on the line that is actually in front of the player.
                HudToken.icon(IconKey.ARROW_LEFT), HudToken.icon(IconKey.ARROW_RIGHT),
                HudToken.text(" turn   "),
                HudToken.icon(IconKey.SHIFT), HudToken.text(" turn fast   "),
                HudToken.icon(IconKey.I), HudToken.text(" disguise   "),
                HudToken.icon(IconKey.J), HudToken.text(" journal   "),
                // The switch has to be findable from the map, or nobody ever presses it.
                HudToken.icon(IconKey.V), HudToken.text(" first person   "),
                HudToken.icon(IconKey.P), HudToken.text(" release"));
    }

    /**
     * The legend shown in place of the verb line while the first-person view is up. The verbs
     * themselves do not change — a stair is a stair from either camera — but the keys that
     * mean something different do, and those are the ones a player needs told: WASD is now
     * forward and strafe relative to where you are looking rather than north and east, and
     * the left/right arrows turn instead of scrubbing floors.
     *
     * <p>This line is <b>not</b> a place to park a binding that the map-side legend ought to
     * carry. It only gets to be the sole home of a key that <em>does nothing at all</em> until
     * this frame is up — today that is exactly {@code PageUp}/{@code PageDown}, the look
     * keys — and {@code HudTextTest} enforces both halves of that: the category is named
     * explicitly, and every key in it is checked against the input sources to prove it really
     * is unreachable from the tile view.
     */
    public static List<HudToken> firstPersonKeybindingTokens() {
        return List.of(
                HudToken.icon(IconKey.W), HudToken.icon(IconKey.A), HudToken.icon(IconKey.S),
                HudToken.icon(IconKey.D), HudToken.text(" walk   "),
                HudToken.icon(IconKey.ARROW_LEFT), HudToken.icon(IconKey.ARROW_RIGHT),
                HudToken.text(" turn   "),
                HudToken.icon(IconKey.SHIFT), HudToken.text(" turn fast   "),
                HudToken.icon(IconKey.PAGE_UP), HudToken.icon(IconKey.PAGE_DOWN),
                HudToken.text(" look   "),
                HudToken.icon(IconKey.ARROW_UP), HudToken.icon(IconKey.ARROW_DOWN),
                HudToken.text(" climb   "),
                HudToken.icon(IconKey.V), HudToken.text(" back to the map"));
    }

    /** {@link #purse}'s "no bank account behind this hand" sentinel for {@code royals}. */
    public static final long NO_ACCOUNT = -1L;

    /**
     * The played actor's PURSE readout (S8): the two kinds of money in this economy, named
     * apart because they behave differently — {@code Royals} are a ledger balance a counter
     * sale settles into ({@code BankLedger}), {@code Coins} are the physical specie in the
     * pack. Selling a scalp moves the Royals number and leaves the Coins number alone, and a
     * player has to be able to see that. {@code royals == }{@link #NO_ACCOUNT} (no ID card, so
     * no account authorizes anything) reads as {@code "no account"} rather than as zero — an
     * unbanked hand is not a broke one.
     */
    public static String purse(long royals, int coins) {
        String banked = royals == NO_ACCOUNT ? "(no account)" : royals + " Royals (banked)";
        return "PURSE  " + banked + "   " + coins + " Coins (carried)";
    }

    /** {@link #purse} as the HUD's own token line (its own row while an actor is driven). */
    public static List<HudToken> purseTokens(long royals, int coins) {
        return List.of(HudToken.text(purse(royals, coins)));
    }

    /**
     * The OBSERVER verb legend (the same third HUD line while no actor is driven, on a
     * populated fixture): selection, follow, play, nameplates, journal.
     */
    public static List<HudToken> observerVerbKeybindingTokens() {
        return List.of(
                HudToken.icon(IconKey.MOUSE_LEFT_CLICK), HudToken.text(" select   "),
                HudToken.icon(IconKey.C), HudToken.text(" follow   "),
                HudToken.icon(IconKey.P), HudToken.text(" play as   "),
                HudToken.icon(IconKey.N), HudToken.text(" names (hold)   "),
                HudToken.icon(IconKey.J), HudToken.text(" journal   "),
                HudToken.icon(IconKey.M), HudToken.text(" masters   "),
                HudToken.icon(IconKey.L), HudToken.text(" feed"));
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
     * full time-control HUD line, ready to hand to {@code IconTextLine.draw}. The raw-tick
     * suffix is a {@link HudToken#dimText dim} token: still on screen for devs, but visually
     * subordinate to the {@code Day N, HH:MM Phase} clock. */
    public static List<HudToken> describeTimeTokens(long tick, String speedLabel) {
        List<HudToken> tokens = new ArrayList<>();
        tokens.add(HudToken.text(clockAndSpeed(tick, speedLabel)));
        tokens.add(HudToken.dimText(tickSuffix(tick)));
        tokens.addAll(timeKeybindingTokens());
        return tokens;
    }
}
