package com.trojia.tools.palette;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;

/**
 * Immutable snapshot of everything the palette needs from one raws directory:
 * materials (base + treatment-minted) and fluids, each <strong>sorted ascending by
 * id</strong> ({@link String#compareTo}) — the canonical palette order.
 *
 * @param materials materials sorted by id; immutable, ids unique
 * @param fluids    fluids sorted by id; immutable, ids unique
 */
record PaletteRaws(List<PaletteMaterial> materials, List<PaletteFluid> fluids) {

    /**
     * Sorts both lists by id defensively (canonical form regardless of load order).
     *
     * @throws NullPointerException        if a list or element is {@code null}
     * @throws PaletteGenerationException  if two materials or two fluids share an id
     */
    PaletteRaws {
        Objects.requireNonNull(materials, "materials");
        Objects.requireNonNull(fluids, "fluids");
        List<PaletteMaterial> ms = new ArrayList<>(materials);
        ms.sort(Comparator.comparing(PaletteMaterial::id));
        List<PaletteFluid> fs = new ArrayList<>(fluids);
        fs.sort(Comparator.comparing(PaletteFluid::id));
        Set<String> seen = new HashSet<>();
        for (PaletteMaterial m : ms) {
            if (!seen.add(m.id())) {
                throw new PaletteGenerationException("duplicate material id \"" + m.id() + "\"");
            }
        }
        seen.clear();
        for (PaletteFluid f : fs) {
            if (!seen.add(f.id())) {
                throw new PaletteGenerationException("duplicate fluid id \"" + f.id() + "\"");
            }
        }
        materials = List.copyOf(ms);
        fluids = List.copyOf(fs);
    }
}
