package com.trojia.sim.world;

/**
 * Flyweight read cursor over the world's lanes: obtain one via
 * {@link World#cursor()}, {@link #moveTo(int)} it around, read through the
 * {@link Tile} getters. Zero allocation after construction; caches the current
 * chunk so runs of nearby reads skip the chunk lookup.
 *
 * <p>Not thread-safe, not stable across ticks: debug builds stamp the cursor
 * with the tick it was positioned on and assert reads happen on the same tick.
 * Reading a VOID-border or FROZEN chunk is legal (frozen rind is readable,
 * never writable).
 */
public final class TileCursor implements Tile {

    private final World world;
    private final Coords coords;
    /** Tick-stamp source; null when the world implementation carries no tick. */
    private final DenseWorld stamped;
    private final LaneId materialLane;
    private final LaneId formLane;
    private final LaneId flagsLane;
    private final LaneId temperatureLane;
    private final LaneId fluidLane;
    private final LaneId lightLane;
    private final LaneId opacityLane;
    private short[] material;
    private byte[] form;
    private byte[] flags;
    private short[] temperature;
    private short[] fluid;
    private short[] light;
    private byte[] opacity;
    private int chunkIndex = -1;
    private int localIdx = -1;
    private int packedPos = -1;
    private long stampTick;

    TileCursor(World world) {
        this.world = world;
        this.coords = world.coords();
        this.stamped = world instanceof DenseWorld dense ? dense : null;
        LaneRegistry lanes = world.lanes();
        this.materialLane = lanes.byName(Lanes.MATERIAL);
        this.formLane = lanes.byName(Lanes.FORM);
        this.flagsLane = lanes.byName(Lanes.FLAGS);
        this.temperatureLane = lanes.byName(Lanes.TEMPERATURE);
        this.fluidLane = lanes.byName(Lanes.FLUID);
        this.lightLane = lanes.byName(Lanes.LIGHT);
        this.opacityLane = lanes.byName(Lanes.OPACITY);
    }

    /**
     * Repositions this cursor; returns {@code this} for chaining. The position
     * must be in world bounds (border included).
     */
    public TileCursor moveTo(int packedPos) {
        int targetChunk = coords.chunkIndex(packedPos);
        if (targetChunk != chunkIndex) {
            ChunkView chunk = world.chunk(targetChunk);
            material = chunk.shortLane(materialLane);
            form = chunk.byteLane(formLane);
            flags = chunk.byteLane(flagsLane);
            temperature = chunk.shortLane(temperatureLane);
            fluid = chunk.shortLane(fluidLane);
            light = chunk.shortLane(lightLane);
            opacity = chunk.byteLane(opacityLane);
            chunkIndex = targetChunk;
        }
        this.localIdx = coords.localIdx(packedPos);
        this.packedPos = packedPos;
        this.stampTick = stamped == null ? 0L : stamped.tickStamp();
        return this;
    }

    @Override
    public int packedPos() {
        assert positioned() : "cursor was never positioned";
        return packedPos;
    }

    @Override
    public short materialId() {
        assert fresh() : STALE_MESSAGE;
        return material[localIdx];
    }

    @Override
    public TileForm form() {
        assert fresh() : STALE_MESSAGE;
        return TileForm.ofOrdinal(form[localIdx] & 0xFF);
    }

    @Override
    public int flags() {
        assert fresh() : STALE_MESSAGE;
        return flags[localIdx] & 0xFF;
    }

    @Override
    public int temperatureDeciK() {
        assert fresh() : STALE_MESSAGE;
        return temperature[localIdx] & 0xFFFF;
    }

    @Override
    public int fluidBits() {
        assert fresh() : STALE_MESSAGE;
        return fluid[localIdx] & 0xFFFF;
    }

    @Override
    public int lightBits() {
        assert fresh() : STALE_MESSAGE;
        return light[localIdx] & 0xFFFF;
    }

    @Override
    public int opacity() {
        assert fresh() : STALE_MESSAGE;
        return opacity[localIdx] & 0xFF;
    }

    private static final String STALE_MESSAGE =
            "cursor read on a different tick than it was positioned — reposition each tick";

    private boolean positioned() {
        return packedPos >= 0;
    }

    /** Debug staleness check: positioned, and on the tick it was positioned on. */
    private boolean fresh() {
        return positioned() && (stamped == null || stamped.tickStamp() == stampTick);
    }
}
