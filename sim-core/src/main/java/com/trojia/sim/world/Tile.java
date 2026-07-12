package com.trojia.sim.world;

/**
 * Read-only view of one tile's lane values. Implemented by the flyweight
 * {@link TileCursor}; never materialized per tile. All getters return the
 * committed state of the current tick's write stream (reads-see-writes within
 * a tick; cross-tick visibility is governed by the tick pipeline).
 */
public interface Tile {

    /** The packed position this view currently points at. */
    int packedPos();

    /** MATERIAL lane: the material id (unsigned short range). */
    short materialId();

    /** FORM lane, decoded. */
    TileForm form();

    /** FLAGS lane byte; test with {@link FlagBits} masks. */
    int flags();

    /** TEMPERATURE lane: unsigned 16-bit deci-Kelvin (0..65535). */
    int temperatureDeciK();

    /** FLUID lane: raw bit-packed fluid state (depth 0–2, fluidId 3–5, SETTLED 6). */
    int fluidBits();

    /** LIGHT lane: raw packed light, sky(5b)+block(5b). */
    int lightBits();

    /** OPACITY extension lane: 0..31 opacity owned by the light system. */
    int opacity();
}
