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

    /**
     * The bake-side identity table (S1 NameForge): names/epithets/bios per ActorId for the
     * inspector's nameplate and the headless roster listing. Default is the empty table —
     * only fixtures that run a NameForge pass (the docks) override it; identity is scenario
     * data, so exposing it here adds nothing to the sim surface.
     */
    default IdentityRegistry identity() {
        return IdentityRegistry.EMPTY;
    }

    /**
     * The bake-bound quest table (S3 "The Vanished Clerk") — the client journal and the
     * talk surface's quest keys read titles/objectives/party symbols here. Every system
     * carries one ({@code QuestRegistry.EMPTY} where no quests are baked), so the default
     * simply reads it off {@link #system()}.
     */
    default com.trojia.sim.actor.quest.QuestRegistry questRegistry() {
        return system().questRegistry();
    }

    /**
     * The baked cross-z connector table (S4 "the climb") — the observer's climb keys read
     * it to resolve which stair/ramp destination the played actor's Up/Down press arms
     * ({@code Actor.setPlayerMoveTarget} with a cross-z cell; the sim's own
     * {@code tryStepVertical} guard re-validates the pair). Immutable baked config, no sim
     * state — reading it can never move the tick hash. Default is {@code EMPTY} (single-z
     * fixtures: the climb keys toast their refusal).
     */
    default com.trojia.sim.actor.ZLinkTable zLinks() {
        return com.trojia.sim.actor.ZLinkTable.EMPTY;
    }

    /**
     * The soul's speakable ask-topics (S4 "the rumor verb"), or {@code null} for a forged
     * non-notable — the read the observer's talk-topics surface makes before calling
     * {@code BarkSelector.selectAsk}. Default: no topics anywhere (fixtures without a
     * notables bake; the talk panel simply offers no ask rows).
     */
    default AskTopicsBake.Topics askTopicsOf(int actorId) {
        return null;
    }

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
