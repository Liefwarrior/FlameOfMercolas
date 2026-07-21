package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/**
 * The wharf cat (living-docks beast pass) — ownerless warehouse hunter: prowls the shops and
 * stores ({@code beast.prowl} wander) and hunts mice as its food channel
 * ({@code BeastHuntPolicy} — the beast replacement for the citizen {@code SEEK_FOOD} machine
 * no beast can act on). Not an {@code animal}: no Keeper, no STAY_NEAR_OWNER. Thin subclass
 * (§1.4).
 */
public final class CatActor extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("cat");

    private static final PolicyStack STACK = PolicyStack.of(
            Policies.FLEE,
            Policies.BEAST_HUNT,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public CatActor(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
