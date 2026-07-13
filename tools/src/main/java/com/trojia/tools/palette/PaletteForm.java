package com.trojia.tools.palette;

/**
 * Tile form vocabulary of the palette, mirroring the world's {@code TileForm} as
 * fixed by the tileset property contract in {@code content/maps/README.md}:
 * {@code WALL | FLOOR | OPEN | RAMP | STAIR_UP | STAIR_DOWN}.
 *
 * <p><strong>Determinism contract:</strong> declaration order here <em>is</em> the
 * canonical per-material tile order in generated palettes ("forms in enum order").
 * Never reorder or rename constants — that would silently renumber every generated
 * tile id. {@code OPEN} exists only for vocabulary completeness; per the collapse
 * rule (ARCHITECTURE.md section 1.1 #17) OPEN is authored by leaving a cell empty,
 * so no palette tile ever carries it.</p>
 *
 * <p>This is a tools-local mirror: the engine-side enum lives in
 * {@code com.trojia.sim.world} once the M1 world API lands; the string names in the
 * {@code form} custom property are the shared contract between the two.</p>
 */
public enum PaletteForm {

    /** Cell fully solid (walls and solid furniture alike in v0). */
    WALL,
    /** Walkable surface at the bottom of the cell. */
    FLOOR,
    /** Empty cell. Never emitted as a palette tile (collapse rule). */
    OPEN,
    /** Walkable slope connecting to the z-level above. */
    RAMP,
    /** Lower half of a vertical passage (pairs with {@link #STAIR_DOWN} above). */
    STAIR_UP,
    /** Upper half of a vertical passage (pairs with {@link #STAIR_UP} below). */
    STAIR_DOWN
}
