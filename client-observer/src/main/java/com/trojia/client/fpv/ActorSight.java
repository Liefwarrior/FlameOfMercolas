package com.trojia.client.fpv;

import com.trojia.client.sprite.SpriteRef;

import java.util.List;

/**
 * The planner's read-only window onto who is standing where — the actor half of
 * {@link CellSight}. Queried per cell as the planner walks the visible grid, so the planner
 * never iterates the whole registry and never needs to know what an {@code ActorRegistry} is.
 *
 * <p>The world-backed implementation ({@link CellActorIndex}) buckets the live registry by
 * cell once per frame; a headless test hands over a lambda. Sprite choice is resolved by the
 * caller through the same {@code SpriteIndex.forActor} pick the top-down view uses, so an
 * actor wears the same face in both views by construction rather than by coincidence.
 */
@FunctionalInterface
public interface ActorSight {

    /** No one home. */
    List<Mark> NONE_HERE = List.of();

    /**
     * The actors standing on exactly this cell, ascending id. Occupancy is capped at one per
     * cell sim-side, so this is almost always empty or a single entry — the top-down view's
     * stack-cascade crutch has no meaning here and is deliberately not carried over.
     */
    List<Mark> at(int x, int y, int z);

    /**
     * One actor to draw.
     *
     * @param actorId the registry id
     * @param sprite  the actor's per-life sprite ({@code SpriteIndex.forActor})
     * @param corpse  whether this is a dead body (drawn squashed and blood-dimmed)
     */
    record Mark(int actorId, SpriteRef sprite, boolean corpse) {
    }

    /** An empty ward — the planner terrain tests' stand-in. */
    static ActorSight empty() {
        return (x, y, z) -> NONE_HERE;
    }
}
