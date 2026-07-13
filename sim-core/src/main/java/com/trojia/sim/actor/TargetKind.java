package com.trojia.sim.actor;

/**
 * The shared target vocabulary (ACTORS-SPEC.md §1.1): a policy's or a job's
 * current target is one of these kinds plus an integer key (a
 * {@code PackedPos}, an {@code ActorId}, or an item id, depending on kind).
 */
public enum TargetKind {
    NONE,
    CELL,
    ACTOR,
    ITEM
}
