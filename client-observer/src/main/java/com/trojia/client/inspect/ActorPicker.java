package com.trojia.client.inspect;

import com.trojia.client.render.DepthSight;
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
 *
 * <p><b>Depth fallback</b> (Sprint 4 EPIC, lead's ruling): {@link #pickThroughDepth} lets
 * a click land on an actor rendered THROUGH empty air by the depth-vision pass — the
 * same-z actor always wins; only an empty same-z tile falls through to the below-z actor
 * the column's look-down shows. Inspect-only: Play-mode verbs (talk / pickpocket / eat /
 * act-as) keep the plain same-z {@link #pickAt} — you cannot reach a soul a storey below.
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

    /**
     * {@link #pickAt} with the depth-vision fallback: the actor on the tile at the viewed
     * {@code viewZ} wins outright; an empty tile asks {@code sight} which lower z' the
     * column's air-depth look-down shows and picks there instead — exactly the actor the
     * depth pass rendered at that screen cell, so what you click is what you see. A
     * {@code null} sight (or an occluded / bottomless column) degrades to the plain
     * same-z pick.
     */
    public static int pickThroughDepth(ActorRegistry registry, int tileX, int tileY,
            int viewZ, DepthSight sight) {
        int samePlane = pickAt(registry, tileX, tileY, viewZ);
        if (samePlane != Actor.NONE || sight == null) {
            return samePlane;
        }
        int belowZ = sight.visibleBelowZ(viewZ, tileX, tileY);
        return belowZ == DepthSight.NONE ? Actor.NONE
                : pickAt(registry, tileX, tileY, belowZ);
    }
}
