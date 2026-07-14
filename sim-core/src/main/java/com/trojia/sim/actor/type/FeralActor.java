package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/**
 * The ninth type — ambience with teeth: gull/rat/stray cur, ownerless
 * (ACTORS-SPEC.md §4.9, the Q7 Docks blessing). Thin subclass (§1.4): the
 * §1.4 walkthrough exercised for real — one file, one raws entry, one sorted
 * line in {@code ActorTypes}, no engine change.
 */
public final class FeralActor extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("feral");

    private static final PolicyStack STACK = PolicyStack.of(
            Policies.FLEE,
            Policies.SEEK_FOOD,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public FeralActor(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
