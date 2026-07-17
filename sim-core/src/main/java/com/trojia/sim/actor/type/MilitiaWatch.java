package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/**
 * The law pillar's face (ACTORS-SPEC.md §4.1). Thin subclass: declares only
 * the type id, the composed policy stack and the constructor (§1.4) — every
 * field and verb lives on {@link Actor}.
 */
public final class MilitiaWatch extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("militia_watch");

    private static final PolicyStack STACK = PolicyStack.of(
            Policies.HELD,
            Policies.PLAYER_CONTROL,
            Policies.DEFER_WIELDER,
            Policies.FLEE,
            // Law & order pass (Pass 11): the Watch-side enforcement loop. Declared above
            // GOAL_PURSUE / below FLEE per the stack convention; its HIGH FIXED score (1500,
            // landmine D) is what actually outranks every NEED band while a case is open.
            Policies.APPREHEND,
            Policies.SEEK_FOOD,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public MilitiaWatch(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
