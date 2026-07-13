package com.trojia.sim.material;

import java.util.List;
import java.util.Objects;

/**
 * One immutable material definition: every ARCHITECTURE.md §10 property in
 * loader-normalized integer form. Temperatures arrive in raws as integer
 * Kelvin and are converted to deciK here (×10, matching the 2-byte deciK
 * TEMPERATURE lane); absent temperatures are {@link #NONE}; absent substance
 * references are {@code null}.
 *
 * <p>Substance references ({@code meltsTo}, {@code boilsTo}, {@code burnsTo},
 * feature targets) are kept as string ids: {@code meltsTo}/{@code boilsTo}
 * resolve in the <em>united</em> material∪fluid namespace (BLESSING-QUEUE
 * ruling 3 — ice melts to the fluid {@code water}), so a short-typed id of
 * either registry cannot represent them. The loader guarantees they resolve;
 * hot systems resolve them once at boot, never per tick.</p>
 *
 * <p>The record deliberately carries no {@link MaterialId}: numeric identity
 * is assigned by {@link MaterialRegistry} from the sorted key order and would
 * be meaningless on a free-standing definition.</p>
 *
 * @param key             unique string id from the raw ({@code "id"} field)
 * @param displayName     human-readable name
 * @param phase           intrinsic phase
 * @param density         relative density; 1..65535
 * @param hardness        dig/damage resistance tier; 0..255
 * @param flammability    0..3 severity scale (0 = inert; BLESSING-QUEUE ruling 11)
 * @param ignitionDeciK   ignition temperature in deciK, or {@link #NONE}
 * @param meltDeciK       melt temperature in deciK, or {@link #NONE}
 * @param meltsTo         substance id minted at melt, or {@code null}
 * @param meltYieldUnits  units of {@code meltsTo} minted per tile; 0 when no melt
 * @param boilDeciK       boil temperature in deciK, or {@link #NONE}
 * @param boilsTo         substance id minted at boil, or {@code null}
 * @param conductivityQ8  thermal conductivity, Q8 (256 = 1.0); 0..256
 * @param heatCapacityQ8  thermal capacity, Q8; {@code >=} 12 (stability floor)
 * @param fuelTicks       burn duration in ticks; 0..4095 (0 for non-flammables)
 * @param burnsTo         material id left after burnout, or {@code null}
 * @param valueCp         economic value in copper pieces
 * @param tags            tag list in raw order (treatment tags appended)
 * @param opacity         light opacity 0..31 (raw {@code light.opacity})
 * @param features        optional behaviors in canonical kind order
 */
public record Material(
        String key,
        String displayName,
        MaterialPhase phase,
        int density,
        int hardness,
        int flammability,
        int ignitionDeciK,
        int meltDeciK,
        String meltsTo,
        int meltYieldUnits,
        int boilDeciK,
        String boilsTo,
        int conductivityQ8,
        int heatCapacityQ8,
        int fuelTicks,
        String burnsTo,
        int valueCp,
        List<String> tags,
        int opacity,
        List<MaterialFeature> features) {

    /** Sentinel for an absent temperature threshold. */
    public static final int NONE = -1;

    /**
     * Validates cheap structural invariants (the loader enforces the full
     * ARCHITECTURE.md §10 rule set with file/field context before construction)
     * and defensively copies the lists.
     *
     * @throws NullPointerException     if a required reference is {@code null}
     * @throws IllegalArgumentException if a value is structurally out of range
     */
    public Material {
        Objects.requireNonNull(key, "key");
        Objects.requireNonNull(displayName, "displayName");
        Objects.requireNonNull(phase, "phase");
        if (key.isEmpty()) {
            throw new IllegalArgumentException("material key must be non-empty");
        }
        tags = List.copyOf(tags);
        features = List.copyOf(features);
    }

    /** Returns whether this material can ignite ({@code flammability > 0}). */
    public boolean flammable() {
        return flammability > 0;
    }

    /** Returns whether this material melts ({@code meltDeciK != NONE}). */
    public boolean melts() {
        return meltDeciK != NONE;
    }

    /**
     * Returns this material's feature of the given kind, or {@code null} if
     * absent. Deterministic linear scan over at most four entries.
     *
     * @param <T>  the feature kind
     * @param kind the feature class, e.g. {@code MaterialFeature.Chargeable.class}
     * @return the feature instance, or {@code null}
     */
    public <T extends MaterialFeature> T feature(Class<T> kind) {
        for (MaterialFeature feature : features) {
            if (kind.isInstance(feature)) {
                return kind.cast(feature);
            }
        }
        return null;
    }

    /**
     * Returns whether this material carries the given tag (exact match).
     *
     * @param tag the tag to test
     * @return {@code true} iff present
     */
    public boolean tagged(String tag) {
        return tags.contains(tag);
    }
}
