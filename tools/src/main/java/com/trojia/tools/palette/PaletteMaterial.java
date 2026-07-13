package com.trojia.tools.palette;

import java.util.List;
import java.util.Objects;

/**
 * Loader-internal projection of one material raw (base or treatment-minted): exactly
 * the fields palette generation consumes. Everything else in the raw (thermal
 * constants, features, provenance, ...) is deliberately not read here.
 *
 * @param id    material id, e.g. {@code granite} or the minted
 *              {@code trudgeon_wood@getilia_soak}; never {@code null} or blank
 * @param phase physical phase, never {@code null}
 * @param tags  tag list in raw order (treatment {@code addTags} appended,
 *              first-occurrence deduped); immutable, never {@code null}
 */
record PaletteMaterial(String id, PalettePhase phase, List<String> tags) {

    /**
     * @throws NullPointerException     if any component or tag is {@code null}
     * @throws IllegalArgumentException if {@code id} is blank
     */
    PaletteMaterial {
        Objects.requireNonNull(id, "id");
        Objects.requireNonNull(phase, "phase");
        if (id.isBlank()) {
            throw new IllegalArgumentException("material id must not be blank");
        }
        tags = List.copyOf(tags);
    }

    /**
     * @param tag tag to test, never {@code null}
     * @return {@code true} iff the material carries {@code tag}
     */
    boolean hasTag(String tag) {
        return tags.contains(Objects.requireNonNull(tag, "tag"));
    }
}
