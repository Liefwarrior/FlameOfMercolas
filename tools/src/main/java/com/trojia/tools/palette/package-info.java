/**
 * Palette generation (ARCHITECTURE.md section 3, tools; M1 "genPalette"): turns the
 * raws directory into the shared imageless Tiled tileset {@code materials.tsx} that
 * map authors paint with and the importer resolves against.
 *
 * <p>The compatibility target is the hand-authored
 * {@code content/maps/src/materials.tsx}: every tile the generator emits carries the
 * same custom-property contract (string {@code material} + {@code form}, or string
 * {@code fluid} + int {@code depth}) and the same {@code material/FORM} /
 * {@code fluid/depthN} class labels, so {@link com.trojia.tools.tmx.TsxReader} and the
 * importer consume generated and hand-authored palettes identically.</p>
 *
 * <p><strong>Determinism contract (binding):</strong> output is a pure function of the
 * raws directory content — materials sorted by id, forms in {@link
 * com.trojia.tools.palette.PaletteForm} declaration order, fluids sorted by id with
 * depth variants in fixed descending order; tile ids are assigned {@code 0..n-1} over
 * that canonical sequence. Regenerating from unchanged raws is byte-identical,
 * including tile ids.</p>
 */
package com.trojia.tools.palette;
