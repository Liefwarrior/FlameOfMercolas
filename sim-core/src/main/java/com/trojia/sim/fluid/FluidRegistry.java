package com.trojia.sim.fluid;

import com.trojia.sim.random.RandomSource;

import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Objects;

/**
 * The immutable, boot-built fluid universe (ARCHITECTURE.md §3).
 *
 * <p><strong>Deterministic id assignment:</strong> fluids are sorted by string
 * id ({@link String#compareTo}) and numbered 0..n-1 — the same raws always
 * yield the same {@link FluidId}s on every platform, keeping FLUID-lane bytes
 * golden-master-stable.</p>
 *
 * <p>Deeply immutable and safe to share across engines and threads.</p>
 */
public final class FluidRegistry {

    /** Pinned fingerprint seed constant ("FLUREG01"). */
    private static final long FINGERPRINT_SEED = 0x464C555245473031L;

    private final FluidDefinition[] byId;
    private final String[] sortedKeys;
    private final long fingerprint;

    private FluidRegistry(FluidDefinition[] byId) {
        this.byId = byId;
        this.sortedKeys = new String[byId.length];
        for (int i = 0; i < byId.length; i++) {
            sortedKeys[i] = byId[i].key();
        }
        this.fingerprint = computeFingerprint();
    }

    /**
     * Builds a registry from the given definitions. Input order is irrelevant:
     * ids are assigned from the sorted key order.
     *
     * @param fluids the fluid definitions; keys must be unique
     * @return the immutable registry
     * @throws NullPointerException     if an argument or element is {@code null}
     * @throws IllegalArgumentException if a key is duplicated, or more than
     *                                  32768 fluids are supplied
     */
    public static FluidRegistry of(Collection<FluidDefinition> fluids) {
        Objects.requireNonNull(fluids, "fluids");
        if (fluids.size() > Short.MAX_VALUE + 1) {
            throw new IllegalArgumentException("too many fluids for a short id: " + fluids.size());
        }
        FluidDefinition[] byId = fluids.toArray(new FluidDefinition[0]);
        Arrays.sort(byId, (a, b) -> a.key().compareTo(b.key()));
        for (int i = 1; i < byId.length; i++) {
            if (byId[i].key().equals(byId[i - 1].key())) {
                throw new IllegalArgumentException("duplicate fluid key: " + byId[i].key());
            }
        }
        return new FluidRegistry(byId);
    }

    /** Returns the number of fluids. */
    public int size() {
        return byId.length;
    }

    /**
     * Returns whether a fluid with the given string id exists.
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
     * @throws IllegalArgumentException if no such fluid exists
     */
    public FluidId id(String key) {
        int index = Arrays.binarySearch(sortedKeys, Objects.requireNonNull(key, "key"));
        if (index < 0) {
            throw new IllegalArgumentException("unknown fluid id: " + key);
        }
        return FluidId.of(index);
    }

    /**
     * Returns the fluid for an assigned id.
     *
     * @param id the assigned id
     * @return the definition
     * @throws IllegalArgumentException if the id was not assigned by this registry
     */
    public FluidDefinition get(FluidId id) {
        return get(id.raw());
    }

    /**
     * Returns the fluid at a raw id index (FLUID-lane id value).
     *
     * @param index the raw id, {@code 0..size()-1}
     * @return the definition
     * @throws IllegalArgumentException if {@code index} is out of range
     */
    public FluidDefinition get(int index) {
        if (index < 0 || index >= byId.length) {
            throw new IllegalArgumentException("fluid id out of range: " + index);
        }
        return byId[index];
    }

    /** Returns all fluids in id order (index == raw id); immutable. */
    public List<FluidDefinition> all() {
        return List.of(byId);
    }

    /**
     * Returns the deterministic content fingerprint of this registry: a pure
     * function of every fluid in canonical (id) order, pinned to the
     * {@code mix64} fold — identical across runs, JVMs and platforms for
     * identical raws. Feeds the TROJSAV {@code rawsFingerprint} guard
     * (ARCHITECTURE.md §9).
     */
    public long fingerprint() {
        return fingerprint;
    }

    private long computeFingerprint() {
        long h = RandomSource.mix64(FINGERPRINT_SEED);
        h = fold(h, byId.length);
        for (FluidDefinition f : byId) {
            h = foldString(h, f.key());
            h = foldString(h, f.displayName());
            h = fold(h, f.density());
            h = fold(h, f.conductivityQ8());
            h = fold(h, f.heatCapacityQ8());
            h = fold(h, f.freezeDeciK());
            h = foldNullable(h, f.freezesTo());
            h = fold(h, f.freezeMinDepth());
            h = fold(h, f.boilDeciK());
            h = foldNullable(h, f.boilsTo());
            h = fold(h, f.evapMaxDepth());
            h = fold(h, f.evapMinDeciK());
            h = fold(h, f.evapChanceQ16());
            h = fold(h, f.tags().size());
            for (String tag : f.tags()) {
                h = foldString(h, tag);
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
