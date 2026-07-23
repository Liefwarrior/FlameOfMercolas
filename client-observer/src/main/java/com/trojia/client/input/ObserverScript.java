package com.trojia.client.input;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The scripted-playtest input tape (Sprint 4 CLIENT, the live-GL playtest harness):
 * a parsed list of {@code <frame> <verb>[=<args>]} rows the observer replays during a
 * {@code --smoke} run — each verb dispatched on its exact rendered frame through the SAME
 * deterministic {@code apply*} seams the live keyboard wrappers call
 * ({@link TalkInput#applyTalk}, {@link TheftInput#applyPickpocket},
 * {@link PlayModeInput#applyMovement}, ...). The established {@code --debug-select}
 * convention ("bypass the input device, exercise the real code path"), grown from single
 * boot-time flags into a whole session: walk a soul, climb a stair, open the talk panel,
 * and capture a screenshot at any frame — repeatably, with no human hands.
 *
 * <p><b>Deterministic.</b> Smoke runs execute exactly one sim tick per rendered frame, so a
 * frame number IS a tick number and two runs of the same script produce identical sessions
 * (and pixel-identical screenshots). GL-free: parsing and the frame query are plain data
 * work, unit-tested without a window; only the dispatcher in {@code ObserverApp} touches GL.
 *
 * <p><b>Format</b> (one action per line; blank lines and {@code #} comments skipped):
 * <pre>
 *   30 select=371        # inspector-select actor 371 (the K21 sergeant)
 *   31 follow            # toggle camera-follow
 *   32 play              # enter Play mode on the selection (the P key's real path)
 *   40 hold=1,0          # hold a movement direction (WASD): dx,dy each in -1/0/1
 *   90 hold=0,0          # release movement
 *   95 talk              # the T verb (adjacency greet)
 *   96 topic=2           # the 1-9/0 ask keys (Sprint 4 talk topics)
 *   97 pickpocket        # the G verb
 *   98 eat               # the E verb (Sprint 4 played-actor eat)
 *   99 climb=up          # the Up/Down climb keys (Sprint 4 the climb); up|down
 *  100 journal           # toggle the J journal pane
 *  101 plates            # toggle hold-N nameplates
 *  110 zoom=4            # camera zoom (clamped)
 *  111 center=75,105     # center camera on tile
 *  112 z=12              # z-level scrub (clamped)
 *  120 shot=C:/tmp/a.png # capture this frame's framebuffer to a PNG
 * </pre>
 */
public final class ObserverScript {

    /** The verb vocabulary. Unknown verbs fail the parse loudly (never silently skipped). */
    public enum Verb {
        SELECT, FOLLOW, PLAY, HOLD, TALK, TOPIC, PICKPOCKET, EAT, CLIMB, JOURNAL, PLATES,
        ZOOM, CENTER, Z, SHOT
    }

    /** One scripted action: the frame it fires on, its verb, and the raw argument text. */
    public record Action(int frame, Verb verb, String args) {

        /** {@code args} split on commas and parsed as ints — the {@code dx,dy}-style verbs. */
        public int[] intArgs() {
            String[] parts = args.split(",");
            int[] out = new int[parts.length];
            for (int i = 0; i < parts.length; i++) {
                out[i] = Integer.parseInt(parts[i].trim());
            }
            return out;
        }
    }

    private final List<Action> actions;

    private ObserverScript(List<Action> actions) {
        this.actions = List.copyOf(actions);
    }

    /** Parses a script file; malformed rows fail loudly with the offending line. */
    public static ObserverScript load(Path file) {
        try {
            return parse(Files.readString(file, StandardCharsets.UTF_8));
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read observer script " + file, e);
        }
    }

    /** Parses script text (the unit-test surface). */
    public static ObserverScript parse(String text) {
        List<Action> actions = new ArrayList<>();
        int lineNo = 0;
        for (String raw : text.split("\r?\n")) {
            lineNo++;
            String line = stripComment(raw).trim();
            if (line.isEmpty()) {
                continue;
            }
            int space = line.indexOf(' ');
            if (space < 0) {
                throw new IllegalArgumentException(
                        "script line " + lineNo + ": expected '<frame> <verb>[=args]': " + raw);
            }
            int frame;
            try {
                frame = Integer.parseInt(line.substring(0, space).trim());
            } catch (NumberFormatException e) {
                throw new IllegalArgumentException(
                        "script line " + lineNo + ": bad frame number: " + raw);
            }
            String rest = line.substring(space + 1).trim();
            int eq = rest.indexOf('=');
            String verbText = (eq < 0 ? rest : rest.substring(0, eq)).trim();
            String args = eq < 0 ? "" : rest.substring(eq + 1).trim();
            Verb verb;
            try {
                verb = Verb.valueOf(verbText.toUpperCase(Locale.ROOT));
            } catch (IllegalArgumentException e) {
                throw new IllegalArgumentException(
                        "script line " + lineNo + ": unknown verb \"" + verbText + "\"");
            }
            if (frame < 0) {
                throw new IllegalArgumentException(
                        "script line " + lineNo + ": frame must be >= 0");
            }
            actions.add(new Action(frame, verb, args));
        }
        actions.sort((a, b) -> Integer.compare(a.frame(), b.frame()));
        return new ObserverScript(actions);
    }

    /** Every action scheduled for {@code frame}, in file order. */
    public List<Action> at(int frame) {
        List<Action> out = new ArrayList<>();
        for (Action action : actions) {
            if (action.frame() == frame) {
                out.add(action);
            }
        }
        return out;
    }

    /** The highest scheduled frame (so a runner can size its {@code --smoke} window). */
    public int lastFrame() {
        int last = 0;
        for (Action action : actions) {
            last = Math.max(last, action.frame());
        }
        return last;
    }

    /** All parsed actions, frame-ascending (stable within a frame). */
    public List<Action> actions() {
        return actions;
    }

    private static String stripComment(String line) {
        int hash = line.indexOf('#');
        return hash < 0 ? line : line.substring(0, hash);
    }
}
