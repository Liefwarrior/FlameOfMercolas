package com.trojia.sim.actor;

/**
 * One behavior in the shared policy library (ACTORS-SPEC.md §1.2): sense →
 * score → act, deterministic. Policies are <strong>stateless singletons</strong>
 * — one instance shared by every actor of every type; all per-actor policy
 * state lives on {@link Actor} (targetKind/targetKey/policyTimer, goal
 * fields, …).
 */
public interface BehaviorPolicy {

    /** This policy's stable, append-only identity. */
    PolicyId id();

    /**
     * Pure, integer, draw-free score for {@code self} right now; {@code 0}
     * means "not applicable". Selection picks the maximum across the type's
     * stack, ties broken by stack position (ACTORS-SPEC.md §1.2).
     */
    int score(Actor self, ActorContext ctx);

    /**
     * One tick of behavior for the policy that won selection. May draw named
     * RNG (ACTORS-SPEC.md §2.2) but must never touch world lanes directly
     * (§2.3) — effects route through {@link ActorContext}'s shared verbs.
     */
    void act(Actor self, ActorContext ctx);
}
