package com.trojia.client.render;

import com.trojia.client.hud.DayPhase;
import com.trojia.sim.actor.DailyRhythm;

/**
 * The per-frame ambient light of the day/night cycle — a batch-multiply colour the whole
 * scene (terrain, fluids, air-depth look-downs, actors) sits in, plus the lamp-glow gate
 * {@link #lampFactor()}. Presentation-only and a pure function of the engine tick: derived
 * from {@code tick % DailyRhythm.DAY} each frame, never read by sim-core or the
 * {@code WorldHasher}, no determinism constraint (plain float math).
 *
 * <p><b>Phase agreement.</b> The curve's anchor ticks are {@link DayPhase}'s own boundaries
 * ({@code DAY_START}/{@code DUSK_START}/{@code NIGHT_START}), so the light never disagrees
 * with the HUD clock's Dawn/Day/Dusk/Night tag: when the tag flips to Dusk the amber fade is
 * under way; when it flips to Night the dark settle is; the whole Day span is exactly
 * neutral.
 *
 * <p><b>The curve</b> (piecewise smoothstep-eased lerp between keyframes; every segment
 * shares its endpoint with the next, and smoothstep's zero endpoint slope makes the joins
 * C1 — no pop at any boundary, including the midnight wrap):
 * <ul>
 *   <li><b>[0, 1200)</b> pre-dawn: night blue rising into a soft warm sunrise
 *       ({@link #DAWN_R}/{@code G}/{@code B}).</li>
 *   <li><b>[1200, 2000)</b> the warm rise burns off into full neutral by
 *       {@code DayPhase.DAY_START}.</li>
 *   <li><b>[2000, 12000)</b> Day: exactly {@code (1, 1, 1)} — a multiply by this is the
 *       identity, so the daytime game is pixel-identical to the pre-cycle look.</li>
 *   <li><b>[12000, 13000)</b> Dusk: neutral fades to amber
 *       ({@link #DUSK_R}/{@code G}/{@code B}).</li>
 *   <li><b>[13000, 15500)</b> the amber cools and darkens into the night colour, crossing
 *       {@code DayPhase.NIGHT_START} mid-fade.</li>
 *   <li><b>[15500, 24000)</b> Night: holds {@link #NIGHT_R}/{@code G}/{@code B} — a deep
 *       cool darkening (~36% luminance, blue-forward) that still reads the city, wrapping
 *       continuously into the next pre-dawn.</li>
 * </ul>
 *
 * <p>{@link #lampFactor()} gates {@link LampGlowMap}'s warm pools with the same easing:
 * {@code 0} all day, ramping in across Dusk (full slightly before Night starts — lamps are
 * lit as the light fails, not after), holding {@code 1} all night, and burning off during
 * Dawn as the sun takes over. It doubles as the renderer's "nightness" scalar for the
 * slightly-cooler night water.
 *
 * @param r          ambient multiply, red channel, 0..1
 * @param g          ambient multiply, green channel, 0..1
 * @param b          ambient multiply, blue channel, 0..1
 * @param lampFactor lamp-glow gate / nightness, 0 (day) .. 1 (night)
 */
public record AmbientLight(float r, float g, float b, float lampFactor) {

    /** Identity light: full neutral, lamps out — a multiply by this changes nothing. */
    public static final AmbientLight NEUTRAL = new AmbientLight(1f, 1f, 1f, 0f);

    /** Night ambient target — deep cool, blue-forward, luminance &asymp; 0.36 (tuned dark
     * against the docks' pale granite so the lamp pools carry the night scene). */
    static final float NIGHT_R = 0.30f;
    static final float NIGHT_G = 0.36f;
    static final float NIGHT_B = 0.58f;

    /** Dawn keyframe — the soft warm sunrise the pre-dawn blue rises into. */
    static final float DAWN_R = 0.98f;
    static final float DAWN_G = 0.82f;
    static final float DAWN_B = 0.66f;

    /** Dusk keyframe — the amber fade the neutral day light falls into (kept soft: a
     * heavier amber turned the whole district pumpkin in the tuning renders). */
    static final float DUSK_R = 1.00f;
    static final float DUSK_G = 0.83f;
    static final float DUSK_B = 0.64f;

    /** Tick-of-day of the dawn warm peak: 3/5 through the Dawn phase (1,200). */
    static final long DAWN_WARM_T = DayPhase.DAY_START * 3 / 5;

    /** Tick-of-day the pre-dawn lamps finish burning off: 4/5 through Dawn (1,600). */
    static final long LAMPS_OUT_T = DayPhase.DAY_START * 4 / 5;

    /** Tick-of-day of the dusk amber peak: halfway through the Dusk phase (13,000). */
    static final long DUSK_AMBER_T =
            DayPhase.DUSK_START + (DayPhase.NIGHT_START - DayPhase.DUSK_START) / 2;

    /** Tick-of-day the lamps reach full: 3/5 through Dusk, before Night starts (13,200). */
    static final long LAMPS_LIT_T =
            DayPhase.DUSK_START + (DayPhase.NIGHT_START - DayPhase.DUSK_START) * 3 / 5;

    /** Tick-of-day the amber has fully settled into the night colour (15,500). */
    static final long NIGHT_SETTLE_T = DayPhase.NIGHT_START + 1_500;

    /** Ambient keyframe ticks (ascending; first and last colour agree for a seamless wrap). */
    private static final long[] KEY_T = {
            0, DAWN_WARM_T, DayPhase.DAY_START, DayPhase.DUSK_START, DUSK_AMBER_T,
            NIGHT_SETTLE_T, DailyRhythm.DAY};
    private static final float[] KEY_R = {NIGHT_R, DAWN_R, 1f, 1f, DUSK_R, NIGHT_R, NIGHT_R};
    private static final float[] KEY_G = {NIGHT_G, DAWN_G, 1f, 1f, DUSK_G, NIGHT_G, NIGHT_G};
    private static final float[] KEY_B = {NIGHT_B, DAWN_B, 1f, 1f, DUSK_B, NIGHT_B, NIGHT_B};

    /** Lamp-gate keyframes: lit through the night, out by mid-Dawn, relit across Dusk. */
    private static final long[] LAMP_T = {
            0, LAMPS_OUT_T, DayPhase.DUSK_START, LAMPS_LIT_T, DailyRhythm.DAY};
    private static final float[] LAMP_V = {1f, 0f, 0f, 1f, 1f};

    /**
     * The ambient light at {@code tick} (any absolute engine tick — wrapped via
     * {@code tick % DailyRhythm.DAY}, matching {@link DayPhase#of}).
     */
    public static AmbientLight at(long tick) {
        long t = DailyRhythm.tickOfDay(tick);
        return new AmbientLight(
                sample(t, KEY_T, KEY_R),
                sample(t, KEY_T, KEY_G),
                sample(t, KEY_T, KEY_B),
                sample(t, LAMP_T, LAMP_V));
    }

    /** Smoothstep-eased piecewise lerp over an ascending keyframe track. */
    private static float sample(long t, long[] keyT, float[] keyV) {
        for (int i = 1; i < keyT.length; i++) {
            if (t < keyT[i]) {
                float a = keyV[i - 1];
                float b = keyV[i];
                if (a == b) {
                    return a; // exact on flat segments (the Day span stays identity)
                }
                float f = (float) (t - keyT[i - 1]) / (keyT[i] - keyT[i - 1]);
                float eased = f * f * (3f - 2f * f);
                return a + (b - a) * eased;
            }
        }
        return keyV[keyV.length - 1];
    }

    /** Whether this light is the exact identity (skip-tinting fast path for renderers). */
    public boolean isNeutral() {
        return r == 1f && g == 1f && b == 1f && lampFactor == 0f;
    }
}
