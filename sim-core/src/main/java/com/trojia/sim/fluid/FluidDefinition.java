package com.trojia.sim.fluid;

import java.util.List;
import java.util.Objects;

/**
 * One immutable fluid definition in loader-normalized integer form, per the
 * shipped fluids raw schema (water). Temperatures arrive in raws as integer
 * Kelvin and are converted to deciK here (×10); absent thresholds are
 * {@link #NONE}; absent substance references are {@code null}.
 *
 * <p><strong>Freeze:</strong> at {@code temp <= freezeDeciK} a pooled column of
 * depth {@code >= freezeMinDepth} converts to the grid material
 * {@code freezesTo} (water → ice; a cross-registry reference, BLESSING-QUEUE
 * ruling 3). <strong>Boil:</strong> at {@code temp >= boilDeciK} pooled units
 * vaporize; {@code boilsTo} stays {@code null} in v0 — the
 * {@code liquid}-tag⇒boilsTo rule is <em>not</em> binding for fluids (ruling 3;
 * steam is the reserved seam). <strong>Evaporation:</strong> hash-phase, no RNG
 * draws: depth {@code <= evapMaxDepth} and {@code temp >= evapMinDeciK} remove
 * one unit with per-cell per-tick chance {@code evapChanceQ16 / 65536}.</p>
 *
 * @param key            unique string id from the raw ({@code "id"} field)
 * @param displayName    human-readable name
 * @param density        relative density; 1..65535
 * @param conductivityQ8 thermal conductivity substituted into the kernel for
 *                       pooled depth; 0..256
 * @param heatCapacityQ8 thermal capacity, Q8; {@code >=} 12 (stability floor)
 * @param freezeDeciK    freeze threshold in deciK, or {@link #NONE}
 * @param freezesTo      material id minted at freeze, or {@code null}
 * @param freezeMinDepth minimum pooled depth that freezes; 1..7 (0 when no freeze)
 * @param boilDeciK      boil threshold in deciK, or {@link #NONE}
 * @param boilsTo        substance id minted at boil, or {@code null} (v0: always null)
 * @param evapMaxDepth   deepest pool that still evaporates; 0..7 (0 = never)
 * @param evapMinDeciK   evaporation threshold in deciK, or {@link #NONE}
 * @param evapChanceQ16  per-cell per-tick evaporation chance, Q16; 0..65536
 * @param tags           tag list in raw order ({@code "liquid"} marks a phorys
 *                       reagent and quench capability)
 */
public record FluidDefinition(
        String key,
        String displayName,
        int density,
        int conductivityQ8,
        int heatCapacityQ8,
        int freezeDeciK,
        String freezesTo,
        int freezeMinDepth,
        int boilDeciK,
        String boilsTo,
        int evapMaxDepth,
        int evapMinDeciK,
        int evapChanceQ16,
        List<String> tags) {

    /** Sentinel for an absent temperature threshold. */
    public static final int NONE = -1;

    /**
     * Validates cheap structural invariants (the loader enforces the full rule
     * set with file/field context) and defensively copies the tag list.
     *
     * @throws NullPointerException     if a required reference is {@code null}
     * @throws IllegalArgumentException if {@code key} is empty
     */
    public FluidDefinition {
        Objects.requireNonNull(key, "key");
        Objects.requireNonNull(displayName, "displayName");
        if (key.isEmpty()) {
            throw new IllegalArgumentException("fluid key must be non-empty");
        }
        tags = List.copyOf(tags);
    }

    /** Returns whether this fluid freezes ({@code freezeDeciK != NONE}). */
    public boolean freezes() {
        return freezeDeciK != NONE;
    }

    /**
     * Returns whether this fluid carries the given tag (exact match).
     *
     * @param tag the tag to test
     * @return {@code true} iff present
     */
    public boolean tagged(String tag) {
        return tags.contains(tag);
    }
}
