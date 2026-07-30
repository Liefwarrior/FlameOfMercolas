package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.fpv.FirstPersonCamera;
import com.trojia.client.fpv.ViewFacing;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.sim.actor.ActorRegistry;

/**
 * Polls the first-person view's keys, and turns them into exactly two things: a client-side
 * yaw change, and a call to {@link PlayModeInput#applyMovement} — the same single movement
 * seam the top-down WASD path uses.
 *
 * <p>There is deliberately <b>no second route into the sim</b>. First person does not have its
 * own step logic, its own occupancy check or its own walkability test; it rotates a
 * forward/strafe intent into world axes ({@link ViewFacing#stepDelta}) and hands the resulting
 * {@code (dx, dy)} to the existing seam, which sets the pending move target the sim resolves
 * on its own terms. Everything load-bearing — the one-occupant cap, A*, the speed gate, the
 * shove escape — is untouched and unaware a camera changed.
 *
 * <ul>
 *   <li>{@code W}/{@code S} — step forward / back, relative to where you are looking.</li>
 *   <li>{@code A}/{@code D} — strafe left / right. (Not turn: turning with the same keys that
 *       move you is what makes a tile-stepped first-person view feel like driving a tank.)</li>
 *   <li>{@code Left}/{@code Right} — turn, continuously, at {@link #TURN_DEGREES_PER_SECOND}.
 *       Held {@code Shift} doubles it for a quick about-face. Live from the tile view too
 *       while an actor is driven ({@link #pollTurn}), which is what makes the facing wedge
 *       something you can aim before you switch rather than a read-out.</li>
 *   <li>{@code Up}/{@code Down} — unchanged: they are still the CLIMB verb
 *       ({@link ClimbInput}), because a stair is a stair from either camera.</li>
 *   <li>{@code PageUp}/{@code PageDown} — look up / down. A horizon shear, not a pitch (see
 *       {@code EyeProjection}); these two were unbound, and every nearby alternative was
 *       already a verb ({@code R} casts, {@code E} eats).</li>
 * </ul>
 *
 * <p>Mirrors the established shape: a thin {@code Gdx.input} wrapper around deterministic
 * {@code apply*} seams the scripted-playtest tape can drive with no keyboard.
 */
public final class FirstPersonInput {

    /** Continuous turn rate. Fast enough to whip round, slow enough to aim a doorway. */
    public static final float TURN_DEGREES_PER_SECOND = 165f;

    /** Vertical look (horizon shear) rate, in screen px per second at a 720px viewport. */
    public static final float SHEAR_PX_PER_SECOND = 420f;

    private FirstPersonInput() {
    }

    /**
     * Applies one frame's first-person input.
     *
     * @param camera        the eye whose yaw this may turn
     * @param playMode      the driven-actor state; a no-op when nothing is driven
     * @param registry      the live actor registry
     * @param deltaSeconds  frame time, for the continuous turn
     * @param shearPx       the current horizon shear, in px
     * @return the new horizon shear in px (unchanged unless the look keys are held)
     */
    public static float poll(FirstPersonCamera camera, PlayModeState playMode,
            ActorRegistry registry, float deltaSeconds, float shearPx) {
        if (!playMode.active()) {
            return shearPx;
        }
        pollTurn(camera, playMode, deltaSeconds);
        int look = (Gdx.input.isKeyPressed(Input.Keys.PAGE_UP) ? 1 : 0)
                - (Gdx.input.isKeyPressed(Input.Keys.PAGE_DOWN) ? 1 : 0);
        float shear = shearPx;
        if (look != 0) {
            shear += look * SHEAR_PX_PER_SECOND * deltaSeconds;
        }

        int forward = (Gdx.input.isKeyPressed(Input.Keys.W) ? 1 : 0)
                - (Gdx.input.isKeyPressed(Input.Keys.S) ? 1 : 0);
        int strafe = (Gdx.input.isKeyPressed(Input.Keys.D) ? 1 : 0)
                - (Gdx.input.isKeyPressed(Input.Keys.A) ? 1 : 0);
        applyMove(camera, playMode, registry, forward, strafe);
        return shear;
    }

    /**
     * <b>The turn keys alone</b>, polled from either view.
     *
     * <p>This exists split out because the facing wedge is one of the three anchors that make
     * the mode switch survivable, and an anchor you cannot aim is not one. Left/Right turn the
     * view yaw while the tile view is on screen too — the keys are free there, since
     * {@code CameraInput} suppresses arrow-key panning whenever an actor is being driven — so a
     * player can point the wedge down the street they mean to walk and then press V already
     * looking that way. In first person {@link #poll} calls this as its first act, so it is the
     * same code path and there is no second turn implementation to drift.
     */
    public static void pollTurn(FirstPersonCamera camera, PlayModeState playMode,
            float deltaSeconds) {
        if (!playMode.active()) {
            return;
        }
        boolean fast = Gdx.input.isKeyPressed(Input.Keys.SHIFT_LEFT)
                || Gdx.input.isKeyPressed(Input.Keys.SHIFT_RIGHT);
        int turn = (Gdx.input.isKeyPressed(Input.Keys.RIGHT) ? 1 : 0)
                - (Gdx.input.isKeyPressed(Input.Keys.LEFT) ? 1 : 0);
        if (turn != 0) {
            applyTurn(camera, turn * (fast ? 2f : 1f) * TURN_DEGREES_PER_SECOND * deltaSeconds);
        }
    }

    /**
     * The deterministic turn seam (degrees, positive clockwise on the compass) — split out
     * from the live key read so the scripted tape drives the same path with no keyboard.
     */
    public static void applyTurn(FirstPersonCamera camera, float degrees) {
        camera.turn((float) Math.toRadians(degrees));
    }

    /**
     * The deterministic movement seam: rotate the look-relative intent into world axes and
     * hand it to {@link PlayModeInput#applyMovement}. A no-op with no intent, or with nothing
     * driven.
     */
    public static void applyMove(FirstPersonCamera camera, PlayModeState playMode,
            ActorRegistry registry, int forward, int strafe) {
        if (forward == 0 && strafe == 0) {
            return;
        }
        int[] delta = ViewFacing.stepDelta(camera.yaw(), forward, strafe);
        PlayModeInput.applyMovement(playMode, registry, delta[0], delta[1]);
    }
}
