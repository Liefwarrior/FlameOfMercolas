package com.trojia.sim.engine;

import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.World;
import com.trojia.sim.world.io.TrojSav;
import com.trojia.sim.world.io.WorldLoader;

import java.io.IOException;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;

/**
 * Static factories for {@link SimulationEngine} instances — the only way to
 * obtain one. Pure factory class: no mutable statics, so any number of
 * engines coexist in one JVM (the twin-run determinism harness depends on
 * exactly this).
 *
 * <p>Boot validation (hard failures, never silent fixes): duplicate system
 * names, {@link SystemId} salt collisions, use of the reserved
 * {@code "input-gate"} identity (the InputGate's bus position).
 */
public final class Simulations {

    private Simulations() {
    }

    /**
     * Creates an engine at tick 0 over {@code world}. {@code systems} is the
     * registration list: within each phase, list order is registration order
     * and therefore event-visibility order.
     */
    public static SimulationEngine create(EngineConfig config, TickableWorld world,
            List<SimulationSystem> systems) {
        if (world == null) {
            throw new IllegalArgumentException("world must be non-null; "
                    + "use create(config, systems) for a world-less bootstrap engine");
        }
        return newEngine(config, world, systems);
    }

    /**
     * Creates a world-less engine at tick 0: clock + phase loop + event bus +
     * RNG binding only, for bootstrap systems, tools and engine tests. Paint
     * commands may not be submitted to it (there is no ChunkWriter to apply
     * them).
     */
    public static SimulationEngine create(EngineConfig config, List<SimulationSystem> systems) {
        return newEngine(config, null, systems);
    }

    /**
     * Restores an engine from a TROJSAV written by
     * {@link SimulationEngine#save}: header validation, world + section load,
     * clock reset — such that {@code run K+N ≡ save@K, load, run N}. The
     * save's {@code worldSeed} is authoritative (the seed is the only
     * persisted RNG state); {@code config.worldSeed()} is ignored on this
     * path. Raws-fingerprint validation lands with the raws pipeline (M1).
     *
     * @throws IOException on container/section corruption or a registered
     *                     system whose section is absent
     */
    public static SimulationEngine load(EngineConfig config, Path saveFile,
            List<SimulationSystem> systems) throws IOException {
        return load(config, saveFile, world -> systems);
    }

    /**
     * The factory the world-bound {@link #load(EngineConfig, Path,
     * SystemsFactory)} path builds its registration list through: real systems
     * hold the world they tick (readers, frontiers, writers bind at
     * construction), and on a load that world only exists after the WRLD
     * section is decoded.
     */
    @FunctionalInterface
    public interface SystemsFactory {

        /**
         * The registration list bound to {@code world} ({@code null} on a
         * world-less save). Same ordering semantics as
         * {@link Simulations#create(EngineConfig, TickableWorld, List)}.
         */
        List<SimulationSystem> create(World world);
    }

    /**
     * Restores an engine from a TROJSAV, building the registration list
     * <em>after</em> the world is decoded so systems can bind to it (change-log
     * readers, frontiers, the writer). Semantics otherwise identical to
     * {@link #load(EngineConfig, Path, List)}.
     */
    public static SimulationEngine load(EngineConfig config, Path saveFile,
            SystemsFactory systemsFactory) throws IOException {
        if (config == null) {
            throw new IllegalArgumentException("config must be non-null");
        }
        TrojSav save = TrojSav.read(saveFile);
        TrojSav.Header header = save.header();
        EngineConfig bound = new EngineConfig(header.worldSeed());
        TickableWorld world = save.hasSection(TrojSav.WRLD) ? new WorldLoader().load(save) : null;
        SimulationSystem[] ordered = systemsFactory.create(world)
                .toArray(new SimulationSystem[0]);
        checkIdentities(ordered);
        PhasedSimulationEngine engine = new PhasedSimulationEngine(bound, world, ordered);
        engine.restore(save);
        return engine;
    }

    private static SimulationEngine newEngine(EngineConfig config, TickableWorld world,
            List<SimulationSystem> systems) {
        if (config == null) {
            throw new IllegalArgumentException("config must be non-null");
        }
        SimulationSystem[] ordered = systems.toArray(new SimulationSystem[0]);
        checkIdentities(ordered);
        return new PhasedSimulationEngine(config, world, ordered);
    }

    /** TROJSAV section ids owned by the container/engine, never by a system. */
    private static final String[] RESERVED_SECTION_IDS = {
            TrojSav.META, TrojSav.INPT, TrojSav.EVNT, TrojSav.WRLD, "AETH"
    };

    /**
     * Rejects duplicate names, salt collisions and TROJSAV section-id
     * collisions across the registration list, including the engine's
     * reserved InputGate identity and the container's reserved section ids.
     */
    private static void checkIdentities(SimulationSystem[] systems) {
        String[] names = new String[systems.length + 1];
        long[] salts = new long[systems.length + 1];
        String[] sections = new String[systems.length + RESERVED_SECTION_IDS.length];
        names[0] = PhasedSimulationEngine.INPUT_GATE_ID.name();
        salts[0] = PhasedSimulationEngine.INPUT_GATE_ID.salt();
        System.arraycopy(RESERVED_SECTION_IDS, 0, sections, 0, RESERVED_SECTION_IDS.length);
        for (int i = 0; i < systems.length; i++) {
            SystemId id = systems[i].id();
            names[i + 1] = id.name();
            salts[i + 1] = id.salt();
            sections[RESERVED_SECTION_IDS.length + i] = id.sectionId();
        }
        Arrays.sort(names);
        Arrays.sort(salts);
        Arrays.sort(sections);
        for (int i = 1; i < names.length; i++) {
            if (names[i].equals(names[i - 1])) {
                throw new IllegalArgumentException("duplicate system name: " + names[i]);
            }
            if (salts[i] == salts[i - 1]) {
                throw new IllegalArgumentException(
                        "SystemId salt collision: 0x" + Long.toHexString(salts[i]));
            }
        }
        for (int i = 1; i < sections.length; i++) {
            if (sections[i].equals(sections[i - 1])) {
                throw new IllegalArgumentException("TROJSAV section-id collision: '"
                        + sections[i] + "' (rename one system — ids are the first four "
                        + "letters/digits of the name, upper-cased)");
            }
        }
    }
}
