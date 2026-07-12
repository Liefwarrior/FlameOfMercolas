package com.trojia.sim.world;

/**
 * The sparse 16-bit overlays. Overlays store an unsigned 16-bit value for a
 * small set of tiles (no dense lane); values survive freeze verbatim inside
 * the chunk blob. Overlay writes go through {@link ChunkWriter} and mark
 * changedBits + revision, but have no change logs (owning systems self-track
 * their tile sets — ARCHITECTURE.md §1.2, world ruling b). Ordinals are
 * save-format-stable — append only, never reorder.
 */
public enum OverlayId {

    /** Chromatis/lightstone stored charge in charge units (cu); owner: ChargeSystem. */
    CHARGE
}
