package com.trojia.tools.palette;

/**
 * Physical phase of a material raw ({@code "phase"} field, ARCHITECTURE.md section 10),
 * as far as palette generation cares: the phase decides which {@link PaletteForm}s are
 * relevant for the material.
 *
 * <p>Package-private on purpose — this is a loader-internal projection of the raws,
 * not part of the tool's public API.</p>
 */
enum PalettePhase {

    /** Solid grid material: gets architectural forms. */
    SOLID,
    /** Liquid grid material (e.g. {@code chromatis_melt}): lies on the ground, FLOOR only. */
    LIQUID,
    /** Gaseous material: not paintable as terrain, no palette tiles in v0. */
    GAS
}
