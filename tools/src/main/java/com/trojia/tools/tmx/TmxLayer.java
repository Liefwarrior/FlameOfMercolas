package com.trojia.tools.tmx;

/**
 * A node in a map's layer tree: a tile layer, an object layer, or a group of layers.
 *
 * <p>Sealed so importer passes can switch exhaustively over the three v0 layer kinds
 * (image layers are skipped at parse time with a warning).</p>
 */
public sealed interface TmxLayer permits TmxTileLayer, TmxObjectLayer, TmxLayerGroup {

    /** @return Tiled's unique layer id ({@code 0} if the document omitted it) */
    int id();

    /** @return layer name, never {@code null} (may be empty) */
    String name();

    /** @return custom properties of this layer, never {@code null} */
    TmxProperties properties();
}
