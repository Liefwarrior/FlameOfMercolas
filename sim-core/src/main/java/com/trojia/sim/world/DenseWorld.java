package com.trojia.sim.world;

import com.trojia.sim.world.change.ChangeLogs;
import com.trojia.sim.world.change.ChunkRevisions;

import java.util.Arrays;

/**
 * The dense-lane world behind {@link WorldBuilder}: a flat {@link Chunk}
 * array in canonical chunkIndex order, a per-chunk lifecycle state byte, the
 * single {@link DenseChunkWriter}, and the tick open/commit bookkeeping of
 * {@link TickableWorld}.
 *
 * <p>F1 scope: every interior chunk is allocated concrete at build
 * (whole-map-active, per milestone M2); the bubble manager later drives
 * {@link ChunkLifecycle#freeze}/{@link ChunkLifecycle#thaw}. VOID border
 * chunks are permanent flyweights sharing one immutable set of lane arrays.
 */
final class DenseWorld implements TickableWorld, ChunkLifecycle {

    /** Lifecycle state: concrete — writes accepted. */
    static final byte STATE_CONCRETE = 0;
    /** Lifecycle state: frozen (resident rind) — reads legal, writes rejected. */
    static final byte STATE_FROZEN = 1;
    /** Lifecycle state: permanent VOID border — reads legal, writes rejected. */
    static final byte STATE_VOID = 2;

    private final WorldConfig config;
    private final Coords coords;
    private final LaneRegistry lanes;
    private final ChangeLogs changeLogs;
    private final ChunkRevisions revisions;
    private final ChangeLogSink changeLogSink;
    private final RevisionSink revisionSink;
    /** Flat chunk storage, canonical ascending-chunkIndex order. Writer-shared. */
    final Chunk[] chunks;
    /** Per-chunk lifecycle state ({@code STATE_*}). Writer-shared. */
    final byte[] states;
    private final DenseChunkWriter writer;
    private long currentTick;
    private boolean tickOpen;

    DenseWorld(WorldConfig config, Coords coords, LaneRegistry lanes, ChangeLogs changeLogs,
            ChunkRevisions revisions, ChangeLogSink changeLogSink, RevisionSink revisionSink,
            TileClassifier classifier) {
        this.config = config;
        this.coords = coords;
        this.lanes = lanes;
        this.changeLogs = changeLogs;
        this.revisions = revisions;
        this.changeLogSink = changeLogSink;
        this.revisionSink = revisionSink;
        int chunkCount = coords.chunkCount();
        this.chunks = new Chunk[chunkCount];
        this.states = new byte[chunkCount];
        allocateChunks(classifier);
        this.writer = new DenseChunkWriter(this, coords, lanes, changeLogSink, revisionSink,
                classifier);
    }

    /** Allocates the flat chunk array: shared-array VOID flyweights on the border, fresh interior. */
    private void allocateChunks(TileClassifier classifier) {
        short[][] voidShorts = newShortLanes();
        byte[][] voidBytes = newByteLanes();
        fillInitialState(voidBytes, TileForm.VOID, classifier);
        for (int chunkIndex = 0; chunkIndex < chunks.length; chunkIndex++) {
            if (coords.isVoidBorder(chunkIndex)) {
                states[chunkIndex] = STATE_VOID;
                chunks[chunkIndex] = new Chunk(chunkIndex, voidShorts, voidBytes, false);
            } else {
                short[][] shorts = newShortLanes();
                byte[][] bytes = newByteLanes();
                fillInitialState(bytes, TileForm.OPEN, classifier);
                chunks[chunkIndex] = new Chunk(chunkIndex, shorts, bytes, true);
            }
        }
    }

    /** One backing array per 2-byte lane, indexed by lane index (null for 1-byte lanes). */
    private short[][] newShortLanes() {
        short[][] arrays = new short[lanes.count()][];
        for (int i = 0; i < arrays.length; i++) {
            if (lanes.byIndex(i).bytesPerTile() == 2) {
                arrays[i] = new short[Coords.TILES_PER_CHUNK];
            }
        }
        return arrays;
    }

    /** One backing array per 1-byte lane, indexed by lane index (null for 2-byte lanes). */
    private byte[][] newByteLanes() {
        byte[][] arrays = new byte[lanes.count()][];
        for (int i = 0; i < arrays.length; i++) {
            if (lanes.byIndex(i).bytesPerTile() == 1) {
                arrays[i] = new byte[Coords.TILES_PER_CHUNK];
            }
        }
        return arrays;
    }

    /** Seeds a fresh chunk's FORM lane with {@code form} and FLAGS with its derived bits. */
    private static void fillInitialState(byte[][] byteLanes, TileForm form,
            TileClassifier classifier) {
        byte formOrdinal = (byte) form.ordinal();
        if (formOrdinal != 0) {
            Arrays.fill(byteLanes[Lanes.FORM_INDEX], formOrdinal);
        }
        int flags = DenseChunkWriter.derivedFlags(classifier, (short) 0, form);
        if (flags != 0) {
            Arrays.fill(byteLanes[Lanes.FLAGS_INDEX], (byte) flags);
        }
    }

    @Override
    public WorldConfig config() {
        return config;
    }

    @Override
    public Coords coords() {
        return coords;
    }

    @Override
    public LaneRegistry lanes() {
        return lanes;
    }

    @Override
    public ChunkView chunk(int chunkIndex) {
        Chunk chunk = chunks[chunkIndex];
        if (chunk == null) {
            throw new IllegalStateException("chunk " + chunkIndex + " has no resident storage");
        }
        return chunk;
    }

    @Override
    public TileCursor cursor() {
        return new TileCursor(this);
    }

    @Override
    public ChunkWriter writer() {
        return writer;
    }

    @Override
    public ChangeLogs changeLogs() {
        return changeLogs;
    }

    @Override
    public ChunkRevisions revisions() {
        return revisions;
    }

    @Override
    public void beginTick(long tick) {
        if (tickOpen) {
            throw new IllegalStateException(
                    "beginTick(" + tick + ") while tick " + currentTick + " is still open");
        }
        if (tick <= currentTick) {
            throw new IllegalStateException(
                    "ticks must advance: beginTick(" + tick + ") after tick " + currentTick);
        }
        currentTick = tick;
        tickOpen = true;
    }

    @Override
    public void commitTick(long tick) {
        if (!tickOpen) {
            throw new IllegalStateException("commitTick(" + tick + ") without an open tick");
        }
        if (tick != currentTick) {
            throw new IllegalStateException(
                    "commitTick(" + tick + ") does not match the open tick " + currentTick);
        }
        revisionSink.commit(tick);
        changeLogSink.compact(tick);
        tickOpen = false;
    }

    @Override
    public boolean isConcrete(int chunkIndex) {
        return states[chunkIndex] == STATE_CONCRETE;
    }

    @Override
    public boolean isFrozen(int chunkIndex) {
        return states[chunkIndex] == STATE_FROZEN;
    }

    @Override
    public boolean isVoid(int chunkIndex) {
        return states[chunkIndex] == STATE_VOID;
    }

    @Override
    public void freeze(int chunkIndex) {
        requireNotVoid(chunkIndex, "freeze");
        states[chunkIndex] = STATE_FROZEN;
    }

    @Override
    public void thaw(int chunkIndex) {
        requireNotVoid(chunkIndex, "thaw");
        states[chunkIndex] = STATE_CONCRETE;
    }

    private void requireNotVoid(int chunkIndex, String operation) {
        if (states[chunkIndex] == STATE_VOID) {
            throw new IllegalStateException(
                    "cannot " + operation + " permanent VOID border chunk " + chunkIndex);
        }
    }

    /** The tick {@link TileCursor} stamps its position with (debug staleness asserts). */
    long tickStamp() {
        return currentTick;
    }
}
