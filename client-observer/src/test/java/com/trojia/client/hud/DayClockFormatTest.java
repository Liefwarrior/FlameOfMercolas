package com.trojia.client.hud;

import com.trojia.sim.actor.DailyRhythm;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * The tick -> {@code "Day N, HH:MM Phase"} mapping ({@link HudText#clock} + {@link DayPhase})
 * — pure, headless. The 24,000-tick sim day reads as a 24h clock ({@code minutesOfDay =
 * tickOfDay * 1440 / 24000}), days are 1-based, and the phase tag tracks the sim's real
 * behavior windows (see {@link DayPhase}'s boundary derivation).
 */
class DayClockFormatTest {

    private static String clock(long tick) {
        // The phase column is padded to a fixed width for stable HUD columns; trim so the
        // vectors below assert exact content without the layout padding.
        return HudText.clock(tick).trim();
    }

    @Test
    void bootTickIsDayOneMidnightAtDawn() {
        assertEquals("Day 1, 00:00 Dawn", clock(0));
    }

    @Test
    void midDayTickReadsTwelveHundred() {
        // 12,000 ticks = half the 24,000-tick day = 12:00 — and DailyRhythm's dusk anchor,
        // where the earliest returnHome night window (militia_watch) opens.
        assertEquals("Day 1, 12:00 Dusk", clock(12_000));
    }

    @Test
    void lastTickOfTheDayReadsTwentyThreeFiftyNine() {
        assertEquals("Day 1, 23:59 Night", clock(23_999));
    }

    @Test
    void dayRolloverWrapsTheClockAndAdvancesTheDay() {
        assertEquals("Day 2, 00:00 Dawn", clock(24_000));
    }

    @Test
    void hoursAndMinutesAreZeroPadded() {
        // 3,661 * 1440 / 24000 = 219 minutes = 03:39 (integer math, floor).
        assertEquals("Day 1, 03:39 Day", clock(3_661));
        // 100 * 1440 / 24000 = 6 minutes = 00:06.
        assertEquals("Day 1, 00:06 Dawn", clock(100));
    }

    @Test
    void phaseBoundariesMatchTheSimsBehaviorWindows() {
        // Dawn [0, 2000): job rhythm windows are still opening (earliest 500, latest 2000).
        assertEquals(DayPhase.DAWN, DayPhase.of(0));
        assertEquals(DayPhase.DAWN, DayPhase.of(1_999));
        // Day [2000, 12000): every day-shift job window open, no night window yet.
        assertEquals(DayPhase.DAY, DayPhase.of(2_000));
        assertEquals(DayPhase.DAY, DayPhase.of(11_999));
        // Dusk [12000, 14000): nightWindowStart spread across the actor raws —
        // 12000 (militia_watch) through 14000 (serf/shopkeeper).
        assertEquals(DayPhase.DUSK, DayPhase.of(12_000));
        assertEquals(DayPhase.DUSK, DayPhase.of(13_999));
        // Night [14000, 24000): every home-going type's night window is open.
        assertEquals(DayPhase.NIGHT, DayPhase.of(14_000));
        assertEquals(DayPhase.NIGHT, DayPhase.of(23_999));
    }

    @Test
    void phaseWrapsAcrossDaysLikeTheRhythmWindowsDo() {
        // Same tick % DAY wrap the sim's rhythm windows key off.
        assertEquals(DayPhase.DAWN, DayPhase.of(DailyRhythm.DAY));
        assertEquals(DayPhase.NIGHT, DayPhase.of(DailyRhythm.DAY + 15_000));
        assertEquals(DayPhase.DUSK, DayPhase.of(3 * DailyRhythm.DAY + 12_500));
    }
}
