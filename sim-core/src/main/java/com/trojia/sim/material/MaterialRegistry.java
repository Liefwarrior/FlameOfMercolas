package com.trojia.sim.material;

import com.trojia.sim.random.RandomSource;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Objects;

/**
 * The immutable, boot-built material universe (ARCHITECTURE.md §3).
 *
 * <p><strong>Deterministic id assignment:</strong> materials are sorted by
 * string id ({@link String#compareTo}) and numbered 0..n-1 — the same raws
 * always yield the same {@link MaterialId}s on every platform, which is what
 * makes MATERIAL-lane bytes golden-master-stable.</p>
 *
 * <p><strong>Hot tables:</strong> the per-tick systems never touch
 * {@link Material} records; the thermal/light kernels read precomputed
 * primitive arrays indexed by raw id, including {@link #invCapQ16(int)} —
 * {@code floor(2^24 / heatCapacityQ8)}, the Q16 reciprocal of the material's
 * heat capacity ({@code capQ8/256}) for the diffusion multiply-shift.</p>
 *
 * <p><strong>Reactions:</strong> the registry also holds the parsed
 * {@link ReactionDefinition}s (sorted by reaction id) for the F3 reaction
 * systems; reactions reference materials by string id, resolved here.</p>
 *
 * <p>Deeply immutable and safe to share across engines and threads.</p>
 */
public final class MaterialRegistry {

    /** Pinned fingerprint seed constant ("MATREG01"). */
    private static final long FINGERPRINT_SEED = 0x4D41545245473031L;

    private final Material[] byId;
    private final String[] sortedKeys;
    private final List<ReactionDefinition> reactions;
    private final int[] conductivityQ8;
    private final int[] heatCapacityQ8;
    private final int[] invCapQ16;
    private final int[] opacity;
    private final int[] flammability;
    private final int[] ignitionDeciK;
    private final long fingerprint;

    private MaterialRegistry(Material[] byId, List<ReactionDefinition> reactions) {
        this.byId = byId;
        this.sortedKeys = new String[byId.length];
        this.reactions = reactions;
        this.conductivityQ8 = new int[byId.length];
        this.heatCapacityQ8 = new int[byId.length];
        this.invCapQ16 = new int[byId.length];
        this.opacity = new int[byId.length];
        this.flammability = new int[byId.length];
        this.ignitionDeciK = new int[byId.length];
        for (int i = 0; i < byId.length; i++) {
            Material material = byId[i];
            sortedKeys[i] = material.key();
            conductivityQ8[i] = material.conductivityQ8();
            heatCapacityQ8[i] = material.heatCapacityQ8();
            invCapQ16[i] = (1 << 24) / material.heatCapacityQ8();
            opacity[i] = material.opacity();
            flammability[i] = material.flammability();
            ignitionDeciK[i] = material.ignitionDeciK();
        }
        this.fingerprint = computeFingerprint();
    }

    /**
     * Builds a registry from the given definitions. Input order is irrelevant:
     * ids are assigned from the sorted key order; reactions are sorted by
     * reaction id.
     *
     * @param materials the material definitions; keys must be unique
     * @param reactions the reaction definitions; keys must be unique
     * @return the immutable registry
     * @throws NullPointerException     if an argument or element is {@code null}
     * @throws IllegalArgumentException if a key is duplicated, or more than
     *                                  32768 materials are supplied
     */
    public static MaterialRegistry of(Collection<Material> materials,
            Collection<ReactionDefinition> reactions) {
        Objects.requireNonNull(materials, "materials");
        Objects.requireNonNull(reactions, "reactions");
        if (materials.size() > Short.MAX_VALUE + 1) {
            throw new IllegalArgumentException(
                    "too many materials for a short id: " + materials.size());
        }
        Material[] byId = materials.toArray(new Material[0]);
        Arrays.sort(byId, (a, b) -> a.key().compareTo(b.key()));
        for (int i = 1; i < byId.length; i++) {
            if (byId[i].key().equals(byId[i - 1].key())) {
                throw new IllegalArgumentException("duplicate material key: " + byId[i].key());
            }
        }
        List<ReactionDefinition> sortedReactions = new ArrayList<>(reactions);
        sortedReactions.sort((a, b) -> a.key().compareTo(b.key()));
        for (int i = 1; i < sortedReactions.size(); i++) {
            if (sortedReactions.get(i).key().equals(sortedReactions.get(i - 1).key())) {
                throw new IllegalArgumentException(
                        "duplicate reaction key: " + sortedReactions.get(i).key());
            }
        }
        return new MaterialRegistry(byId, List.copyOf(sortedReactions));
    }

    /** Returns the number of materials. */
    public int size() {
        return byId.length;
    }

    /**
     * Returns whether a material with the given string id exists.
     *
     * @param key the string id
     * @return {@code true} iff present
     */
    public boolean contains(String key) {
        return Arrays.binarySearch(sortedKeys, Objects.requireNonNull(key, "key")) >= 0;
    }

    /**
     * Resolves a string id to its assigned numeric id.
     *
     * @param key the string id
     * @return the assigned id
     * @throws IllegalArgumentException if no such material exists
     */
    public MaterialId id(String key) {
        int index = Arrays.binarySearch(sortedKeys, Objects.requireNonNull(key, "key"));
        if (index < 0) {
            throw new IllegalArgumentException("unknown material id: " + key);
        }
        return MaterialId.of(index);
    }

    /**
     * Returns the material for an assigned id.
     *
     * @param id the assigned id
     * @return the definition
     * @throws IllegalArgumentException if the id was not assigned by this registry
     */
    public Material get(MaterialId id) {
        return get(id.raw());
    }

    /**
     * Returns the material at a raw id index (MATERIAL-lane value).
     *
     * @param index the raw id, {@code 0..size()-1}
     * @return the definition
     * @throws IllegalArgumentException if {@code index} is out of range
     */
    public Material get(int index) {
        if (index < 0 || index >= byId.length) {
            throw new IllegalArgumentException("material id out of range: " + index);
        }
        return byId[index];
    }

    /** Returns all materials in id order (index == raw id); immutable. */
    public List<Material> all() {
        return List.of(byId);
    }

    /** Returns all reactions sorted by reaction id; immutable. */
    public List<ReactionDefinition> reactions() {
        return reactions;
    }

    // ------------------------------------------------------------- hot tables

    /** Returns {@code conductivityQ8} for a raw id; no bounds check beyond the array's. */
    public int conductivityQ8(int index) {
        return conductivityQ8[index];
    }

    /** Returns {@code heatCapacityQ8} for a raw id; no bounds check beyond the array's. */
    public int heatCapacityQ8(int index) {
        return heatCapacityQ8[index];
    }

    /**
     * Returns {@code floor(2^24 / heatCapacityQ8)} for a raw id: the Q16
     * reciprocal of the heat capacity used by the thermal multiply-shift.
     * Always fits an int (the loader enforces {@code heatCapacityQ8 >= 12},
     * bounding this at 1,398,101).
     */
    public int invCapQ16(int index) {
        return invCapQ16[index];
    }

    /** Returns light opacity 0..31 for a raw id; no bounds check beyond the array's. */
    public int opacity(int index) {
        return opacity[index];
    }

    /** Returns flammability 0..3 for a raw id; no bounds check beyond the array's. */
    public int flammability(int index) {
        return flammability[index];
    }

    /**
     * Returns the ignition threshold in deciK for a raw id, or
     * {@link Material#NONE} for non-flammables.
     */
    public int ignitionDeciK(int index) {
        return ignitionDeciK[index];
    }

    // ------------------------------------------------------------ fingerprint

    /**
     * Returns the deterministic content fingerprint of this registry: a pure
     * function of every material and reaction in canonical (id) order, pinned
     * to the {@code mix64} fold — identical across runs, JVMs and platforms
     * for identical raws. Feeds the TROJSAV {@code rawsFingerprint} guard
     * (ARCHITECTURE.md §9).
     */
    public long fingerprint() {
        return fingerprint;
    }

    private long computeFingerprint() {
        long h = RandomSource.mix64(FINGERPRINT_SEED);
        h = fold(h, byId.length);
        for (Material m : byId) {
            h = foldString(h, m.key());
            h = foldString(h, m.displayName());
            h = fold(h, m.phase().ordinal());
            h = fold(h, m.density());
            h = fold(h, m.hardness());
            h = fold(h, m.flammability());
            h = fold(h, m.ignitionDeciK());
            h = fold(h, m.meltDeciK());
            h = foldNullable(h, m.meltsTo());
            h = fold(h, m.meltYieldUnits());
            h = fold(h, m.boilDeciK());
            h = foldNullable(h, m.boilsTo());
            h = fold(h, m.conductivityQ8());
            h = fold(h, m.heatCapacityQ8());
            h = fold(h, m.fuelTicks());
            h = foldNullable(h, m.burnsTo());
            h = fold(h, m.valueCp());
            h = fold(h, m.tags().size());
            for (String tag : m.tags()) {
                h = foldString(h, tag);
            }
            h = fold(h, m.opacity());
            h = fold(h, m.features().size());
            for (MaterialFeature feature : m.features()) {
                h = foldFeature(h, feature);
            }
        }
        h = fold(h, reactions.size());
        for (ReactionDefinition r : reactions) {
            h = foldString(h, r.key());
            h = foldString(h, r.displayName());
            h = foldString(h, r.solidId());
            h = foldString(h, r.triggerFluidTag());
            h = fold(h, r.expansion());
            h = fold(h, r.wearPerUnit());
            h = fold(h, r.wearCapacity());
            h = foldNullable(h, r.pulseGasId());
            h = fold(h, r.pulseMagnitudeCap());
        }
        return h;
    }

    private static long foldFeature(long h, MaterialFeature feature) {
        switch (feature) {
            case MaterialFeature.Chargeable c -> {
                h = fold(h, 1);
                h = fold(h, c.capacityCu());
                h = fold(h, c.maxSafeDischargePerTick());
                h = fold(h, c.saturationPct());
                h = fold(h, c.saturationHeatDeciKPerTick());
                h = fold(h, c.equilibriumDeciK());
                h = fold(h, c.colorStops().size());
                for (MaterialFeature.Chargeable.ColorStop stop : c.colorStops()) {
                    h = fold(h, stop.uptoPct());
                    h = fold(h, stop.tintRgb());
                    h = fold(h, stop.lightLevel());
                }
            }
            case MaterialFeature.ContactReactive c -> {
                h = fold(h, 2);
                h = foldString(h, c.reagentTag());
            }
            case MaterialFeature.Emissive e -> {
                h = fold(h, 3);
                h = fold(h, e.lightLevel());
                h = fold(h, e.tintRgb());
            }
            case MaterialFeature.ShatterOnSpike s -> {
                h = fold(h, 4);
                h = fold(h, s.spikeCuPerTick());
                h = foldString(h, s.shattersTo());
                h = fold(h, s.radiusChebyshev());
            }
        }
        return h;
    }

    private static long fold(long h, long value) {
        return RandomSource.mix64(h ^ value);
    }

    private static long foldNullable(long h, String s) {
        return s == null ? fold(h, -1L) : foldString(h, s);
    }

    private static long foldString(long h, String s) {
        h = fold(h, s.length());
        for (int i = 0; i < s.length(); i++) {
            h = fold(h, s.charAt(i));
        }
        return h;
    }
}
