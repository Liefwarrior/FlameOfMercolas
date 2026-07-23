package com.trojia.client.hud;

import com.trojia.client.hud.icons.HudToken;
import com.trojia.client.hud.icons.IconKey;
import com.trojia.sim.actor.DailyRhythm;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
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
    void describesTickSpeedAndDayClock() {
        // The 24,000-tick day reads as 24h: tick 3661 -> 3661*1440/24000 = 219 min = 03:39,
        // squarely in the Day phase (all job windows open, no night window yet).
        String line = HudText.describeTime(3661, "RUN");
        assertTrue(line.contains("tick=3661"), () -> "expected tick=3661 in: " + line);
        assertTrue(line.contains("speed=RUN"), () -> "expected speed=RUN in: " + line);
        assertTrue(line.contains("Day 1, 03:39 Day"), () -> "expected Day 1, 03:39 Day in: " + line);
    }

    @Test
    void describesTheDefaultBootTimeState() {
        String line = HudText.describeTime(0, "PAUSED");
        assertTrue(line.contains("tick=0"));
        assertTrue(line.contains("speed=PAUSED"));
        assertTrue(line.contains("Day 1, 00:00 Dawn"), () -> "expected Day 1, 00:00 Dawn in: " + line);
    }

    @Test
    void describesDayRollover() {
        // A tick just past DailyRhythm.DAY wraps time of day back down while advancing the
        // 1-based day counter.
        long tick = DailyRhythm.DAY + 3661;
        String line = HudText.describeTime(tick, "FAST");
        assertTrue(line.contains("Day 2, 03:39"), () -> "expected Day 2, 03:39 in: " + line);
        assertTrue(line.contains("tick=" + tick), () -> "expected tick=" + tick + " in: " + line);
    }

    @Test
    void statusTextNoLongerSpellsOutKeybindings() {
        // The keybinding reminder moved to icon tokens (navKeybindingTokens); the plain
        // status string only carries z/zoom now.
        String line = HudText.describe(9, 3);
        assertFalse(line.toUpperCase().contains("WASD"), () -> "unexpected keybinding text: " + line);
        assertFalse(line.contains("ESC"), () -> "unexpected keybinding text: " + line);
    }

    @Test
    void navKeybindingTokensCarryTheExpectedIcons() {
        List<HudToken> tokens = HudText.navKeybindingTokens();
        assertTrue(tokens.contains(HudToken.icon(IconKey.W)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.A)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.S)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.D)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.ARROW_UP)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.ARROW_DOWN)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.ARROW_LEFT)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.ARROW_RIGHT)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.BRACKET_OPEN)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.BRACKET_CLOSE)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.ESCAPE)));
    }

    @Test
    void timeKeybindingTokensCarryTheExpectedIcons() {
        List<HudToken> tokens = HudText.timeKeybindingTokens();
        assertTrue(tokens.contains(HudToken.icon(IconKey.SPACE)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.F)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.PERIOD)));
    }

    @Test
    void playModeLegendCarriesEverySocialVerbKey() {
        // Sprint 4 playtest fix: the whole verb surface must be on screen while driving.
        List<HudToken> tokens = HudText.playModeKeybindingTokens();
        assertTrue(tokens.contains(HudToken.icon(IconKey.T)), "talk");
        assertTrue(tokens.contains(HudToken.icon(IconKey.G)), "pickpocket");
        assertTrue(tokens.contains(HudToken.icon(IconKey.E)), "eat");
        assertTrue(tokens.contains(HudToken.icon(IconKey.ARROW_UP)), "climb up");
        assertTrue(tokens.contains(HudToken.icon(IconKey.ARROW_DOWN)), "climb down");
        assertTrue(tokens.contains(HudToken.icon(IconKey.I)), "disguise");
        assertTrue(tokens.contains(HudToken.icon(IconKey.J)), "journal");
        assertTrue(tokens.contains(HudToken.icon(IconKey.P)), "release");
    }

    @Test
    void observerVerbLegendCarriesTheSelectionSurface() {
        List<HudToken> tokens = HudText.observerVerbKeybindingTokens();
        assertTrue(tokens.contains(HudToken.icon(IconKey.MOUSE_LEFT_CLICK)), "select");
        assertTrue(tokens.contains(HudToken.icon(IconKey.C)), "follow");
        assertTrue(tokens.contains(HudToken.icon(IconKey.P)), "play as");
        assertTrue(tokens.contains(HudToken.icon(IconKey.N)), "names");
        assertTrue(tokens.contains(HudToken.icon(IconKey.J)), "journal");
    }

    @Test
    void describeTokensStartsWithTheStatusTextThenTheKeybindingLegend() {
        List<HudToken> tokens = HudText.describeTokens(9, 3);
        assertEquals(HudToken.text(HudText.describe(9, 3)), tokens.get(0));
        assertTrue(tokens.containsAll(HudText.navKeybindingTokens()));
    }

    @Test
    void describeTimeTokensStartsWithTheStatusTextThenTheKeybindingLegend() {
        // The status text is split across two leading text tokens — the bright clock+speed
        // part, then the raw-tick suffix as a DIM token (dev detail, visually subordinate) —
        // which together are exactly describeTime's string.
        List<HudToken> tokens = HudText.describeTimeTokens(37, "RUN");
        HudToken.Text clockAndSpeed = (HudToken.Text) tokens.get(0);
        HudToken.Text tickSuffix = (HudToken.Text) tokens.get(1);
        assertFalse(clockAndSpeed.dim(), "clock+speed run must be full brightness");
        assertTrue(tickSuffix.dim(), "raw-tick suffix must be the dim run");
        assertTrue(tickSuffix.value().contains("tick=37"),
                () -> "expected tick=37 in dim suffix: " + tickSuffix.value());
        assertEquals(HudText.describeTime(37, "RUN"), clockAndSpeed.value() + tickSuffix.value());
        assertTrue(tokens.containsAll(HudText.timeKeybindingTokens()));
    }
}
