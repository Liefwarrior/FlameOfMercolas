package com.trojia.sim.actor;

import java.util.ArrayList;
import java.util.List;

/**
 * The Home side-table (ACTORS-SPEC.md §11.1): a registry-owned array, dense
 * and sorted by {@code homeId} (ids assigned 0-based on {@link #addHome}, so
 * array index == homeId — no separate lookup needed, same convention
 * {@link ActorRegistry} uses for ActorId).
 */
public final class HomeRegistry {

    private final List<Home> homes = new ArrayList<>();

    /** Adds a new home at {@code homeCell}; returns its assigned {@code homeId}. */
    public int addHome(int homeCell) {
        int homeId = homes.size();
        homes.add(new Home(homeId, homeCell));
        return homeId;
    }

    public int size() {
        return homes.size();
    }

    public Home get(int homeId) {
        return homes.get(homeId);
    }

    /**
     * Every actor whose {@code homeId} equals the given id, ascending ActorId
     * (derived, never stored — §11.1). {@code registry} is scanned directly;
     * cheap at the "hundreds, not millions" actor scale.
     */
    public List<Actor> occupantsOf(int homeId, ActorRegistry registry) {
        List<Actor> occupants = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (actor.homeId() == homeId) {
                occupants.add(actor);
            }
        }
        return occupants;
    }
}
