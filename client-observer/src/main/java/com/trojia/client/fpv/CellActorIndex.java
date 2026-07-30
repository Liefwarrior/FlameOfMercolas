package com.trojia.client.fpv;

import com.trojia.client.inspect.DeathPresentation;
import com.trojia.client.sprite.SpriteIndex;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.world.PackedPos;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * {@link ActorSight} over the live registry: buckets every actor by the cell it is standing on,
 * once per frame, so the planner can ask "who is on this tile" without walking 692 actors per
 * visible cell.
 *
 * <p>Positions are read fresh on every {@link #refresh}, never cached across frames — the same
 * no-staleness contract {@code ActorRenderer} and {@code WorldRenderer} keep, so an actor
 * walking home visibly walks. Sprite choice goes through {@code SpriteIndex.forActor}, the
 * same pure per-life pick the top-down view makes, which is why a dockhand you saw from above
 * is recognisably the same dockhand when you look him in the face.
 *
 * <p>Reads only. Nothing here writes to an actor, and in particular nothing writes
 * {@code Actor.setFacing} — turning the camera is a client-side view yaw, never a fact in the
 * world (see {@link FirstPersonCamera}).
 *
 * <p><b>This index buckets everybody, deliberately.</b> Hiding the driven actor from its own
 * view is a visibility decision, and visibility decisions live in
 * {@link FirstPersonPlanner#plan(EyeProjection, int, CellSight, ActorSight, int)} where a
 * headless test can read them — filtering here as well would put the same rule in two places
 * and let one of them rot.
 */
public final class CellActorIndex implements ActorSight {

    private final ActorRegistry registry;
    private final SpriteIndex sprites;
    private final Map<Integer, List<Mark>> byCell = new HashMap<>();

    public CellActorIndex(ActorRegistry registry, SpriteIndex sprites) {
        this.registry = registry;
        this.sprites = sprites;
    }

    /** Rebuilds the bucket map from the registry's live positions. Call once per frame. */
    public void refresh() {
        byCell.clear();
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            byCell.computeIfAbsent(actor.cell(), c -> new ArrayList<>(1))
                    .add(new Mark(actor.id(), sprites.forActor(actor.typeId().key(), actor.id()),
                            DeathPresentation.isDead(actor)));
        }
    }

    @Override
    public List<Mark> at(int x, int y, int z) {
        List<Mark> here = byCell.get(PackedPos.pack(x, y, z));
        return here == null ? NONE_HERE : here;
    }
}
