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
 * <p>Bindings: {@code W}/{@code A}/{@code S}/{@code D} and the arrow keys pan
 * continuously, scaled by frame delta so pan speed is frame-rate independent;
 * {@code [ }/{@code ]} step {@link MapCamera#zoomOut()}/{@link MapCamera#zoomIn()} one
 * integer level per key press; {@code Page Up}/{@code Page Down} step
 * {@link ZLevelCursor#up()}/{@link ZLevelCursor#down()} one z-level per key press.
 */
public final class CameraInput {

    /** Pan speed in screen px/second at any zoom ({@link MapCamera#pan} divides by zoom itself). */
    private static final float PAN_SPEED_SCREEN_PX_PER_SEC = 320f;

    private CameraInput() {
    }

    /** Reads the current keyboard state and applies one frame's worth of navigation. */
    public static void poll(MapCamera camera, ZLevelCursor zLevel, float deltaSeconds) {
        int dx = 0;
        int dy = 0;
        if (Gdx.input.isKeyPressed(Input.Keys.A) || Gdx.input.isKeyPressed(Input.Keys.LEFT)) {
            dx -= 1;
        }
        if (Gdx.input.isKeyPressed(Input.Keys.D) || Gdx.input.isKeyPressed(Input.Keys.RIGHT)) {
            dx += 1;
        }
        if (Gdx.input.isKeyPressed(Input.Keys.W) || Gdx.input.isKeyPressed(Input.Keys.UP)) {
            dy -= 1;
        }
        if (Gdx.input.isKeyPressed(Input.Keys.S) || Gdx.input.isKeyPressed(Input.Keys.DOWN)) {
            dy += 1;
        }
        if (dx != 0 || dy != 0) {
            int step = Math.round(PAN_SPEED_SCREEN_PX_PER_SEC * deltaSeconds);
            camera.pan(dx * step, dy * step);
        }

        if (Gdx.input.isKeyJustPressed(Input.Keys.LEFT_BRACKET)) {
            camera.zoomOut();
        }
        if (Gdx.input.isKeyJustPressed(Input.Keys.RIGHT_BRACKET)) {
            camera.zoomIn();
        }

        if (Gdx.input.isKeyJustPressed(Input.Keys.PAGE_UP)) {
            zLevel.up();
        }
        if (Gdx.input.isKeyJustPressed(Input.Keys.PAGE_DOWN)) {
            zLevel.down();
        }
    }
}
