package com.trojia.sim.world;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/**
 * Flyweight cursor contract: reads-see-writes within a tick, chunk-crossing
 * repositioning, legal reads of VOID/frozen chunks, and the debug tick-stamp
 * staleness assertion.
 */
final class TileCursorTest {

    private static final int POS = WorldFixture.interiorPos();

    @Test
    void moveToChainsAndReportsThePosition() {
        WorldFixture fixture = WorldFixture.minimal();
        TileCursor cursor = fixture.world.cursor();
        assertSame(cursor, cursor.moveTo(POS));
        assertEquals(POS, cursor.packedPos());
    }

    @Test
    void readsSeeWritesWithinTheSameTick() {
        WorldFixture fixture = WorldFixture.minimal();
        TileCursor cursor = fixture.world.cursor().moveTo(POS);
        assertEquals(0, cursor.materialId());
        fixture.writer.setMaterialAndForm(POS, (short) 11, TileForm.RAMP);
        fixture.writer.setTemperatureDeciK(POS, 3652);
        assertEquals(11, cursor.materialId());
        assertEquals(TileForm.RAMP, cursor.form());
        assertEquals(3652, cursor.temperatureDeciK());
    }

    @Test
    void crossingChunksRefetchesTheBackingArrays() {
        // 4×3×3 world: two interior chunks, (1,1,1) and (2,1,1).
        WorldFixture fixture = WorldFixture.of(new WorldConfig(4, 3, 3));
        int left = PackedPos.pack(40, 40, 10);
        int right = PackedPos.pack(70, 40, 10);
        fixture.writer.setMaterial(left, (short) 1);
        fixture.writer.setMaterial(right, (short) 2);
        TileCursor cursor = fixture.world.cursor();
        assertEquals(1, cursor.moveTo(left).materialId());
        assertEquals(2, cursor.moveTo(right).materialId());
        assertEquals(1, cursor.moveTo(left).materialId());
    }

    @Test
    void voidBorderAndFrozenChunksStayReadable() {
        WorldFixture fixture = WorldFixture.minimal();
        TileCursor cursor = fixture.world.cursor();
        assertEquals(TileForm.VOID, cursor.moveTo(WorldFixture.borderPos()).form());
        fixture.writer.setMaterial(POS, (short) 6);
        fixture.lifecycle.freeze(fixture.interiorChunk());
        assertEquals(6, cursor.moveTo(POS).materialId());
    }

    @Test
    void staleCursorReadAssertsOnANewTick() {
        assumeTrue(TileCursor.class.desiredAssertionStatus(),
                "debug tick-stamp is assert-based; run with -ea");
        WorldFixture fixture = WorldFixture.minimal();
        fixture.world.beginTick(1);
        TileCursor cursor = fixture.world.cursor().moveTo(POS);
        assertEquals(0, cursor.materialId());
        fixture.world.commitTick(1);
        // Between-ticks reads of the same tick's position are legal (observer window).
        assertEquals(0, cursor.materialId());
        fixture.world.beginTick(2);
        assertThrows(AssertionError.class, cursor::materialId);
        // Repositioning refreshes the stamp.
        assertEquals(0, cursor.moveTo(POS).materialId());
    }

    @Test
    void unpositionedCursorAssertsOnRead() {
        assumeTrue(TileCursor.class.desiredAssertionStatus(),
                "positioning guard is assert-based; run with -ea");
        WorldFixture fixture = WorldFixture.minimal();
        TileCursor cursor = fixture.world.cursor();
        assertThrows(AssertionError.class, cursor::packedPos);
        assertThrows(AssertionError.class, cursor::form);
    }
}
