package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/**
 * Priest of the Flame — GAME-CANON-ADDITION rank, canon-anchored slot
 * (ACTORS-SPEC.md §4.4). Thin subclass (§1.4).
 */
public final class PriestOfTheFlame extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("priest_of_the_flame");

    private static final PolicyStack STACK = PolicyStack.of(
            // Law & order pass: the guard-side APPREHEND can now arrest ANY civic type (not just
            // the villain-hosting Wastrel), so custody must dominate this stack too or a HELD
            // offender of this type would never be escorted or released.
            Policies.HELD,
            // Density revisit: shove-riot house arrest (score 4500, just under HELD 5000) --
            // any citizen type can shove, so any can be sent home to sleep for a day.
            Policies.HOUSE_ARREST,
            Policies.PLAYER_CONTROL,
            Policies.DEFER_WIELDER,
            Policies.FLEE,
            Policies.SEEK_FOOD,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public PriestOfTheFlame(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
