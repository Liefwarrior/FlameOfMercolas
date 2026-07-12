package com.trojia.sim.world;

/**
 * Dense-lane storage of one 32×32×8 chunk (ARCHITECTURE.md §8): one primitive
 * array per registered lane, indexed by {@code localIdx = (z<<10)|(y<<5)|x},
 * plus lazily allocated sparse 16-bit overlays. This class is pure storage —
 * every mutation rule (reject codes, change logs, derived flags, revisions)
 * lives in {@link DenseChunkWriter}.
 *
 * <p>VOID border chunks are flyweights: every border chunk shares one
 * immutable set of lane arrays (the writer rejects border writes before ever
 * touching storage) and carries no overlays.
 */
final class Chunk implements ChunkView {

    private static final int OVERLAY_COUNT = OverlayId.values().length;

    private final int chunkIndex;
    /** Per-lane backing arrays, indexed by {@link LaneId#index()}; null where the lane is 1-byte. */
    final short[][] shortLanes;
    /** Per-lane backing arrays, indexed by {@link LaneId#index()}; null where the lane is 2-byte. */
    final byte[][] byteLanes;
    private final boolean ownsOverlays;
    private SparseOverlay[] overlays;

    Chunk(int chunkIndex, short[][] shortLanes, byte[][] byteLanes, boolean ownsOverlays) {
        this.chunkIndex = chunkIndex;
        this.shortLanes = shortLanes;
        this.byteLanes = byteLanes;
        this.ownsOverlays = ownsOverlays;
    }

    @Override
    public int chunkIndex() {
        return chunkIndex;
    }

    @Override
    public short[] shortLane(LaneId lane) {
        short[] backing = shortLanes[lane.index()];
        if (backing == null) {
            throw new IllegalArgumentException(lane.name() + " is not a 2-byte lane");
        }
        return backing;
    }

    @Override
    public byte[] byteLane(LaneId lane) {
        byte[] backing = byteLanes[lane.index()];
        if (backing == null) {
            throw new IllegalArgumentException(lane.name() + " is not a 1-byte lane");
        }
        return backing;
    }

    @Override
    public OverlayView overlay(OverlayId overlay) {
        SparseOverlay stored = overlays == null ? null : overlays[overlay.ordinal()];
        return stored == null ? EmptyOverlay.INSTANCE : stored;
    }

    /** Writer-only: the mutable overlay, allocating storage on first use. */
    SparseOverlay overlayForWrite(OverlayId overlay) {
        if (overlays == null) {
            if (!ownsOverlays) {
                throw new IllegalStateException(
                        "VOID border chunk " + chunkIndex + " cannot carry overlays");
            }
            overlays = new SparseOverlay[OVERLAY_COUNT];
        }
        SparseOverlay stored = overlays[overlay.ordinal()];
        if (stored == null) {
            stored = new SparseOverlay();
            overlays[overlay.ordinal()] = stored;
        }
        return stored;
    }

    /** Writer-only: the mutable overlay if any cell storage exists, else null (no allocation). */
    SparseOverlay overlayOrNull(OverlayId overlay) {
        return overlays == null ? null : overlays[overlay.ordinal()];
    }
}
