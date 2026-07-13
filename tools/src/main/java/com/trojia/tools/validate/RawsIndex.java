package com.trojia.tools.validate;

import java.util.Collections;
import java.util.Map;
import java.util.OptionalInt;
import java.util.Set;
import java.util.SortedMap;
import java.util.SortedSet;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * Immutable id universe distilled from the content raws for map validation:
 * material ids (including treatment-minted derived ids such as
 * {@code trudgeon_wood@getilia_soak}), fluid ids, and per-material flammability
 * (with treatment overrides applied).
 *
 * <p><strong>Determinism contract:</strong> {@link #materialIds()} and
 * {@link #fluidIds()} iterate in natural (lexicographic) order.</p>
 */
public final class RawsIndex {

    private final SortedMap<String, Integer> flammabilityByMaterial;
    private final SortedSet<String> materialIds;
    private final SortedSet<String> fluidIds;

    private RawsIndex(TreeMap<String, Integer> flammabilityByMaterial, TreeSet<String> fluidIds) {
        this.flammabilityByMaterial = Collections.unmodifiableSortedMap(flammabilityByMaterial);
        this.materialIds = Collections.unmodifiableSortedSet(flammabilityByMaterial.navigableKeySet());
        this.fluidIds = Collections.unmodifiableSortedSet(fluidIds);
    }

    /**
     * @param flammabilityByMaterial material id → flammability (0 = inert); copied
     * @param fluidIds               fluid ids; copied
     * @return an immutable index over copies of the inputs
     * @throws NullPointerException if a map/set, key or value is {@code null}
     */
    public static RawsIndex of(Map<String, Integer> flammabilityByMaterial, Set<String> fluidIds) {
        TreeMap<String, Integer> materials = new TreeMap<>();
        flammabilityByMaterial.forEach((id, flam) -> {
            if (id == null || flam == null) {
                throw new NullPointerException("material id/flammability must not be null");
            }
            materials.put(id, flam);
        });
        TreeSet<String> fluids = new TreeSet<>();
        for (String id : fluidIds) {
            if (id == null) {
                throw new NullPointerException("fluid id must not be null");
            }
            fluids.add(id);
        }
        return new RawsIndex(materials, fluids);
    }

    /** @return all known material ids (base + treatment-derived), lexicographic order */
    public SortedSet<String> materialIds() {
        return materialIds;
    }

    /** @return all known fluid ids, lexicographic order */
    public SortedSet<String> fluidIds() {
        return fluidIds;
    }

    /** @param id candidate material id
     *  @return {@code true} iff a raw (or minted treatment) declares this material */
    public boolean isMaterial(String id) {
        return flammabilityByMaterial.containsKey(id);
    }

    /** @param id candidate fluid id
     *  @return {@code true} iff a fluid raw declares this id */
    public boolean isFluid(String id) {
        return fluidIds.contains(id);
    }

    /**
     * @param id a material id
     * @return the material's flammability (treatment overrides applied), or empty
     *         when the id is unknown
     */
    public OptionalInt flammability(String id) {
        Integer f = flammabilityByMaterial.get(id);
        return f == null ? OptionalInt.empty() : OptionalInt.of(f);
    }

    @Override
    public String toString() {
        return "RawsIndex[" + flammabilityByMaterial.size() + " materials, " + fluidIds.size() + " fluids]";
    }
}
