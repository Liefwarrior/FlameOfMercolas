package com.trojia.sim.world.io;

import com.trojia.sim.world.ChunkView;
import com.trojia.sim.world.OverlayId;
import com.trojia.sim.world.OverlayView;

/**
 * Read-side {@link ChunkOverlays} adapter over the world's
 * {@link ChunkView#overlay(OverlayId)} seam: presents a live chunk's sparse
 * overlay cells to the codec and hasher in their canonical ascending-localIdx
 * order (which {@link OverlayView} already guarantees). Rebindable so save and
 * hash loops reuse one instance across all chunks allocation-free (beyond the
 * per-overlay view lookup).
 *
 * <p>Strictly read-only: {@link #put} throws — decoded cells are delivered to
 * the world through {@code ChunkWriter.setOverlay} by the loader, never
 * through this adapter.
 */
final class ViewOverlays implements ChunkOverlays {

    private static final OverlayId[] OVERLAYS = OverlayId.values();

    private final OverlayView[] views = new OverlayView[OVERLAYS.length];

    /** An unbound adapter; call {@link #bind} before use. */
    ViewOverlays() {
    }

    /** Rebinds this adapter to {@code chunk}'s overlays and returns {@code this}. */
    ViewOverlays bind(ChunkView chunk) {
        for (OverlayId overlay : OVERLAYS) {
            views[overlay.ordinal()] = chunk.overlay(overlay);
        }
        return this;
    }

    @Override
    public int size(OverlayId overlay) {
        return views[overlay.ordinal()].size();
    }

    @Override
    public int localIdxAt(OverlayId overlay, int i) {
        return views[overlay.ordinal()].localIdxAt(i);
    }

    @Override
    public int valueAt(OverlayId overlay, int i) {
        return views[overlay.ordinal()].valueAt(i);
    }

    @Override
    public void put(OverlayId overlay, int localIdx, int value) {
        throw new UnsupportedOperationException(
                "ViewOverlays is the read-side seam; loads deliver cells via ChunkWriter");
    }
}
