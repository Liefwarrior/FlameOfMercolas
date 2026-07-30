package com.trojia.client.fpv;

import com.trojia.client.camera.MapCamera;

/**
 * Which camera is looking, and the cross-fade between them.
 *
 * <h2>Why it is a cross-fade and not a continuous camera move</h2>
 *
 * <p>The obvious idea is to interpolate the camera itself: swing the top-down orthographic
 * view down onto its nose until it becomes the first-person frustum. It does not work. An
 * axis-aligned orthographic projection and a perspective fan are not two points on one path;
 * tweening between them looks like a lens fault, not like a head turning, and it would need
 * exactly the real 3D pipeline the planner deliberately does not have.
 *
 * <p>What actually preserves orientation across a mode change is not motion, it is
 * <b>anchors</b> — the same three things staying nailed down while the picture changes:
 *
 * <ol>
 *   <li><b>The tile stays put.</b> The follow camera is already centred on the driven actor
 *       every frame, and the first-person eye starts at that same tile's centre. The point of
 *       the world under the middle of the screen never moves.</li>
 *   <li><b>The direction stays put.</b> The top-down view draws a facing wedge on the driven
 *       actor pointing along the <em>view yaw</em> — the same yaw the first-person camera is
 *       about to use. So you choose which way you will be looking before you switch, and when
 *       the frame changes you are already looking that way.</li>
 *   <li><b>The compass stays put.</b> It is drawn in both modes, in the same place, reading
 *       the same yaw, and it never blinks during the fade.</li>
 * </ol>
 *
 * <p>With those anchored, the transition itself can be short and simple: over
 * {@link #TRANSITION_SECONDS} the top-down camera dollies in toward {@link MapCamera#MAX_ZOOM}
 * and the first-person frame fades up over it. The zoom-in is what sells it — the ground rushes
 * up to meet you and then you are standing on it — and it also does real work, because at
 * maximum zoom the top-down frame is showing almost exactly the patch of ground the
 * first-person frame opens on.
 *
 * <p>Pure state and pure math: no libGDX, no world. The renderers ask it for a blend factor
 * and a target zoom; it never draws anything itself.
 */
public final class ViewModeState {

    /** How long the cross-fade takes, either way. Long enough to read, short enough that it
     * never feels like a loading screen. */
    public static final float TRANSITION_SECONDS = 0.34f;

    /** Which way the world is being looked at. */
    public enum Mode {
        /** The tile view: the ward from above, the verification source of truth. */
        TOP_DOWN,
        /** Through the driven actor's eyes. */
        FIRST_PERSON
    }

    private Mode mode = Mode.TOP_DOWN;
    /** 0 fully top-down, 1 fully first-person; anything between is the fade. */
    private float blend;
    private boolean transitioning;
    /** The top-down zoom to return to when the view comes back up. */
    private int restoreZoom = -1;

    public Mode mode() {
        return mode;
    }

    /** Whether the first-person frame should be planned and drawn at all this frame. */
    public boolean firstPersonVisible() {
        return blend > 0f;
    }

    /** Whether the top-down frame should still be drawn underneath. */
    public boolean topDownVisible() {
        return blend < 1f;
    }

    /** The first-person frame's alpha this frame, 0..1. */
    public float blend() {
        return blend;
    }

    /** Whether a transition is still running. */
    public boolean transitioning() {
        return transitioning;
    }

    /** Whether the view has fully arrived in first person (no fade in flight). */
    public boolean settledFirstPerson() {
        return mode == Mode.FIRST_PERSON && !transitioning;
    }

    /**
     * Flips the mode and starts the fade. Entering remembers the current top-down zoom so
     * leaving can put it back — a player who was surveying the ward at 2x should not be
     * dumped back at 8x because the transition dollied in.
     *
     * @return the mode now being transitioned toward
     */
    public Mode toggle(int currentZoom) {
        if (mode == Mode.TOP_DOWN) {
            restoreZoom = currentZoom;
            mode = Mode.FIRST_PERSON;
        } else {
            mode = Mode.TOP_DOWN;
        }
        transitioning = true;
        return mode;
    }

    /**
     * The driven actor went away — released, or dead, or taken over by something else. Sends
     * the view back to the tile camera <b>through the same fade the V key uses</b>, which is
     * the point: this was the one mode change in the client that was a hard cut, and a hard cut
     * out of first person is exactly the disorientation the cross-fade exists to prevent.
     *
     * <p>Idempotent — the caller runs it every frame while nothing is driven, and once the fade
     * has landed it does nothing at all.
     */
    public void releaseToTopDown() {
        if (mode == Mode.TOP_DOWN && blend <= 0f) {
            transitioning = false;
            return;
        }
        mode = Mode.TOP_DOWN;
        transitioning = blend > 0f;
    }

    /** Advances the fade. */
    public void advance(float deltaSeconds) {
        if (!transitioning) {
            return;
        }
        float step = deltaSeconds / TRANSITION_SECONDS;
        if (mode == Mode.FIRST_PERSON) {
            blend = Math.min(1f, blend + step);
            transitioning = blend < 1f;
        } else {
            blend = Math.max(0f, blend - step);
            transitioning = blend > 0f;
        }
    }

    /**
     * The top-down zoom the transition wants this frame: dollying toward
     * {@link MapCamera#MAX_ZOOM} on the way in, and back toward whatever the player had on the
     * way out. Integer, because {@link MapCamera}'s zoom is integer by contract — the pixel
     * snapping that keeps the tile art crisp depends on it, and a first-person view is not a
     * good enough reason to break the tile view's own rules.
     *
     * @param currentZoom the camera's zoom right now
     * @return the zoom to set this frame
     */
    public int dollyZoom(int currentZoom) {
        int target = mode == Mode.FIRST_PERSON ? MapCamera.MAX_ZOOM
                : (restoreZoom > 0 ? restoreZoom : currentZoom);
        if (!transitioning || currentZoom == target) {
            return currentZoom;
        }
        // One step per frame of the fade: at 60fps the 0.34s transition walks the whole
        // 1..8 range comfortably, and at a low framerate it simply arrives sooner in fewer,
        // larger visual steps rather than lagging behind the fade.
        return currentZoom + Integer.signum(target - currentZoom);
    }
}
