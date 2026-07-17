package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/** Stall/shop keepers of the Docks set (ACTORS-SPEC.md §4.6). Thin subclass (§1.4). */
public final class Shopkeeper extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("shopkeeper");

    private static final PolicyStack STACK = PolicyStack.of(
            // Law & order pass: the guard-side APPREHEND can now arrest ANY civic type (not just
            // the villain-hosting Wastrel), so custody must dominate this stack too or a HELD
            // offender of this type would never be escorted or released.
            Policies.HELD,
            Policies.PLAYER_CONTROL,
            Policies.DEFER_WIELDER,
            Policies.FLEE,
            Policies.SEEK_FOOD,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public Shopkeeper(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
