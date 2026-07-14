package com.trojia.sim.world;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link Walkability}'s FORM + FLUID-depth rule (§2.5's collision addendum):
 * WALL/VOID/OPEN block; FLOOR/RAMP/STAIR are walkable unless a FLUID-lane
 * depth of {@link Walkability#BLOCKING_FLUID_DEPTH} or more sits on the tile
 * (a flooded conduit or open deep water), in which case they block too.
 */
final class WalkabilityTest {

    private static final int POS = WorldFixture.interiorPos();

    @Test
    void wallBlocks() {
        WorldFixture fixture = WorldFixture.minimal();
        fixture.writer.setForm(POS, TileForm.WALL);
        assertFalse(Walkability.isWalkable(fixture.world.cursor().moveTo(POS)),
                "a WALL tile must block movement (Eli's directive)");
    }

    @Test
    void voidBlocks() {
        // The permanent border ring bakes as VOID and is never paintable.
        WorldFixture fixture = WorldFixture.minimal();
        assertFalse(Walkability.isWalkable(fixture.world.cursor().moveTo(WorldFixture.borderPos())),
                "VOID (world border / unauthored air) must block movement");
    }

    @Test
    void openBlocksEvenThoughItsDerivedFlagBitDoesNot() {
        // Confirms the documented distinction from FlagBits.BLOCKS_MOVE / FormOnlyClassifier
        // (which treat OPEN as non-blocking) — a genuine actor walkability rule must not
        // reuse that bit as-is: OPEN means "nothing authored here", not "walkable ground"
        // (open harbor water, K13's "truly OPEN: drops to the undercellar" fall hazard).
        WorldFixture fixture = WorldFixture.minimal();
        fixture.writer.setForm(POS, TileForm.OPEN);
        assertFalse(Walkability.isWalkable(fixture.world.cursor().moveTo(POS)),
                "OPEN must block actor movement even though it doesn't block the derived flag bit");
    }

    @Test
    void floorIsWalkable() {
        WorldFixture fixture = WorldFixture.minimal();
        fixture.writer.setForm(POS, TileForm.FLOOR);
        assertTrue(Walkability.isWalkable(fixture.world.cursor().moveTo(POS)));
    }

    @Test
    void doorGapIsJustFloorAndIsWalkable() {
        // The authoring convention (gen_docks_surface.py's shell(doors=...)): a door is simply
        // a wall-run gap left as FLOOR, no distinct tile/marker — "doors work" falls out for
        // free from the FLOOR rule with zero special-case logic.
        WorldFixture fixture = WorldFixture.minimal();
        fixture.writer.setForm(POS, TileForm.FLOOR);
        assertTrue(Walkability.isWalkable(fixture.world.cursor().moveTo(POS)),
                "a door gap bakes to plain FLOOR and must be walkable");
    }

    @Test
    void rampIsWalkable() {
        WorldFixture fixture = WorldFixture.minimal();
        fixture.writer.setForm(POS, TileForm.RAMP);
        assertTrue(Walkability.isWalkable(fixture.world.cursor().moveTo(POS)));
    }

    @Test
    void stairIsWalkable() {
        WorldFixture fixture = WorldFixture.minimal();
        fixture.writer.setForm(POS, TileForm.STAIR);
        assertTrue(Walkability.isWalkable(fixture.world.cursor().moveTo(POS)));
    }

    @Test
    void deckTileOverWaterWithNoFluidHereIsWalkable() {
        // A bridge/deck plank: FLOOR form, no fluid authored on THIS tile (the water is on a
        // different z-level/tile, as the docks harbor is authored two z below the street).
        WorldFixture fixture = WorldFixture.minimal();
        fixture.writer.setForm(POS, TileForm.FLOOR);
        assertTrue(Walkability.isWalkable(fixture.world.cursor().moveTo(POS)));
    }

    @Test
    void shallowPoolOnRealFloorIsWalkable() {
        // Squall's Bathhouse pools: depth-2 WATER over GRANITE_FLOOR — meant to be walkable.
        WorldFixture fixture = WorldFixture.minimal();
        fixture.writer.setForm(POS, TileForm.FLOOR);
        fixture.writer.setFluidBits(POS, 2);
        assertTrue(Walkability.isWalkable(fixture.world.cursor().moveTo(POS)));
    }

    @Test
    void floodedConduitAtDepthFourBlocksDespiteFloorForm() {
        // The outfall storm-drain stub: depth-4 WATER over GRANITE_FLOOR — a hazard.
        WorldFixture fixture = WorldFixture.minimal();
        fixture.writer.setForm(POS, TileForm.FLOOR);
        fixture.writer.setFluidBits(POS, 4);
        assertFalse(Walkability.isWalkable(fixture.world.cursor().moveTo(POS)));
    }

    @Test
    void deepWaterAtDepthSevenBlocksEvenOnFloorForm() {
        WorldFixture fixture = WorldFixture.minimal();
        fixture.writer.setForm(POS, TileForm.FLOOR);
        fixture.writer.setFluidBits(POS, 7);
        assertFalse(Walkability.isWalkable(fixture.world.cursor().moveTo(POS)));
    }
}
