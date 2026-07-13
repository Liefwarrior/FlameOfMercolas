package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/**
 * The innocent chaos vector — dog/pig/goat/mule (ACTORS-SPEC.md §4.8). Thin
 * subclass (§1.4). No {@code DEFER_WIELDER} in the stack: Animals carry no
 * deference row (INDIFFERENT, §4.10 — no canon of animals reacting to the
 * Flame), demonstrating the "add a type" flexibility (§1.4) without any
 * engine change: a type simply composes a different subset of the shared
 * policy library.
 */
public final class AnimalActor extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("animal");

    private static final PolicyStack STACK = PolicyStack.of(
            Policies.FLEE,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public AnimalActor(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
