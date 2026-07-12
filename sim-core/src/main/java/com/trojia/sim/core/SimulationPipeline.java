package com.trojia.sim.core;

import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.HashSet;

/**
 * The immutable, explicitly ordered list of systems that make up one tick.
 *
 * <p>Order is fixed at construction and is part of the determinism contract:
 * changing it invalidates golden-master hashes and is a save-version event.
 */
public final class SimulationPipeline {

    private final List<SimulationSystem> systems;

    private SimulationPipeline(List<SimulationSystem> systems) {
        this.systems = systems;
    }

    public static SimulationPipeline of(SimulationSystem... systems) {
        List<SimulationSystem> ordered = List.of(systems);
        Set<String> ids = new HashSet<>();
        for (SimulationSystem system : ordered) {
            Objects.requireNonNull(system.id(), "system id");
            if (!ids.add(system.id())) {
                throw new IllegalArgumentException("Duplicate system id: " + system.id());
            }
        }
        return new SimulationPipeline(ordered);
    }

    /** Systems in tick order; the returned list is immutable. */
    public List<SimulationSystem> systems() {
        return systems;
    }
}
