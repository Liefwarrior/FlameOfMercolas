package com.trojia.sim.world;

/**
 * Shared test bed: a world built with recording sinks (independent of the
 * parallel {@code world.change} implementation), plus the handles the world
 * tests keep reaching for. The default config is the minimum 3×3×3-chunk
 * world — a single concrete interior chunk wrapped in the VOID border.
 */
final class WorldFixture {

    final TickableWorld world;
    final ChunkLifecycle lifecycle;
    final ChunkWriter writer;
    final Coords coords;
    final RecordingChangeLogSink logs;
    final RecordingRevisionSink revisions;

    private WorldFixture(TickableWorld world, RecordingChangeLogSink logs,
            RecordingRevisionSink revisions) {
        this.world = world;
        this.lifecycle = (ChunkLifecycle) world;
        this.writer = world.writer();
        this.coords = world.coords();
        this.logs = logs;
        this.revisions = revisions;
    }

    /** A 3×3×3-chunk world whose named lanes have change-log readers. */
    static WorldFixture minimal(String... lanesWithReaders) {
        return of(new WorldConfig(3, 3, 3), lanesWithReaders);
    }

    /** A world of {@code config} whose named lanes have change-log readers. */
    static WorldFixture of(WorldConfig config, String... lanesWithReaders) {
        return build(WorldBuilder.create(config), lanesWithReaders);
    }

    /** Builds {@code builder} (already carrying extension lanes etc.) with recording sinks. */
    static WorldFixture build(WorldBuilder builder, String... lanesWithReaders) {
        RecordingChangeLogSink logs = new RecordingChangeLogSink(lanesWithReaders);
        RecordingRevisionSink revisions = new RecordingRevisionSink();
        TickableWorld world = builder.changeLogSink(logs).revisionSink(revisions).build();
        return new WorldFixture(world, logs, revisions);
    }

    /** The chunk index of the single interior chunk of the minimal 3×3×3 world. */
    int interiorChunk() {
        return coords.chunkIndexOf(1, 1, 1);
    }

    /** A tile inside the interior chunk of the minimal world. */
    static int interiorPos() {
        return PackedPos.pack(33, 34, 9);
    }

    /** A tile on the VOID border (chunk (0,0,0)) of any world. */
    static int borderPos() {
        return PackedPos.pack(1, 2, 3);
    }
}
