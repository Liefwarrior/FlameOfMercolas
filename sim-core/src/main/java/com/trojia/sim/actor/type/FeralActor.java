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
 *
 * <p>Beast food channel (living-docks beast pass): {@code BEAST_HUNT} replaces the citizen
 * {@code SEEK_FOOD} machine — a gull has no ID card, no stocked larder and no commons
 * access, so SEEK_FOOD scored unconditionally on hunger while never being able to feed it,
 * pinning every gull forever (the observed two-cell oscillation). A gull now hunts mice.
 */
public final class FeralActor extends Actor {

    public static final ActorTypeId TYPE = ActorTypeId.of("feral");

    private static final PolicyStack STACK = PolicyStack.of(
            Policies.FLEE,
            Policies.BEAST_HUNT,
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
