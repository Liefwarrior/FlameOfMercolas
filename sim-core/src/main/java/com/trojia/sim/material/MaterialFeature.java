package com.trojia.sim.material;

import java.util.List;

/**
 * The sealed vocabulary of optional material behaviors parsed from a raw's
 * {@code features} block (ARCHITECTURE.md §3; shapes blessed as spec by
 * BLESSING-QUEUE ruling 2). A material carries at most one feature of each
 * kind; the loader stores them in canonical kind order (alphabetical by JSON
 * key: {@code chargeable}, {@code contactReactive}, {@code emissive},
 * {@code shatterOnSpike}) regardless of source order, so registry fingerprints
 * are independent of raw-file member ordering.
 *
 * <p>All values are integer/fixed-point (ARCHITECTURE.md §6); tints are packed
 * {@code 0xRRGGBB} ints parsed from {@code "#RRGGBB"} literals. Every
 * charge/spike magnitude fits 16 bits — loader-validated, so systems may store
 * them in short-width overlays without further checks.</p>
 */
public sealed interface MaterialFeature {

    /**
     * Stores energy in the CHARGE overlay; ticked by the charge system
     * (chromatis, lightstone).
     *
     * <p>{@code colorStops} are a pure <em>fill ramp</em> for appearance
     * buckets (BLESSING-QUEUE ruling 5): the appearance bucket is the ordinal
     * of the first stop whose {@code uptoPct} is {@code >=} the current charge
     * percentage. Heat-glow (orange while discharging) is a renderer overlay,
     * never a stop.</p>
     *
     * @param capacityCu                 total charge capacity in cu; 1..65535
     * @param maxSafeDischargePerTick    highest non-spike discharge rate in cu/tick; 1..65535
     * @param saturationPct              charge percentage at which saturation heating begins; 0..100
     * @param saturationHeatDeciKPerTick heat injected per tick while saturated, in deciK; 0..65535
     * @param equilibriumDeciK           temperature the saturated tile trends toward, in deciK; 0..65535
     * @param colorStops                 1..4 fill-ramp stops, strictly increasing, last at 100
     */
    record Chargeable(int capacityCu, int maxSafeDischargePerTick, int saturationPct,
            int saturationHeatDeciKPerTick, int equilibriumDeciK,
            List<ColorStop> colorStops) implements MaterialFeature {

        /**
         * Defensively copies the stop list.
         */
        public Chargeable {
            colorStops = List.copyOf(colorStops);
        }

        /**
         * One appearance stop of the charge fill ramp; the stop's list index is
         * the appearance bucket (0..3) served to the art mapping.
         *
         * @param uptoPct    inclusive upper charge percentage of this bucket; 1..100
         * @param tintRgb    fill tint as packed {@code 0xRRGGBB}
         * @param lightLevel emitted light while in this bucket; 0..31
         */
        public record ColorStop(int uptoPct, int tintRgb, int lightLevel) {
        }
    }

    /**
     * Converts to a debris material when hit by a discharge spike; consumed by
     * the shatter system scanning Chebyshev distance {@code <= radiusChebyshev}
     * including distance 0 — self-shatter (ARCHITECTURE.md §3).
     *
     * @param spikeCuPerTick  discharge rate at or above which the material shatters; 1..65535
     * @param shattersTo      string id of the debris material; resolves in the material registry
     * @param radiusChebyshev blast radius in Chebyshev metric; 0..15
     */
    record ShatterOnSpike(int spikeCuPerTick, String shattersTo,
            int radiusChebyshev) implements MaterialFeature {
    }

    /**
     * Emits light passively — always on, never charged, never shattering
     * (glowstone).
     *
     * @param lightLevel emitted light level; 0..31
     * @param tintRgb    emission tint as packed {@code 0xRRGGBB}
     */
    record Emissive(int lightLevel, int tintRgb) implements MaterialFeature {
    }

    /**
     * Reacts on fluid contact: gates per-tile {@code ReagentContactEvent}
     * emission (ARCHITECTURE.md §6 — per-tile events are forbidden unless
     * raws-flagged) and sets the per-chunk {@code containsReagents} gate
     * (phorys). The reaction numbers live in the sibling reaction raw.
     *
     * @param reagentTag fluid tag that triggers contact events (e.g. {@code "liquid"})
     */
    record ContactReactive(String reagentTag) implements MaterialFeature {
    }
}
