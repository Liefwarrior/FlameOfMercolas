package com.trojia.sim.actor;

import com.trojia.sim.actor.job.JobRegistry;

/**
 * The per-tick facade handed to every {@link Actor#tick} and
 * {@link BehaviorPolicy} (ACTORS-SPEC.md §2). Mirrors
 * {@code com.trojia.sim.engine.TickContext}'s shape but scoped to the actor
 * system: actors never touch world lanes directly (§2.3), so this exposes
 * only actor-owned registries, named-draw access, and the small set of
 * cross-actor queries the starter policy library needs.
 */
public interface ActorContext {

    /** The tick currently being simulated. */
    long tick();

    /** The one persisted RNG seed (ARCHITECTURE.md §1.1 #16). */
    long worldSeed();

    /** The actor registry (ascending-id iteration, spatial/home/job lookups). */
    ActorRegistry registry();

    /** The Home side-table (ACTORS-SPEC.md §11.1). */
    HomeRegistry homes();

    /** The relationship side-table (ACTORS-SPEC.md §11.3). */
    RelationshipRegistry relationships();

    /** The ItemsLite side-table (ACTORS-SPEC.md §2.6, §11.2). */
    ItemsLiteRegistry items();

    /** The bound Job taxonomy (ACTORS-SPEC.md §10.2). */
    JobRegistry jobs();

    /**
     * One named draw (ACTORS-SPEC.md §2.2): {@code spatialKey = actorId}
     * always; {@code drawIndex} is the per-actor per-tick counter the caller
     * maintains (shared across every stream, the pinned attribution rule).
     */
    long draw(ActorRngStream stream, int actorId, int drawIndex);

    /**
     * The next draw index for {@code actorId} this tick, incrementing the
     * shared per-actor counter (ACTORS-SPEC.md §2.2's "one counter per actor
     * shared across all streams"). Convenience over tracking indices by hand.
     */
    int nextDrawIndex(int actorId);

    /**
     * The cell of the actor currently presenting as the Wielder (the first
     * bound {@code Job.FlameOfMerc} holder, ascending id), or
     * {@link Actor#NONE} if none is spawned. Backs {@code DEFER_WIELDER}
     * (§1.3) — deference reads the PRESENTED identity per the DECISIONS
     * seam, so a disguised Wielder (presented != true) does not trigger it.
     */
    int wielderCell();

    /** The actor id backing {@link #wielderCell()}, or {@link Actor#NONE} if none is spawned. */
    int wielderId();

    /**
     * The one well-known arrest holding-cell (ARREST-SPEC addendum): the walkable floor cell
     * beside the K34 Guardhouse's cage bars, wired in at scenario-bake time (mirrors how
     * {@link #wielderCell()}/homes are populated) — a fixed baked cell, not a spatial query.
     * {@link Actor#NONE} for scenarios/tests that never wire one (e.g. the world-less
     * bootstrap): {@link HeldPolicy} degrades to "hold in place" when this is unset.
     */
    int arrestHoldCell();

    /**
     * Pure read (§2.3 "actors never write lanes"): {@code true} if {@code cell}
     * is safe to step onto right now ({@code com.trojia.sim.world.Walkability
     * .isWalkable}). Systems bound to no world (the headless world-less
     * bootstrap, {@code ActorsDemoMain}) return {@code true} unconditionally —
     * there is nothing to collide with.
     */
    boolean isWalkable(int cell);

    /**
     * The live actor-actor occupancy view for this tick (the "only 2 to a cell" cap, {@link
     * Actor#MAX_OCCUPANTS_PER_CELL}). {@link ActorsSystem} rebuilds a shared {@link
     * OccupancyIndex} from every actor's cell before ticking and returns a view whose {@code
     * occupantsAt} reads it and whose {@code onEnter} keeps it live as actors move; the world-less
     * bootstrap and test doubles return {@link Actor.OccupancyQuery#UNLIMITED} (no cap). Movement
     * call sites pass this into {@code stepToward}/{@code stepAlongRoute}.
     */
    Actor.OccupancyQuery occupancy();
}
