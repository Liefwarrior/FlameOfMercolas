package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.world.ZLevelCursor;

/**
 * Polls keyboard state each frame and drives {@link MapCamera} and {@link ZLevelCursor}
 * (M1 Behavior 3). GL-dependent (reads {@link Gdx#input}); call once per {@code render()}
 * frame.
 *
 * <p>Bindings: {@code W}/{@code S} pan vertically and {@code A}/{@code D} (aliased by
 * {@code Left}/{@code Right}) pan horizontally, continuously, scaled by frame delta so pan
 * speed is frame-rate independent; {@code [ }/{@code ]} step
 * {@link MapCamera#zoomOut()}/{@link MapCamera#zoomIn()} one integer level per key press;
 * {@code Up}/{@code Down} arrow step {@link ZLevelCursor#up()}/{@link ZLevelCursor#down()}
 * one z-level per key press (Dwarf-Fortress-style level scrub). {@code Up}/{@code Down} are
 * deliberately excluded from panning so the two behaviors never fight over the same key in
 * the same frame.
 *
 * <p>{@code panEnabled} (PLAY-MODE-SPEC.md §5.1): Play mode repurposes {@code WASD} to drive
 * the played actor instead of the camera, so the caller passes {@code false} while it is
 * active — zoom and z-scrub keep working either way (harmless, useful even mid-Play-mode).
 */
public final class CameraInput {

    /** Pan speed in screen px/second at any zoom ({@link MapCamera#pan} divides by zoom itself). */
    private static final float PAN_SPEED_SCREEN_PX_PER_SEC = 320f;

    private CameraInput() {
    }

    /**
     * The pre-Sprint-4 overload (pan gate only): z-scrub always on. Kept so pre-climb call
     * sites and tests compile unchanged; delegates below.
     */
    public static void poll(MapCamera camera, ZLevelCursor zLevel, float deltaSeconds,
            boolean panEnabled) {
        poll(camera, zLevel, deltaSeconds, panEnabled, true);
    }

    /**
     * Reads the current keyboard state and applies one frame's worth of navigation.
     * {@code panEnabled} gates the {@code WASD}/arrow pan — {@code false} while Play mode
     * owns {@code WASD} (PLAY-MODE-SPEC.md §5.1). {@code zScrubEnabled} gates the
     * {@code Up}/{@code Down} z-level scrub — {@code false} while Play mode owns the
     * arrows as CLIMB keys (Sprint 4: while driving a soul, your z-control is the stair
     * under your feet, and the follow-camera owns the viewed floor). Zoom always works.
     */
    public static void poll(MapCamera camera, ZLevelCursor zLevel, float deltaSeconds,
            boolean panEnabled, boolean zScrubEnabled) {
        if (panEnabled) {
            boolean left = Gdx.input.isKeyPressed(Input.Keys.A) || Gdx.input.isKeyPressed(Input.Keys.LEFT);
            boolean right = Gdx.input.isKeyPressed(Input.Keys.D) || Gdx.input.isKeyPressed(Input.Keys.RIGHT);
            boolean up = Gdx.input.isKeyPressed(Input.Keys.W);
            boolean down = Gdx.input.isKeyPressed(Input.Keys.S);
            int[] pan = panDelta(left, right, up, down);
            if (pan[0] != 0 || pan[1] != 0) {
                int step = Math.round(PAN_SPEED_SCREEN_PX_PER_SEC * deltaSeconds);
                camera.pan(pan[0] * step, pan[1] * step);
            }
        }

        if (Gdx.input.isKeyJustPressed(Input.Keys.LEFT_BRACKET)) {
            camera.zoomOut();
        }
        if (Gdx.input.isKeyJustPressed(Input.Keys.RIGHT_BRACKET)) {
            camera.zoomIn();
        }

        // Dwarf-Fortress-style level scrub: Up/Down are step (justPressed) events, never
        // held-repeat panning — z should move exactly one level per key press.
        if (zScrubEnabled) {
            if (Gdx.input.isKeyJustPressed(Input.Keys.UP)) {
                zLevel.up();
            }
            if (Gdx.input.isKeyJustPressed(Input.Keys.DOWN)) {
                zLevel.down();
            }
        }
    }

    /**
     * Pure pan-delta computation, package-private so {@code CameraInputTest} can exercise the
     * binding logic itself without a live GL context. Note {@code up}/{@code down} here are
     * ONLY {@code W}/{@code S} — the arrow Up/Down keys never reach this method, which is the
     * structural fix for the z-level bug (arrow Up/Down used to double as pan keys here,
     * silently overriding any attempt to bind them to the z-cursor).
     *
     * @return {@code [dx, dy]}, each in {@code {-1, 0, 1}}
     */
    static int[] panDelta(boolean left, boolean right, boolean up, boolean down) {
        int dx = 0;
        int dy = 0;
        if (left) {
            dx -= 1;
        }
        if (right) {
            dx += 1;
        }
        if (up) {
            dy -= 1;
        }
        if (down) {
            dy += 1;
        }
        return new int[] {dx, dy};
    }
}
