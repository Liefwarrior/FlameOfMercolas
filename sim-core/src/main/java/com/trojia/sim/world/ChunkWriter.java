package com.trojia.sim.world;

/**
 * THE only write path into world state (ARCHITECTURE.md §3, §6). Every setter:
 * appends to the lane's change log (if it has readers), marks changedBits and
 * the chunk revision, and — on material/form writes — recomputes the derived
 * {@link FlagBits#BLOCKS_MOVE}/{@link FlagBits#BLOCKS_LIGHT} bits.
 *
 * <p>Writes into non-concrete chunks (FROZEN, FROZEN_RESIDENT rind, VOID
 * border) are <b>rejected</b> with a status code, never applied and never an
 * exception: the calling system must route rejected quantities to
 * {@code BoundaryFlux.credit} so nothing silently vanishes at the hull
 * (§1.1 #11). Callers on hot paths check the returned code.
 *
 * <p>Sole-writer discipline per field (§6) is a convention enforced by review
 * and ArchUnit, not by this interface.
 */
public interface ChunkWriter {

    /** Status: the write was applied. */
    int APPLIED = 0;
    /** Status: rejected — target chunk is FROZEN or FROZEN_RESIDENT (route to BoundaryFlux). */
    int REJECTED_FROZEN = 1;
    /** Status: rejected — target is the immutable VOID border. */
    int REJECTED_VOID = 2;

    /** Sets MATERIAL, keeping the current form; refreshes derived flag bits. */
    int setMaterial(int packedPos, short materialId);

    /** Sets MATERIAL and FORM atomically (one change-log entry per lane); refreshes derived flags. */
    int setMaterialAndForm(int packedPos, short materialId, TileForm form);

    /** Sets FORM, keeping the current material; refreshes derived flag bits. */
    int setForm(int packedPos, TileForm form);

    /**
     * Sets ({@code value = true}) or clears one or more non-derived FLAGS bits
     * (e.g. {@link FlagBits#ON_FIRE}). Derived bits may not be written directly.
     */
    int setFlag(int packedPos, int flagMask, boolean value);

    /** Sets TEMPERATURE, unsigned 16-bit deci-Kelvin (0..65535). */
    int setTemperatureDeciK(int packedPos, int deciK);

    /** Sets the raw bit-packed FLUID lane value. */
    int setFluidBits(int packedPos, int fluidBits);

    /** Sets the raw packed LIGHT lane value, sky(5b)+block(5b). */
    int setLightBits(int packedPos, int lightBits);

    /**
     * Generic lane write for extension lanes (e.g. OPACITY); {@code value} is
     * truncated to the lane's width. Core lanes should use the typed setters.
     */
    int setLane(int packedPos, LaneId lane, int value);

    /**
     * Sets a sparse overlay cell to an unsigned 16-bit value (0 is a valid
     * stored value; use {@link #clearOverlay} to remove the cell). No change
     * log — the owning system self-tracks its tile set; changedBits + revision
     * still fire.
     */
    int setOverlay(int packedPos, OverlayId overlay, int value);

    /** Removes a sparse overlay cell entirely. */
    int clearOverlay(int packedPos, OverlayId overlay);
}
