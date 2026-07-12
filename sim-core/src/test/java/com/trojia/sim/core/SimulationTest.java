package com.trojia.sim.core;

import com.trojia.sim.time.TickClock;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

final class SimulationTest {

    /** A system that records the order and tick of every invocation. */
    private static final class RecordingSystem implements SimulationSystem {
        private final String id;
        private final List<String> journal;

        RecordingSystem(String id, List<String> journal) {
            this.id = id;
            this.journal = journal;
        }

        @Override
        public String id() {
            return id;
        }

        @Override
        public void tick(SimulationContext context) {
            journal.add(id + "@" + context.currentTick());
        }
    }

    @Test
    void tickAdvancesClockAndRunsSystemsInPipelineOrder() {
        List<String> journal = new ArrayList<>();
        Simulation simulation = new Simulation(
                SimulationPipeline.of(
                        new RecordingSystem("alpha", journal),
                        new RecordingSystem("beta", journal)),
                new TickClock());

        simulation.tick();
        simulation.tick();

        assertEquals(2, simulation.clock().currentTick());
        assertEquals(List.of("alpha@1", "beta@1", "alpha@2", "beta@2"), journal);
    }

    @Test
    void pipelineRejectsDuplicateSystemIds() {
        List<String> journal = new ArrayList<>();
        assertThrows(IllegalArgumentException.class, () -> SimulationPipeline.of(
                new RecordingSystem("dupe", journal),
                new RecordingSystem("dupe", journal)));
    }
}
