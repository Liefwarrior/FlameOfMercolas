package com.trojia.headless;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRawsLoader;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.ActorTypeStatsTable;
import com.trojia.sim.actor.ActorTypes;
import com.trojia.sim.actor.ActorsSystem;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.HouseholdFormer;
import com.trojia.sim.actor.HouseholdRaws;
import com.trojia.sim.actor.HouseholdRawsLoader;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobBinder;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.AnimalActor;
import com.trojia.sim.actor.type.AnimalKeeper;
import com.trojia.sim.actor.type.DiscipleOfTheFlame;
import com.trojia.sim.actor.type.FeralActor;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.PriestOfTheFlame;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Shopkeeper;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.engine.EngineConfig;
import com.trojia.sim.engine.SimulationEngine;
import com.trojia.sim.engine.Simulations;
import com.trojia.sim.world.PackedPos;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * F2.5 foundation demo: builds a small Docks-flavored actor population over
 * the real engine (world-less bootstrap — actors never touch world lanes),
 * runs a batch of ticks, and prints a legibility table.
 *
 * <p>Exercises: the real {@code ACTORS} {@link com.trojia.sim.engine.TickPhase}
 * wired into {@link com.trojia.sim.engine.SimulationEngine}; the Job binder
 * (fail-fast 1:1); the household bake pass (a shared multi-member Home plus
 * two singly-housed actors); an EMPLOYER edge (Shopkeeper &rarr; hired Serf)
 * and a free one (Priest &rarr; Disciple); a stand-in "Wielder" (one Serf's
 * job overridden to {@code flame_of_merc}) close enough to trigger
 * {@code DEFER_WIELDER} on nearby civic actors; and needs decay driving
 * {@code RETURN_HOME} once REST crosses LOW.
 */
public final class ActorsDemoMain {

    private static final long WORLD_SEED = 0xF1A4E0F_5EEDL;

    private ActorsDemoMain() {
    }

    public static void main(String[] args) {
        Path rawsRoot = locateRawsRoot();
        ActorTypeStatsTable typeStats = ActorRawsLoader.load(rawsRoot.resolve("actors"));
        HouseholdRaws householdRaws =
                HouseholdRawsLoader.load(rawsRoot.resolve("actors").resolve("household.json"));
        JobRegistry jobs = JobBinder.bind(rawsRoot.resolve("jobs").resolve("jobs.json"),
                ActorTypes.allTypeIds());

        ActorRegistry registry = new ActorRegistry();
        HomeRegistry homes = new HomeRegistry();
        RelationshipRegistry relationships = new RelationshipRegistry();
        ItemsLiteRegistry items = new ItemsLiteRegistry();

        // ---- spawn a small Docks-flavored roster --------------------------------
        Actor watch1 = spawn(registry, typeStats, MilitiaWatch.TYPE, cellAt(10, 10));
        Actor watch2 = spawn(registry, typeStats, MilitiaWatch.TYPE, cellAt(40, 10));

        // serfA/serfB/serfC/watch1/watch2/wastrel1/wastrel2/shopkeeper are all
        // "crowd filler": which of them end up sharing a Home is the deterministic
        // household.size named draw's call (§11.4 step 2), not a fixed pairing here —
        // the printed report always shows at least one multi-actor Home either way.
        Actor serfA = spawn(registry, typeStats, Serf.TYPE, cellAt(12, 12));
        Actor serfB = spawn(registry, typeStats, Serf.TYPE, cellAt(12, 12));
        Actor serfC = spawn(registry, typeStats, Serf.TYPE, cellAt(20, 20)); // gets hired by the Shopkeeper

        Actor wastrel1 = spawn(registry, typeStats, Wastrel.TYPE, cellAt(15, 30));
        Actor wastrel2 = spawn(registry, typeStats, Wastrel.TYPE, cellAt(16, 31));

        Actor priest = spawn(registry, typeStats, PriestOfTheFlame.TYPE, cellAt(50, 5));
        Actor disciple = spawn(registry, typeStats, DiscipleOfTheFlame.TYPE, cellAt(50, 5));

        Actor shopkeeper = spawn(registry, typeStats, Shopkeeper.TYPE, cellAt(20, 20));

        Actor keeper = spawn(registry, typeStats, AnimalKeeper.TYPE, cellAt(8, 40));
        Actor dog1 = spawn(registry, typeStats, AnimalActor.TYPE, cellAt(8, 40));
        Actor dog2 = spawn(registry, typeStats, AnimalActor.TYPE, cellAt(9, 40));
        dog1.setOwnerId(keeper.id());
        dog2.setOwnerId(keeper.id());

        Actor gull1 = spawn(registry, typeStats, FeralActor.TYPE, cellAt(60, 60));
        Actor gull2 = spawn(registry, typeStats, FeralActor.TYPE, cellAt(61, 60));

        // ---- civic default job assignment (spawn-bake simplification, §10.4) ----
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            actor.setJobOrdinal((short) jobs.defaultOrdinalFor(actor.typeId()));
        }
        // Stand-in Wielder for this demo: overrides serfA's default job so nearby
        // civic actors' DEFER_WIELDER can be observed triggering (§1.3, §4.10).
        serfA.setJobOrdinal((short) jobs.ordinalOf(Job.FlameOfMerc.ID));

        // ---- Home addendum: authored homes first (§11.1), then the bake pass ----
        // Priest + Disciple share an authored Mission home (draw-free, step 1's shape).
        int missionHome = homes.addHome(priest.cell());
        priest.setHomeId(missionHome);
        disciple.setHomeId(missionHome);
        // Animal Keeper "sleeps by the pen" (§4.7); Animals share the same home shape.
        int penHome = homes.addHome(keeper.cell());
        keeper.setHomeId(penHome);
        dog1.setHomeId(penHome);
        dog2.setHomeId(penHome);
        // Feral gulls roost at the map edge — no household formation for ownerless Ferals.
        int roost = homes.addHome(cellAt(60, 60));
        gull1.setHomeId(roost);
        gull2.setHomeId(roost);

        // Crowd-filler households (§11.4 step 2): serfA+serfB group (shared Home),
        // serfC/watch1/watch2/wastrel1/wastrel2/shopkeeper single-or-grouped by the
        // named household.size draw.
        List<Actor> unhoused = new ArrayList<>(List.of(
                watch1, watch2, serfA, serfB, serfC, wastrel1, wastrel2, shopkeeper));
        HouseholdFormer.formHouseholds(unhoused, homes, relationships, WORLD_SEED, householdRaws);

        // ---- Relationships addendum: employer/employee + free mentor pair -------
        // A dedicated staffCount=1 raws for this call (vs. the shared householdRaws
        // used for grouping above) so the demo always visibly hires — the general
        // [0,2] range is exercised, with its full randomness, by
        // HomeInventoryRelationshipTest.employerHireGetsAnEmployerEdgeToTheNearestCandidate.
        HouseholdRaws guaranteedHire = new HouseholdRaws(
                householdRaws.householdSizeWeights(), 1, 1, 0, 0, 0, 0);
        HouseholdFormer.formEmployment(shopkeeper, List.of(serfC), relationships,
                WORLD_SEED, guaranteedHire);
        HouseholdFormer.bindMentorPairFree(priest, disciple, relationships);

        // ---- wire the real ACTORS phase into the real engine --------------------
        ActorsSystem actorsSystem = new ActorsSystem(WORLD_SEED, typeStats, jobs, registry,
                homes, relationships, items);
        SimulationEngine engine = Simulations.create(new EngineConfig(WORLD_SEED),
                List.of(actorsSystem));

        int ticks = parseTicks(args, 200);
        engine.step(ticks);

        printReport(registry, homes, jobs, ticks);
        printRelationships(relationships);
    }

    private static Actor spawn(ActorRegistry registry, ActorTypeStatsTable typeStats,
            com.trojia.sim.actor.ActorTypeId type, int cell) {
        ActorTypeStats stats = typeStats.get(type);
        return registry.spawn(type, stats, cell);
    }

    private static int cellAt(int x, int y) {
        return PackedPos.pack(x, y, 1);
    }

    private static void printReport(ActorRegistry registry, HomeRegistry homes,
            JobRegistry jobs, int ticks) {
        System.out.println("Ran " + ticks + " ACTORS-phase ticks over " + registry.size()
                + " actors. Granadad's Docks stir.");
        System.out.printf("%-4s %-22s %-20s %-6s %-14s %-10s %s%n",
                "id", "type", "job", "home", "position", "goalState", "needs(H/R/C/S/D)");
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            String job = actor.jobOrdinal() >= 0
                    ? jobs.get(actor.jobOrdinal()).id().value() : "-";
            int x = PackedPos.x(actor.cell());
            int y = PackedPos.y(actor.cell());
            short[] needs = actor.needsSnapshot();
            System.out.printf("%-4d %-22s %-20s %-6d (%-3d,%-3d)   %-10s %d/%d/%d/%d/%d%n",
                    actor.id(), actor.typeId().key(), job, actor.homeId(), x, y,
                    actor.goalState(), needs[0], needs[1], needs[2], needs[3], needs[4]);
        }
        System.out.println("Homes baked: " + homes.size());
    }

    private static void printRelationships(RelationshipRegistry relationships) {
        System.out.println("Relationships baked: " + relationships.size());
        for (int i = 0; i < relationships.size(); i++) {
            var edge = relationships.get(i);
            System.out.println("  " + edge.fromId() + " --" + edge.kind() + "--> " + edge.toId());
        }
    }

    private static int parseTicks(String[] args, int fallback) {
        for (int i = 0; i < args.length - 1; i++) {
            if ("--ticks".equals(args[i])) {
                return Integer.parseInt(args[i + 1]);
            }
        }
        return fallback;
    }

    /** Same walk-up-from-cwd convention the sim-core raws tests use. */
    private static Path locateRawsRoot() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (java.nio.file.Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws not found above " + Path.of("").toAbsolutePath());
    }
}
