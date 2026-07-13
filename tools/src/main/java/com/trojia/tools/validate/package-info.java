/**
 * Map and raws validation for the Tiled content pipeline (ARCHITECTURE.md section 3,
 * tools entry; error-UX requirement of section 12, M1).
 *
 * <p>{@link com.trojia.tools.validate.TiledValidator} runs an <em>ordered</em> list of
 * {@link com.trojia.tools.validate.ValidationPass} instances over the immutable
 * {@link com.trojia.tools.tmx.TmxMap} / {@link com.trojia.tools.tmx.TmxTileset} model
 * produced by the {@code com.trojia.tools.tmx} readers. The standard order is:</p>
 *
 * <ol>
 *   <li>{@link com.trojia.tools.validate.ZGroupContiguityPass} — z-group naming,
 *       duplicates, contiguity, sane zOrigin;</li>
 *   <li>{@link com.trojia.tools.validate.SublayerContractPass} — required
 *       {@code terrain}+{@code floor} sublayers, only known sublayers, layer kinds and
 *       dimensions;</li>
 *   <li>{@link com.trojia.tools.validate.MaterialResolutionPass} — every
 *       {@code material=} tile property resolves against the raws id set (including
 *       treatment-minted derived ids), every {@code fluid=} against the fluid raws;</li>
 *   <li>{@link com.trojia.tools.validate.GidBoundsPass} — gid bounds versus tileset
 *       tilecount, flip-bit rejection;</li>
 *   <li>{@link com.trojia.tools.validate.MarkerContractPass} — marker/object contract
 *       (named objects, known marker classes, luminance range, anchor uniqueness,
 *       ignition anchors on flammable material);</li>
 *   <li>{@link com.trojia.tools.validate.ChunkAlignmentPass} — 32x32 chunk alignment
 *       (warning, never an error).</li>
 * </ol>
 *
 * <p><strong>Error UX contract.</strong> Every {@link
 * com.trojia.tools.validate.ValidationIssue} is human-readable and carries the map
 * name, the offending layer path (e.g. {@code z:+0/terrain}), tile coordinates
 * {@code (x,y)} where applicable, and a concrete fix hint.</p>
 *
 * <p><strong>Determinism contract.</strong> Passes run in list order; each pass walks
 * layers in document order and tile grids row-major, so issue order is a pure function
 * of the input documents. No pass mutates the model.</p>
 */
package com.trojia.tools.validate;
