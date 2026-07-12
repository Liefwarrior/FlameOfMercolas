package com.trojia.sim.event;

/**
 * Sealed root of the simulation event taxonomy (ARCHITECTURE.md §5). Events are
 * the low-volume half of the two-channel plumbing rule (§6): semantic facts
 * travel here as records of primitives and ids only (ArchUnit-enforced); field
 * deltas travel on per-lane change logs. Per-tile event emission is forbidden
 * unless gated by a raws flag.
 *
 * <p>Every event is stamped {@code (tick, phase, regIndex, seq)} at emission.
 * An event emitted at {@code (phase P, regIndex I)} of tick T is visible to
 * consumers positioned after {@code (P, I)} in T and at-or-before {@code (P, I)}
 * in T+1, then retired ("one lap"). Cells are 30-bit
 * {@link com.trojia.sim.world.PackedPos} ints; consumers must skip cells
 * outside the concrete set.
 *
 * <p>Macro observer rows of §5 (MacroIncident*, PriceChanged,
 * ProductionCompleted) do not ride the bus — they drain into polled logs —
 * and are therefore not part of this hierarchy.
 */
public sealed interface SimEvent
        permits ExternalIgnition, ExternalFluidSpawned, ExternalChargeApplied,
        ChunkThawed, ChunkFrozen,
        ReagentContactEvent, FluidVaporizedEvent, FluidFrozenEvent,
        TileIgnited, TileExtinguished, FireLuminanceChanged,
        TemperatureThresholdEvent, MaterialPhaseChangedEvent, MaterialTransformedEvent,
        PressurePulseEvent,
        ChargeStopChangedEvent, ChargeSaturatedEvent, EnergyDischargedEvent {
}
