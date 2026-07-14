package com.trojia.client.input;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;

/**
 * {@link CameraInput#panDelta} — the pure pan-binding logic, exercised directly (no live GL
 * context / no {@code Gdx.input}) so the z-level keybind fix has a real automated regression
 * test, not just screenshot evidence. The bug this guards against: Up/Down arrows used to be
 * wired into this same delta computation as pan aliases, silently fighting any attempt to also
 * bind them to the z-cursor. {@code panDelta}'s signature now only accepts W/S for vertical
 * pan — Up/Down never reach it at all — so a future regression that re-adds them here would
 * have to change this method's signature, not just add a stray {@code isKeyPressed} call.
 */
class CameraInputTest {

    @Test
    void noKeysPressedIsNoPan() {
        assertArrayEquals(new int[] {0, 0}, CameraInput.panDelta(false, false, false, false));
    }

    @Test
    void leftAndRightMoveHorizontally() {
        assertArrayEquals(new int[] {-1, 0}, CameraInput.panDelta(true, false, false, false));
        assertArrayEquals(new int[] {1, 0}, CameraInput.panDelta(false, true, false, false));
    }

    @Test
    void wAndSMoveVertically() {
        assertArrayEquals(new int[] {0, -1}, CameraInput.panDelta(false, false, true, false));
        assertArrayEquals(new int[] {0, 1}, CameraInput.panDelta(false, false, false, true));
    }

    @Test
    void oppositeHorizontalKeysCancelOut() {
        assertArrayEquals(new int[] {0, 0}, CameraInput.panDelta(true, true, false, false));
    }

    @Test
    void oppositeVerticalKeysCancelOut() {
        assertArrayEquals(new int[] {0, 0}, CameraInput.panDelta(false, false, true, true));
    }

    @Test
    void diagonalPanCombinesBothAxes() {
        assertArrayEquals(new int[] {1, -1}, CameraInput.panDelta(false, true, true, false));
    }
}
