package com.trojia.tools.palette;

import java.util.Objects;

/**
 * One planned palette tile — either a {@code (material, form)} tile or a
 * {@code (fluid, depth)} tile, matching the tileset property contract of
 * {@code content/maps/README.md}.
 *
 * <p>Local tile ids are <em>not</em> part of this model: ids are positional, assigned
 * {@code 0..n-1} over the canonical tile sequence at render time
 * ({@link RawsPaletteGenerator}).</p>
 */
public sealed interface PaletteTile {

    /**
     * @return the Tiled tile class label ({@code material/FORM} or
     *         {@code fluid/depthN}) — an editor affordance; the importer reads only
     *         the custom properties
     */
    String typeName();

    /**
     * A grid-material tile carrying string properties {@code material} and
     * {@code form}.
     *
     * @param materialId raws material id (base or treatment-minted), never
     *                   {@code null} or blank
     * @param form       tile form; never {@code null}, never {@link PaletteForm#OPEN}
     */
    record MaterialTile(String materialId, PaletteForm form) implements PaletteTile {

        /**
         * @throws NullPointerException     if a component is {@code null}
         * @throws IllegalArgumentException if {@code materialId} is blank or
         *                                  {@code form} is {@link PaletteForm#OPEN}
         */
        public MaterialTile {
            Objects.requireNonNull(materialId, "materialId");
            Objects.requireNonNull(form, "form");
            if (materialId.isBlank()) {
                throw new IllegalArgumentException("materialId must not be blank");
            }
            if (form == PaletteForm.OPEN) {
                throw new IllegalArgumentException(
                        "OPEN has no palette tile (authored by leaving the cell empty)");
            }
        }

        @Override
        public String typeName() {
            return materialId + "/" + form.name();
        }
    }

    /**
     * A pooled-fluid tile carrying string property {@code fluid} and int property
     * {@code depth}, legal only on the {@code fluids} sublayer.
     *
     * @param fluidId raws fluid id (v0: {@code water}), never {@code null} or blank
     * @param depth   initial pooled depth, {@code 1..7} (the FLUID lane stores 3
     *                depth bits)
     */
    record FluidTile(String fluidId, int depth) implements PaletteTile {

        /**
         * @throws NullPointerException     if {@code fluidId} is {@code null}
         * @throws IllegalArgumentException if {@code fluidId} is blank or
         *                                  {@code depth} is outside {@code 1..7}
         */
        public FluidTile {
            Objects.requireNonNull(fluidId, "fluidId");
            if (fluidId.isBlank()) {
                throw new IllegalArgumentException("fluidId must not be blank");
            }
            if (depth < 1 || depth > 7) {
                throw new IllegalArgumentException("depth must be 1..7, was " + depth);
            }
        }

        @Override
        public String typeName() {
            return fluidId + "/depth" + depth;
        }
    }
}
