package com.trojia.sim.engine;

import com.trojia.sim.event.EventSink;
import com.trojia.sim.event.SimEvent;
import com.trojia.sim.world.ChunkWriter;
import com.trojia.sim.world.LaneId;
import com.trojia.sim.world.OverlayId;
import com.trojia.sim.world.TileForm;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Contract tests for the InputGate: arrival-order draining, paints via
 * ChunkWriter, quantity inputs as {@code External*} events, scheduled-action
 * ordering, and INPT-section round trips.
 */
final class InputGateTest {

    /** Journals every materialization (paints and events) into one shared list. */
    private static final class Journal {
        final List<String> entries = new ArrayList<>();
    }

    /** A ChunkWriter that records paint calls and applies everything. */
    private static final class RecordingWriter implements ChunkWriter {
        private final Journal journal;

        RecordingWriter(Journal journal) {
            this.journal = journal;
        }

        @Override
        public int setMaterial(int packedPos, short materialId) {
            journal.entries.add("material(" + packedPos + "," + materialId + ")");
            return APPLIED;
        }

        @Override
        public int setMaterialAndForm(int packedPos, short materialId, TileForm form) {
            journal.entries.add("paint(" + packedPos + "," + materialId + "," + form + ")");
            return APPLIED;
        }

        @Override
        public int setForm(int packedPos, TileForm form) {
            journal.entries.add("form(" + packedPos + "," + form + ")");
            return APPLIED;
        }

        @Override
        public int setFlag(int packedPos, int flagMask, boolean value) {
            return APPLIED;
        }

        @Override
        public int setTemperatureDeciK(int packedPos, int deciK) {
            return APPLIED;
        }

        @Override
        public int setFluidBits(int packedPos, int fluidBits) {
            return APPLIED;
        }

        @Override
        public int setLightBits(int packedPos, int lightBits) {
            return APPLIED;
        }

        @Override
        public int setLane(int packedPos, LaneId lane, int value) {
            return APPLIED;
        }

        @Override
        public int setOverlay(int packedPos, OverlayId overlay, int value) {
            return APPLIED;
        }

        @Override
        public int clearOverlay(int packedPos, OverlayId overlay) {
            return APPLIED;
        }
    }

    /** An EventSink that records emissions into the shared journal. */
    private static final class RecordingSink implements EventSink {
        private final Journal journal;

        RecordingSink(Journal journal) {
            this.journal = journal;
        }

        @Override
        public void emit(SimEvent event) {
            journal.entries.add(event.toString());
        }
    }

    @Test
    void drainsQueuedCommandsInArrivalOrder() {
        Journal journal = new Journal();
        InputGate gate = new InputGate();
        gate.submit(new SimCommand.PaintMaterial(100, (short) 7, TileForm.WALL));
        gate.submit(new SimCommand.Ignite(200));
        gate.submit(new SimCommand.AddFluid(300, (short) 1, 8));
        gate.submit(new SimCommand.InjectCharge(400, 600));
        gate.submit(new SimCommand.ClearTile(100));

        gate.drain(1, new RecordingWriter(journal), new RecordingSink(journal));

        assertEquals(List.of(
                "paint(100,7,WALL)",
                "ExternalIgnition[cell=200]",
                "ExternalFluidSpawned[cell=300, fluidId=1, units=8]",
                "ExternalChargeApplied[cell=400, deltaCu=600]",
                "paint(100,0,OPEN)"), journal.entries);
    }

    @Test
    void scheduledActionsDrainAtTheirDueTickAfterQueuedCommands() {
        Journal journal = new Journal();
        InputGate gate = new InputGate();
        gate.schedule(3, new SimCommand.Ignite(33));
        gate.schedule(2, new SimCommand.Ignite(22));
        RecordingWriter writer = new RecordingWriter(journal);
        RecordingSink sink = new RecordingSink(journal);

        gate.drain(1, writer, sink);
        assertTrue(journal.entries.isEmpty(), "nothing is due at tick 1");

        gate.submit(new SimCommand.Ignite(20));
        gate.drain(2, writer, sink);
        assertEquals(List.of(
                "ExternalIgnition[cell=20]",   // queued commands first...
                "ExternalIgnition[cell=22]"),  // ...then scheduled actions due this tick
                journal.entries);

        journal.entries.clear();
        gate.drain(3, writer, sink);
        assertEquals(List.of("ExternalIgnition[cell=33]"), journal.entries);
    }

    /** Actions overdue together drain by (dueTick, schedule order). */
    @Test
    void overdueActionsDrainByDueTickThenScheduleOrder() {
        Journal journal = new Journal();
        InputGate gate = new InputGate();
        gate.schedule(5, new SimCommand.Ignite(51));
        gate.schedule(3, new SimCommand.Ignite(31));
        gate.schedule(5, new SimCommand.Ignite(52));
        gate.schedule(3, new SimCommand.Ignite(32));

        gate.drain(5, new RecordingWriter(journal), new RecordingSink(journal));

        assertEquals(List.of(
                "ExternalIgnition[cell=31]",
                "ExternalIgnition[cell=32]",
                "ExternalIgnition[cell=51]",
                "ExternalIgnition[cell=52]"), journal.entries);
    }

    /** Commands without an F1 materialization path are logged, not applied. */
    @Test
    void unmaterializedCommandsAreLoggedOnly() throws IOException {
        Journal journal = new Journal();
        InputGate gate = new InputGate();
        gate.submit(new SimCommand.Extinguish(1));
        gate.submit(new SimCommand.RemoveFluid(2, (short) 1, 4));
        gate.submit(new SimCommand.PlaceLightSource(9, 3, 20));
        gate.submit(new SimCommand.RemoveLightSource(9));
        gate.submit(new SimCommand.SetFocus(4));

        gate.drain(1, new RecordingWriter(journal), new RecordingSink(journal));
        assertTrue(journal.entries.isEmpty(), "no paints, no events");

        // ...but all five landed in the input log.
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        gate.serialize(new DataOutputStream(bytes));
        DataInputStream in = new DataInputStream(new ByteArrayInputStream(bytes.toByteArray()));
        assertEquals(5, in.readInt(), "input log entry count");
    }

    @Test
    void paintOnWorldlessEngineIsAnError() {
        Journal journal = new Journal();
        InputGate gate = new InputGate();
        gate.submit(new SimCommand.PaintMaterial(1, (short) 1, TileForm.WALL));
        assertThrows(IllegalStateException.class,
                () -> gate.drain(1, null, new RecordingSink(journal)));
    }

    /**
     * INPT round trip: log + pending queue + pending scheduled actions
     * re-serialize byte-identically, and the loaded gate materializes the
     * remaining input exactly like the original would.
     */
    @Test
    void serializeLoadRoundTripsExactly() throws IOException {
        Journal drained = new Journal();
        InputGate original = new InputGate();
        original.submit(new SimCommand.PaintMaterial(100, (short) 7, TileForm.FLOOR));
        original.submit(new SimCommand.Ignite(200));
        original.schedule(9, new SimCommand.AddFluid(300, (short) 2, 16));
        original.drain(1, new RecordingWriter(drained), new RecordingSink(drained));
        original.submit(new SimCommand.InjectCharge(400, -50)); // still pending
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        original.serialize(new DataOutputStream(bytes));

        InputGate loaded = new InputGate();
        loaded.load(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));
        ByteArrayOutputStream reserialized = new ByteArrayOutputStream();
        loaded.serialize(new DataOutputStream(reserialized));
        assertArrayEquals(bytes.toByteArray(), reserialized.toByteArray(),
                "serialize(load(x)) must be byte-identical to x");

        // The loaded gate continues the run: pending charge at tick 2, scheduled fluid at 9.
        Journal journal = new Journal();
        RecordingWriter writer = new RecordingWriter(journal);
        RecordingSink sink = new RecordingSink(journal);
        loaded.drain(2, writer, sink);
        loaded.drain(9, writer, sink);
        assertEquals(List.of(
                "ExternalChargeApplied[cell=400, deltaCu=-50]",
                "ExternalFluidSpawned[cell=300, fluidId=2, units=16]"), journal.entries);
    }
}
