package com.trojia.client.hud;

import com.trojia.sim.actor.DailyRhythm;

/**
 * The coarse time-of-day tag shown next to the HUD clock's {@code HH:MM} digits — a label the
 * eye catches faster than digits. Presentation-only: derived from {@code tick % DailyRhythm.DAY}
 * each frame, no sim state.
 *
 * <p>The boundaries are derived from how the sim <em>actually</em> uses the day, so the tag
 * never lies about behavior (all values are ticks-of-day out of {@link DailyRhythm#DAY} =
 * 24,000; the HUD clock maps that span onto a 24h readout, so 1,000 ticks = 1 displayed hour):
 * <ul>
 *   <li><b>Dawn [0, 2,000)</b> — {@code DailyRhythm}'s day starts at dawn (its anchor points
 *       are dawn 0, noon 6,000, dusk 12,000, midnight 18,000). Day-work {@code rhythmWindow}s
 *       in {@code content/raws/jobs/jobs.json} open progressively across this span (earliest
 *       500/1,000, latest 2,000): the wake-up/ramp-up stretch.</li>
 *   <li><b>Day [2,000, 12,000)</b> — every day-shift job window is open by 2,000; no actor
 *       type's {@code returnHome} night window has started yet.</li>
 *   <li><b>Dusk [12,000, 14,000)</b> — the going-home spread: {@code nightWindowStart} across
 *       {@code content/raws/actors} runs from 12,000 (militia_watch — also {@code DailyRhythm}'s
 *       dusk anchor) through 14,000 (serf, shopkeeper). Work and homing overlap here.</li>
 *   <li><b>Night [14,000, 24,000)</b> — every home-going actor type's night window is open
 *       (latest start 14,000), the longest day-work window ([500, 13,500]) has closed, and
 *       night-shift job windows ([18,000, 24,000]) sit wholly inside it.</li>
 * </ul>
 */
public enum DayPhase {

    DAWN("Dawn"),
    DAY("Day"),
    DUSK("Dusk"),
    NIGHT("Night");

    // Public: the day/night lighting cycle (render.AmbientLight) anchors its curve on these
    // same boundaries so the light and the HUD clock tag can never disagree.

    /** First tick-of-day where every day-shift job rhythm window is open (jobs.json). */
    public static final long DAY_START = 2_000L;
    /** DailyRhythm's dusk anchor; earliest actor-type nightWindowStart (militia_watch). */
    public static final long DUSK_START = 12_000L;
    /** Latest actor-type nightWindowStart (serf/shopkeeper) — all night windows open here. */
    public static final long NIGHT_START = 14_000L;

    private final String label;

    DayPhase(String label) {
        this.label = label;
    }

    /** The short display tag ({@code Dawn}/{@code Day}/{@code Dusk}/{@code Night}). */
    public String label() {
        return label;
    }

    /** The phase at {@code tick} (any absolute engine tick — wrapped via {@code tick % DAY}). */
    public static DayPhase of(long tick) {
        long tickOfDay = DailyRhythm.tickOfDay(tick);
        if (tickOfDay < DAY_START) {
            return DAWN;
        }
        if (tickOfDay < DUSK_START) {
            return DAY;
        }
        if (tickOfDay < NIGHT_START) {
            return DUSK;
        }
        return NIGHT;
    }
}
