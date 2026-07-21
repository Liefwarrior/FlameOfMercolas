package com.trojia.sim.actor.type;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Policies;
import com.trojia.sim.actor.PolicyStack;

/**
 * The quay mouse (living-docks beast pass) — small prey underfoot: scurries around its den
 * (warehouse corners, garbage bins, the granary), flees a lingering predator, and is the
 * BEAST food channel's supply side ({@code BeastHuntPolicy}). Thin subclass (§1.4).
 *
 * <p>Stack notes: {@code DOWNED_INERT} first — a caught mouse lies still until the existing
 * {@code downedTimer} revive clears DOWNED. Deliberately NO {@code SEEK_FOOD}: a mouse is not
 * a market participant; its eat loop is the den nibble inside {@code beast.prey}'s scurry
 * ({@code JobBehaviors.pursuePreyScurry}), which touches no FOOD item.
 */
public final class MouseActor extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("mouse");

    private static final PolicyStack STACK = PolicyStack.of(
            Policies.DOWNED_INERT,
            Policies.FLEE,
            Policies.RETURN_HOME,
            Policies.GOAL_PURSUE,
            Policies.LOITER);

    public MouseActor(int id, ActorTypeStats stats, int cell) {
        super(id, TYPE, stats, cell);
    }

    @Override
    protected PolicyStack policies() {
        return STACK;
    }
}
