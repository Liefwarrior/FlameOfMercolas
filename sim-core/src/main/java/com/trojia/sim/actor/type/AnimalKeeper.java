package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/**
 * Owns and works animals; always owns &ge; 1 Animal, enforced by the spawner
 * (ACTORS-SPEC.md §4.7, §4.8.3 — not this class). Thin subclass (§1.4).
 */
public final class AnimalKeeper extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("animal_keeper");

    private static final PolicyStack STACK = PolicyStack.of(
            Policies.DEFER_WIELDER,
            Policies.FLEE,
            Policies.SEEK_FOOD,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public AnimalKeeper(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
