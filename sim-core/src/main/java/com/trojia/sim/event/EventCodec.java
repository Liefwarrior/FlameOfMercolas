package com.trojia.sim.event;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.util.List;

/**
 * The canonical topic table and wire codec of the sealed {@link SimEvent}
 * hierarchy. The index of a topic in {@link #topics()} is its stable topic id:
 * it keys the EVNT carry-over section layout and the bus's internal buffers,
 * so the order is save-format-stable — append new topics at the end, never
 * reorder (mirrors the §5 taxonomy order).
 *
 * <p>Static pure functions only; events are records of primitives + ids, so
 * every field round-trips exactly through {@link DataOutput}/{@link DataInput}.
 */
final class EventCodec {

    /** Canonical topic order (§5). Immutable; index == topic id. */
    private static final List<Class<? extends SimEvent>> TOPICS = List.of(
            ExternalIgnition.class,
            ExternalFluidSpawned.class,
            ExternalChargeApplied.class,
            ChunkThawed.class,
            ChunkFrozen.class,
            ReagentContactEvent.class,
            FluidVaporizedEvent.class,
            FluidFrozenEvent.class,
            TileIgnited.class,
            TileExtinguished.class,
            FireLuminanceChanged.class,
            TemperatureThresholdEvent.class,
            MaterialPhaseChangedEvent.class,
            MaterialTransformedEvent.class,
            PressurePulseEvent.class,
            ChargeStopChangedEvent.class,
            ChargeSaturatedEvent.class,
            EnergyDischargedEvent.class);

    private EventCodec() {
    }

    /** The canonical topic list (immutable). */
    static List<Class<? extends SimEvent>> topics() {
        return TOPICS;
    }

    /** The topic id of {@code topic}, or -1 if it is not a registered topic. */
    static int topicIndex(Class<?> topic) {
        for (int i = 0; i < TOPICS.size(); i++) {
            if (TOPICS.get(i) == topic) {
                return i;
            }
        }
        return -1;
    }

    /** Writes {@code event}'s fields (no topic tag — the caller keys by topic id). */
    static void write(DataOutput out, SimEvent event) throws IOException {
        switch (event) {
            case ExternalIgnition e -> out.writeInt(e.cell());
            case ExternalFluidSpawned e -> {
                out.writeInt(e.cell());
                out.writeShort(e.fluidId());
                out.writeInt(e.units());
            }
            case ExternalChargeApplied e -> {
                out.writeInt(e.cell());
                out.writeInt(e.deltaCu());
            }
            case ChunkThawed e -> out.writeInt(e.chunkIndex());
            case ChunkFrozen e -> out.writeInt(e.chunkIndex());
            case ReagentContactEvent e -> {
                out.writeInt(e.cell());
                out.writeShort(e.fluidId());
                out.writeInt(e.units());
                out.writeShort(e.solidMaterialId());
            }
            case FluidVaporizedEvent e -> {
                out.writeInt(e.cell());
                out.writeShort(e.fluidId());
                out.writeInt(e.units());
                out.writeInt(e.cause());
            }
            case FluidFrozenEvent e -> {
                out.writeInt(e.cell());
                out.writeShort(e.fluidId());
                out.writeInt(e.units());
            }
            case TileIgnited e -> out.writeInt(e.cell());
            case TileExtinguished e -> out.writeInt(e.cell());
            case FireLuminanceChanged e -> {
                out.writeInt(e.cell());
                out.writeInt(e.oldBucket());
                out.writeInt(e.newBucket());
            }
            case TemperatureThresholdEvent e -> {
                out.writeInt(e.cell());
                out.writeInt(e.thresholdId());
                out.writeInt(e.direction());
            }
            case MaterialPhaseChangedEvent e -> {
                out.writeInt(e.cell());
                out.writeShort(e.fromMaterialId());
                out.writeShort(e.toMaterialId());
                out.writeInt(e.yieldUnits());
            }
            case MaterialTransformedEvent e -> {
                out.writeInt(e.cell());
                out.writeShort(e.fromMaterialId());
                out.writeShort(e.toMaterialId());
                out.writeInt(e.cause());
            }
            case PressurePulseEvent e -> {
                out.writeInt(e.cell());
                out.writeShort(e.gasId());
                out.writeInt(e.magnitude());
            }
            case ChargeStopChangedEvent e -> {
                out.writeInt(e.cell());
                out.writeInt(e.oldStop());
                out.writeInt(e.newStop());
            }
            case ChargeSaturatedEvent e -> out.writeInt(e.cell());
            case EnergyDischargedEvent e -> {
                out.writeInt(e.cell());
                out.writeInt(e.releasedCu());
                out.writeInt(e.ratePerTick());
            }
        }
    }

    /**
     * Reads one event of topic id {@code topicIndex}; exact inverse of
     * {@link #write}.
     *
     * @throws IOException on an unknown topic id (corrupt EVNT section)
     */
    static SimEvent read(DataInput in, int topicIndex) throws IOException {
        return switch (topicIndex) {
            case 0 -> new ExternalIgnition(in.readInt());
            case 1 -> new ExternalFluidSpawned(in.readInt(), in.readShort(), in.readInt());
            case 2 -> new ExternalChargeApplied(in.readInt(), in.readInt());
            case 3 -> new ChunkThawed(in.readInt());
            case 4 -> new ChunkFrozen(in.readInt());
            case 5 -> new ReagentContactEvent(in.readInt(), in.readShort(), in.readInt(),
                    in.readShort());
            case 6 -> new FluidVaporizedEvent(in.readInt(), in.readShort(), in.readInt(),
                    in.readInt());
            case 7 -> new FluidFrozenEvent(in.readInt(), in.readShort(), in.readInt());
            case 8 -> new TileIgnited(in.readInt());
            case 9 -> new TileExtinguished(in.readInt());
            case 10 -> new FireLuminanceChanged(in.readInt(), in.readInt(), in.readInt());
            case 11 -> new TemperatureThresholdEvent(in.readInt(), in.readInt(), in.readInt());
            case 12 -> new MaterialPhaseChangedEvent(in.readInt(), in.readShort(),
                    in.readShort(), in.readInt());
            case 13 -> new MaterialTransformedEvent(in.readInt(), in.readShort(),
                    in.readShort(), in.readInt());
            case 14 -> new PressurePulseEvent(in.readInt(), in.readShort(), in.readInt());
            case 15 -> new ChargeStopChangedEvent(in.readInt(), in.readInt(), in.readInt());
            case 16 -> new ChargeSaturatedEvent(in.readInt());
            case 17 -> new EnergyDischargedEvent(in.readInt(), in.readInt(), in.readInt());
            default -> throw new IOException("unknown event topic id: " + topicIndex);
        };
    }
}
