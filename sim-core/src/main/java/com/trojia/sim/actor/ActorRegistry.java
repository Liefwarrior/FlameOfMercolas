package com.trojia.sim.actor;

import java.util.ArrayList;
import java.util.List;

/**
 * The deterministic actor registry (ACTORS-SPEC.md §2.2): real objects,
 * hundreds not millions. ActorIds are assigned by a monotonic counter, never
 * reused; since ids are assigned densely starting at 0 and this registry
 * never removes an actor (death/removal is a later milestone — DOWNED actors
 * stay resident), {@code array index == ActorId}, so "ascending ActorId
 * iteration" is simply array order — no separate sort or map needed.
 */
public final class ActorRegistry {

    private final List<Actor> actors = new ArrayList<>();

    /**
     * Spawns a new actor of {@code type} at {@code cell}, assigning the next
     * ActorId. Returns the constructed actor (already appended, ascending-id
     * order preserved by construction).
     */
    public Actor spawn(ActorTypeId type, ActorTypeStats stats, int cell) {
        ActorTypes.Registration registration = ActorTypes.find(type);
        int id = actors.size();
        Actor actor = registration.factory().create(id, stats, cell);
        if (actor.id() != id) {
            throw new IllegalStateException(
                    "actor factory for " + type + " assigned the wrong id");
        }
        actors.add(actor);
        return actor;
    }

    public int size() {
        return actors.size();
    }

    /** The actor with this id — {@code array index == ActorId} by construction. */
    public Actor get(int actorId) {
        return actors.get(actorId);
    }

    /** Ticks every actor in ascending ActorId order (ACTORS-SPEC.md §2.2, test A2). */
    public void tickAll(ActorContext ctx) {
        for (Actor actor : actors) {
            actor.tick(ctx);
        }
    }

    /** Read-only ascending-id view, for inspectors/tests. */
    public List<Actor> all() {
        return List.copyOf(actors);
    }
}
