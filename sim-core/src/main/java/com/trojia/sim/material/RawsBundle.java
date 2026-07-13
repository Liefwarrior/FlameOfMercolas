package com.trojia.sim.material;

import com.trojia.sim.fluid.FluidRegistry;
import com.trojia.sim.random.RandomSource;

import java.util.List;
import java.util.Objects;

/**
 * The complete result of one raws load: both substance registries plus the
 * treatments that minted derived materials into the material registry.
 * Cross-registry references ({@code meltsTo}/{@code freezesTo} across the
 * united substance-id namespace, BLESSING-QUEUE ruling 3) are already
 * validated — consumers may resolve them without re-checking.
 *
 * @param materials  the material universe (includes treatment-minted derived
 *                   materials and the parsed reactions)
 * @param fluids     the fluid universe
 * @param treatments the treatments applied at load, sorted by treatment id
 */
public record RawsBundle(MaterialRegistry materials, FluidRegistry fluids,
        List<Treatment> treatments) {

    /**
     * Validates presence and defensively copies the treatment list.
     *
     * @throws NullPointerException if any component is {@code null}
     */
    public RawsBundle {
        Objects.requireNonNull(materials, "materials");
        Objects.requireNonNull(fluids, "fluids");
        treatments = List.copyOf(treatments);
    }

    /**
     * Returns the combined content fingerprint of both registries — the
     * {@code rawsFingerprint} candidate for the TROJSAV header
     * (ARCHITECTURE.md §9). Deterministic across runs, JVMs and platforms.
     */
    public long fingerprint() {
        return RandomSource.mix64(materials.fingerprint() ^ RandomSource.mix64(fluids.fingerprint()));
    }
}
