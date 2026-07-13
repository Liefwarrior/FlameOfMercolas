package com.trojia.client.hud;

import com.trojia.client.hud.icons.HudToken;
import com.trojia.client.hud.icons.IconKey;
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
        assertTrue(tokens.contains(HudToken.icon(IconKey.PAGE_UP)));
        assertTrue(tokens.contains(HudToken.icon(IconKey.PAGE_DOWN)));
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
    void describeTokensStartsWithTheStatusTextThenTheKeybindingLegend() {
        List<HudToken> tokens = HudText.describeTokens(9, 3);
        assertEquals(HudToken.text(HudText.describe(9, 3)), tokens.get(0));
        assertTrue(tokens.containsAll(HudText.navKeybindingTokens()));
    }

    @Test
    void describeTimeTokensStartsWithTheStatusTextThenTheKeybindingLegend() {
        List<HudToken> tokens = HudText.describeTimeTokens(37, "RUN", 3661);
        assertEquals(HudToken.text(HudText.describeTime(37, "RUN", 3661)), tokens.get(0));
        assertTrue(tokens.containsAll(HudText.timeKeybindingTokens()));
    }
}
