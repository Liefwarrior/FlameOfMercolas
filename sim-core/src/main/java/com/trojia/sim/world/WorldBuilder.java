package com.trojia.sim.world;

import com.trojia.sim.world.change.ChangeLogs;
import com.trojia.sim.world.change.ChunkRevisions;

/**
 * Composition root for one world: owns the {@link LaneRegistry} instance (no
 * statics — two engines in one JVM never share lane tables), registers the
 * seven core lanes in {@link Lanes} order, lets extension owners register
 * their lanes, then builds the world and seals the registry.
 *
 * <p>The built {@link TickableWorld} also implements {@link ChunkLifecycle}
 * (the bubble module's freeze/thaw seam). F1 allocates every interior chunk
 * concrete — whole-map-active until the ticketed bubble manager lands.
 */
public final class WorldBuilder {

    private final WorldConfig config;
    private final LaneRegistry lanes;
    private TileClassifier classifier = TileClassifier.formOnly();
    private ChangeLogSink changeLogSink;
    private RevisionSink revisionSink;
    private boolean built;

    private WorldBuilder(WorldConfig config) {
        this.config = config;
        this.lanes = new LaneRegistry();
        lanes.register(Lanes.MATERIAL, 2);
        lanes.register(Lanes.FORM, 1);
        lanes.register(Lanes.FLAGS, 1);
        lanes.register(Lanes.TEMPERATURE, 2);
        lanes.register(Lanes.FLUID, 2);
        lanes.register(Lanes.LIGHT, 2);
        lanes.register(Lanes.OPACITY, 1);
    }

    /**
     * Starts a builder for {@code config}. The core lanes (MATERIAL..OPACITY)
     * are registered immediately with stable indices 0..6.
     */
    public static WorldBuilder create(WorldConfig config) {
        if (config == null) {
            throw new IllegalArgumentException("config must be non-null");
        }
        return new WorldBuilder(config);
    }

    /** The dimensions this builder was created with. */
    public WorldConfig config() {
        return config;
    }

    /**
     * The world's lane registry, open for extension-lane registration until
     * {@link #build()} seals it.
     */
    public LaneRegistry lanes() {
        return lanes;
    }

    /**
     * Replaces the derived-flag policy (default: {@link TileClassifier#formOnly()}).
     * The material registry supplies an opacity-aware policy at load; the
     * policy is part of the determinism contract and must not change after
     * build. Returns {@code this} for chaining.
     */
    public WorldBuilder classifier(TileClassifier classifier) {
        if (classifier == null) {
            throw new IllegalArgumentException("classifier must be non-null");
        }
        requireNotBuilt();
        this.classifier = classifier;
        return this;
    }

    /** Test hook: replaces the writer's change-log seam (default: the world's real {@link ChangeLogs}). */
    WorldBuilder changeLogSink(ChangeLogSink sink) {
        requireNotBuilt();
        this.changeLogSink = sink;
        return this;
    }

    /** Test hook: replaces the writer's revision seam (default: the world's real {@link ChunkRevisions}). */
    WorldBuilder revisionSink(RevisionSink sink) {
        requireNotBuilt();
        this.revisionSink = sink;
        return this;
    }

    /**
     * Allocates chunk storage (all chunks VOID/empty at first), seals the lane
     * registry, and returns the world. Call at most once.
     */
    public TickableWorld build() {
        requireNotBuilt();
        built = true;
        lanes.seal();
        Coords coords = Coords.of(config);
        ChangeLogs changeLogs = new ChangeLogs();
        ChunkRevisions revisions = new ChunkRevisions(coords.chunkCount());
        ChangeLogSink logSeam =
                changeLogSink != null ? changeLogSink : new ChangeLogsAdapter(changeLogs);
        RevisionSink revisionSeam =
                revisionSink != null ? revisionSink : new RevisionsAdapter(revisions);
        return new DenseWorld(config, coords, lanes, changeLogs, revisions, logSeam,
                revisionSeam, classifier);
    }

    private void requireNotBuilt() {
        if (built) {
            throw new IllegalStateException("build() was already called on this builder");
        }
    }
}
