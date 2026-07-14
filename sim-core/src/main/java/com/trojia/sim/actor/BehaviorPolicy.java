package com.trojia.sim.actor;

/**
 * One behavior in the shared policy library (ACTORS-SPEC.md §1.2): sense →
 * score → act, deterministic. Policies are <strong>stateless singletons</strong>
 * — one instance shared by every actor of every type; all per-actor policy
 * state lives on {@link Actor}.
 *
 * <p>Today the only per-actor policy state any shipped policy actually reads
 * or writes in-tick is the Job-driven {@code goalTargetKind}/{@code
 * goalTargetKey}/{@code goalWorkTicks} trio (used by {@code GOAL_PURSUE} and
 * {@code JobBehaviors}). {@link Actor#targetKind()}/{@link Actor#targetKey()}/
 * {@link Actor#policyTimer()} are generic scratch fields reserved for a
 * future policy that needs its own target/timer distinct from the Job's
 * (e.g. a chase timeout or loiter dwell) — no shipped policy (FLEE,
 * GOAL_PURSUE, RETURN_HOME, LOITER, DEFER_WIELDER) populates them in-tick;
 * they only round-trip through save/load. Do not assume they reflect live
 * in-tick state until a policy actually calls {@code setTarget}/{@code
 * setPolicyTimer}.
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
