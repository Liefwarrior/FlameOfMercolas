package com.trojia.sim.event;

/**
 * Charge left a tile in one tick (REACTIONS phase, charge sub-system). The
 * shatter system — same phase, later registration index — consumes it same
 * tick and spikes lightstone within Chebyshev distance 2 (including 0) when
 * the rate exceeds the material's safe discharge.
 *
 * @param cell        packed position of the discharging tile
 * @param releasedCu  total charge released this tick, in cu
 * @param ratePerTick discharge rate in cu/tick (compared against maxSafeDischargePerTick)
 */
public record EnergyDischargedEvent(int cell, int releasedCu, int ratePerTick)
        implements SimEvent {
}
