package com.trojia.sim.engine;

/**
 * The total order of one simulation tick (ARCHITECTURE.md §4). The pipeline is
 * an explicit enum, never dependency-inferred; ordinal order IS execution order
 * and is part of the determinism contract — reordering or renumbering phases
 * invalidates every golden master.
 *
 * <p>Within a phase, systems run in registration order; event visibility is
 * keyed on {@code (phase, registrationIndex)}.
 */
public enum TickPhase {

    /**
     * tick++; InputGate drains SimCommands (arrival order) and scripted actions
     * into the input log; paints applied via ChunkWriter; quantity inputs emitted
     * as External* events; event epoch advance.
     */
    TICK_BEGIN,

    /** Bubble retarget diff and budgeted thaw (≤ 2 chunks and ≤ 2 ms); emits ChunkThawed. */
    BUBBLE_PROMOTE,

    /** Mass moves: fall, pressure BFS, spread, evaporate/boil/freeze, reagent consume. */
    FLUIDS,

    /** Energy: [ThermalDiffusion → PhaseTransition → Fire] in registration order. */
    THERMAL,

    /** State: [PhorysReaction → ChromatisCharge → LightstoneShatter] in registration order. */
    REACTIONS,

    /** Reserved empty slot: aether/uether lands here without renumbering goldens. */
    FIELDS_RESERVED,

    /** Opacity cache update from change logs, then budgeted four-queue relight. */
    LIGHT,

    /** Applies hull credits to chunk summaries/incidents; macro inflows into BORDER. */
    BOUNDARY_FLUX,

    /** [EconomyAccumulator folds lap events → site deltas] then MacroScheduler.runDue. */
    ECONOMY,

    /** Budgeted freeze (≤ 2 chunks/tick): veto → grace → summarize → snapshot → ChunkFrozen. */
    BUBBLE_DEMOTE,

    /**
     * Retire lap events; compact change logs; world.commitTick (revisions bumped,
     * dirty chunks sorted by chunkIndex); TickProfile. Saves are legal ONLY here.
     */
    TICK_END
}
