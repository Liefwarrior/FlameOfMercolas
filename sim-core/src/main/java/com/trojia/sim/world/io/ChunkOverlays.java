package com.trojia.sim.world.io;

import com.trojia.sim.world.OverlayId;

/**
 * Transfer seam for one chunk's sparse 16-bit overlays, used by
 * {@link ChunkCodec} and {@link WorldHasher}. The world's {@code ChunkView}
 * exposes only dense lanes, so overlay content crosses the io boundary through
 * this interface: the owning side presents its cells in <b>ascending
 * localIdx</b> order (the canonical overlay order, ARCHITECTURE.md §1.1 #9)
 * and receives decoded cells via {@link #put}.
 *
 * <p>Values are unsigned 16-bit (0 is a valid stored value — an absent cell is
 * expressed by not listing it); {@code localIdx} is the in-chunk tile index
 * {@code (z<<10)|(y<<5)|x}, range [0, 8192).
 */
public interface ChunkOverlays {

    /**
     * The canonical empty instance: no cells for any overlay, and it refuses
     * {@link #put} — decoding a stream that carries overlay cells against
     * {@code EMPTY} is a format error surfaced by the codec.
     */
    ChunkOverlays EMPTY = new ChunkOverlays() {

        @Override
        public int size(OverlayId overlay) {
            return 0;
        }

        @Override
        public int localIdxAt(OverlayId overlay, int i) {
            throw new IndexOutOfBoundsException("EMPTY overlay has no cell " + i);
        }

        @Override
        public int valueAt(OverlayId overlay, int i) {
            throw new IndexOutOfBoundsException("EMPTY overlay has no cell " + i);
        }

        @Override
        public void put(OverlayId overlay, int localIdx, int value) {
            throw new UnsupportedOperationException("EMPTY overlay seam cannot accept cells");
        }
    };

    /** Number of occupied cells of {@code overlay} in this chunk. */
    int size(OverlayId overlay);

    /**
     * The localIdx of the {@code i}-th occupied cell of {@code overlay},
     * {@code 0 <= i < size(overlay)}, in ascending-localIdx order.
     */
    int localIdxAt(OverlayId overlay, int i);

    /**
     * The unsigned 16-bit value of the {@code i}-th occupied cell of
     * {@code overlay}, same ordering as {@link #localIdxAt}.
     */
    int valueAt(OverlayId overlay, int i);

    /**
     * Stores a decoded cell. Loader/thaw side of the seam; implementations
     * accept cells in ascending-localIdx order as the codec emits them.
     *
     * @param localIdx in-chunk tile index, [0, 8192)
     * @param value    unsigned 16-bit overlay value
     */
    void put(OverlayId overlay, int localIdx, int value);
}
