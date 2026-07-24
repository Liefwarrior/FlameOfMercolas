package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.inspect.ActorPicker;
import com.trojia.client.inspect.InspectorState;
import com.trojia.client.render.DepthSight;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;

/**
 * Polls mouse + keyboard for the observer's actor inspector (M-inspector Behaviors 1 &amp;
 * 4). GL-dependent (reads {@link Gdx#input}); call once per {@code render()} frame,
 * alongside {@link CameraInput#poll}/{@link TimeControlInput#poll} — its bindings are
 * disjoint from theirs (WASD/arrows, {@code [ }/{@code ]}, PageUp/PageDown, SPACE/F/PERIOD,
 * ESC are all taken):
 * <ul>
 *   <li><b>Left mouse click</b> — selects the actor under the cursor on the currently
 *       viewed z-level, or deselects on empty space / off-world.</li>
 *   <li>{@code C} — toggles camera-follow on the selected actor (no-op with no selection).</li>
 * </ul>
 *
 * <p><b>Click&rarr;tile mapping.</b> {@link Gdx.input#getX()}/{@code getY()} are in
 * top-down window pixels (origin top-left) — exactly the screen space
 * {@link MapCamera#screenToTileX(int)}/{@link MapCamera#screenToTileY(int)} are defined
 * against (the camera's tile&harr;screen transforms are top-down; {@code ActorRenderer}
 * bridges that to the y-up GL projection when it <em>draws</em>). So the mouse coordinates
 * feed the camera directly with no y-flip; picking then agrees with rendering exactly (the
 * camera's snapping contract guarantees {@code screenToTile(tileToScreen(t)) == t}).
 */
public final class InspectorInput {

    private InspectorInput() {
    }

    /**
     * Applies one frame's inspector input to {@code state}: a click resolves the cursor to
     * a tile on z-level {@code z} and selects the actor there (via {@link ActorPicker}), and
     * {@code C} toggles follow. The no-depth overload — clicks pick on the viewed z only.
     */
    public static void poll(InspectorState state, MapCamera camera, ActorRegistry registry, int z) {
        poll(state, camera, registry, z, null);
    }

    /**
     * The depth-aware poll (Sprint 4 EPIC): a click on an empty same-z tile falls through
     * to the actor the air-depth look-down renders there ({@link ActorPicker#pickThroughDepth}
     * — the same-z actor always wins), so click-to-inspect reaches the souls you can SEE a
     * band below. {@code sight} may be null: plain same-z picking.
     */
    public static void poll(InspectorState state, MapCamera camera, ActorRegistry registry, int z,
            DepthSight sight) {
        if (Gdx.input.isButtonJustPressed(Input.Buttons.LEFT)) {
            int tileX = camera.screenToTileX(Gdx.input.getX());
            int tileY = camera.screenToTileY(Gdx.input.getY());
            int picked = camera.isInWorld(tileX, tileY)
                    ? ActorPicker.pickThroughDepth(registry, tileX, tileY, z, sight)
                    : Actor.NONE;
            state.select(picked);
        }

        if (Gdx.input.isKeyJustPressed(Input.Keys.C)) {
            state.toggleFollow();
        }
    }
}
