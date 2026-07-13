package com.trojia.tools.palette;

import java.util.Objects;

/**
 * Loader-internal projection of one fluid raw: palette generation only needs the id
 * (depth variants are a fixed palette convention, see
 * {@link RawsPaletteGenerator#FLUID_DEPTHS}).
 *
 * @param id fluid id, e.g. {@code water}; never {@code null} or blank
 */
record PaletteFluid(String id) {

    /**
     * @throws NullPointerException     if {@code id} is {@code null}
     * @throws IllegalArgumentException if {@code id} is blank
     */
    PaletteFluid {
        Objects.requireNonNull(id, "id");
        if (id.isBlank()) {
            throw new IllegalArgumentException("fluid id must not be blank");
        }
    }
}
