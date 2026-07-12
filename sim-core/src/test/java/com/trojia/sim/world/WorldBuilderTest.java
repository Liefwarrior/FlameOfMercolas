package com.trojia.sim.world;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Composition-root contract: core lanes at stable indices, extension
 * registration until build, sealing, and the freshly built world's initial
 * state (interior OPEN + concrete, border VOID + immutable flags).
 */
final class WorldBuilderTest {

    @Test
    void coreLanesAreRegisteredAtStableIndices() {
        LaneRegistry lanes = WorldBuilder.create(new WorldConfig(3, 3, 3)).lanes();
        assertEquals(Lanes.CORE_LANE_COUNT, lanes.count());
        assertLane(lanes, Lanes.MATERIAL_INDEX, Lanes.MATERIAL, 2);
        assertLane(lanes, Lanes.FORM_INDEX, Lanes.FORM, 1);
        assertLane(lanes, Lanes.FLAGS_INDEX, Lanes.FLAGS, 1);
        assertLane(lanes, Lanes.TEMPERATURE_INDEX, Lanes.TEMPERATURE, 2);
        assertLane(lanes, Lanes.FLUID_INDEX, Lanes.FLUID, 2);
        assertLane(lanes, Lanes.LIGHT_INDEX, Lanes.LIGHT, 2);
        assertLane(lanes, Lanes.OPACITY_INDEX, Lanes.OPACITY, 1);
    }

    private static void assertLane(LaneRegistry lanes, int index, String name, int width) {
        LaneId lane = lanes.byIndex(index);
        assertEquals(name, lane.name());
        assertEquals(width, lane.bytesPerTile());
        assertSame(lane, lanes.byName(name));
    }

    @Test
    void extensionLanesRegisterAfterTheCoresAndBuildSeals() {
        WorldBuilder builder = WorldBuilder.create(new WorldConfig(3, 3, 3));
        LaneId aether = builder.lanes().register("aether", 1);
        assertEquals(Lanes.CORE_LANE_COUNT, aether.index());
        World world = builder.build();
        assertThrows(IllegalStateException.class, () -> world.lanes().register("late", 1));
    }

    @Test
    void buildTwiceThrows() {
        WorldBuilder builder = WorldBuilder.create(new WorldConfig(3, 3, 3));
        builder.build();
        assertThrows(IllegalStateException.class, builder::build);
    }

    @Test
    void nullArgumentsAreRejected() {
        assertThrows(IllegalArgumentException.class, () -> WorldBuilder.create(null));
        WorldBuilder builder = WorldBuilder.create(new WorldConfig(3, 3, 3));
        assertThrows(IllegalArgumentException.class, () -> builder.classifier(null));
    }

    @Test
    void builtWorldExposesItsCollaborators() {
        WorldConfig config = new WorldConfig(3, 3, 3);
        WorldBuilder builder = WorldBuilder.create(config);
        TickableWorld world = builder.build();
        assertSame(config, world.config());
        assertSame(builder.lanes(), world.lanes());
        assertNotNull(world.coords());
        assertNotNull(world.writer());
        assertNotNull(world.changeLogs());
        assertNotNull(world.revisions());
        assertNotNull(world.cursor());
        assertInstanceOf(ChunkLifecycle.class, world);
    }

    @Test
    void everyChunkHasResidentStorageWithCorrectIndex() {
        World world = WorldBuilder.create(new WorldConfig(4, 3, 3)).build();
        for (int chunkIndex = 0; chunkIndex < world.coords().chunkCount(); chunkIndex++) {
            assertEquals(chunkIndex, world.chunk(chunkIndex).chunkIndex());
        }
    }

    @Test
    void interiorStartsOpenEmptyAndConcrete() {
        TickableWorld world = WorldBuilder.create(new WorldConfig(3, 3, 3)).build();
        ChunkLifecycle lifecycle = (ChunkLifecycle) world;
        int interior = world.coords().chunkIndexOf(1, 1, 1);
        assertTrue(lifecycle.isConcrete(interior));
        Tile tile = world.cursor().moveTo(WorldFixture.interiorPos());
        assertEquals(TileForm.OPEN, tile.form());
        assertEquals(0, tile.materialId());
        assertEquals(0, tile.flags());
        assertEquals(0, tile.temperatureDeciK());
        assertEquals(0, tile.fluidBits());
        assertEquals(0, tile.lightBits());
        assertEquals(0, tile.opacity());
    }

    @Test
    void borderStartsVoidBlockingAndImmutable() {
        TickableWorld world = WorldBuilder.create(new WorldConfig(3, 3, 3)).build();
        ChunkLifecycle lifecycle = (ChunkLifecycle) world;
        int border = world.coords().chunkIndexOf(0, 0, 0);
        assertTrue(lifecycle.isVoid(border));
        assertThrows(IllegalStateException.class, () -> lifecycle.freeze(border));
        assertThrows(IllegalStateException.class, () -> lifecycle.thaw(border));
        Tile tile = world.cursor().moveTo(WorldFixture.borderPos());
        assertEquals(TileForm.VOID, tile.form());
        assertEquals(FlagBits.BLOCKS_MOVE | FlagBits.BLOCKS_LIGHT, tile.flags());
    }

    @Test
    void untouchedChunkHasEmptyOverlays() {
        World world = WorldBuilder.create(new WorldConfig(3, 3, 3)).build();
        OverlayView overlay =
                world.chunk(world.coords().chunkIndexOf(1, 1, 1)).overlay(OverlayId.CHARGE);
        assertEquals(0, overlay.size());
        assertEquals(-1, overlay.get(0, -1));
        assertThrows(IndexOutOfBoundsException.class, () -> overlay.localIdxAt(0));
    }
}
