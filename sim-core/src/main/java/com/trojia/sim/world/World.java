package com.trojia.sim.world;

import com.trojia.sim.world.change.ChangeLogs;
import com.trojia.sim.world.change.ChunkRevisions;

/**
 * Read/write access to the tile world's lanes and overlays. Deliberately has
 * NO tick methods — advancing time is the scheduler's job via
 * {@link TickableWorld}, and no system may drive the world's lifecycle.
 *
 * <p>Reading non-concrete chunks (frozen rind, VOID border) is legal; writing
 * anywhere goes through {@link #writer()}, which rejects non-concrete targets
 * with a status code.
 */
public interface World {

    /** The immutable dimensions this world was built with. */
    WorldConfig config();

    /** The world's bound coordinate math (chunkIndex/localIdx/packedPos). */
    Coords coords();

    /** The sealed lane registry: core lanes 0..6 plus any extension lanes. */
    LaneRegistry lanes();

    /**
     * Borrowed lane arrays of a concrete or frozen-resident chunk. Throws
     * {@code IllegalStateException} for chunks with no resident storage.
     */
    ChunkView chunk(int chunkIndex);

    /** A new flyweight read cursor; callers keep and reuse it (no per-read allocation). */
    TileCursor cursor();

    /** The single write path. One writer per world; all setters return a status code. */
    ChunkWriter writer();

    /** Per-lane change logs (registration is sealed at world build). */
    ChangeLogs changeLogs();

    /** Per-chunk revision counters + changedBits, the observer's diff key. */
    ChunkRevisions revisions();
}
