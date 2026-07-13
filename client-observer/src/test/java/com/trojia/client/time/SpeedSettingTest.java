package com.trojia.client.time;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link SpeedSetting} contracts per the ARCHITECTURE section 3 SimulationDriver entry
 * (fixed-timestep, 100 ms/tick TickClock, 12 ms frame tick budget).
 */
class SpeedSettingTest {

    @Test
    void pausedNeverAdvances() {
        assertEquals(0, SpeedSetting.PAUSED.multiplier());
        assertFalse(SpeedSetting.PAUSED.autoAdvances());
        assertFalse(SpeedSetting.PAUSED.manualStep());
        assertEquals(0, SpeedSetting.PAUSED.ticksPerSecond());
        assertThrows(IllegalStateException.class, SpeedSetting.PAUSED::tickPeriodMillis);
    }

    @Test
    void stepAdvancesOnlyByExplicitRequest() {
        assertEquals(0, SpeedSetting.STEP.multiplier());
        assertFalse(SpeedSetting.STEP.autoAdvances());
        assertTrue(SpeedSetting.STEP.manualStep());
        assertEquals(0, SpeedSetting.STEP.ticksPerSecond());
        assertThrows(IllegalStateException.class, SpeedSetting.STEP::tickPeriodMillis);
    }

    @Test
    void runIsRealTime() {
        assertEquals(1, SpeedSetting.RUN.multiplier());
        assertTrue(SpeedSetting.RUN.autoAdvances());
        assertFalse(SpeedSetting.RUN.manualStep());
        assertEquals(SpeedSetting.BASE_TICK_MILLIS, SpeedSetting.RUN.tickPeriodMillis());
        assertEquals(10, SpeedSetting.RUN.ticksPerSecond());
    }

    @Test
    void fastIsFourTimesRealTime() {
        assertEquals(4, SpeedSetting.FAST.multiplier());
        assertTrue(SpeedSetting.FAST.autoAdvances());
        assertEquals(25, SpeedSetting.FAST.tickPeriodMillis());
        assertEquals(40, SpeedSetting.FAST.ticksPerSecond());
    }

    @Test
    void constantsMatchTheArchitectureEntry() {
        assertEquals(100, SpeedSetting.BASE_TICK_MILLIS); // TickClock: 100 ms/tick
        assertEquals(12, SpeedSetting.FRAME_TICK_BUDGET_MILLIS); // whole ticks in 12 ms
    }

    @Test
    void onlyNonAdvancingSettingsLackAPeriod() {
        for (SpeedSetting setting : SpeedSetting.values()) {
            if (setting.autoAdvances()) {
                assertTrue(setting.tickPeriodMillis() > 0);
                assertEquals(1000 / setting.tickPeriodMillis(), setting.ticksPerSecond());
            } else {
                assertThrows(IllegalStateException.class, setting::tickPeriodMillis);
            }
        }
    }
}
