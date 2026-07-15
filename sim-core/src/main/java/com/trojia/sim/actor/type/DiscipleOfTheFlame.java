package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/**
 * Disciple of the Flame — GAME-CANON-ADDITION rank, canon-anchored slot
 * (ACTORS-SPEC.md §4.5). Thin subclass (§1.4). Every Disciple spawns bound to
 * a Priest (the EMPLOYER relationship edge, §11.4 step 3) — enforced by the
 * spawner, not this class.
 */
public final class DiscipleOfTheFlame extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("disciple_of_the_flame");

    private static final PolicyStack STACK = PolicyStack.of(
            Policies.PLAYER_CONTROL,
            Policies.DEFER_WIELDER,
            Policies.FLEE,
            Policies.SEEK_FOOD,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public DiscipleOfTheFlame(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
