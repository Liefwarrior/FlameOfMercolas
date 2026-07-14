package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/** Beggars/urchins/petty-thief texture (ACTORS-SPEC.md §4.3). Thin subclass (§1.4). */
public final class Wastrel extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("wastrel");

    private static final PolicyStack STACK = PolicyStack.of(
            Policies.DEFER_WIELDER,
            Policies.FLEE,
            Policies.SEEK_FOOD,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public Wastrel(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
