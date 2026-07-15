package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.inspect.ActorPicker;
import com.trojia.client.inspect.InspectorState;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.world.PackedPos;

/**
 * Polls keyboard + mouse for Play mode (PLAY-MODE-SPEC.md §5). Mirrors
 * {@link InspectorInput}'s shape; call once per {@code render()} frame, ahead of
 * {@link InspectorInput#poll}: while impersonation-pick is armed, this class consumes the left
 * click itself (its return value tells the caller whether to skip {@code InspectorInput.poll}
 * for that frame), so a click meant to choose a disguise target never also reselects the
 * played actor.
 *
 * <ul>
 *   <li>{@code P} — toggles direct control of the currently selected actor on/off (no-op with
 *       no selection). Turning on forces camera-follow; turning off restores the actor's own
 *       AI immediately (its {@code StatusBit.PLAYER_CONTROLLED} clears, so next tick's
 *       selection reverts to whatever the actor's own stack would otherwise pick).</li>
 *   <li>{@code I} (only while active) — arms impersonation-pick: the next left click resolves
 *       an actor via {@link ActorPicker} and calls {@link Actor#setActAs(int)} on the played
 *       actor (clicking the played actor's own tile drops the disguise).</li>
 *   <li>{@code W}/{@code A}/{@code S}/{@code D} (only while active, held) — drive the played
 *       actor's pending step target (§5.2); {@link CameraInput}'s pan is gated off by the
 *       caller while Play mode is active so the two never fight over the same keys.</li>
 * </ul>
 *
 * <p>If the selection changes to a different actor via the ordinary {@code InspectorInput}
 * click path while Play mode stays active (§3 item 1), the played actor follows the new
 * selection — detected here by comparing {@link InspectorState#selectedActorId()} against the
 * {@link PlayModeState}'s own tracked id.
 */
public final class PlayModeInput {

    private PlayModeInput() {
    }

    /**
     * Applies one frame's Play-mode input. Returns {@code true} if this call consumed the
     * frame's left click (impersonation-pick fired) — the caller should then skip
     * {@code InspectorInput.poll} for this frame so the same click does not also reselect.
     */
    public static boolean poll(PlayModeState playMode, InspectorState inspector, MapCamera camera,
            ActorRegistry registry, int z) {
        followSelectionChange(playMode, inspector, registry);
        pollToggle(playMode, inspector, registry);

        if (!playMode.active()) {
            return false;
        }

        if (Gdx.input.isKeyJustPressed(Input.Keys.I)) {
            playMode.armImpersonatePick();
        }

        boolean clickConsumed = false;
        if (playMode.impersonatePickArmed() && Gdx.input.isButtonJustPressed(Input.Buttons.LEFT)) {
            clickConsumed = true;
            applyImpersonatePick(playMode, camera, registry, z);
        }

        pollMovement(playMode, registry);
        return clickConsumed;
    }

    /** §3 item 1: a reselect via the ordinary click path hands control to the new actor. */
    private static void followSelectionChange(PlayModeState playMode, InspectorState inspector,
            ActorRegistry registry) {
        if (!playMode.active() || inspector.selectedActorId() == playMode.playedActorId()) {
            return;
        }
        if (playMode.playedActorId() != Actor.NONE) {
            registry.get(playMode.playedActorId()).setStatus(StatusBit.PLAYER_CONTROLLED, false);
        }
        if (inspector.hasSelection()) {
            registry.get(inspector.selectedActorId()).setStatus(StatusBit.PLAYER_CONTROLLED, true);
            playMode.retarget(inspector.selectedActorId());
        } else {
            playMode.disable();
        }
    }

    private static void pollToggle(PlayModeState playMode, InspectorState inspector,
            ActorRegistry registry) {
        if (!Gdx.input.isKeyJustPressed(Input.Keys.P) || !inspector.hasSelection()) {
            return;
        }
        if (playMode.active()) {
            registry.get(playMode.playedActorId()).setStatus(StatusBit.PLAYER_CONTROLLED, false);
            playMode.disable();
        } else {
            int playedId = inspector.selectedActorId();
            registry.get(playedId).setStatus(StatusBit.PLAYER_CONTROLLED, true);
            playMode.enable(playedId);
            if (!inspector.followActive()) {
                inspector.toggleFollow();
            }
        }
    }

    private static void applyImpersonatePick(PlayModeState playMode, MapCamera camera,
            ActorRegistry registry, int z) {
        int tileX = camera.screenToTileX(Gdx.input.getX());
        int tileY = camera.screenToTileY(Gdx.input.getY());
        if (!camera.isInWorld(tileX, tileY)) {
            playMode.clearImpersonatePick();
            return;
        }
        int picked = ActorPicker.pickAt(registry, tileX, tileY, z);
        if (picked != Actor.NONE) {
            registry.get(playMode.playedActorId()).setActAs(picked);
        }
        playMode.clearImpersonatePick();
    }

    private static void pollMovement(PlayModeState playMode, ActorRegistry registry) {
        boolean left = Gdx.input.isKeyPressed(Input.Keys.A);
        boolean right = Gdx.input.isKeyPressed(Input.Keys.D);
        boolean up = Gdx.input.isKeyPressed(Input.Keys.W);
        boolean down = Gdx.input.isKeyPressed(Input.Keys.S);
        applyMovement(playMode, registry, (left ? -1 : 0) + (right ? 1 : 0),
                (up ? -1 : 0) + (down ? 1 : 0));
    }

    /**
     * Computes and applies the pending step target from a raw {@code (dx, dy)} signed delta
     * (each in {@code {-1, 0, 1}}) — split out from the live {@code Gdx.input} read above so a
     * debug/verification caller (no cursor, no physical keys — mirrors the existing
     * {@code --debug-select} convention) can drive the exact same code path deterministically.
     */
    public static void applyMovement(PlayModeState playMode, ActorRegistry registry, int dx, int dy) {
        if (!playMode.active() || (dx == 0 && dy == 0)) {
            return;
        }
        Actor played = registry.get(playMode.playedActorId());
        int cell = played.cell();
        int target = PackedPos.pack(PackedPos.x(cell) + dx, PackedPos.y(cell) + dy, PackedPos.z(cell));
        played.setPlayerMoveTarget(target);
    }
}
