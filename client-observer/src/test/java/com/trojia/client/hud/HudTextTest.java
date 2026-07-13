package com.trojia.client.hud;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

/** {@link HudText} formatting — pure, headless. */
class HudTextTest {

    @Test
    void describesZAndZoom() {
        String line = HudText.describe(9, 3);
        assertTrue(line.contains("z=9"), () -> "expected z=9 in: " + line);
        assertTrue(line.contains("zoom=3x"), () -> "expected zoom=3x in: " + line);
    }

    @Test
    void handlesTheDefaultBootState() {
        String line = HudText.describe(9, 1);
        assertTrue(line.contains("z=9"));
        assertTrue(line.contains("zoom=1x"));
    }

    @Test
    void describesTickSpeedAndElapsedClock() {
        // 3661 seconds = 1h 01m 01s.
        String line = HudText.describeTime(37, "RUN", 3661);
        assertTrue(line.contains("tick=37"), () -> "expected tick=37 in: " + line);
        assertTrue(line.contains("speed=RUN"), () -> "expected speed=RUN in: " + line);
        assertTrue(line.contains("01:01:01"), () -> "expected 01:01:01 in: " + line);
    }

    @Test
    void describesTheDefaultBootTimeState() {
        String line = HudText.describeTime(0, "PAUSED", 0);
        assertTrue(line.contains("tick=0"));
        assertTrue(line.contains("speed=PAUSED"));
        assertTrue(line.contains("00:00:00"));
    }
}
