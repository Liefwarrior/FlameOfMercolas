package com.trojia.sim.world;

/**
 * Pure walkability CHECK over the existing FORM/FLUID lanes (a read only —
 * actors never write lanes, ACTORS-SPEC.md §2.3). Deliberately separate from
 * {@link FlagBits#BLOCKS_MOVE} / {@link TileClassifier#formOnly()}: that
 * derived bit treats {@link TileForm#OPEN} as non-blocking (it also feeds
 * {@code BLOCKS_LIGHT}/opacity elsewhere, so its blast radius is unknown and
 * it is left untouched), whereas a genuine actor walkability rule must NOT —
 * {@code OPEN} means "nothing authored here at all" (no floor, no wall), not
 * "walkable ground" (content/maps/README.md; confirmed by
 * {@code TiledWorldImporter.bakeGroup} and the docks fixture's harbor-water /
 * K13 Drowned Hold authoring).
 *
 * <p>Rule: {@link TileForm#FLOOR}, {@link TileForm#RAMP}, {@link TileForm#STAIR}
 * are walkable forms; {@link TileForm#WALL}, {@link TileForm#VOID}, and
 * {@link TileForm#OPEN} block. On top of that, a walkable-form tile is only
 * truly walkable if its FLUID-lane depth is below {@link #BLOCKING_FLUID_DEPTH}
 * — a shallow puddle/pool (depth 2) sits on real floor and is walkable, but a
 * flooded conduit or deep water (depth 4/7) blocks regardless of form.
 */
public final class Walkability {

    /** FLUID-lane depth (bits 0-2 of {@link Tile#fluidBits()}) at/above which a tile blocks. */
    public static final int BLOCKING_FLUID_DEPTH = 4;

    private static final int FLUID_DEPTH_MASK = 0x7;

    /** Whether {@code tile} is safe for an actor to step onto right now. */
    public static boolean isWalkable(Tile tile) {
        TileForm form = tile.form();
        if (form != TileForm.FLOOR && form != TileForm.RAMP && form != TileForm.STAIR) {
            return false; // WALL/VOID/OPEN — OPEN means no floor authored here at all
        }
        return (tile.fluidBits() & FLUID_DEPTH_MASK) < BLOCKING_FLUID_DEPTH;
    }

    private Walkability() {
    }
}
