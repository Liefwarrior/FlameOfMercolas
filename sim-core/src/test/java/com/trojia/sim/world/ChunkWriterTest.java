package com.trojia.sim.world;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.assertThrows;

/**
 * The sole-write-path contract: lane mutation, change-log appends gated on
 * readers, changedBits marking, derived BLOCKS_MOVE/BLOCKS_LIGHT maintenance
 * on material/form writes, and reject codes for VOID/FROZEN targets — all
 * verified against recording fakes of the change/revision seams.
 */
final class ChunkWriterTest {

    private static final int POS = WorldFixture.interiorPos();

    private static String mark(WorldFixture fixture, int packedPos) {
        return fixture.coords.chunkIndex(packedPos) + ":" + fixture.coords.localIdx(packedPos);
    }

    @Test
    void setMaterialMutatesLogsAndMarks() {
        WorldFixture fixture = WorldFixture.minimal(Lanes.MATERIAL);
        assertEquals(ChunkWriter.APPLIED, fixture.writer.setMaterial(POS, (short) 7));
        assertEquals(7, fixture.world.cursor().moveTo(POS).materialId());
        assertEquals(List.of(Lanes.MATERIAL + "@" + POS), fixture.logs.appends);
        assertEquals(List.of(mark(fixture, POS)), fixture.revisions.marks);
    }

    @Test
    void wallFormSetsDerivedFlagsAndAppendsFlagsLog() {
        WorldFixture fixture = WorldFixture.minimal(Lanes.FORM, Lanes.FLAGS);
        assertEquals(ChunkWriter.APPLIED, fixture.writer.setForm(POS, TileForm.WALL));
        Tile tile = fixture.world.cursor().moveTo(POS);
        assertEquals(TileForm.WALL, tile.form());
        assertEquals(FlagBits.BLOCKS_MOVE | FlagBits.BLOCKS_LIGHT, tile.flags());
        assertEquals(List.of(Lanes.FORM + "@" + POS, Lanes.FLAGS + "@" + POS),
                fixture.logs.appends);
        // Back to FLOOR: derived bits drop, FLAGS log fires again.
        fixture.writer.setForm(POS, TileForm.FLOOR);
        assertEquals(0, fixture.world.cursor().moveTo(POS).flags());
        assertEquals(4, fixture.logs.appends.size());
    }

    @Test
    void unchangedDerivedFlagsDoNotAppendToTheFlagsLog() {
        WorldFixture fixture = WorldFixture.minimal(Lanes.FORM, Lanes.FLAGS);
        fixture.writer.setForm(POS, TileForm.FLOOR); // OPEN -> FLOOR: derived stays clear
        assertEquals(List.of(Lanes.FORM + "@" + POS), fixture.logs.appends);
    }

    @Test
    void setMaterialAndFormAppendsOneEntryPerLane() {
        WorldFixture fixture =
                WorldFixture.minimal(Lanes.MATERIAL, Lanes.FORM, Lanes.FLAGS);
        assertEquals(ChunkWriter.APPLIED,
                fixture.writer.setMaterialAndForm(POS, (short) 3, TileForm.WALL));
        Tile tile = fixture.world.cursor().moveTo(POS);
        assertEquals(3, tile.materialId());
        assertEquals(TileForm.WALL, tile.form());
        assertEquals(List.of(Lanes.MATERIAL + "@" + POS, Lanes.FORM + "@" + POS,
                Lanes.FLAGS + "@" + POS), fixture.logs.appends);
        assertEquals(List.of(mark(fixture, POS)), fixture.revisions.marks);
    }

    @Test
    void readerLessLanesSkipAppendsEntirely() {
        WorldFixture fixture = WorldFixture.minimal(); // no lane has readers
        fixture.writer.setMaterialAndForm(POS, (short) 9, TileForm.WALL);
        fixture.writer.setTemperatureDeciK(POS, 2931);
        assertTrue(fixture.logs.appends.isEmpty());
        assertEquals(9, fixture.world.cursor().moveTo(POS).materialId());
    }

    @Test
    void systemFlagsSetAndClearWithoutTouchingDerivedBits() {
        WorldFixture fixture = WorldFixture.minimal(Lanes.FLAGS);
        fixture.writer.setForm(POS, TileForm.WALL);
        assertEquals(ChunkWriter.APPLIED, fixture.writer.setFlag(POS, FlagBits.ON_FIRE, true));
        assertEquals(FlagBits.BLOCKS_MOVE | FlagBits.BLOCKS_LIGHT | FlagBits.ON_FIRE,
                fixture.world.cursor().moveTo(POS).flags());
        // Derived recompute on a later material write preserves the system bit.
        fixture.writer.setMaterial(POS, (short) 4);
        assertEquals(FlagBits.BLOCKS_MOVE | FlagBits.BLOCKS_LIGHT | FlagBits.ON_FIRE,
                fixture.world.cursor().moveTo(POS).flags());
        fixture.writer.setFlag(POS, FlagBits.ON_FIRE, false);
        assertEquals(FlagBits.BLOCKS_MOVE | FlagBits.BLOCKS_LIGHT,
                fixture.world.cursor().moveTo(POS).flags());
    }

    @Test
    void derivedBitsAndOversizedMasksAreRejectedFromSetFlag() {
        WorldFixture fixture = WorldFixture.minimal();
        assertThrows(IllegalArgumentException.class,
                () -> fixture.writer.setFlag(POS, FlagBits.BLOCKS_MOVE, true));
        assertThrows(IllegalArgumentException.class,
                () -> fixture.writer.setFlag(POS, FlagBits.BLOCKS_LIGHT, false));
        assertThrows(IllegalArgumentException.class,
                () -> fixture.writer.setFlag(POS, 0x100, true));
    }

    @Test
    void voidFormIsNeverPaintable() {
        WorldFixture fixture = WorldFixture.minimal();
        assertThrows(IllegalArgumentException.class,
                () -> fixture.writer.setForm(POS, TileForm.VOID));
        assertThrows(IllegalArgumentException.class,
                () -> fixture.writer.setMaterialAndForm(POS, (short) 1, TileForm.VOID));
        assertThrows(IllegalArgumentException.class,
                () -> fixture.writer.setForm(POS, null));
    }

    @Test
    void quantityLanesRoundTripUnsigned() {
        WorldFixture fixture =
                WorldFixture.minimal(Lanes.TEMPERATURE, Lanes.FLUID, Lanes.LIGHT);
        fixture.writer.setTemperatureDeciK(POS, 65535);
        fixture.writer.setFluidBits(POS, 0b1000_0000_0100_1011);
        fixture.writer.setLightBits(POS, (31 << 5) | 17);
        Tile tile = fixture.world.cursor().moveTo(POS);
        assertEquals(65535, tile.temperatureDeciK());
        assertEquals(0b1000_0000_0100_1011, tile.fluidBits());
        assertEquals((31 << 5) | 17, tile.lightBits());
        assertEquals(List.of(Lanes.TEMPERATURE + "@" + POS, Lanes.FLUID + "@" + POS,
                Lanes.LIGHT + "@" + POS), fixture.logs.appends);
    }

    @Test
    void genericLaneWriteServesExtensionLanes() {
        WorldBuilder builder = WorldBuilder.create(new WorldConfig(3, 3, 3));
        LaneId aether = builder.lanes().register("aether", 1);
        WorldFixture fixture = WorldFixture.build(builder, "aether");
        assertEquals(ChunkWriter.APPLIED, fixture.writer.setLane(POS, aether, 0x1AB));
        int localIdx = fixture.coords.localIdx(POS);
        int chunkIndex = fixture.coords.chunkIndex(POS);
        // Truncated to the lane's 1-byte width.
        assertEquals((byte) 0xAB, fixture.world.chunk(chunkIndex).byteLane(aether)[localIdx]);
        assertEquals(List.of("aether@" + POS), fixture.logs.appends);
        assertEquals(List.of(mark(fixture, POS)), fixture.revisions.marks);
    }

    @Test
    void genericLaneWriteRefusesTheDerivedFlagTrio() {
        WorldFixture fixture = WorldFixture.minimal();
        LaneRegistry lanes = fixture.world.lanes();
        assertThrows(IllegalArgumentException.class,
                () -> fixture.writer.setLane(POS, lanes.byName(Lanes.MATERIAL), 1));
        assertThrows(IllegalArgumentException.class,
                () -> fixture.writer.setLane(POS, lanes.byName(Lanes.FORM), 1));
        assertThrows(IllegalArgumentException.class,
                () -> fixture.writer.setLane(POS, lanes.byName(Lanes.FLAGS), 1));
        // OPACITY is light-owned but derived-free: the generic path serves it.
        assertEquals(ChunkWriter.APPLIED,
                fixture.writer.setLane(POS, lanes.byName(Lanes.OPACITY), 31));
        assertEquals(31, fixture.world.cursor().moveTo(POS).opacity());
    }

    @Test
    void voidBorderWritesAreRejectedUntouched() {
        WorldFixture fixture = WorldFixture.minimal(Lanes.MATERIAL, Lanes.FORM, Lanes.FLAGS);
        int border = WorldFixture.borderPos();
        assertEquals(ChunkWriter.REJECTED_VOID, fixture.writer.setMaterial(border, (short) 5));
        assertEquals(ChunkWriter.REJECTED_VOID, fixture.writer.setForm(border, TileForm.WALL));
        assertEquals(ChunkWriter.REJECTED_VOID, fixture.writer.setTemperatureDeciK(border, 100));
        assertEquals(ChunkWriter.REJECTED_VOID,
                fixture.writer.setFlag(border, FlagBits.ON_FIRE, true));
        assertEquals(ChunkWriter.REJECTED_VOID,
                fixture.writer.setOverlay(border, OverlayId.CHARGE, 1));
        assertTrue(fixture.logs.appends.isEmpty());
        assertTrue(fixture.revisions.marks.isEmpty());
        Tile tile = fixture.world.cursor().moveTo(border);
        assertEquals(TileForm.VOID, tile.form());
        assertEquals(0, tile.materialId());
    }

    @Test
    void frozenChunkWritesAreRejectedUntilThaw() {
        WorldFixture fixture = WorldFixture.minimal(Lanes.MATERIAL);
        fixture.writer.setMaterial(POS, (short) 2);
        fixture.lifecycle.freeze(fixture.interiorChunk());
        assertEquals(ChunkWriter.REJECTED_FROZEN, fixture.writer.setMaterial(POS, (short) 9));
        assertEquals(ChunkWriter.REJECTED_FROZEN,
                fixture.writer.setOverlay(POS, OverlayId.CHARGE, 50));
        // Frozen rind stays readable, value unchanged.
        assertEquals(2, fixture.world.cursor().moveTo(POS).materialId());
        fixture.lifecycle.thaw(fixture.interiorChunk());
        assertEquals(ChunkWriter.APPLIED, fixture.writer.setMaterial(POS, (short) 9));
        assertEquals(9, fixture.world.cursor().moveTo(POS).materialId());
    }

    @Test
    void overlayWritesMarkButNeverLog() {
        WorldFixture fixture = WorldFixture.minimal(Lanes.MATERIAL, Lanes.FORM, Lanes.FLAGS,
                Lanes.TEMPERATURE, Lanes.FLUID, Lanes.LIGHT, Lanes.OPACITY);
        assertEquals(ChunkWriter.APPLIED, fixture.writer.setOverlay(POS, OverlayId.CHARGE, 600));
        OverlayView overlay =
                fixture.world.chunk(fixture.interiorChunk()).overlay(OverlayId.CHARGE);
        int localIdx = fixture.coords.localIdx(POS);
        assertEquals(600, overlay.get(localIdx, -1));
        assertTrue(fixture.logs.appends.isEmpty());
        assertEquals(List.of(mark(fixture, POS)), fixture.revisions.marks);

        assertEquals(ChunkWriter.APPLIED, fixture.writer.clearOverlay(POS, OverlayId.CHARGE));
        assertEquals(-1, overlay.get(localIdx, -1));
        assertTrue(fixture.logs.appends.isEmpty());
        assertEquals(2, fixture.revisions.marks.size());
    }

    @Test
    void overlayZeroIsAStoredValueDistinctFromAbsent() {
        WorldFixture fixture = WorldFixture.minimal();
        int localIdx = fixture.coords.localIdx(POS);
        fixture.writer.setOverlay(POS, OverlayId.CHARGE, 0);
        OverlayView overlay =
                fixture.world.chunk(fixture.interiorChunk()).overlay(OverlayId.CHARGE);
        assertTrue(overlay.contains(localIdx));
        assertEquals(0, overlay.get(localIdx, -1));
        fixture.writer.clearOverlay(POS, OverlayId.CHARGE);
        assertTrue(!overlay.contains(localIdx));
    }

    @Test
    void materialAwareClassifierDrivesDerivedBits() {
        // Material 5 is opaque in any form; everything else follows the form-only rule.
        TileClassifier opaqueFive = new TileClassifier() {
            @Override
            public boolean blocksMove(short materialId, TileForm form) {
                return TileClassifier.formOnly().blocksMove(materialId, form);
            }

            @Override
            public boolean blocksLight(short materialId, TileForm form) {
                return materialId == 5 || TileClassifier.formOnly().blocksLight(materialId, form);
            }
        };
        WorldFixture fixture = WorldFixture.build(
                WorldBuilder.create(new WorldConfig(3, 3, 3)).classifier(opaqueFive));
        fixture.writer.setMaterial(POS, (short) 5);
        assertEquals(FlagBits.BLOCKS_LIGHT, fixture.world.cursor().moveTo(POS).flags());
        fixture.writer.setMaterial(POS, (short) 1);
        assertEquals(0, fixture.world.cursor().moveTo(POS).flags());
    }
}
