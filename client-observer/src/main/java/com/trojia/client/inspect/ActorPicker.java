package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.world.PackedPos;

/**
 * Resolves a world tile (on one z-level) to the actor standing on it (M-inspector
 * Behavior 1). Pure and GL-free — the input layer converts a mouse click to a tile via
 * {@code MapCamera.screenToTileX/Y} and asks this class which actor, if any, is there, so
 * the tile&rarr;actor lookup is unit-testable without a real click.
 *
 * <p><b>Linear scan.</b> A single pass over the {@link ActorRegistry} in ascending
 * {@code ActorId} order — the project's documented "hundreds, not millions" convention
 * (ARCHITECTURE §3; the same scan {@code RelationshipRegistry}/{@code HomeRegistry} use),
 * fine at the compound's ~64 actors. When two actors share a tile (household members spawn
 * co-located), the lowest {@code ActorId} wins, deterministically.
 */
public final class ActorPicker {

    private ActorPicker() {
    }

    /**
     * The {@code ActorId} of the first actor (ascending id) whose cell is exactly
     * {@code (tileX, tileY, z)}, or {@link Actor#NONE} if the tile is empty on that
     * z-level. Only actors on the given z are considered — picking is scoped to the
     * currently-viewed floor.
     */
    public static int pickAt(ActorRegistry registry, int tileX, int tileY, int z) {
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            int cell = actor.cell();
            if (PackedPos.z(cell) == z
                    && PackedPos.x(cell) == tileX
                    && PackedPos.y(cell) == tileY) {
                return actor.id();
            }
        }
        return Actor.NONE;
    }
}
