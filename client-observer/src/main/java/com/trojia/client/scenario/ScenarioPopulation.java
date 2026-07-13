package com.trojia.client.scenario;

import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorsSystem;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.JobRegistry;

/**
 * The read surface a populated fixture exposes to {@code ObserverApp}'s boot/render path:
 * one ticking {@link ActorsSystem} plus the registries its renderers/inspector read. Both
 * populated fixtures satisfy it — {@link DocksPopulation} directly, and the pre-existing
 * {@link CompoundBlockPopulation} via {@link #of(CompoundBlockPopulation)} (an adapter, so
 * that class stays untouched by the docks milestone).
 */
public interface ScenarioPopulation {

    ActorsSystem system();

    ActorRegistry registry();

    HomeRegistry homes();

    RelationshipRegistry relationships();

    ItemsLiteRegistry items();

    JobRegistry jobs();

    /** See {@link CompoundBlockPopulation#trackedGroundMoverId()} — the smoke proof's mover. */
    int trackedGroundMoverId();

    /** Adapts the compound-block population (kept untouched) to this surface. */
    static ScenarioPopulation of(CompoundBlockPopulation population) {
        return new ScenarioPopulation() {
            @Override
            public ActorsSystem system() {
                return population.system();
            }

            @Override
            public ActorRegistry registry() {
                return population.registry();
            }

            @Override
            public HomeRegistry homes() {
                return population.homes();
            }

            @Override
            public RelationshipRegistry relationships() {
                return population.relationships();
            }

            @Override
            public ItemsLiteRegistry items() {
                return population.items();
            }

            @Override
            public JobRegistry jobs() {
                return population.jobs();
            }

            @Override
            public int trackedGroundMoverId() {
                return population.trackedGroundMoverId();
            }
        };
    }
}
