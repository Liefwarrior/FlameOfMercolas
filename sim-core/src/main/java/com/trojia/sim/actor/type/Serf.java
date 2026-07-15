package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/** The Docks laboring commoner (ACTORS-SPEC.md §4.2). Thin subclass (§1.4). */
public final class Serf extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("serf");

    private static final PolicyStack STACK = PolicyStack.of(
            Policies.PLAYER_CONTROL,
            Policies.DEFER_WIELDER,
            Policies.FLEE,
            Policies.SEEK_FOOD,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public Serf(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
