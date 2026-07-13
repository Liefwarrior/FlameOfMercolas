package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.inspect.JobDisplay;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.Home;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.RelationshipEdge;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.PackedPos;

import java.util.List;

/**
 * GL-free legibility listing for the Trojian-Compound population (ACTORS-SPEC.md §7.2 shape,
 * the "PROVE IT" surface): loads the baked {@code compound_block} world, spawns the
 * deterministic {@link CompoundBlockPopulation} onto it, ticks the real {@code ACTORS} phase
 * through a {@link SimulationDriver} (the observer's own driver, run headless via
 * {@code requestStep}), and prints one row per actor — id / type / true+presented job /
 * home / position / goal state / needs — so the whole 40-actor roster is inspectable without
 * a GL window. The displaced, REST-depleted movers are reported before and after ticking to
 * make {@code RETURN_HOME} movement visible as concrete coordinate deltas.
 *
 * <p>Run: {@code ./gradlew.bat :client-observer:runCompoundActors --args="--ticks 600"}.
 */
public final class CompoundBlockActorsMain {

    private CompoundBlockActorsMain() {
    }

    public static void main(String[] args) {
        int ticks = parseTicks(args, 600);

        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadCompoundBlock();
        CompoundBlockPopulation population = CompoundBlockPopulation.build(loaded.worldSeed());

        ActorRegistry registry = population.registry();
        HomeRegistry homes = population.homes();
        RelationshipRegistry relationships = population.relationships();
        JobRegistry jobs = population.jobs();

        // Capture the movers' spawn cells before any tick so RETURN_HOME movement shows as
        // a before/after coordinate delta.
        List<Integer> movers = population.moverIds();

        System.out.println("compound_block: spawned " + registry.size()
                + " actors over the baked world; ticking " + ticks + " ACTORS-phase ticks.");
        System.out.println("movers (displaced at spawn, REST depleted -> RETURN_HOME):");
        for (int id : movers) {
            Actor a = registry.get(id);
            Home home = homes.get(a.homeId());
            System.out.println("  before: actor#" + id + " cell=" + xyz(a.cell())
                    + " home=" + xyz(home.homeCell()));
        }

        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));
        for (int i = 0; i < ticks; i++) {
            driver.requestStep();
        }

        printRoster(registry, homes, jobs);
        printMoversAfter(population, driver.currentTick(), movers);
        printGraphSample(homes, relationships);
        System.out.println("items minted (placeholder ids + quantities, §11.2): "
                + population.items().size());
    }

    private static void printRoster(ActorRegistry registry, HomeRegistry homes, JobRegistry jobs) {
        System.out.println();
        System.out.printf("%-3s %-22s %-18s %-18s %-5s %-13s %-13s %-10s %s%n",
                "id", "type", "job(true)", "presents", "home", "homeCell", "position",
                "goalState", "needs(H/R/C/S/D)");
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            Job job = actor.jobOrdinal() >= 0 ? jobs.get(actor.jobOrdinal()) : null;
            String trueJob = JobDisplay.trueJobId(job);
            String presented = JobDisplay.presentedJobId(job);
            String cover = JobDisplay.isSecret(job) ? "  <-- secret" : "";
            Home home = homes.get(actor.homeId());
            short[] needs = actor.needsSnapshot();
            System.out.printf("%-3d %-22s %-18s %-18s %-5d %-13s %-13s %-10s %d/%d/%d/%d/%d%s%n",
                    actor.id(), actor.typeId().key(), trueJob, presented, actor.homeId(),
                    xyz(home.homeCell()), xyz(actor.cell()), actor.goalState(),
                    needs[0], needs[1], needs[2], needs[3], needs[4], cover);
        }
        System.out.println("homes baked: " + homes.size());
    }

    private static void printMoversAfter(CompoundBlockPopulation population, long tick,
            List<Integer> movers) {
        System.out.println();
        System.out.println("movers after " + tick + " ticks:");
        for (int id : movers) {
            Actor actor = population.registry().get(id);
            Home home = population.homes().get(actor.homeId());
            boolean arrived = actor.cell() == home.homeCell();
            System.out.println("  actor#" + id + " cell=" + xyz(actor.cell())
                    + " home=" + xyz(home.homeCell()) + " arrivedHome=" + arrived
                    + " lastReason=" + policyName(actor));
        }
    }

    private static void printGraphSample(HomeRegistry homes, RelationshipRegistry relationships) {
        System.out.println();
        System.out.println("relationships baked: " + relationships.size()
                + " (HOUSEHOLD cliques per unit, EMPLOYER edges per business/mission)");
        int shown = 0;
        for (int i = 0; i < relationships.size() && shown < 12; i++, shown++) {
            RelationshipEdge edge = relationships.get(i);
            System.out.println("  " + edge.fromId() + " --" + edge.kind() + "--> " + edge.toId());
        }
        if (relationships.size() > shown) {
            System.out.println("  ... (" + (relationships.size() - shown) + " more)");
        }
    }

    private static String policyName(Actor actor) {
        return actor.lastReasonCode() == null ? "-" : actor.lastReasonCode().name();
    }

    private static String xyz(int cell) {
        return "(" + PackedPos.x(cell) + "," + PackedPos.y(cell) + "," + PackedPos.z(cell) + ")";
    }

    private static int parseTicks(String[] args, int fallback) {
        for (int i = 0; i < args.length - 1; i++) {
            if ("--ticks".equals(args[i])) {
                return Integer.parseInt(args[i + 1]);
            }
        }
        return fallback;
    }
}
