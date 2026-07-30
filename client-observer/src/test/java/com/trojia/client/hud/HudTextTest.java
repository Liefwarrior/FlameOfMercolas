package com.trojia.client.hud;

import com.trojia.client.hud.icons.HudToken;
import com.trojia.client.hud.icons.IconKey;
import com.trojia.sim.actor.DailyRhythm;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.SortedSet;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
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
    void playModeLegendCarriesEveryPlayModeVerbKeyTheInputPackageBinds() throws IOException {
        // Sprint 4 playtest fix (#1): the whole verb surface must be on screen while driving.
        // Sprint 8 re-opened it — K and B shipped bound to nothing on any screen, and the old
        // version of THIS test stayed green because it enumerated a hand-written list of the
        // pre-S8 verbs. So it no longer enumerates anything: it READS the play-mode input
        // classes and demands the legend carry every key they bind. Add a verb, bind a key,
        // forget the legend, and this goes red without anyone remembering to edit a list.
        List<Path> sources = playModeInputSources();
        assertTrue(sources.size() >= 5,
                () -> "the input-package scan found almost nothing (" + sources
                        + ") — the scan is broken, not the legend");
        // RESTORED (round 3) to what Sprint 8 demanded and no wider: the ONE legend line the
        // player is looking at while driving. Round 2 widened this to the union of the driving
        // line and the first-person line so that map-side turn keys could pass while appearing
        // on neither screen the player was looking at — which is precisely the failure this
        // guard was built to catch, admitted by loosening the guard. The turn keys are on the
        // driving line now. The single narrow exemption below is a category, not a union.
        List<HudToken> legend = HudText.playModeKeybindingTokens();
        SortedSet<String> bound = new TreeSet<>();
        for (Path source : sources) {
            Matcher m = KEY_BINDING.matcher(Files.readString(source));
            while (m.find()) {
                bound.add(m.group(1));
            }
        }
        assertTrue(bound.size() >= 8,
                () -> "found only " + bound + " — the key regex stopped matching");
        for (String key : bound) {
            if (NOT_VERB_KEYS.contains(key)) {
                continue;
            }
            if (FIRST_PERSON_ONLY_KEYS.contains(key)) {
                continue; // its own test below, which is stricter than this one
            }
            IconKey icon = iconFor(key);
            assertNotNull(icon, () -> "a play-mode input class binds " + key
                    + " and IconKey has no glyph for it — add one (the S8 K/B defect)");
            assertTrue(legend.contains(HudToken.icon(icon)),
                    () -> "a play-mode input class binds " + key
                            + " and the play-mode legend does not carry it — the verb is on"
                            + " no screen (PLAYTEST-LOG.md fix #1)");
        }
    }

    /**
     * <b>The one narrow exemption</b>, and the price of it is the test right below.
     *
     * <p>A key may live on the first-person legend instead of the driving legend only if it
     * does <em>nothing whatsoever</em> until the first-person frame is up — not "means
     * something else there", which is what the turn keys are, and what the widened guard was
     * quietly letting through. Adding a name here is not a way past the guard: whatever is
     * named here is then proved unreachable from the tile view against the input sources
     * themselves.
     */
    private static final Set<String> FIRST_PERSON_ONLY_KEYS = Set.of("PAGE_UP", "PAGE_DOWN");

    /**
     * The exemption, audited. Two demands, and a key has to pass both:
     *
     * <ol>
     *   <li>it is on the first-person legend, so it is on <em>a</em> screen; and</li>
     *   <li>it is genuinely unreachable while the tile view is on screen — the only
     *       first-person input the tile view polls is {@code FirstPersonInput.pollTurn}, so the
     *       key must appear nowhere in that method and nowhere in any other play-mode input
     *       class.</li>
     * </ol>
     *
     * <p>Without the second demand the category is just the widened net with a nicer name.
     */
    @Test
    void theFirstPersonOnlyExemptionIsAuditedRatherThanAsserted() throws IOException {
        List<HudToken> firstPersonLegend = HudText.firstPersonKeybindingTokens();
        Path firstPersonInput = null;
        for (Path source : playModeInputSources()) {
            if (source.getFileName().toString().equals("FirstPersonInput.java")) {
                firstPersonInput = source;
            }
        }
        assertNotNull(firstPersonInput, "the first-person input class moved or vanished");
        String pollTurnBody = methodBody(Files.readString(firstPersonInput), "void pollTurn(");
        // The extraction has to be doing something, or every assertion below passes vacuously:
        // pollTurn demonstrably reads the turn keys, which is why they are on the driving line.
        assertTrue(pollTurnBody.contains("Input.Keys.LEFT")
                        && pollTurnBody.contains("Input.Keys.RIGHT"),
                "the pollTurn extraction found no turn keys — the audit is reading nothing");

        for (String key : FIRST_PERSON_ONLY_KEYS) {
            IconKey icon = iconFor(key);
            assertNotNull(icon, () -> key + " is exempt from the driving legend and has no "
                    + "glyph for the legend it IS on");
            assertTrue(firstPersonLegend.contains(HudToken.icon(icon)),
                    () -> key + " is exempt from the driving legend and is not on the "
                            + "first-person legend either — it is on no screen at all");
            assertFalse(pollTurnBody.contains("Input.Keys." + key),
                    () -> key + " is read by pollTurn, which the TILE view polls every frame — "
                            + "it is live on the map and belongs on the driving legend");
            for (Path source : playModeInputSources()) {
                if (source.equals(firstPersonInput)) {
                    continue;
                }
                assertFalse(Files.readString(source).contains("Input.Keys." + key),
                        () -> key + " is bound by " + source.getFileName() + " as well, so it "
                                + "is not first-person-only — put it on the driving legend");
            }
        }
    }

    /**
     * The source text of one method, from its declaration to the first close brace at method
     * indentation. Crude on purpose: it only has to be right about one small method whose shape
     * a compiler is already checking.
     */
    private static String methodBody(String source, String declaration) {
        String normalized = source.replace("\r\n", "\n");
        int start = normalized.indexOf(declaration);
        assertTrue(start > 0, () -> "cannot find " + declaration + " to audit");
        int end = normalized.indexOf("\n    }", start);
        assertTrue(end > start, () -> "cannot find the end of " + declaration);
        return normalized.substring(start, end);
    }

    /** {@code Input.Keys.NAME} — every key any play-mode input class binds. */
    private static final Pattern KEY_BINDING =
            Pattern.compile("Input\\.Keys\\.([A-Z][A-Z_0-9]*)");

    /**
     * Keys a play-mode input class binds that the VERB legend deliberately does not carry,
     * each with the surface that does carry it. Anything not named here must appear on the
     * legend — the default is "on screen".
     */
    private static final Set<String> NOT_VERB_KEYS = Set.of(
            "W", "A", "S", "D",   // movement: the nav line's pan group reads as movement
            "ESCAPE",             // closes the talk pane / quits: on the nav line
            "NUM_0");             // the talk pane's own numbered topic rows, labelled in it

    /** The {@link IconKey} for a libGDX key name, or {@code null} when the pack has no glyph. */
    private static IconKey iconFor(String keyName) {
        String enumName = switch (keyName) {
            case "SHIFT_LEFT", "SHIFT_RIGHT" -> "SHIFT";
            case "UP" -> "ARROW_UP";
            case "DOWN" -> "ARROW_DOWN";
            case "LEFT" -> "ARROW_LEFT";
            case "RIGHT" -> "ARROW_RIGHT";
            case "LEFT_BRACKET" -> "BRACKET_OPEN";
            case "RIGHT_BRACKET" -> "BRACKET_CLOSE";
            default -> keyName;
        };
        for (IconKey icon : IconKey.values()) {
            if (icon.name().equals(enumName)) {
                return icon;
            }
        }
        return null;
    }

    /**
     * Every {@code com.trojia.client.input} source that gates on {@link
     * com.trojia.client.inspect.PlayModeState} — i.e. every input surface that only exists
     * while an actor is being driven, which is exactly what the play-mode legend describes.
     */
    private static List<Path> playModeInputSources() throws IOException {
        Path dir = null;
        for (Path candidate : List.of(
                Path.of("src/main/java/com/trojia/client/input"),
                Path.of("client-observer/src/main/java/com/trojia/client/input"))) {
            if (Files.isDirectory(candidate)) {
                dir = candidate;
                break;
            }
        }
        assertNotNull(dir, "cannot find the input package sources from " + Path.of("").toAbsolutePath());
        try (var paths = Files.list(dir)) {
            List<Path> sources = new ArrayList<>();
            for (Path path : paths.sorted().toList()) {
                if (path.getFileName().toString().endsWith("Input.java")
                        && Files.readString(path).contains("PlayModeState")) {
                    sources.add(path);
                }
            }
            return sources;
        }
    }

    @Test
    void observerVerbLegendCarriesTheSelectionSurface() {
        List<HudToken> tokens = HudText.observerVerbKeybindingTokens();
        assertTrue(tokens.contains(HudToken.icon(IconKey.MOUSE_LEFT_CLICK)), "select");
        assertTrue(tokens.contains(HudToken.icon(IconKey.C)), "follow");
        assertTrue(tokens.contains(HudToken.icon(IconKey.P)), "play as");
        assertTrue(tokens.contains(HudToken.icon(IconKey.N)), "names");
        assertTrue(tokens.contains(HudToken.icon(IconKey.J)), "journal");
        assertTrue(tokens.contains(HudToken.icon(IconKey.M)), "masters board (S5)");
        assertTrue(tokens.contains(HudToken.icon(IconKey.L)), "feed filter (S5)");
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
