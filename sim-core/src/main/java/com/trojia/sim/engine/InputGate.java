package com.trojia.sim.engine;

import com.trojia.sim.event.EventSink;
import com.trojia.sim.event.ExternalChargeApplied;
import com.trojia.sim.event.ExternalFluidSpawned;
import com.trojia.sim.event.ExternalIgnition;
import com.trojia.sim.world.ChunkWriter;
import com.trojia.sim.world.TileForm;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;

/**
 * The single doorway for external input (ARCHITECTURE.md §4, phase 0).
 * Commands arrive via {@link #submit} in arrival order; at TICK_BEGIN the
 * engine drains them plus any scripted actions due this tick: every drained
 * command is appended to the input log (replayable, saved as the INPT
 * section), material/form paints are applied directly via ChunkWriter, and
 * quantity inputs are emitted as {@code External*} events for their owning
 * systems — the gate never writes system-owned lanes.
 *
 * <p><b>F1 materialization coverage:</b> {@code PaintMaterial}/{@code
 * ClearTile} paint via the writer; {@code Ignite}/{@code AddFluid}/{@code
 * InjectCharge} emit their §5 {@code External*} events. The remaining
 * commands ({@code Extinguish}, {@code RemoveFluid}, {@code
 * PlaceLightSource}, {@code RemoveLightSource}, {@code SetFocus}) are logged
 * but not yet materialized — their owning-system intake seams land with the
 * fire/fluid/light/bubble implementers. Paint writes rejected by the writer
 * (non-concrete target) are dropped in F1; BoundaryFlux routing lands with
 * the bubble module.
 */
public final class InputGate {

    /** One drained command, recorded at the tick it materialized. */
    private record LogEntry(long tick, SimCommand command) {
    }

    /** One scripted action awaiting its due tick (insertion order preserved). */
    private record Scheduled(long dueTick, SimCommand command) {
    }

    private final List<SimCommand> pending = new ArrayList<>();
    private final List<Scheduled> scheduled = new ArrayList<>();
    private final List<LogEntry> log = new ArrayList<>();

    /** An empty gate. */
    public InputGate() {
    }

    /** Queues a command (arrival order preserved). Legal between ticks. */
    public void submit(SimCommand command) {
        if (command == null) {
            throw new IllegalArgumentException("command must be non-null");
        }
        pending.add(command);
    }

    /**
     * Schedules a scripted action for {@code dueTick} (scenario scripts);
     * scheduled actions drain after queued commands of the same tick, ordered
     * by {@code (dueTick, schedule order)}.
     */
    public void schedule(long dueTick, SimCommand command) {
        if (command == null) {
            throw new IllegalArgumentException("command must be non-null");
        }
        scheduled.add(new Scheduled(dueTick, command));
    }

    /**
     * TICK_BEGIN drain: logs and materializes everything due at {@code tick}
     * (paints via {@code writer}; quantity inputs via {@code events}).
     * Engine-only. Queued commands drain first in arrival order, then
     * scheduled actions with {@code dueTick <= tick} in {@code (dueTick,
     * schedule order)} order. {@code writer} may be null on a world-less
     * bootstrap engine — draining a paint command there is an error.
     */
    public void drain(long tick, ChunkWriter writer, EventSink events) {
        for (SimCommand command : pending) {
            log.add(new LogEntry(tick, command));
            materialize(command, writer, events);
        }
        pending.clear();
        if (scheduled.isEmpty()) {
            return;
        }
        List<Scheduled> due = new ArrayList<>();
        for (Iterator<Scheduled> it = scheduled.iterator(); it.hasNext(); ) {
            Scheduled action = it.next();
            if (action.dueTick() <= tick) {
                due.add(action);
                it.remove();
            }
        }
        due.sort(Comparator.comparingLong(Scheduled::dueTick)); // stable: preserves schedule order
        for (Scheduled action : due) {
            log.add(new LogEntry(tick, action.command()));
            materialize(action.command(), writer, events);
        }
    }

    /** Serializes the input log + pending queue + pending scheduled actions (INPT section). Pure. */
    public void serialize(DataOutput out) throws IOException {
        out.writeInt(log.size());
        for (LogEntry entry : log) {
            out.writeLong(entry.tick());
            writeCommand(out, entry.command());
        }
        out.writeInt(pending.size());
        for (SimCommand command : pending) {
            writeCommand(out, command);
        }
        out.writeInt(scheduled.size());
        for (Scheduled action : scheduled) {
            out.writeLong(action.dueTick());
            writeCommand(out, action.command());
        }
    }

    /** Restores state written by {@link #serialize}, replacing this gate's state. */
    public void load(DataInput in) throws IOException {
        log.clear();
        pending.clear();
        scheduled.clear();
        int logged = in.readInt();
        for (int i = 0; i < logged; i++) {
            long tick = in.readLong();
            log.add(new LogEntry(tick, readCommand(in)));
        }
        int queued = in.readInt();
        for (int i = 0; i < queued; i++) {
            pending.add(readCommand(in));
        }
        int planned = in.readInt();
        for (int i = 0; i < planned; i++) {
            long dueTick = in.readLong();
            scheduled.add(new Scheduled(dueTick, readCommand(in)));
        }
    }

    /** Applies one command: paints via the writer, quantity inputs via events. */
    private static void materialize(SimCommand command, ChunkWriter writer, EventSink events) {
        switch (command) {
            case SimCommand.PaintMaterial c ->
                    paints(writer).setMaterialAndForm(c.cell(), c.materialId(), c.form());
            case SimCommand.ClearTile c ->
                    paints(writer).setMaterialAndForm(c.cell(), (short) 0, TileForm.OPEN);
            case SimCommand.Ignite c -> events.emit(new ExternalIgnition(c.cell()));
            case SimCommand.AddFluid c ->
                    events.emit(new ExternalFluidSpawned(c.cell(), c.fluidId(), c.units()));
            case SimCommand.InjectCharge c ->
                    events.emit(new ExternalChargeApplied(c.cell(), c.deltaCu()));
            case SimCommand.Extinguish c -> {
                // Logged only in F1: the fire system's extinguish intake lands with F3.
            }
            case SimCommand.RemoveFluid c -> {
                // Logged only in F1: the fluid system's removal intake lands with F3.
            }
            case SimCommand.PlaceLightSource c -> {
                // Logged only in F1: the light source registry lands with F4.
            }
            case SimCommand.RemoveLightSource c -> {
                // Logged only in F1: the light source registry lands with F4.
            }
            case SimCommand.SetFocus c -> {
                // Logged only in F1: the ticket retargeter lands with F5.
            }
        }
    }

    private static ChunkWriter paints(ChunkWriter writer) {
        if (writer == null) {
            throw new IllegalStateException("paint command drained on a world-less engine");
        }
        return writer;
    }

    /** Writes one command with a stable type tag (save-format order of the sealed list). */
    private static void writeCommand(DataOutput out, SimCommand command) throws IOException {
        switch (command) {
            case SimCommand.PaintMaterial c -> {
                out.writeByte(0);
                out.writeInt(c.cell());
                out.writeShort(c.materialId());
                out.writeByte(c.form().ordinal());
            }
            case SimCommand.ClearTile c -> {
                out.writeByte(1);
                out.writeInt(c.cell());
            }
            case SimCommand.Ignite c -> {
                out.writeByte(2);
                out.writeInt(c.cell());
            }
            case SimCommand.Extinguish c -> {
                out.writeByte(3);
                out.writeInt(c.cell());
            }
            case SimCommand.AddFluid c -> {
                out.writeByte(4);
                out.writeInt(c.cell());
                out.writeShort(c.fluidId());
                out.writeInt(c.units());
            }
            case SimCommand.RemoveFluid c -> {
                out.writeByte(5);
                out.writeInt(c.cell());
                out.writeShort(c.fluidId());
                out.writeInt(c.units());
            }
            case SimCommand.InjectCharge c -> {
                out.writeByte(6);
                out.writeInt(c.cell());
                out.writeInt(c.deltaCu());
            }
            case SimCommand.PlaceLightSource c -> {
                out.writeByte(7);
                out.writeInt(c.handle());
                out.writeInt(c.cell());
                out.writeInt(c.level());
            }
            case SimCommand.RemoveLightSource c -> {
                out.writeByte(8);
                out.writeInt(c.handle());
            }
            case SimCommand.SetFocus c -> {
                out.writeByte(9);
                out.writeInt(c.cell());
            }
        }
    }

    /** Exact inverse of {@link #writeCommand}. */
    private static SimCommand readCommand(DataInput in) throws IOException {
        int tag = in.readUnsignedByte();
        return switch (tag) {
            case 0 -> new SimCommand.PaintMaterial(in.readInt(), in.readShort(),
                    TileForm.ofOrdinal(in.readUnsignedByte()));
            case 1 -> new SimCommand.ClearTile(in.readInt());
            case 2 -> new SimCommand.Ignite(in.readInt());
            case 3 -> new SimCommand.Extinguish(in.readInt());
            case 4 -> new SimCommand.AddFluid(in.readInt(), in.readShort(), in.readInt());
            case 5 -> new SimCommand.RemoveFluid(in.readInt(), in.readShort(), in.readInt());
            case 6 -> new SimCommand.InjectCharge(in.readInt(), in.readInt());
            case 7 -> new SimCommand.PlaceLightSource(in.readInt(), in.readInt(), in.readInt());
            case 8 -> new SimCommand.RemoveLightSource(in.readInt());
            case 9 -> new SimCommand.SetFocus(in.readInt());
            default -> throw new IOException("unknown SimCommand tag: " + tag);
        };
    }
}
