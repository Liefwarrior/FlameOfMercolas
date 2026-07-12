/**
 * Immutable object model and StAX readers for Tiled map ({@code .tmx}) and external
 * tileset ({@code .tsx}) documents. JDK-only ({@code javax.xml.stream}); no third-party
 * XML dependency.
 *
 * <h2>Scope (v0)</h2>
 * <ul>
 *   <li>Tile layer data must be {@code encoding="csv"}, uncompressed. Any other encoding
 *       or a compression attribute is a hard {@link com.trojia.tools.tmx.TmxParseException}.</li>
 *   <li>The three Tiled gid flip bits (horizontal {@code 0x80000000}, vertical
 *       {@code 0x40000000}, diagonal {@code 0x20000000}) are masked off wherever a gid
 *       appears (tile layers and tile objects) and reported through
 *       {@link com.trojia.tools.tmx.TmxWarningListener}; tile flips are not supported.</li>
 *   <li>Tilesets referenced from a map must be external ({@code source="....tsx"});
 *       embedded tilesets are rejected.</li>
 *   <li>Infinite (chunked) maps are rejected.</li>
 * </ul>
 *
 * <h2>Determinism contract</h2>
 * Parsing is a single forward pass over the document. Every collection in the model
 * preserves <em>document order</em>: map tilesets, layers (top-level and inside each
 * {@link com.trojia.tools.tmx.TmxLayerGroup}), objects within an object layer,
 * properties within a property block, and tiles within a tileset. Warnings are emitted
 * in document order. Two byte-identical inputs always produce equal models and the
 * identical warning sequence; downstream importer output can therefore be
 * byte-deterministic (ARCHITECTURE.md section 9).
 *
 * <h2>Security</h2>
 * Readers disable DTD processing and external entity resolution.
 */
package com.trojia.tools.tmx;
