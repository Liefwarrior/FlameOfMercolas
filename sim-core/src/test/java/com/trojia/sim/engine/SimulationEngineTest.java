package com.trojia.sim.engine;

import com.trojia.sim.event.ExternalIgnition;
import com.trojia.sim.event.EventReader;
import com.trojia.sim.event.TileIgnited;
import com.trojia.sim.world.ChunkView;
import com.trojia.sim.world.ChunkWriter;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.LaneId;
import com.trojia.sim.world.LaneRegistry;
import com.trojia.sim.world.OverlayId;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.WorldConfig;
import com.trojia.sim.world.change.ChangeLogs;
import com.trojia.sim.world.change.ChunkRevisions;
import com.trojia.sim.world.io.WorldHasher;
import org.junit.jupiter.api.Test;

import java.io.DataInput;
import java.io.DataOutput;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Contract tests for the F1 engine: phase-ordered execution, clock semantics,
 * boot-time identity checks, per-system RNG determinism, world begin/commit
 * bracketing, InputGate materialization and the §4 one-lap event visibility
 * window — all against fake systems and a fake TickableWorld. Grows into the
 * twin-run golden harness.
 */
final class SimulationEngineTest {

    /** A stateless system that journals its invocations. */
    private static class RecordingSystem implements SimulationSystem {
        private final SystemId id;
        private final TickPhase phase;
        final List<String> journal;

        RecordingSystem(String name, TickPhase phase, List<String> journal) {
            this.id = SystemId.of(name);
            this.phase = phase;
            this.journal = journal;
        }

        @Override
        public SystemId id() {
            return id;
        }

        @Override
        public TickPhase phase() {
            return phase;
        }

        @Override
        public void tick(TickContext context) {
            journal.add(id.name() + "@" + context.tick());
        }

        @Override
        public void serialize(DataOutput out) {
        }

        @Override
        public void load(DataInput in) {
        }

        @Override
        public void hashInto(WorldHasher.Sink sink) {
        }
    }

    /** A ChunkWriter journaling paints; everything is applied. */
    private static final class RecordingWriter implements ChunkWriter {
        final List<String> journal;

        RecordingWriter(List<String> journal) {
            this.journal = journal;
        }

        @Override
        public int setMaterial(int packedPos, short materialId) {
            return APPLIED;
        }

        @Override
        public int setMaterialAndForm(int packedPos, short materialId, TileForm form) {
            journal.add("paint(" + packedPos + "," + materialId + "," + form + ")");
            return APPLIED;
        }

        @Override
        public int setForm(int packedPos, TileForm form) {
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

    /**
     * A fake TickableWorld: journals begin/commit and serves a recording
     * writer; everything else is out of scope for engine tests.
     */
    private static final class FakeWorld implements TickableWorld {
        final List<String> journal;
        final RecordingWriter writer;

        FakeWorld(List<String> journal) {
            this.journal = journal;
            this.writer = new RecordingWriter(journal);
        }

        @Override
        public void beginTick(long tick) {
            journal.add("begin@" + tick);
        }

        @Override
        public void commitTick(long tick) {
            journal.add("commit@" + tick);
        }

        @Override
        public ChunkWriter writer() {
            return writer;
        }

        @Override
        public WorldConfig config() {
            throw new UnsupportedOperationException("not needed by engine tests");
        }

        @Override
        public Coords coords() {
            throw new UnsupportedOperationException("not needed by engine tests");
        }

        @Override
        public LaneRegistry lanes() {
            throw new UnsupportedOperationException("not needed by engine tests");
        }

        @Override
        public ChunkView chunk(int chunkIndex) {
            throw new UnsupportedOperationException("not needed by engine tests");
        }

        @Override
        public TileCursor cursor() {
            throw new UnsupportedOperationException("not needed by engine tests");
        }

        @Override
        public ChangeLogs changeLogs() {
            throw new UnsupportedOperationException("not needed by engine tests");
        }

        @Override
        public ChunkRevisions revisions() {
            throw new UnsupportedOperationException("not needed by engine tests");
        }
    }

    @Test
    void systemsRunInPhaseOrderNotRegistrationOrder() {
        List<String> journal = new ArrayList<>();
        // Registered "backwards": the THERMAL system first, the FLUIDS system second.
        SimulationEngine engine = Simulations.create(new EngineConfig(42L), List.of(
                new RecordingSystem("thermal", TickPhase.THERMAL, journal),
                new RecordingSystem("fluids", TickPhase.FLUIDS, journal)));

        engine.step(2);

        assertEquals(2, engine.currentTick());
        assertEquals(List.of("fluids@1", "thermal@1", "fluids@2", "thermal@2"), journal);
    }

    @Test
    void registrationOrderBreaksTiesWithinAPhase() {
        List<String> journal = new ArrayList<>();
        SimulationEngine engine = Simulations.create(new EngineConfig(42L), List.of(
                new RecordingSystem("first", TickPhase.REACTIONS, journal),
                new RecordingSystem("second", TickPhase.REACTIONS, journal)));

        engine.tick();

        assertEquals(List.of("first@1", "second@1"), journal);
    }

    @Test
    void duplicateSystemNamesAreABootFailure() {
        List<String> journal = new ArrayList<>();
        assertThrows(IllegalArgumentException.class, () -> Simulations.create(
                new EngineConfig(42L), List.of(
                        new RecordingSystem("dupe", TickPhase.FLUIDS, journal),
                        new RecordingSystem("dupe", TickPhase.THERMAL, journal))));
    }

    @Test
    void inputGateIdentityIsReserved() {
        List<String> journal = new ArrayList<>();
        assertThrows(IllegalArgumentException.class, () -> Simulations.create(
                new EngineConfig(42L), List.of(
                        new RecordingSystem("input-gate", TickPhase.FLUIDS, journal))));
    }

    @Test
    void contextRngIsDeterministicAcrossTwinEngines() {
        List<Long> drawsA = new ArrayList<>();
        List<Long> drawsB = new ArrayList<>();
        SimulationEngine a = Simulations.create(new EngineConfig(7L),
                List.of(drawingSystem(drawsA)));
        SimulationEngine b = Simulations.create(new EngineConfig(7L),
                List.of(drawingSystem(drawsB)));

        a.step(3);
        b.step(3);

        assertEquals(drawsA, drawsB);
        assertEquals(3, drawsA.size());
    }

    private static SimulationSystem drawingSystem(List<Long> draws) {
        return new SimulationSystem() {
            private final SystemId id = SystemId.of("drawer");

            @Override
            public SystemId id() {
                return id;
            }

            @Override
            public TickPhase phase() {
                return TickPhase.FLUIDS;
            }

            @Override
            public void tick(TickContext context) {
                draws.add(context.rng().draw(0x123456L, 0));
            }

            @Override
            public void serialize(DataOutput out) {
            }

            @Override
            public void load(DataInput in) {
            }

            @Override
            public void hashInto(WorldHasher.Sink sink) {
            }
        };
    }

    @Test
    void clockRunsAtHundredMillisPerTick() {
        TickClock clock = new TickClock();
        assertEquals(0, clock.currentTick());
        clock.advance();
        clock.advance();
        assertEquals(2, clock.currentTick());
        assertEquals(200, clock.simTimeMillis());
        assertEquals(100, TickClock.MILLIS_PER_TICK);
    }

    @Test
    void worldBeginAndCommitBracketEveryTick() {
        List<String> journal = new ArrayList<>();
        SimulationEngine engine = Simulations.create(new EngineConfig(1L),
                new FakeWorld(journal),
                List.of(new RecordingSystem("fluids", TickPhase.FLUIDS, journal)));

        engine.step(2);

        assertEquals(List.of(
                "begin@1", "fluids@1", "commit@1",
                "begin@2", "fluids@2", "commit@2"), journal);
    }

    /** Submitted paints materialize via the world's ChunkWriter at TICK_BEGIN. */
    @Test
    void submittedPaintsApplyThroughTheChunkWriterAtTickBegin() {
        List<String> journal = new ArrayList<>();
        SimulationEngine engine = Simulations.create(new EngineConfig(1L),
                new FakeWorld(journal),
                List.of(new RecordingSystem("fluids", TickPhase.FLUIDS, journal)));

        engine.submit(new SimCommand.PaintMaterial(77, (short) 3, TileForm.WALL));
        engine.submit(new SimCommand.ClearTile(78));
        engine.tick();

        assertEquals(List.of(
                "begin@1",
                "paint(77,3,WALL)",
                "paint(78,0,OPEN)",
                "fluids@1",
                "commit@1"), journal);
    }

    /** A consumer system reading a topic it (pre-)registered at boot. */
    private static final class ConsumingSystem extends RecordingSystem {
        private final Class<? extends com.trojia.sim.event.SimEvent> topic;

        ConsumingSystem(String name, TickPhase phase,
                Class<? extends com.trojia.sim.event.SimEvent> topic, List<String> journal) {
            super(name, phase, journal);
            this.topic = topic;
        }

        @Override
        public void tick(TickContext context) {
            EventReader<? extends com.trojia.sim.event.SimEvent> reader = context.events(topic);
            while (reader.hasNext()) {
                journal.add(id().name() + " got " + reader.next() + "@" + context.tick());
            }
        }
    }

    /** Quantity commands become External* events consumed the same tick. */
    @Test
    void submittedIgnitionIsConsumedAsAnEventSameTick() {
        List<String> journal = new ArrayList<>();
        SimulationEngine engine = Simulations.create(new EngineConfig(1L), List.of(
                new ConsumingSystem("fire", TickPhase.THERMAL, ExternalIgnition.class, journal)));

        engine.submit(new SimCommand.Ignite(555));
        engine.step(2); // tick 2 must not re-deliver

        assertEquals(List.of("fire got ExternalIgnition[cell=555]@1"), journal);
    }

    /**
     * The §4 visibility rule end-to-end: an event emitted at (THERMAL, 0) of
     * tick 1 is seen same tick by the REACTIONS consumer (after the emitter),
     * next tick by the FLUIDS consumer (at-or-before the emitter), and never
     * again by either (retired after one lap).
     */
    @Test
    void eventVisibilityIsOneLapAcrossSystems() {
        List<String> journal = new ArrayList<>();
        SimulationSystem emitter = new SimulationSystem() {
            private final SystemId id = SystemId.of("emitter");

            @Override
            public SystemId id() {
                return id;
            }

            @Override
            public TickPhase phase() {
                return TickPhase.THERMAL;
            }

            @Override
            public void tick(TickContext context) {
                if (context.tick() == 1) {
                    context.emit().emit(new TileIgnited(9));
                }
            }

            @Override
            public void serialize(DataOutput out) {
            }

            @Override
            public void load(DataInput in) {
            }

            @Override
            public void hashInto(WorldHasher.Sink sink) {
            }
        };
        SimulationEngine engine = Simulations.create(new EngineConfig(1L), List.of(
                new ConsumingSystem("early", TickPhase.FLUIDS, TileIgnited.class, journal),
                emitter,
                new ConsumingSystem("late", TickPhase.REACTIONS, TileIgnited.class, journal)));

        engine.step(3);

        assertEquals(List.of(
                "late got TileIgnited[cell=9]@1",   // after (THERMAL,0): same tick
                "early got TileIgnited[cell=9]@2"), // before (THERMAL,0): next tick, then retired
                journal);
    }

    /** Twin engines with interleaved ticks share no state (multi-engine per JVM). */
    @Test
    void twinEnginesAreFullyIsolated() {
        List<String> journalA = new ArrayList<>();
        List<String> journalB = new ArrayList<>();
        SimulationEngine a = Simulations.create(new EngineConfig(1L), List.of(
                new ConsumingSystem("fire", TickPhase.THERMAL, ExternalIgnition.class, journalA)));
        SimulationEngine b = Simulations.create(new EngineConfig(1L), List.of(
                new ConsumingSystem("fire", TickPhase.THERMAL, ExternalIgnition.class, journalB)));

        a.submit(new SimCommand.Ignite(1));
        a.tick();
        b.tick();
        a.tick();
        b.submit(new SimCommand.Ignite(2));
        b.tick();

        assertEquals(List.of("fire got ExternalIgnition[cell=1]@1"), journalA);
        assertEquals(List.of("fire got ExternalIgnition[cell=2]@2"), journalB);
        assertEquals(2, a.currentTick());
        assertEquals(2, b.currentTick());
    }

    /** The engine reports a per-phase profile for the last completed tick. */
    @Test
    void inspectReportsTheLastCompletedTick() {
        SimulationEngine engine = Simulations.create(new EngineConfig(1L), List.of());
        assertEquals(0, engine.inspect().tick());
        engine.step(3);
        assertEquals(3, engine.inspect().tick());
        assertTrue(engine.inspect().totalNanos() >= 0);
    }
}
