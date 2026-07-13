package com.trojia.sim.engine;

import com.trojia.sim.bubble.ActiveBubble;
import com.trojia.sim.event.EventReader;
import com.trojia.sim.event.EventSink;
import com.trojia.sim.event.Events;
import com.trojia.sim.event.SimEvent;
import com.trojia.sim.random.CounterRandomSource;
import com.trojia.sim.random.RandomSource;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.World;
import com.trojia.sim.world.io.ChunkCodec;
import com.trojia.sim.world.io.TrojSav;
import com.trojia.sim.world.io.WorldSaver;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInput;
import java.io.DataInputStream;
import java.io.DataOutput;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.file.Path;
import java.util.List;

/**
 * The engine implementation behind {@link Simulations}: the deterministic
 * tick loop of ARCHITECTURE.md §4. Owns the clock, the phase-ordered system
 * execution (stable registration order within a phase), the event bus
 * (advanced to each system's {@code (tick, phase, regIndex)} position before
 * it runs; lap retired at TICK_END), the InputGate (drained at position
 * {@code (TICK_BEGIN, 0)} — the reserved gate slot) and per-system counter-RNG
 * binding. All state is instance-owned: any number of engines coexist in one
 * JVM.
 *
 * <p>The bubble seam serves the engine-default {@link AllConcreteBubble}
 * (whole-map-active) until the bubble module's manager is wired in (F5).
 */
final class PhasedSimulationEngine implements SimulationEngine {

    private static final TickPhase[] PHASES = TickPhase.values();

    /**
     * The reserved identity of the InputGate's bus position
     * {@code (TICK_BEGIN, regIndex 0)}; boot rejects user systems named
     * {@code "input-gate"}.
     */
    static final SystemId INPUT_GATE_ID = SystemId.of("input-gate");

    /**
     * Raws fingerprint written into save headers until the raws pipeline
     * lands (M1) and mints real fingerprints.
     */
    private static final long RAWS_FINGERPRINT_NONE = 0L;

    private final EngineConfig config;
    private final TickableWorld world;
    private final TickClock clock = new TickClock();
    /** Registration list, stably sorted by phase ordinal (registration order within a phase). */
    private final SimulationSystem[] systems;
    /** Within-phase registration index per system (TICK_BEGIN indices start at 1 — 0 is the gate). */
    private final int[] regIndexes;
    private final SystemContext[] contexts;
    private final Events events;
    private final InputGate inputGate = new InputGate();
    private final EventSink gateSink;
    private final ActiveBubble bubble = new AllConcreteBubble();
    private TickProfile lastProfile = TickProfile.empty();

    PhasedSimulationEngine(EngineConfig config, TickableWorld world,
            SimulationSystem[] registration) {
        this.config = config;
        this.world = world;
        this.systems = sortedByPhase(registration);
        this.events = Events.create();
        this.gateSink = events.sink(INPUT_GATE_ID, TickPhase.TICK_BEGIN, 0);
        this.regIndexes = new int[systems.length];
        this.contexts = new SystemContext[systems.length];

        List<Class<? extends SimEvent>> topics = Events.topics();
        int[] counters = new int[PHASES.length];
        counters[TickPhase.TICK_BEGIN.ordinal()] = 1; // regIndex 0 of TICK_BEGIN is the gate
        for (int i = 0; i < systems.length; i++) {
            SimulationSystem system = systems[i];
            TickPhase phase = system.phase();
            int regIndex = counters[phase.ordinal()]++;
            regIndexes[i] = regIndex;
            EventSink sink = events.sink(system.id(), phase, regIndex);
            EventReader<?>[] readers = new EventReader<?>[topics.size()];
            for (int t = 0; t < readers.length; t++) {
                readers[t] = events.reader(system.id(), phase, regIndex, topics.get(t));
            }
            contexts[i] = new SystemContext(phase,
                    CounterRandomSource.of(config.worldSeed(), system.id().salt()),
                    sink, readers);
        }
        events.seal();
    }

    /** Stable sort by phase ordinal — preserves registration order within a phase. */
    private static SimulationSystem[] sortedByPhase(SimulationSystem[] registration) {
        SimulationSystem[] sorted = registration.clone();
        for (int i = 1; i < sorted.length; i++) {
            SimulationSystem s = sorted[i];
            int j = i - 1;
            while (j >= 0 && sorted[j].phase().ordinal() > s.phase().ordinal()) {
                sorted[j + 1] = sorted[j];
                j--;
            }
            sorted[j + 1] = s;
        }
        return sorted;
    }

    @Override
    public long currentTick() {
        return clock.currentTick();
    }

    @Override
    public World world() {
        return world;
    }

    @Override
    public void tick() {
        clock.advance();
        long tick = clock.currentTick();
        long[] phaseNanos = new long[PHASES.length];
        long tickStart = System.nanoTime();

        for (SystemContext context : contexts) {
            context.rng.beginTick(tick);
        }
        if (world != null) {
            world.beginTick(tick);
        }
        int next = 0;
        for (TickPhase phase : PHASES) {
            long phaseStart = System.nanoTime();
            if (phase == TickPhase.TICK_BEGIN) {
                events.advanceTo(tick, TickPhase.TICK_BEGIN, 0);
                inputGate.drain(tick, world == null ? null : world.writer(), gateSink);
            }
            while (next < systems.length && systems[next].phase() == phase) {
                events.advanceTo(tick, phase, regIndexes[next]);
                systems[next].tick(contexts[next]);
                next++;
            }
            if (phase == TickPhase.TICK_END) {
                events.retireLap(tick); // one-lap retirement, before the world commit
            }
            phaseNanos[phase.ordinal()] = System.nanoTime() - phaseStart;
        }
        if (world != null) {
            world.commitTick(tick);
        }
        lastProfile = new TickProfile(tick, System.nanoTime() - tickStart, phaseNanos);
    }

    @Override
    public void step(int ticks) {
        if (ticks < 0) {
            throw new IllegalArgumentException("ticks must be >= 0: " + ticks);
        }
        for (int i = 0; i < ticks; i++) {
            tick();
        }
    }

    @Override
    public void submit(SimCommand command) {
        inputGate.submit(command);
    }

    /**
     * {@inheritDoc}
     *
     * <p>System sections are keyed by {@link SystemId#sectionId()} (collision
     * checked at boot); the header's raws fingerprint is {@code 0} until the
     * raws pipeline lands (M1).
     */
    @Override
    public void save(Path file) throws IOException {
        TrojSav save = TrojSav.create(new TrojSav.Header(
                TrojSav.FORMAT_VERSION, config.worldSeed(), clock.currentTick(),
                RAWS_FINGERPRINT_NONE));
        save.putSection(TrojSav.INPT, toBytes(inputGate::serialize));
        save.putSection(TrojSav.EVNT, toBytes(events::serializeCarryOver));
        save.putSection(TrojSav.AETH, new byte[0]); // §9 reserved-empty section
        if (world != null) {
            WorldSaver saver = new WorldSaver(new ChunkCodec(world.lanes()));
            save.putSection(TrojSav.META, saver.writeMetaSection(world));
            save.putSection(TrojSav.WRLD, saver.writeWorldSection(world));
            save.putSection(TrojSav.CHNG, toBytes(world.changeLogs()::serialize));
        }
        for (SimulationSystem system : systems) {
            save.putSection(system.id().sectionId(), toBytes(system::serialize));
        }
        save.writeTo(file);
    }

    /**
     * Load-path restore (engine already booted over the loaded world): resets
     * the clock to the saved tick, then restores the input log, the event
     * carry-over lap, the change-log carry-over (CHNG — retained backlog +
     * reader cursors, against the readers registered at boot) and every system
     * section (a registered system whose section is absent is a hard fail).
     */
    void restore(TrojSav save) throws IOException {
        clock.resetTo(save.header().tick());
        inputGate.load(dataInput(save.section(TrojSav.INPT)));
        events.loadCarryOver(dataInput(save.section(TrojSav.EVNT)));
        if (world != null) {
            world.changeLogs().load(dataInput(save.section(TrojSav.CHNG)));
        }
        for (SimulationSystem system : systems) {
            system.load(dataInput(save.section(system.id().sectionId())));
        }
    }

    @Override
    public TickProfile inspect() {
        return lastProfile;
    }

    /** A serializer writing one save section's bytes. */
    private interface SectionWriter {
        void write(DataOutput out) throws IOException;
    }

    private static byte[] toBytes(SectionWriter writer) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        writer.write(new DataOutputStream(bytes));
        return bytes.toByteArray();
    }

    private static DataInput dataInput(byte[] bytes) {
        return new DataInputStream(new ByteArrayInputStream(bytes));
    }

    /** Per-system context; the engine rebinds the RNG each tick. */
    private final class SystemContext implements TickContext {

        private final TickPhase phase;
        private final CounterRandomSource rng;
        private final EventSink sink;
        /** Readers pre-registered at boot, indexed by canonical topic id. */
        private final EventReader<?>[] readers;

        SystemContext(TickPhase phase, CounterRandomSource rng, EventSink sink,
                EventReader<?>[] readers) {
            this.phase = phase;
            this.rng = rng;
            this.sink = sink;
            this.readers = readers;
        }

        @Override
        public long tick() {
            return clock.currentTick();
        }

        @Override
        public TickPhase phase() {
            return phase;
        }

        @Override
        public RandomSource rng() {
            return rng;
        }

        @Override
        public <E extends SimEvent> EventReader<E> events(Class<E> topic) {
            for (EventReader<?> reader : readers) {
                if (reader.topic() == topic) {
                    @SuppressWarnings("unchecked")
                    EventReader<E> typed = (EventReader<E>) reader;
                    return typed;
                }
            }
            throw new IllegalStateException("no reader registered for topic "
                    + topic.getName());
        }

        @Override
        public EventSink emit() {
            return sink;
        }

        @Override
        public ActiveBubble bubble() {
            return bubble;
        }
    }
}
