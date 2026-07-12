package com.trojia.sim.world;

/**
 * The one {@link ChunkWriter} of a {@link DenseWorld}. Every accepted write:
 * mutates the lane array, appends to the lane's change log when it has
 * readers, records the tile in the revision tracker's changedBits, and — on
 * material/form writes — recomputes the derived
 * {@link FlagBits#BLOCKS_MOVE}/{@link FlagBits#BLOCKS_LIGHT} bits via the
 * world's {@link TileClassifier} (a derived-bit change appends to the FLAGS
 * log too, so light wakes through change logs, never events).
 *
 * <p>Writes into non-concrete chunks return
 * {@link #REJECTED_FROZEN}/{@link #REJECTED_VOID} before touching any
 * storage. Hot-path discipline: no allocation, no boxing; range checks on
 * quantity arguments are {@code assert}s (debug builds only).
 */
final class DenseChunkWriter implements ChunkWriter {

    private static final int DERIVED_MASK = FlagBits.BLOCKS_MOVE | FlagBits.BLOCKS_LIGHT;

    private final Chunk[] chunks;
    private final byte[] states;
    private final Coords coords;
    private final ChangeLogSink logs;
    private final RevisionSink revisions;
    private final TileClassifier classifier;
    private final LaneId materialLane;
    private final LaneId formLane;
    private final LaneId flagsLane;
    private final LaneId temperatureLane;
    private final LaneId fluidLane;
    private final LaneId lightLane;

    DenseChunkWriter(DenseWorld world, Coords coords, LaneRegistry lanes, ChangeLogSink logs,
            RevisionSink revisions, TileClassifier classifier) {
        this.chunks = world.chunks;
        this.states = world.states;
        this.coords = coords;
        this.logs = logs;
        this.revisions = revisions;
        this.classifier = classifier;
        this.materialLane = lanes.byIndex(Lanes.MATERIAL_INDEX);
        this.formLane = lanes.byIndex(Lanes.FORM_INDEX);
        this.flagsLane = lanes.byIndex(Lanes.FLAGS_INDEX);
        this.temperatureLane = lanes.byIndex(Lanes.TEMPERATURE_INDEX);
        this.fluidLane = lanes.byIndex(Lanes.FLUID_INDEX);
        this.lightLane = lanes.byIndex(Lanes.LIGHT_INDEX);
    }

    @Override
    public int setMaterial(int packedPos, short materialId) {
        int chunkIndex = coords.chunkIndex(packedPos);
        int status = gate(chunkIndex);
        if (status != APPLIED) {
            return status;
        }
        Chunk chunk = chunks[chunkIndex];
        int localIdx = coords.localIdx(packedPos);
        chunk.shortLanes[Lanes.MATERIAL_INDEX][localIdx] = materialId;
        TileForm form = TileForm.ofOrdinal(chunk.byteLanes[Lanes.FORM_INDEX][localIdx] & 0xFF);
        log(materialLane, packedPos);
        refreshDerived(chunk, localIdx, packedPos, materialId, form);
        revisions.mark(chunkIndex, localIdx);
        return APPLIED;
    }

    @Override
    public int setMaterialAndForm(int packedPos, short materialId, TileForm form) {
        requirePaintable(form);
        int chunkIndex = coords.chunkIndex(packedPos);
        int status = gate(chunkIndex);
        if (status != APPLIED) {
            return status;
        }
        Chunk chunk = chunks[chunkIndex];
        int localIdx = coords.localIdx(packedPos);
        chunk.shortLanes[Lanes.MATERIAL_INDEX][localIdx] = materialId;
        chunk.byteLanes[Lanes.FORM_INDEX][localIdx] = (byte) form.ordinal();
        log(materialLane, packedPos);
        log(formLane, packedPos);
        refreshDerived(chunk, localIdx, packedPos, materialId, form);
        revisions.mark(chunkIndex, localIdx);
        return APPLIED;
    }

    @Override
    public int setForm(int packedPos, TileForm form) {
        requirePaintable(form);
        int chunkIndex = coords.chunkIndex(packedPos);
        int status = gate(chunkIndex);
        if (status != APPLIED) {
            return status;
        }
        Chunk chunk = chunks[chunkIndex];
        int localIdx = coords.localIdx(packedPos);
        chunk.byteLanes[Lanes.FORM_INDEX][localIdx] = (byte) form.ordinal();
        short materialId = chunk.shortLanes[Lanes.MATERIAL_INDEX][localIdx];
        log(formLane, packedPos);
        refreshDerived(chunk, localIdx, packedPos, materialId, form);
        revisions.mark(chunkIndex, localIdx);
        return APPLIED;
    }

    @Override
    public int setFlag(int packedPos, int flagMask, boolean value) {
        if ((flagMask & DERIVED_MASK) != 0) {
            throw new IllegalArgumentException(
                    "derived flag bits are writer-maintained, never written directly: 0x"
                            + Integer.toHexString(flagMask));
        }
        if ((flagMask & ~0xFF) != 0) {
            throw new IllegalArgumentException(
                    "flag mask exceeds the FLAGS byte: 0x" + Integer.toHexString(flagMask));
        }
        int chunkIndex = coords.chunkIndex(packedPos);
        int status = gate(chunkIndex);
        if (status != APPLIED) {
            return status;
        }
        Chunk chunk = chunks[chunkIndex];
        int localIdx = coords.localIdx(packedPos);
        byte[] flags = chunk.byteLanes[Lanes.FLAGS_INDEX];
        int old = flags[localIdx] & 0xFF;
        flags[localIdx] = (byte) (value ? old | flagMask : old & ~flagMask);
        log(flagsLane, packedPos);
        revisions.mark(chunkIndex, localIdx);
        return APPLIED;
    }

    @Override
    public int setTemperatureDeciK(int packedPos, int deciK) {
        assert (deciK & ~0xFFFF) == 0 : "temperature out of unsigned 16-bit range: " + deciK;
        return setShortLane(packedPos, temperatureLane, Lanes.TEMPERATURE_INDEX, deciK);
    }

    @Override
    public int setFluidBits(int packedPos, int fluidBits) {
        assert (fluidBits & ~0xFFFF) == 0 : "fluid bits out of 16-bit range: " + fluidBits;
        return setShortLane(packedPos, fluidLane, Lanes.FLUID_INDEX, fluidBits);
    }

    @Override
    public int setLightBits(int packedPos, int lightBits) {
        assert (lightBits & ~0xFFFF) == 0 : "light bits out of 16-bit range: " + lightBits;
        return setShortLane(packedPos, lightLane, Lanes.LIGHT_INDEX, lightBits);
    }

    @Override
    public int setLane(int packedPos, LaneId lane, int value) {
        if (lane.index() <= Lanes.FLAGS_INDEX) {
            throw new IllegalArgumentException("the " + lane.name()
                    + " lane requires its typed setter (derived-flag maintenance)");
        }
        int chunkIndex = coords.chunkIndex(packedPos);
        int status = gate(chunkIndex);
        if (status != APPLIED) {
            return status;
        }
        Chunk chunk = chunks[chunkIndex];
        int localIdx = coords.localIdx(packedPos);
        if (lane.bytesPerTile() == 2) {
            chunk.shortLanes[lane.index()][localIdx] = (short) value;
        } else {
            chunk.byteLanes[lane.index()][localIdx] = (byte) value;
        }
        log(lane, packedPos);
        revisions.mark(chunkIndex, localIdx);
        return APPLIED;
    }

    @Override
    public int setOverlay(int packedPos, OverlayId overlay, int value) {
        assert (value & ~0xFFFF) == 0 : "overlay value out of unsigned 16-bit range: " + value;
        int chunkIndex = coords.chunkIndex(packedPos);
        int status = gate(chunkIndex);
        if (status != APPLIED) {
            return status;
        }
        int localIdx = coords.localIdx(packedPos);
        chunks[chunkIndex].overlayForWrite(overlay).put(localIdx, value);
        revisions.mark(chunkIndex, localIdx);
        return APPLIED;
    }

    @Override
    public int clearOverlay(int packedPos, OverlayId overlay) {
        int chunkIndex = coords.chunkIndex(packedPos);
        int status = gate(chunkIndex);
        if (status != APPLIED) {
            return status;
        }
        int localIdx = coords.localIdx(packedPos);
        SparseOverlay stored = chunks[chunkIndex].overlayOrNull(overlay);
        if (stored != null) {
            stored.remove(localIdx);
        }
        revisions.mark(chunkIndex, localIdx);
        return APPLIED;
    }

    /** The derived FLAGS bits of a material + form under {@code classifier}. */
    static int derivedFlags(TileClassifier classifier, short materialId, TileForm form) {
        int bits = 0;
        if (classifier.blocksMove(materialId, form)) {
            bits |= FlagBits.BLOCKS_MOVE;
        }
        if (classifier.blocksLight(materialId, form)) {
            bits |= FlagBits.BLOCKS_LIGHT;
        }
        return bits;
    }

    /** Common path of the plain 2-byte lane setters (no derived-flag involvement). */
    private int setShortLane(int packedPos, LaneId lane, int laneIndex, int value) {
        int chunkIndex = coords.chunkIndex(packedPos);
        int status = gate(chunkIndex);
        if (status != APPLIED) {
            return status;
        }
        Chunk chunk = chunks[chunkIndex];
        int localIdx = coords.localIdx(packedPos);
        chunk.shortLanes[laneIndex][localIdx] = (short) value;
        log(lane, packedPos);
        revisions.mark(chunkIndex, localIdx);
        return APPLIED;
    }

    /** Concreteness gate: {@link #APPLIED} to proceed, else the reject status. */
    private int gate(int chunkIndex) {
        byte state = states[chunkIndex];
        if (state == DenseWorld.STATE_CONCRETE) {
            return APPLIED;
        }
        return state == DenseWorld.STATE_VOID ? REJECTED_VOID : REJECTED_FROZEN;
    }

    /**
     * Recomputes the derived FLAGS bits after a material/form write; a change
     * appends to the FLAGS log (revision marking is the caller's, same tile).
     */
    private void refreshDerived(Chunk chunk, int localIdx, int packedPos, short materialId,
            TileForm form) {
        byte[] flags = chunk.byteLanes[Lanes.FLAGS_INDEX];
        int old = flags[localIdx] & 0xFF;
        int updated = (old & ~DERIVED_MASK) | derivedFlags(classifier, materialId, form);
        if (updated == old) {
            return;
        }
        flags[localIdx] = (byte) updated;
        log(flagsLane, packedPos);
    }

    /** Appends to {@code lane}'s change log iff it has readers (§6 skip rule). */
    private void log(LaneId lane, int packedPos) {
        if (logs.hasReaders(lane)) {
            logs.append(lane, packedPos);
        }
    }

    /** VOID is the border's form, never paintable through the writer. */
    private static void requirePaintable(TileForm form) {
        if (form == null || form == TileForm.VOID) {
            throw new IllegalArgumentException("form must be a paintable TileForm: " + form);
        }
    }
}
