package com.trojia.tools.palette;

import java.nio.file.Path;
import java.util.List;

/**
 * Generates the shared material palette tileset ({@code materials.tsx}) from a raws
 * directory (ARCHITECTURE.md section 3, tools; M1 acceptance "genPalette").
 *
 * <p><strong>Determinism contract (binding):</strong> for identical raws directory
 * content, {@link #generate(Path)} returns a byte-identical document (fixed UTF-8
 * text, {@code \n} line separators, no timestamps or environment-dependent content)
 * and {@link #plan(Path)} returns the identical tile sequence. Tile ids are the
 * {@code 0..n-1} positions of that sequence, so ids are stable across regeneration.
 * Adding a material/fluid to the raws may shift later ids — regenerated palettes are
 * paired with re-imported maps, never spliced under existing {@code .tmx} files.</p>
 */
public interface PaletteGenerator {

    /**
     * Plans the canonical tile sequence for the given raws directory: material tiles
     * first (materials sorted ascending by id; per material, relevant forms in
     * {@link PaletteForm} declaration order), then fluid tiles (fluids sorted
     * ascending by id; per fluid, the fixed depth variants in descending order).
     *
     * @param rawsDir raws root containing {@code materials/} (required) and
     *                optionally {@code treatments/}, {@code fluids/}; never {@code null}
     * @return immutable tile sequence; position = local tile id
     * @throws PaletteGenerationException on missing/malformed raws (see
     *                                    {@link PaletteGenerationException})
     */
    List<PaletteTile> plan(Path rawsDir);

    /**
     * Renders the full TSX document for {@link #plan(Path)}.
     *
     * @param rawsDir raws root, never {@code null}
     * @return complete TSX document text (parseable by
     *         {@code com.trojia.tools.tmx.TsxReader}), ending in a single {@code \n}
     * @throws PaletteGenerationException on missing/malformed raws
     */
    String generate(Path rawsDir);
}
