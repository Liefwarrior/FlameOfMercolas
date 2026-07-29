package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.inspect.JobDisplay;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.Home;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.RelationshipEdge;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.PatrolRouteTable;
import com.trojia.sim.actor.ZLinkTable;
import com.trojia.sim.actor.ZReachability;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.PackedPos;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * GL-free legibility listing + <em>daily-life proof</em> + <em>performance gate</em> for the
 * Docks-ward population, the district-scale sibling of {@link CompoundBlockActorsMain}: loads
 * the baked {@code docks_surface} world, spawns the deterministic {@link DocksPopulation}
 * onto it, ticks the real {@code ACTORS} phase headless, and prints one row per actor plus a
 * tracked sample of the general population's real movement (a waterfront commuter, a Watch
 * beat, a Wastrel drift, the kennel dogs trailing their keeper).
 *
 * <p>{@code --perf} additionally measures wall-clock nanoseconds around every engine tick and
 * prints the average/total at the end — kept OFF by default so two plain runs' stdout is
 * byte-comparable (the determinism proof; timing would differ every run).
 *
 * <p>Run: {@code ./gradlew.bat :client-observer:runDocksActors --args="--ticks 50000"}.
 */
public final class DocksActorsMain {

    private DocksActorsMain() {
    }

    /** S6 movement-wave profile: bucket width in ticks-of-day (48 buckets over the day). */
    private static final long WAVE_BUCKET_TICKS = 500;

    public static void main(String[] args) {
        int ticks = parseTicks(args, 50_000);
        boolean perf = hasFlag(args, "--perf");

        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        // The closed-money-supply BEFORE measurement, taken before a single tick runs. The
        // after-measurement alone proves nothing (see CoinCensus) — the invariant is the two
        // scans agreeing across 15k ticks of wages, counters and pickpockets.
        CoinCensus coinAtBake = CoinCensus.of(population.items(),
                DocksPopulation.bankVaultChestCell());
        IdentityRegistry identity = population.identity();   // S1 NameForge (bake-side, pure)

        ActorRegistry registry = population.registry();
        HomeRegistry homes = population.homes();
        RelationshipRegistry relationships = population.relationships();
        JobRegistry jobs = population.jobs();
        List<Integer> movers = population.moverIds();

        System.out.println("docks_surface: spawned " + registry.size()
                + " actors over the baked world; ticking " + ticks + " ACTORS-phase ticks ("
                + ((double) ticks / DailyRhythm.DAY) + " simulated days at DAY="
                + DailyRhythm.DAY + ").");
        printComposition(registry, jobs);
        System.out.println("movers (displaced at spawn, REST depleted -> RETURN_HOME):");
        for (int id : movers) {
            Actor a = registry.get(id);
            Home home = homes.get(a.homeId());
            System.out.println("  before: actor#" + id + " cell=" + xyz(a.cell())
                    + " home=" + xyz(home.homeCell()));
        }

        // ---- pick the tracked general-population actors + capture their spawn cells --------
        Tracked commuter = trackCommuter(registry, homes);
        Tracked patroller = track(registry, firstOfType(registry, "militia_watch"), "Watch patrol");
        Tracked wanderer = track(registry, firstWanderer(registry, movers), "Wastrel wander");
        int keeperId = firstOfType(registry, "animal_keeper");
        Tracked keeper = track(registry, keeperId, "Animal Keeper");
        List<Tracked> beasts = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            if (registry.get(i).ownerId() == keeperId) {
                beasts.add(track(registry, i, "Animal#" + i));
            }
        }
        List<Tracked> all = new ArrayList<>();
        addIfPresent(all, commuter, patroller, wanderer, keeper);
        all.addAll(beasts);

        // ---- beast food channel watch (living-docks beast pass): per-gull roam (the
        // anti-oscillation DoD numbers), the hunt catch/revive counters, and mouse den
        // discipline. LinkedHashMap = insertion order, so the report stays byte-identical. ----
        List<Integer> gullIds = idsOfType(registry, "feral");
        List<Integer> catIds = idsOfType(registry, "cat");
        List<Integer> mouseIds = idsOfType(registry, "mouse");
        Map<Integer, BeastRoam> gullRoam = new LinkedHashMap<>();
        for (int id : gullIds) {
            gullRoam.put(id, new BeastRoam());
        }
        Map<Integer, Boolean> mouseDowned = new LinkedHashMap<>();
        for (int id : mouseIds) {
            mouseDowned.put(id, false);
        }
        int[] huntCounters = new int[2]; // [0] catches, [1] revives

        // ---- density revisit (one per square + push + riot house arrest): the max observed
        // co-occupancy over the whole soak (MUST be 1) and per-actor visited-cell sets for the
        // route-diversity proof (5 same-anchor serf commuter pairs). HashSet membership is only
        // ever COUNTED (never iterated), so it is report-deterministic. ----
        List<int[]> routePairs = pickSameAnchorSerfPairs(registry, homes, 5);
        Map<Integer, java.util.HashSet<Integer>> routeCells = new LinkedHashMap<>();
        for (int[] pair : routePairs) {
            routeCells.put(pair[0], new java.util.HashSet<>());
            routeCells.put(pair[1], new java.util.HashSet<>());
        }
        int maxCoOccupancy = 0;

        // ---- S4 THE CLIMB: the cross-z observation rig. The extracted connector table
        // (re-extracted here — a pure function of the baked world, identical to the one
        // inside the fixtures), the spawn-time 3D reachability audit, the Saltgate-bound
        // patrollers' band trail, and the stairwell shove-funnel watch (the lead's soak
        // order: "soak the riot detector at the stairwells and report"). ----
        ZLinkTable zLinks = ZLinkTable.extract(loaded.world());
        ZReachability spawnAudit = ZReachability.flood(loaded.world(), zLinks,
                DocksPopulation.patrolRoutes().get(0).get(3)); // the Tarwalk (96,33) hub
        PatrolRouteTable allRoutes = PatrolRouteTable.of(DocksPopulation.patrolRoutes());
        List<Integer> riseWalkers = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            if (allRoutes.routeContaining(registry.get(i).anchorCell())
                    == DocksPopulation.SALTGATE_ROUTE_INDEX) {
                riseWalkers.add(i);
            }
        }
        Map<Integer, java.util.TreeMap<Integer, Integer>> riseZTicks = new LinkedHashMap<>();
        for (int id : riseWalkers) {
            riseZTicks.put(id, new java.util.TreeMap<>());
        }
        int riseHead = DocksPopulation.patrolRoutes()
                .get(DocksPopulation.SALTGATE_ROUTE_INDEX).get(0);
        int riseFoot = DocksPopulation.patrolRoutes()
                .get(DocksPopulation.SALTGATE_ROUTE_INDEX).get(2);
        int headArrivals = 0;
        int footArrivals = 0;
        // Per-walker ends, so the report can assert what the ward totals cannot: that EVERY
        // soul bound to the route works the climb, not just that somebody does. And the same
        // two counts restricted to the run's LAST window, which is what a run total cannot
        // say: a soul that quits at tick 15,000 still posts a healthy 60,000-tick figure.
        List<int[]> riseEnds = new ArrayList<>();
        List<int[]> riseLateEnds = new ArrayList<>();
        for (int ignored = 0; ignored < riseWalkers.size(); ignored++) {
            riseEnds.add(new int[2]);
            riseLateEnds.add(new int[2]);
        }
        long riseLateStart = SaltgateRiseProof.lateWindowStart(ticks);
        long stairwellShoves = 0;
        long seenShoves = 0;

        // Sample at a work-window tick (tod 5000) and a deep-night tick (tod 20000) of each day.
        List<Long> sampleTicks = new ArrayList<>();
        for (long d = 0; d * DailyRhythm.DAY + 20_000 <= ticks; d++) {
            sampleTicks.add(d * DailyRhythm.DAY + 5_000);
            sampleTicks.add(d * DailyRhythm.DAY + 20_000);
        }

        // ---- S6 acceptance instruments (the observer-diagnosis metrics folded into the
        // twin-run report): the tod-bucketed movement-wave profile (the "same places at
        // the same time" spikes at tod 1000/11000), the guard-jam tick share, the
        // final-day laborer statue census, and the work-window motivation samples
        // (DUTY-out / working / confined at tod 5000 of each day). Deterministic:
        // ascending scans, insertion-ordered maps, integer stats. ----
        int[] prevCell = new int[registry.size()];
        for (int i = 0; i < registry.size(); i++) {
            prevCell[i] = registry.get(i).cell();
        }
        int waveBuckets = (int) (DailyRhythm.DAY / WAVE_BUCKET_TICKS);
        long[] waveMoved = new long[waveBuckets];
        long[] waveTicks = new long[waveBuckets];
        List<Integer> watchIds = idsOfType(registry, "militia_watch");
        // S7: the honest guard-jam readout. The S6 counter (every watch-x-watch adjacency
        // anywhere, printed against a DIFFERENT predicate's baseline at a DIFFERENT horizon)
        // is replaced wholesale by GuardJamInstrument -- see that class for why. Walkability
        // is read the same way ActorsSystem reads it: one borrowed flyweight cursor.
        var jamCursor = loaded.world().cursor();
        com.trojia.sim.actor.Actor.WalkabilityQuery jamWalk =
                c -> com.trojia.sim.world.Walkability.isWalkable(jamCursor.moveTo(c));
        GuardJamInstrument guardJam = new GuardJamInstrument(registry, watchIds, jamWalk,
                jobs.ordinalOf(Job.Watch.Patrol.ID), jobs,
                Math.max(0, ticks - DailyRhythm.DAY), allRoutes, ticks);
        List<Integer> laborerIds = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            Job job = registry.get(i).jobOrdinal() >= 0
                    ? jobs.get(registry.get(i).jobOrdinal()) : null;
            if ("serf.laborer".equals(JobDisplay.trueJobId(job))) {
                laborerIds.add(i);
            }
        }
        Map<Integer, java.util.HashSet<Integer>> laborerCells = new LinkedHashMap<>();
        for (int id : laborerIds) {
            laborerCells.put(id, new java.util.HashSet<>());
        }
        long statueWindowStart = Math.max(0, ticks - DailyRhythm.DAY);
        List<long[]> motivationSamples = new ArrayList<>(); // {tick, working, dutyOut, arrest, held}

        // ---- run, observing every tick and snapshotting at the sample ticks ----------------
        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));
        long tickNanos = 0;
        for (int i = 0; i < ticks; i++) {
            long before = System.nanoTime();
            driver.requestStep();
            tickNanos += System.nanoTime() - before;
            long tick = driver.currentTick();
            boolean isSample = sampleTicks.contains(tick);
            for (Tracked t : all) {
                int cell = registry.get(t.id).cell();
                t.observe(cell);
                if (isSample) {
                    t.sample(tick, cell);
                }
            }
            for (int id : gullIds) {
                gullRoam.get(id).observe(registry.get(id).cell());
            }
            for (int id : mouseIds) {
                boolean downed = registry.get(id)
                        .hasStatus(com.trojia.sim.actor.StatusBit.DOWNED);
                boolean was = mouseDowned.get(id);
                if (downed && !was) {
                    huntCounters[0]++;
                } else if (!downed && was) {
                    huntCounters[1]++;
                }
                mouseDowned.put(id, downed);
            }
            // S4 climb observation: the Rise walkers' band trail + waypoint arrivals, and
            // the stairwell shove funnel (fresh shove rows within chebyshev 2 of a
            // connector endpoint on either band).
            for (int w = 0; w < riseWalkers.size(); w++) {
                int id = riseWalkers.get(w);
                int cell = registry.get(id).cell();
                riseZTicks.get(id).merge(PackedPos.z(cell), 1, Integer::sum);
                if (cell == riseHead) {
                    headArrivals++;
                    riseEnds.get(w)[0]++;
                    if (i >= riseLateStart) {
                        riseLateEnds.get(w)[0]++;
                    }
                } else if (cell == riseFoot) {
                    footArrivals++;
                    riseEnds.get(w)[1]++;
                    if (i >= riseLateStart) {
                        riseLateEnds.get(w)[1]++;
                    }
                }
            }
            var shoves = population.system().shoveLog();
            long totalShoves = shoves.totalRecorded();
            int freshShoves = (int) Math.min(totalShoves - seenShoves, shoves.size());
            for (int r = shoves.size() - freshShoves; r < shoves.size(); r++) {
                if (nearAConnector(shoves.cellAt(r), zLinks)) {
                    stairwellShoves++;
                }
            }
            seenShoves = totalShoves;
            // Density observation: the worst per-cell stack this tick + the tracked pairs' trails.
            Map<Integer, Integer> perCell = new HashMap<>();
            for (int a = 0; a < registry.size(); a++) {
                int count = perCell.merge(registry.get(a).cell(), 1, Integer::sum);
                maxCoOccupancy = Math.max(maxCoOccupancy, count);
            }
            for (Map.Entry<Integer, java.util.HashSet<Integer>> e : routeCells.entrySet()) {
                e.getValue().add(registry.get(e.getKey()).cell());
            }
            // ---- S6 instruments (see the setup block above) ----
            long tod = DailyRhythm.tickOfDay(tick);
            int moved = 0;
            for (int a = 0; a < registry.size(); a++) {
                int cell = registry.get(a).cell();
                if (cell != prevCell[a]) {
                    moved++;
                    prevCell[a] = cell;
                }
            }
            int bucket = (int) (tod / WAVE_BUCKET_TICKS);
            waveMoved[bucket] += moved;
            waveTicks[bucket]++;
            guardJam.observe(registry, homes, tick);
            if (tick > statueWindowStart) {
                for (int id : laborerIds) {
                    laborerCells.get(id).add(registry.get(id).cell());
                }
            }
            if (tod == 5_000) {
                long working = 0;
                long dutyOut = 0;
                long arrest = 0;
                long held = 0;
                for (int a = 0; a < registry.size(); a++) {
                    Actor actor = registry.get(a);
                    if (actor.hasStatus(com.trojia.sim.actor.StatusBit.DEAD)) {
                        continue;
                    }
                    if (actor.lastReasonCode() == com.trojia.sim.actor.ReasonCode.JOB_GOAL) {
                        working++;
                    }
                    if (actor.need(com.trojia.sim.actor.Need.DUTY) == 0) {
                        dutyOut++;
                    }
                    if (actor.hasStatus(com.trojia.sim.actor.StatusBit.HOUSE_ARREST)) {
                        arrest++;
                    }
                    if (actor.hasStatus(com.trojia.sim.actor.StatusBit.HELD)) {
                        held++;
                    }
                }
                motivationSamples.add(new long[] {tick, working, dutyOut, arrest, held});
            }
        }

        printRoster(registry, homes, jobs, identity);
        printBioSamples(registry, identity);
        printNotables(registry, homes, jobs, identity);
        printMoversAfter(population, driver.currentTick(), movers);
        printGraphSample(homes, relationships);
        System.out.println("items minted (placeholder ids + quantities, §11.2): "
                + population.items().size());
        printEconomyProof(population, coinAtBake);
        printFoodConservation(population);
        printStarvationByClass(registry);
        printSerfStarvationByBand(registry, jobs);
        printMoneyGateProof(population, jobs);
        printJusticeReport(population, jobs);
        printDensityReport(population, maxCoOccupancy, routePairs, routeCells);
        boolean risePass = printClimbReport(population, zLinks, spawnAudit, riseWalkers,
                riseZTicks, headArrivals, footArrivals, stairwellShoves, ticks, riseEnds,
                riseLateEnds);
        printProgressionReport(population);
        printTheftReport(population, identity);
        printBarkProof(population, identity, driver.currentTick());
        printBeastReport(population, gullIds, catIds, mouseIds, gullRoam, huntCounters);
        printMotivationReport(registry, motivationSamples);
        printMovementWaveReport(waveMoved, waveTicks);
        guardJam.print(ticks);
        printStatueReport(laborerCells, ticks);
        printFishingReport(population);
        printCraftingsReport(population);
        printGoodsConservation(population, identity);
        printDeathReport(population, identity);
        printDailyLifeProof(registry, jobs, commuter, patroller, wanderer, keeper, beasts);
        printWorldHash(loaded.world(), population, driver.currentTick());
        if (perf) {
            // Wall-clock timing — printed only under --perf so plain runs stay byte-identical.
            double avgMillis = tickNanos / 1e6 / ticks;
            System.out.println();
            System.out.printf("PERF: %d ticks in %.1f ms wall-clock (engine tick only) -> "
                            + "avg %.4f ms/tick at %d actors (observer FAST budget: 25 ms/tick)%n",
                    ticks, tickNanos / 1e6, avgMillis, registry.size());
        }
        if (!risePass) {
            // The verdict has to COST something. Round 2's report printed PASS/FAIL and then
            // exited 0 either way, so the only real enforcement lived in one committed test at
            // one pinned horizon -- and the stall this proof exists to catch happens at that
            // horizon. A soak that prints FAIL and reports success is a soak nobody reads.
            System.out.println();
            System.out.println("SALTGATE RISE VERDICT: FAIL -> exiting non-zero. See THE CLIMB"
                    + " section above for which check missed.");
            System.exit(1);
        }
    }

    /**
     * Progression + faction proof (Sprint 1 "the character sheet comes alive"): the district
     * trains itself by living — push contests teach open_hand/grit, scavenging teaches
     * streetwise — and the standing ledger remembers who the Watch corrected and who paid
     * the counters. Deterministic ascending-id scans over the persisted side tables.
     */
    private static void printProgressionReport(DocksPopulation population) {
        var tracks = population.system().skillTracks();
        var standings = population.system().factionStandings();
        var registry = population.registry();
        var skills = tracks.skills();
        // Sprint 5 (the awards wave): the report goes DYNAMIC over the whole skill
        // universe — with every job training its trade, the old 4-skill census would
        // miss the point of the sprint. Ascending raw-id order, so twin runs stay
        // byte-identical; skills nobody holds print nothing (the empty rows are noise).
        int[] holders = new int[skills.size()];
        int bestId = -1;
        int bestLevelSum = 0;
        for (int i = 0; i < registry.size(); i++) {
            int sum = 0;
            for (int s = 0; s < skills.size(); s++) {
                int level = tracks.level(i, s);
                holders[s] += level > 0 ? 1 : 0;
                sum += level;
            }
            if (sum > bestLevelSum) {
                bestLevelSum = sum;
                bestId = i;
            }
        }
        int watchMoved = 0;
        int merchantsMoved = 0;
        int mostWantedId = -1;
        int mostWantedStanding = 0;
        for (int i = 0; i < registry.size(); i++) {
            int watch = standings.watchStanding(i);
            watchMoved += watch != 0 ? 1 : 0;
            merchantsMoved += standings.standingOf(i,
                    standings.isWired() ? standings.factions().rawId("merchants") : 0) != 0 ? 1 : 0;
            if (watch < mostWantedStanding) {
                mostWantedStanding = watch;
                mostWantedId = i;
            }
        }
        System.out.println();
        System.out.println("================ PROGRESSION PROOF (the character sheet is alive) ===========");
        System.out.println("level-ups recorded (monotonic): " + tracks.levelLog().totalRecorded());
        StringBuilder census = new StringBuilder("souls holding a nonzero skill:");
        for (int s = 0; s < skills.size(); s++) {
            if (holders[s] > 0) {
                census.append(' ').append(skills.get(s).key()).append('=').append(holders[s]);
            }
        }
        System.out.println(census);
        if (bestId >= 0) {
            StringBuilder best = new StringBuilder("most-trained soul: actor#" + bestId);
            for (int s = 0; s < skills.size(); s++) {
                int level = tracks.level(bestId, s);
                if (level > 0) {
                    best.append(' ').append(skills.get(s).key()).append('=').append(level);
                }
            }
            System.out.println(best);
        }
        System.out.println("standings moved: watch=" + watchMoved + " merchants=" + merchantsMoved);
        if (mostWantedId >= 0) {
            System.out.println("most wanted: actor#" + mostWantedId + " watch standing "
                    + mostWantedStanding);
        }
    }

    /**
     * Theft &amp; justice report (Sprint 2 "reactive streets"): pickpocketing is LIVE — the
     * ambient underworld lifts pocket coin (moves, never mints), the marks catch hands,
     * and the Watch corrects witnessed thefts through the existing justice pipeline with a
     * reconstructable ReasonCode + CrimeLog trail. Deterministic ascending scans only.
     */
    private static void printTheftReport(DocksPopulation population, IdentityRegistry identity) {
        var system = population.system();
        var log = system.crimeLog();
        var registry = population.registry();
        System.out.println();
        System.out.println("================ THEFT & JUSTICE (Sprint 2: reactive streets) ==============");
        System.out.println("  pickpocket attempts: " + system.theftCount()
                + "  (caught in the act: " + system.theftCaughtCount()
                + ");  Royals lifted: " + system.coinsStolen() + " (moved, never minted)");
        System.out.println("  corrections: arrests-for-theft " + system.theftArrests()
                + ";  Skyrunner escalations (maim/hang) " + system.skyrunnerTheftEscalations());
        int shown = 0;
        for (int i = log.size() - 1; i >= 0 && shown < 5; i--, shown++) {
            String thief = identity.get(log.thiefIdAt(i)).fullName();
            String victim = log.victimIdAt(i) >= 0 && log.victimIdAt(i) < registry.size()
                    ? identity.get(log.victimIdAt(i)).fullName() : "#" + log.victimIdAt(i);
            System.out.println("    tick=" + log.tickAt(i) + "  " + thief
                    + (log.witnessedAt(i) ? " was CAUGHT dipping " : " lifted the purse of ")
                    + victim + " at " + xyz(log.cellAt(i))
                    + (log.servedAt(i) ? "  [answered for]" : ""));
        }
        if (log.size() == 0) {
            System.out.println("    (no thefts recorded)");
        }
        System.out.println("============================================================================");
    }

    /**
     * Bark selection proof (Sprint 2 rank 3, the debug-log consumer): the pure selector
     * runs against live end-of-run state for a fixed cast — a Watch guard greeting the
     * district's most-wanted vs a clean citizen (the standing dimension), across the four
     * time buckets (the time dimension) — and resolves text where World's tables exist
     * (degrading to the bare key: the schema seam ships before the words). Deterministic.
     */
    private static void printBarkProof(DocksPopulation population, IdentityRegistry identity,
            long endTick) {
        var registry = population.registry();
        var jobs = population.jobs();
        var standings = population.system().factionStandings();
        var relationships = population.relationships();
        var tables = com.trojia.sim.bark.BarkRawsLoader.load(
                com.trojia.client.boot.RepoPaths.locate("content", "raws"));
        int guardId = firstOfType(registry, "militia_watch");
        int mostWanted = -1;
        int worst = 0;
        int clean = -1;
        for (int i = 0; i < registry.size(); i++) {
            int watch = standings.watchStanding(i);
            if (watch < worst) {
                worst = watch;
                mostWanted = i;
            }
            if (clean == -1 && watch >= 0 && i != guardId
                    && !registry.get(i).typeId().key().equals("militia_watch")) {
                clean = i;
            }
        }
        System.out.println();
        System.out.println("================ BARK SELECTION (deterministic; text tables = World's) ======");
        System.out.println("  authored bark tables: " + tables.size()
                + (tables.size() == 0 ? " (schema seam live; text lands with the World phase)" : ""));
        if (guardId == Actor.NONE) {
            System.out.println("  (no Watch speaker found)");
            System.out.println("============================================================================");
            return;
        }
        Actor guard = registry.get(guardId);
        com.trojia.sim.actor.job.Job guardJob =
                guard.jobOrdinal() >= 0 ? jobs.get(guard.jobOrdinal()) : null;
        long dayBase = (endTick / DailyRhythm.DAY) * DailyRhythm.DAY;
        long[] times = {dayBase + 3_000, dayBase + 8_000, dayBase + 14_000, dayBase + 20_000};
        int[] listeners = mostWanted >= 0 ? new int[] {clean, mostWanted} : new int[] {clean};
        for (int listener : listeners) {
            if (listener < 0) {
                continue;
            }
            System.out.println("  " + identity.get(guardId).fullName() + " [watch] greets "
                    + identity.get(listener).fullName() + " (watch standing "
                    + standings.watchStanding(registry.get(listener).identity().presentedId())
                    + "):");
            for (long t : times) {
                var choice = com.trojia.sim.actor.BarkSelector.select(
                        population.worldSeed(), t, guard, guardJob,
                        registry.get(listener).identity().presentedId(), standings,
                        relationships);
                String text = choice.resolve(tables);
                System.out.println("    tod=" + DailyRhythm.tickOfDay(t) + "  -> "
                        + choice.tableKey() + " row=" + choice.rowIn(Math.max(1,
                                tables.rowCount(choice.tableKey())))
                        + (text == null ? "  (unauthored)" : "  \"" + text + "\""));
            }
        }
        System.out.println("============================================================================");
    }

    /**
     * The canonical world-hash surface (S8 harness slice). Every twin-run comparison in this
     * arc reads THESE three numbers out of the report: the world's own (WRLD) sub-hash, the
     * ACTORS section sub-hash — the one that covers the whole persisted triad, actors through
     * deathLog — and the salt-order-invariant combined hash of both.
     *
     * <p>Printed last so the whole report above it has already been produced from the same
     * end-of-soak state; parsed by {@link TwinRunGateMain} via the {@link #WORLD_HASH_TAG}
     * prefix, and by hand for the committed pre-S8 baseline
     * ({@code docs/BASELINE-WORLD-HASH.md}). {@code Locale.ROOT} pins the digits — a locale
     * with non-ASCII numerals would otherwise make the "byte-identical" claim
     * machine-dependent.
     *
     * <p>This surface is load-bearing for the whole S8-S12 arc: without it a state divergence
     * that never reaches a printed line (the twin-run gate's injection #2 was exactly that
     * shape) is invisible to every other line of this report.
     */
    private static void printWorldHash(com.trojia.sim.world.World world,
            DocksPopulation population, long tick) {
        com.trojia.sim.world.io.WorldHasher hasher = new com.trojia.sim.world.io.WorldHasher();
        hasher.hashWorld(world);
        population.system().hashInto(hasher.sectionSink(population.system().id()));
        long worldSection = hasher.sectionHash(
                com.trojia.sim.world.io.WorldHasher.WORLD_SECTION);
        long actorsSection = hasher.sectionHash(population.system().id());
        long combined = hasher.combinedHash();
        System.out.println();
        System.out.println("================ WORLD HASH (the twin-run comparator) ======================");
        System.out.println(String.format(java.util.Locale.ROOT,
                "  at tick %d;  WRLD=%016x  ACTORS=%016x", tick, worldSection, actorsSection));
        System.out.println(String.format(java.util.Locale.ROOT,
                "%s%016x", WORLD_HASH_TAG, combined));
        System.out.println("============================================================================");
    }

    /**
     * Line prefix {@link TwinRunGateMain} greps for the combined world hash. Load-bearing —
     * changing it silently un-hooks the twin-run gate's hash comparator, so the gate fails
     * hard (rather than skipping) when the tag is absent from a run's stdout.
     */
    static final String WORLD_HASH_TAG = "  COMBINED WORLD HASH: 0x";

    /**
     * Money-conservation proof (Phase-2, repaired in S8): after the run, the hard invariant
     * {@code totalRoyals() == vault COIN count} must still hold, and the closed COIN supply
     * must be intact — wages, counter traffic and pickpockets only ever MOVED Royals, never
     * minted them.
     *
     * <p><b>What was wrong with this proof.</b> It used to derive
     * {@code looseCoin = minted - vault - sunk} and then assert
     * {@code minted == vault + loose + sunk}. That is {@code minted == minted}: a tautology
     * that printed PASS every sprint and checked nothing. Loose coin is now COUNTED by an
     * independent walk of ItemsLite ({@link CoinCensus}), and the identity that carries the
     * weight is a comparison against a second, independent census taken at bake before the
     * first tick — so a stray mint or a lost stack has something to fail against.
     */
    private static void printEconomyProof(DocksPopulation population, CoinCensus atBake) {
        var bank = population.bankAccounts();
        int vaultCell = DocksPopulation.bankVaultChestCell();
        CoinCensus now = CoinCensus.of(population.items(), vaultCell);
        long totalRoyals = bank.totalRoyals();
        boolean closed = CoinCensus.supplyClosed(atBake, now);
        System.out.println();
        System.out.println("================ MONEY CONSERVATION (closed supply) ========================");
        System.out.println("  accounts open: " + bank.accountCount()
                + " (per-actor + 1 employer pool);  totalRoyals=" + totalRoyals
                + "  vaultCOIN=" + now.vault()
                + "  -> invariant totalRoyals==vaultCOIN: " + (totalRoyals == now.vault()));
        System.out.println("  COIN census (independent ItemsLite scan, counted not derived):"
                + "  vault=" + now.vault()
                + "  loose=" + now.loose()
                + " [carried=" + now.carried() + " over " + now.purses()
                + " distinct souls, fattest purse=" + now.fattest()
                + ";  staged-on-cells=" + now.ground() + "]"
                + "  phantom-sunk=" + now.sunk());
        System.out.println("  CLOSED SUPPLY  live at bake=" + atBake.live()
                + "  ==  live now=" + now.live() + ": " + closed
                + (closed ? "" : "  <<< " + (now.live() - atBake.live())
                        + " Royals appeared from nowhere / vanished"));
        System.out.println("  (bake split was vault=" + atBake.vault() + " loose=" + atBake.loose()
                + " over " + atBake.purses() + " purses)");
        System.out.println("============================================================================");
    }

    /**
     * FOOD closed-supply conservation proof (economy-loop pass): every unit ever minted (larder
     * seed at bake + farm work-unit yields + periodic imports) is either still in circulation
     * ({@code liveOfKind(FOOD)}) or has been eaten/sunk — {@code minted == live + eaten}. A stable,
     * bounded {@code live} is the demand-driven-supply signal (growing ⇒ over-import; → 0 ⇒
     * under-supply). Eating is the only FOOD sink; imports mint FOOD, never Royals, so the money
     * invariant above is untouched.
     */
    private static void printFoodConservation(DocksPopulation population) {
        var items = population.items();
        long minted = population.system().foodMinted();
        long eaten = population.system().foodEaten();
        int live = items.liveOfKind(ItemKinds.FOOD);
        System.out.println();
        System.out.println("================ FOOD CONSERVATION (closed supply) =========================");
        System.out.println("  FOOD minted=" + minted
                + " (seed + farm yield + imports + garbage scraps);  live(held)="
                + live + "  eaten(sunk)=" + eaten);
        System.out.println("  invariant minted == live + eaten: " + (minted == live + eaten)
                + "  (" + minted + " == " + (live + eaten) + ")");
        System.out.println("============================================================================");
    }

    /**
     * Starvation-by-class report (the economy-loop acceptance bar): the share of each actor class
     * whose HUNGER is stuck at 0 (starved) at the end of the soak. Eli's hard bar — SERF starvation
     * &le; 5%, and the MIDDLE CLASS (shopkeeper / clergy / watch) NEVER starves. Wastrels are the
     * intended margin (the wageless poor + the roof decks). Deterministic ascending-id scan.
     */
    private static void printStarvationByClass(ActorRegistry registry) {
        Map<String, int[]> byType = new java.util.TreeMap<>(); // key -> {total, starved}
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            int[] row = byType.computeIfAbsent(a.typeId().key(), k -> new int[2]);
            row[0]++;
            if (a.need(com.trojia.sim.actor.Need.HUNGER) == 0) {
                row[1]++;
            }
        }
        System.out.println();
        System.out.println("================ STARVATION BY CLASS (HUNGER == 0 at soak end) ==============");
        System.out.printf("  %-22s %6s %8s %8s%n", "type", "total", "starved", "pct");
        for (Map.Entry<String, int[]> e : byType.entrySet()) {
            int total = e.getValue()[0];
            int starved = e.getValue()[1];
            System.out.printf("  %-22s %6d %8d %7.1f%%%n", e.getKey(), total, starved,
                    100.0 * starved / total);
        }
        // Aggregates against the bar.
        int[] serf = byType.getOrDefault("serf", new int[2]);
        int[] mid = new int[2];
        for (String m : new String[] {"shopkeeper", "militia_watch", "priest_of_the_flame",
                "disciple_of_the_flame"}) {
            int[] r = byType.getOrDefault(m, new int[2]);
            mid[0] += r[0];
            mid[1] += r[1];
        }
        System.out.println("  --------------------------------------------------------------------------");
        System.out.printf("  SERF starvation:         %d / %d = %.2f%%  (bar: <= 5%%)  -> %s%n",
                serf[1], serf[0], pct(serf), serf[0] == 0 || 100.0 * serf[1] / serf[0] <= 5.0
                        ? "PASS" : "FAIL");
        System.out.printf("  MIDDLE CLASS starvation: %d / %d = %.2f%%  (bar: 0%%)     -> %s%n",
                mid[1], mid[0], pct(mid), mid[1] == 0 ? "PASS" : "FAIL");
        System.out.println("============================================================================");
    }

    private static double pct(int[] row) {
        return row[0] == 0 ? 0.0 : 100.0 * row[1] / row[0];
    }

    /**
     * Serf starvation broken out by walk-plane z-band (and, for any residual starved serf, its true
     * job) — the reachability proof: every serf cohort the diagnosis stranded (z:+11 ship crews /
     * dense bunk sites, z:+12/z:+13 terrace residents) now has a reachable stocked same-z source, so
     * each band lands well under the 5% bar rather than one band carrying a 20%+ stranded cohort.
     */
    private static void printSerfStarvationByBand(ActorRegistry registry, JobRegistry jobs) {
        Map<Integer, int[]> byBand = new java.util.TreeMap<>();       // z-world -> {total, starved}
        Map<String, Integer> starvedJobs = new java.util.TreeMap<>(); // true-job -> starved count
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (!a.typeId().key().equals("serf")) {
                continue;
            }
            int[] row = byBand.computeIfAbsent(PackedPos.z(a.cell()), k -> new int[2]);
            row[0]++;
            if (a.need(com.trojia.sim.actor.Need.HUNGER) == 0) {
                row[1]++;
                Job job = a.jobOrdinal() >= 0 ? jobs.get(a.jobOrdinal()) : null;
                starvedJobs.merge(JobDisplay.trueJobId(job), 1, Integer::sum);
            }
        }
        System.out.println();
        System.out.println("================ SERF STARVATION BY WALK-PLANE Z-BAND ======================");
        System.out.printf("  %-10s %6s %8s %8s%n", "z(world)", "total", "starved", "pct");
        for (Map.Entry<Integer, int[]> e : byBand.entrySet()) {
            int total = e.getValue()[0];
            int starved = e.getValue()[1];
            System.out.printf("  z=%-8d %6d %8d %7.1f%%%n", e.getKey(), total, starved,
                    100.0 * starved / total);
        }
        System.out.println("  residual starved serfs by true job: "
                + (starvedJobs.isEmpty() ? "(none)" : starvedJobs));
        System.out.println("============================================================================");
    }

    /**
     * The money-gate proof (Eli's "money matters"): the market only feeds a mouth that pays. This
     * samples, at soak end, a fed WAGED serf (bought its way through the soak) against a starved
     * WAGELESS wastrel (no wage, seed Royals spent down to nothing), showing survival tracks Royals
     * — a waged citizen eats via purchase, the wageless margin starves.
     */
    private static void printMoneyGateProof(DocksPopulation population, JobRegistry jobs) {
        ActorRegistry registry = population.registry();
        var bank = population.bankAccounts();
        int fedSerf = Actor.NONE;
        int starvedWastrel = Actor.NONE;
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            int hunger = a.need(com.trojia.sim.actor.Need.HUNGER);
            String type = a.typeId().key();
            if (type.equals("serf") && hunger > 0 && fedSerf == Actor.NONE) {
                fedSerf = i;
            }
            if (type.equals("wastrel") && hunger == 0 && starvedWastrel == Actor.NONE) {
                starvedWastrel = i;
            }
        }
        System.out.println();
        System.out.println("================ MONEY-GATE PROOF (survival tracks Royals) ==================");
        System.out.println("  Food is BUY-only: a paid ration, a shop counter, or a farm larder you grew.");
        if (fedSerf != Actor.NONE) {
            printMoneyActor(registry, bank, jobs, fedSerf, "FED, waged serf   ");
        }
        if (starvedWastrel != Actor.NONE) {
            printMoneyActor(registry, bank, jobs, starvedWastrel, "STARVED wastrel    ");
        }
        System.out.println("  A waged, solvent citizen buys and eats; the wageless poor spend down");
        System.out.println("  their seed Royals and then starve -- money now gates who survives.");
        System.out.println("============================================================================");
    }

    private static void printMoneyActor(ActorRegistry registry, com.trojia.sim.actor.BankLedger bank,
            JobRegistry jobs, int id, String label) {
        Actor a = registry.get(id);
        Job job = a.jobOrdinal() >= 0 ? jobs.get(a.jobOrdinal()) : null;
        long balance = id < bank.accountCount() ? bank.balanceOf(id) : -1;
        System.out.printf("  %s actor#%-4d [%-16s] HUNGER=%-5d Royals=%d%n",
                label, id, JobDisplay.trueJobId(job), a.need(com.trojia.sim.actor.Need.HUNGER), balance);
    }

    /**
     * Law &amp; order report (Pass 11-13 acceptance): proves the guard-side APPREHEND loop fired
     * LIVE — every {@code offenseCount} bump is one completed correction (a guard-side loiter
     * arrest, a villain-exposure arrest, or a Skyrunner maim/hang escalation), so a nonzero
     * total with offenders outside the old villain pool means the new enforcement is really
     * running. Prints who is in custody right now (and in WHICH assigned cell), who has served
     * and been released, and one sample offender trace. Deterministic ascending-id scans only,
     * so twin runs stay byte-identical.
     */
    private static void printJusticeReport(DocksPopulation population, JobRegistry jobs) {
        ActorRegistry registry = population.registry();
        var bank = population.bankAccounts();
        int totalOffenses = 0;
        int offenders = 0;
        int heldNow = 0;
        int released = 0;
        int maimedOrHanged = 0;
        int warnedNow = 0;
        int sample = Actor.NONE;
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            totalOffenses += a.offenseCount();
            if (a.offenseCount() > 0) {
                offenders++;
                if (sample == Actor.NONE) {
                    sample = i;
                }
            }
            if (a.hasStatus(com.trojia.sim.actor.StatusBit.HELD)) {
                heldNow++;
            } else if (a.hasStatus(com.trojia.sim.actor.StatusBit.EXECUTED)
                    || a.hasStatus(com.trojia.sim.actor.StatusBit.MAIMED)) {
                maimedOrHanged++;
            } else if (a.offenseCount() > 0) {
                released++;
            }
            if (a.hasStatus(com.trojia.sim.actor.StatusBit.MOVE_ALONG)) {
                warnedNow++;
            }
        }
        System.out.println();
        System.out.println("================ LAW & ORDER (guard-side APPREHEND live) ====================");
        System.out.println("  corrections completed (sum offenseCount): " + totalOffenses
                + " across " + offenders + " distinct offenders  -> arrests firing: "
                + (totalOffenses > 0 ? "YES" : "NO"));
        System.out.println("  in custody now: " + heldNow + ";  served + released: " + released
                + ";  maimed/hanged (Skyrunner escalation): " + maimedOrHanged
                + ";  move-along warnings outstanding: " + warnedNow);
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (a.hasStatus(com.trojia.sim.actor.StatusBit.HELD)) {
                System.out.println("    HELD  actor#" + a.id() + " [" + a.typeId().key()
                        + "] cell=" + xyz(a.cell()) + " assignedCell=" + xyz(a.assignedHoldCell())
                        + " heldUntil=" + a.heldUntilTick());
            }
        }
        if (sample != Actor.NONE) {
            Actor a = registry.get(sample);
            Job job = a.jobOrdinal() >= 0 ? jobs.get(a.jobOrdinal()) : null;
            long balance = sample < bank.accountCount() ? bank.balanceOf(sample) : -1;
            System.out.println("  sample offender trace: actor#" + a.id() + " [" + a.typeId().key()
                    + " / " + JobDisplay.trueJobId(job) + "] offenses=" + a.offenseCount()
                    + " held=" + a.hasStatus(com.trojia.sim.actor.StatusBit.HELD)
                    + " assignedCell=" + xyz(a.assignedHoldCell())
                    + " heldUntil=" + a.heldUntilTick()
                    + " Royals=" + balance + " (post-fine) lastReason=" + policyName(a));
        }
        System.out.println("============================================================================");
    }

    /**
     * Density report (density revisit): the one-per-square proof — the maximum co-occupancy any
     * cell ever reached during the soak (MUST be 1) and the shared-cell count at soak end (MUST
     * be 0) — plus the push/riot/house-arrest counters and the route-diversity proof: for each
     * of 5 same-anchor serf commuter pairs, the distinct cells each partner visited and their
     * overlap percentage (well under 100 = the pair genuinely walks different routes to the
     * same workplace, the per-actor path jitter working).
     */
    private static void printDensityReport(DocksPopulation population, int maxCoOccupancy,
            List<int[]> routePairs, Map<Integer, java.util.HashSet<Integer>> routeCells) {
        ActorRegistry registry = population.registry();
        Map<Integer, Integer> perCell = new HashMap<>();
        int sharedCellsNow = 0;
        int maxNow = 0;
        for (int i = 0; i < registry.size(); i++) {
            int count = perCell.merge(registry.get(i).cell(), 1, Integer::sum);
            maxNow = Math.max(maxNow, count);
            if (count == 2) {
                sharedCellsNow++; // counted once, the moment a cell reaches 2
            }
        }
        int underHouseArrest = 0;
        for (int i = 0; i < registry.size(); i++) {
            if (registry.get(i).hasStatus(com.trojia.sim.actor.StatusBit.HOUSE_ARREST)) {
                underHouseArrest++;
            }
        }
        var system = population.system();
        System.out.println();
        System.out.println("================ DENSITY (one per square + push + riot house arrest) ========");
        System.out.println("  max observed co-occupancy over the soak: " + maxCoOccupancy
                + "  (bar: 1)  -> " + (maxCoOccupancy <= 1 ? "PASS" : "FAIL"));
        System.out.println("  shared cells at soak end (>=2 actors): " + sharedCellsNow
                + " (worst stack now: " + maxNow + ")  (bar: 0)  -> "
                + (sharedCellsNow == 0 ? "PASS" : "FAIL"));
        System.out.println("  pushes: " + system.pushCount()
                + ";  riot responses: " + system.riotCount()
                + ";  house arrests issued: " + system.houseArrestsIssued()
                + ";  under house arrest now: " + underHouseArrest);
        System.out.println("  route diversity (same-anchor serf commuter pairs, distinct cells visited):");
        for (int[] pair : routePairs) {
            java.util.HashSet<Integer> a = routeCells.get(pair[0]);
            java.util.HashSet<Integer> b = routeCells.get(pair[1]);
            int shared = 0;
            for (int cell : a) {
                if (b.contains(cell)) {
                    shared++;
                }
            }
            int overlapPct = 100 * shared / Math.min(a.size(), b.size());
            System.out.println("    pair actor#" + pair[0] + "/actor#" + pair[1]
                    + " anchor=" + xyz(registry.get(pair[0]).anchorCell())
                    + "  cellsA=" + a.size() + " cellsB=" + b.size()
                    + " shared=" + shared + " overlap=" + overlapPct + "%");
        }
        System.out.println("============================================================================");
    }

    /** Whether {@code cell} lies within chebyshev 2 of a connector endpoint on its own z. */
    private static boolean nearAConnector(int cell, ZLinkTable links) {
        int z = PackedPos.z(cell);
        for (int i = 0; i < links.linkCount(); i++) {
            int low = links.low(i);
            int high = links.high(i);
            if ((PackedPos.z(low) == z && chebyshev(cell, low) <= 2)
                    || (PackedPos.z(high) == z && chebyshev(cell, high) <= 2)) {
                return true;
            }
        }
        return false;
    }

    /**
     * S4 climb report: the baked connector census, the spawn-time 3D reachability audit
     * (per inhabited band + the actors standing on unreachable cells at spawn — the
     * folklore number, now tracked), the Saltgate Rise walkers' band trail with the
     * z11+z13 waypoint proof, and the stairwell shove-funnel + riot watch. Deterministic
     * ascending scans only, so twin runs stay byte-identical.
     *
     * @return the Saltgate Rise verdict — the caller exits non-zero on FAIL, so the printed
     *         verdict costs something instead of scrolling past
     */
    private static boolean printClimbReport(DocksPopulation population, ZLinkTable zLinks,
            ZReachability spawnAudit, List<Integer> riseWalkers,
            Map<Integer, java.util.TreeMap<Integer, Integer>> riseZTicks,
            int headArrivals, int footArrivals, long stairwellShoves, long ticksRun,
            List<int[]> riseEnds, List<int[]> riseLateEnds) {
        ActorRegistry registry = population.registry();
        System.out.println();
        System.out.println("================ THE CLIMB (S4: cross-z movement live) ======================");
        System.out.println("  connectors baked (stair pairs + ramp exits): " + zLinks.linkCount());
        System.out.println("  reachability audit (flood from the Tarwalk hub, connectors included):");
        System.out.printf("  %-10s %10s %11s %13s%n", "z(world)", "walkable", "reachable", "unreachable");
        for (int z = 17; z <= 22; z++) { // map z:+9 .. z:+14 — strand to roof slums
            int walkable = spawnAudit.walkableAtZ(z);
            if (walkable == 0) {
                continue;
            }
            System.out.printf("  z=%-8d %10d %11d %13d%n", z, walkable,
                    spawnAudit.reachableAtZ(z), walkable - spawnAudit.reachableAtZ(z));
        }
        int strandedActors = 0;
        StringBuilder stranded = new StringBuilder();
        for (int i = 0; i < registry.size(); i++) {
            if (!spawnAudit.reachable(registry.get(i).cell())) {
                strandedActors++;
                if (strandedActors <= 8) {
                    stranded.append(strandedActors == 1 ? "" : ", ").append("#").append(i)
                            .append(xyz(registry.get(i).cell()));
                }
            }
        }
        System.out.println("  actors at soak end on hub-unreachable cells: " + strandedActors
                + " / " + registry.size()
                + (strandedActors > 0 ? "  (" + stranded + (strandedActors > 8 ? ", ..." : "") + ")" : "")
                + "  <- the ex-folklore metric, now tracked");
        System.out.println("  Saltgate Rise beat (route " + DocksPopulation.SALTGATE_ROUTE_INDEX
                + ", z11<->z13): walkers " + riseWalkers);
        for (int id : riseWalkers) {
            System.out.println("    actor#" + id + " ticks-per-band " + riseZTicks.get(id));
        }
        boolean visitedBothBands = false;
        for (int id : riseWalkers) {
            var byZ = riseZTicks.get(id);
            visitedBothBands |= byZ.containsKey(19) && byZ.containsKey(20)
                    && byZ.containsKey(21); // world z 19/20/21 = map z:+11/+12/+13
        }
        // The verdict carries FLOORS now (SaltgateRiseProof): "at least one arrival" is a
        // boolean, and a boolean printed PASS straight through a 50% throughput collapse.
        // And it carries a LATE window, because a run TOTAL printed PASS straight through a
        // sergeant who worked 15,000 ticks and then stopped for 45,000.
        boolean risePass = SaltgateRiseProof.passes(riseWalkers.size(), visitedBothBands,
                riseEnds, riseLateEnds);
        System.out.println("    head(z13) arrivals: " + headArrivals + " ("
                + SaltgateRiseProof.per10k(headArrivals, ticksRun) + "/10k);  foot(z11): "
                + footArrivals + " (" + SaltgateRiseProof.per10k(footArrivals, ticksRun)
                + "/10k);  walkers " + riseWalkers.size() + " (floor "
                + SaltgateRiseProof.WALKER_FLOOR + ")");
        int lateHead = 0;
        int lateFoot = 0;
        for (int[] late : riseLateEnds) {
            lateHead += late[0];
            lateFoot += late[1];
        }
        System.out.println("    ward-wide in the LAST " + SaltgateRiseProof.LATE_WINDOW_TICKS
                + " ticks (the final full DAY, so the window always contains a whole shift"
                + " wherever the run stops): head " + lateHead + " (floor "
                + SaltgateRiseProof.LATE_HEAD_ARRIVALS_FLOOR + ");  foot " + lateFoot
                + " (floor " + SaltgateRiseProof.LATE_FOOT_ARRIVALS_FLOOR + ")");
        for (int w = 0; w < riseWalkers.size(); w++) {
            System.out.println("    actor#" + riseWalkers.get(w) + " reached head "
                    + riseEnds.get(w)[0] + " / foot " + riseEnds.get(w)[1]
                    + "   |  in the LAST " + SaltgateRiseProof.LATE_WINDOW_TICKS
                    + " ticks: head " + riseLateEnds.get(w)[0] + " / foot "
                    + riseLateEnds.get(w)[1] + " (floor "
                    + SaltgateRiseProof.LATE_PER_WALKER_END_FLOOR + " each -- a run total is"
                    + " earned by the past, this is the present)");
        }
        System.out.println("    a walker visited z11+z12+z13: " + visitedBothBands
                + "  -> " + (risePass ? "PASS" : "FAIL")
                + SaltgateRiseProof.verdictDetail(riseWalkers.size(), visitedBothBands,
                        riseWalkers, riseEnds, riseLateEnds));
        System.out.println("  stairwell funnel: pushes within cheb<=2 of a connector: "
                + stairwellShoves + " of " + population.system().pushCount()
                + " total;  riot responses (district-wide): " + population.system().riotCount());
        System.out.println("============================================================================");
        return risePass;
    }

    /**
     * The route-diversity subjects: the first {@code maxPairs} pairs of serf COMMUTERS (home
     * differs from work anchor, so they genuinely travel) sharing one work anchor, ascending
     * actor id — deterministic, so twin runs pick identical pairs.
     */
    private static List<int[]> pickSameAnchorSerfPairs(ActorRegistry registry, HomeRegistry homes,
            int maxPairs) {
        Map<Integer, Integer> firstByAnchor = new LinkedHashMap<>();
        List<int[]> pairs = new ArrayList<>();
        for (int i = 0; i < registry.size() && pairs.size() < maxPairs; i++) {
            Actor a = registry.get(i);
            if (!a.typeId().key().equals("serf") || a.homeId() == Actor.NONE) {
                continue;
            }
            if (a.anchorCell() == homes.get(a.homeId()).homeCell()) {
                continue; // bunk crews never commute; only real travelers prove route variety
            }
            Integer first = firstByAnchor.get(a.anchorCell());
            if (first == null) {
                firstByAnchor.put(a.anchorCell(), i);
            } else if (first >= 0) {
                pairs.add(new int[] {first, i});
                firstByAnchor.put(a.anchorCell(), -1); // one pair per anchor
            }
        }
        return pairs;
    }

    /**
     * Beast food channel report (living-docks beast pass): the anti-oscillation DoD numbers —
     * per-gull distinct-cells-visited + roam bounding box + longest single-cell stall — plus
     * the hunt loop's catch/revive counters, the down-right-now count, mouse den discipline
     * (live mice standing within leash+flee slack of their den), and gull/cat HUNGER health.
     * Deterministic: insertion-ordered maps, integer stats only.
     */
    private static void printBeastReport(DocksPopulation population, List<Integer> gullIds,
            List<Integer> catIds, List<Integer> mouseIds, Map<Integer, BeastRoam> gullRoam,
            int[] huntCounters) {
        ActorRegistry registry = population.registry();
        System.out.println();
        System.out.println("================ BEAST FOOD CHANNEL (gulls range, cats+gulls hunt mice) =====");
        for (int id : gullIds) {
            BeastRoam r = gullRoam.get(id);
            Actor gull = registry.get(id);
            System.out.println("  gull#" + id + " roost=" + xyz(gull.anchorCell())
                    + " distinctCells=" + r.visited.size()
                    + " bbox=" + (r.maxX - r.minX) + "x" + (r.maxY - r.minY)
                    + " maxConsecutiveTicksOnOneCell=" + r.maxRun
                    + " HUNGER=" + gull.need(com.trojia.sim.actor.Need.HUNGER));
        }
        int downNow = 0;
        int atDen = 0;
        for (int id : mouseIds) {
            Actor mouse = registry.get(id);
            if (mouse.hasStatus(com.trojia.sim.actor.StatusBit.DOWNED)) {
                downNow++;
            } else if (chebyshev(mouse.cell(),
                    population.homes().get(mouse.homeId()).homeCell()) <= 12) {
                atDen++;
            }
        }
        int minGullHunger = Integer.MAX_VALUE;
        for (int id : gullIds) {
            minGullHunger = Math.min(minGullHunger, registry.get(id).need(com.trojia.sim.actor.Need.HUNGER));
        }
        int minCatHunger = Integer.MAX_VALUE;
        for (int id : catIds) {
            minCatHunger = Math.min(minCatHunger, registry.get(id).need(com.trojia.sim.actor.Need.HUNGER));
        }
        System.out.println("  hunt loop: catches=" + huntCounters[0] + " revives=" + huntCounters[1]
                + " miceDownNow=" + downNow + " liveMiceNearDen=" + atDen + "/"
                + (mouseIds.size() - downNow));
        System.out.println("  hunger floor at soak end: minGull=" + minGullHunger
                + " minCat=" + minCatHunger + " (starving would read 0)");
        System.out.println("============================================================================");
    }

    // ==================================================================================
    // S6 acceptance reports (the observer-diagnosis metrics, now twin-run evidence)
    // ==================================================================================

    /**
     * S6 motivation report (Eli's bugs 1+3): the end-of-soak DUTY census by type (the
     * diagnosis found 100%-bottomed watch/serfs/shopkeepers/clergy by day 1 with the
     * wastrel the only type holding 10000) and the per-day work-window samples (tod 5000:
     * working / DUTY-out / house-arrest / held among the living). Ascending TreeMap scan.
     */
    private static void printMotivationReport(ActorRegistry registry,
            List<long[]> motivationSamples) {
        Map<String, long[]> byType = new java.util.TreeMap<>(); // type -> {total, dutyZero, dutySum}
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (a.hasStatus(com.trojia.sim.actor.StatusBit.DEAD)) {
                continue; // the dead hold no motivations
            }
            long[] row = byType.computeIfAbsent(a.typeId().key(), k -> new long[3]);
            int duty = a.need(com.trojia.sim.actor.Need.DUTY);
            row[0]++;
            row[1] += duty == 0 ? 1 : 0;
            row[2] += duty;
        }
        System.out.println();
        System.out.println("================ S6 MOTIVATION (DUTY census + work-window idleness) =========");
        System.out.printf("  %-22s %6s %10s %9s%n", "type (living)", "total", "DUTY==0", "avgDUTY");
        for (Map.Entry<String, long[]> e : byType.entrySet()) {
            long[] row = e.getValue();
            System.out.printf("  %-22s %6d %10d %9d%n", e.getKey(), row[0], row[1],
                    row[0] == 0 ? 0 : row[2] / row[0]);
        }
        System.out.println("  work-window samples (tod=5000 of each day, living souls):");
        for (long[] s : motivationSamples) {
            System.out.println("    tick=" + s[0] + "  working(JOB_GOAL)=" + s[1]
                    + "  DUTY-out=" + s[2] + "  houseArrest=" + s[3] + "  held=" + s[4]);
        }
        System.out.println("============================================================================");
    }

    /**
     * S6 movement-wave profile (Eli's bug 4, "same places at the same time"): average
     * district movement per tick, bucketed by time-of-day ({@link #WAVE_BUCKET_TICKS}-tick
     * buckets). The diagnosis measured a flat ~78 with commute spikes at tod 1000 and
     * 11000; staggered rounds/shifts should flatten those two buckets. Average is printed
     * x100 in integer math (no float formatting on the twin-compared path).
     */
    private static void printMovementWaveReport(long[] waveMoved, long[] waveTicks) {
        System.out.println();
        System.out.println("================ S6 MOVEMENT WAVES (avg moves/tick x100, by tod bucket) =====");
        long peak = 0;
        int peakBucket = -1;
        StringBuilder line = new StringBuilder();
        for (int b = 0; b < waveMoved.length; b++) {
            if (waveTicks[b] == 0) {
                continue;
            }
            long avgX100 = waveMoved[b] * 100 / waveTicks[b];
            if (avgX100 > peak) {
                peak = avgX100;
                peakBucket = b;
            }
            line.append(String.format("  tod=%5d %6d", b * WAVE_BUCKET_TICKS, avgX100));
            if ((b + 1) % 4 == 0) {
                System.out.println(line);
                line.setLength(0);
            }
        }
        if (line.length() > 0) {
            System.out.println(line);
        }
        System.out.println("  peak bucket: tod=" + (peakBucket * WAVE_BUCKET_TICKS)
                + " avg x100 = " + peak
                + "  (diagnosis baseline: spikes ~9700 at tod 1000 and ~9650 at 11000 over ~7800 flat)");
        System.out.println("============================================================================");
    }

    /**
     * S6 statue census (Eli's bug 4): distinct cells each serf.laborer visited over the
     * final day (or the whole run when shorter) — the diagnosis found 76 of 378 (20%)
     * touching &le;3 cells ALL DAY. Prints the &le;3-cell statue count and the median.
     * Only set SIZES are read (HashSet membership is counted, never iterated).
     */
    private static void printStatueReport(Map<Integer, java.util.HashSet<Integer>> laborerCells,
            int ticks) {
        List<Integer> sizes = new ArrayList<>();
        int statues = 0;
        for (Map.Entry<Integer, java.util.HashSet<Integer>> e : laborerCells.entrySet()) {
            int size = e.getValue().size();
            sizes.add(size);
            if (size <= 3) {
                statues++;
            }
        }
        sizes.sort(Integer::compare);
        int median = sizes.isEmpty() ? 0 : sizes.get(sizes.size() / 2);
        System.out.println();
        System.out.println("================ S6 STATUE CENSUS (serf.laborer distinct cells, final day) ==");
        System.out.println("  window: last " + Math.min(ticks, DailyRhythm.DAY) + " ticks;  laborers: "
                + sizes.size() + ";  statues (<=3 distinct cells): " + statues
                + ";  median distinct cells: " + median);
        System.out.println("  (diagnosis baseline: 76/378 statues, median 33)");
        System.out.println("============================================================================");
    }

    /**
     * S6 fishing report (Eli's bug 6): the baked zone census, the live spots right now
     * (slot / size / zone / cast stand), the closed-supply FISH conservation identity
     * ({@code minted == live + eaten} — a sold fish MOVES, only eating sinks it), and the
     * FISHING skill census. Ascending scans only.
     */
    private static void printFishingReport(DocksPopulation population) {
        var system = population.system();
        var spots = system.fishingSpots();
        var tracks = system.skillTracks();
        var registry = population.registry();
        long minted = system.fishMinted();
        long eaten = system.fishEaten();
        int live = population.items().liveOfKind(ItemKinds.FISH);
        System.out.println();
        System.out.println("================ S6 FISHING (the water gives) ===============================");
        System.out.println("  zones baked: " + spots.zones().zoneCount()
                + ";  live spots now: " + spots.liveCount()
                + " (caps small/med/large = " + com.trojia.sim.actor.FishingSpots.CAP_SMALL
                + "/" + com.trojia.sim.actor.FishingSpots.CAP_MEDIUM
                + "/" + com.trojia.sim.actor.FishingSpots.CAP_LARGE + ")");
        String[] sizes = {"small", "medium", "large"};
        for (int s = 0; s < spots.slotCapacity(); s++) {
            if (spots.isLive(s)) {
                System.out.println("    slot " + s + ": " + sizes[spots.sizeClassAt(s)]
                        + " zone " + spots.zoneAt(s) + " cast=" + xyz(spots.castCellAt(s))
                        + " surfaced t=" + spots.spawnTickAt(s)
                        + " sinks t=" + spots.expiryTickAt(s));
            }
        }
        System.out.println("  FISH minted=" + minted + " (catches);  live(held)=" + live
                + "  eaten(sunk)=" + eaten + ";  invariant minted == live + eaten: "
                + (minted == live + eaten));
        int holders = 0;
        StringBuilder census = new StringBuilder();
        for (int i = 0; i < registry.size(); i++) {
            int level = tracks.level(i, tracks.fishingRaw());
            if (level > 0) {
                holders++;
                if (holders <= 12) {
                    census.append(holders == 1 ? "" : ", ").append('#').append(i)
                            .append("=L").append(level);
                }
            }
        }
        System.out.println("  FISHING skill holders: " + holders
                + (holders > 0 ? "  (" + census + (holders > 12 ? ", ..." : "") + ")" : ""));
        System.out.println("============================================================================");
    }

    /**
     * SIMPLE MAGIC report: the public shelf as the bake actually bound it — every crafting,
     * what it moves, how far it reaches, what it is checked against and what a novice's odds
     * on it are — plus whatever lingering effects are live right now and the LINKCRAFT census.
     *
     * <p><b>Why the odds are printed here and not just on a toast.</b> Nothing clamps
     * magnitude in this system: {@code SpellCost} prices what a crafting moves and how far,
     * and the check does the rest. That makes the resist column the entire balance argument,
     * and an argument nobody can read is an argument nobody can catch being wrong. A tenth
     * crafting added to the raws tomorrow prints its own row here with no code change, which
     * is the same property the whole pass is built on.
     *
     * <p>No AI works craftings this pass, so an inputless soak prints an empty effect table
     * and a zero census — and that is the honest reading, not a hidden failure. Ascending
     * scans only; every number is a pure read.
     */
    private static void printCraftingsReport(DocksPopulation population) {
        var system = population.system();
        var spells = system.spells();
        var effects = system.activeEffects();
        var tracks = system.skillTracks();
        var registry = population.registry();
        System.out.println();
        System.out.println("================ SIMPLE MAGIC (the public shelf) ============================");
        System.out.println("  craftings bound: " + spells.size()
                + ";  gated on LINKCRAFT (WIT);  lingering rows live: " + effects.liveCount()
                + "/" + effects.slotCapacity());
        for (int raw = 0; raw < spells.size(); raw++) {
            var spell = spells.get(raw);
            int resist = com.trojia.sim.actor.spell.SpellCost.resistFor(spell, spell.reach());
            int noviceOdds = com.trojia.sim.actor.SkillChecks.linkcraftPermille(
                    com.trojia.sim.actor.SkillTrackRegistry.UNWIRED, 0, resist);
            StringBuilder parts = new StringBuilder();
            for (int c = 0; c < spell.components().size(); c++) {
                var part = spell.components().get(c);
                parts.append(c == 0 ? "" : " + ").append(part.kind()).append(' ')
                        .append(part.magnitude() >= 0 ? "+" : "").append(part.magnitude());
                if (part.durationTicks() > 0) {
                    parts.append('/').append(part.durationTicks()).append('t');
                }
            }
            System.out.println("    " + pad(spell.key(), 20)
                    + pad(spell.targetShape().name().toLowerCase(java.util.Locale.ROOT), 7)
                    + " Lv" + spell.minLevel()
                    + "  resist " + pad(Integer.toString(resist), 4)
                    + "  novice " + (noviceOdds / 10) + "%"
                    + "  cd " + pad(spell.cooldownTicks() + "t", 6)
                    + "  " + parts);
        }
        for (int s = 0; s < effects.slotCapacity(); s++) {
            if (effects.isLive(s)) {
                System.out.println("    live: #" + effects.targetAt(s) + " "
                        + effects.kindAt(s) + " " + effects.magnitudeAt(s) + " "
                        + effects.modeAt(s) + " until t=" + effects.endTickAt(s));
            }
        }
        int holders = 0;
        for (int i = 0; i < registry.size(); i++) {
            if (tracks.level(i, tracks.linkcraftRaw()) > 0) {
                holders++;
            }
        }
        System.out.println("  LINKCRAFT skill holders: " + holders
                + "  (no AI works craftings this pass -- the played soul is the only caster)");
        System.out.println("============================================================================");
    }

    /**
     * S8 TRADE-GOOD conservation (one closed-supply line per kind) plus the holder
     * DISTRIBUTION each kind actually landed in.
     *
     * <p><b>Why seven lines and not one.</b> A single lumped "goods minted" total is exactly
     * the kind of number that reads PASS while a yard mints nothing — one busy Ropewalk can
     * carry three dead yards. Each kind gets its own identity, {@code minted == live + sunk},
     * and each can fail alone.
     *
     * <p><b>Why the identity can fail at all</b> (the lesson of the coin-proof defect this
     * sprint opened by fixing): the left side is a COUNTER incremented at the mint site
     * ({@code ActorsSystem.goodsMinted}); the right side is an independent physical SCAN of
     * ItemsLite. Neither is derived from the other. A mint that skipped the counter, a stack
     * destroyed behind the economy's back, or a double-credit all break it. The sunk side is
     * the counter and NOT {@code items.sunkOfKind} — a vacated slot keeps its old quantity, so
     * a fully MOVED stack would read as a phantom sink (see {@code CoinCensus}).
     *
     * <p><b>Distributions, not totals.</b> Every line prints DISTINCT holders and the fattest
     * single holding beside the unit count, because "200 units" is satisfied equally by 200
     * souls holding one each and by one hoarder holding 200, and only one of those is a ward
     * that trades. Ascending-index scans only.
     */
    private static void printGoodsConservation(DocksPopulation population,
            IdentityRegistry identity) {
        var items = population.items();
        var system = population.system();
        var registry = population.registry();
        System.out.println();
        System.out.println("================ S8 TRADE GOODS (closed supply, per kind) ==================");
        for (short kind : GoodsCensus.KINDS) {
            GoodsCensus c = GoodsCensus.of(system, items, registry, kind);
            String top = c.fattestId() < 0 ? "-"
                    : (c.fattestId() < identity.size() && identity.get(c.fattestId()).named()
                            ? identity.get(c.fattestId()).fullName() : "#" + c.fattestId())
                            + " x" + c.fattest();
            System.out.println("  " + pad(c.symbol(), 13) + " [" + c.category() + "]"
                    + "  minted=" + c.minted() + "  live=" + c.live() + "  sunk=" + c.sunk()
                    + ";  invariant minted == live + sunk: " + c.closed()
                    + "  (" + c.minted() + " == " + (c.live() + c.sunk()) + ")");
            System.out.println("      distribution: " + c.holders() + " DISTINCT holders"
                    + ";  fattest holding: " + top);
        }
        // The combined line closes the last hole a hostile reader could pick at: summing the
        // seven per-kind holder counts would double-count anyone holding two kinds. This
        // counts SOULS, once each, across every non-food good.
        long allUnits = 0;
        int allHolders = 0;
        for (short kind : GoodsCensus.KINDS) {
            allUnits += items.liveOfKind(kind);
        }
        for (int i = 0; i < registry.size(); i++) {
            for (short kind : GoodsCensus.KINDS) {
                if (items.countCarriedOfKind(i, kind) > 0) {
                    allHolders++;
                    break;
                }
            }
        }
        System.out.println("  ANY non-food good: " + allUnits + " units live across "
                + allHolders + " DISTINCT souls (each soul counted once)");
        System.out.println("============================================================================");

        // The vermin bounty, read as a DISTRIBUTION. The scalp totals above are a supply
        // number and one soul beside one den could produce all of them; what says the bounty
        // is a ward PRACTICE is how many different hands took one. Ascending-id scan.
        var system2 = population.system();
        int cullers = system2.distinctCullers();
        long scalps = 0;
        for (short kind : new short[] {ItemKinds.RAT_SCALP, ItemKinds.GULL_SCALP,
                ItemKinds.CAT_SCALP}) {
            scalps += system2.goodsMinted(kind);
        }
        StringBuilder busiest = new StringBuilder();
        int shown = 0;
        int bestTally = 0;
        for (int i = 0; i < registry.size(); i++) {
            bestTally = Math.max(bestTally, system2.scalpsTakenBy(i));
        }
        for (int i = 0; i < registry.size() && shown < 10; i++) {
            if (system2.scalpsTakenBy(i) > 0) {
                String who = i < identity.size() && identity.get(i).named()
                        ? identity.get(i).fullName() : "#" + i;
                busiest.append(shown == 0 ? "" : ", ").append(who)
                        .append(" x").append(system2.scalpsTakenBy(i));
                shown++;
            }
        }
        System.out.println();
        System.out.println("================ S8 THE VERMIN BOUNTY (who actually culls) =================");
        System.out.println("  scalps harvested: " + scalps
                + ";  DISTINCT cullers: " + cullers
                + ";  busiest single hand: " + bestTally
                + "  (latch: one attempt per " + com.trojia.sim.actor.CullVerb.CULL_COOLDOWN_TICKS
                + " ticks per soul)");
        System.out.println("  first cullers by id: "
                + (busiest.length() == 0 ? "(nobody)" : busiest
                        + (cullers > shown ? ", ..." : "")));
        System.out.println("============================================================================");
    }

    /** Right-pads {@code s} to {@code width} with spaces (Locale-free, report-text stable). */
    private static String pad(String s, int width) {
        StringBuilder b = new StringBuilder(s == null ? "?" : s);
        while (b.length() < width) {
            b.append(' ');
        }
        return b.toString();
    }

    /**
     * S6 death report (Eli's bug 7): the DeathLog toll BY NAME (the client feed's own
     * source), the resident-dead census, and the roster invariant — the registry never
     * removes a soul, so the count stays 692 however many die.
     */
    private static void printDeathReport(DocksPopulation population, IdentityRegistry identity) {
        var log = population.system().deathLog();
        var registry = population.registry();
        int deadNow = 0;
        for (int i = 0; i < registry.size(); i++) {
            if (registry.get(i).hasStatus(com.trojia.sim.actor.StatusBit.DEAD)) {
                deadNow++;
            }
        }
        System.out.println();
        System.out.println("================ S6 DEATH (rare, real, announced by name) ===================");
        System.out.println("  deaths recorded (monotonic toll): " + log.totalRecorded()
                + ";  resident dead in the roster: " + deadNow
                + ";  roster size (never shrinks): " + registry.size());
        for (int i = 0; i < log.size(); i++) {
            int id = log.actorIdAt(i);
            String name = id >= 0 && id < identity.size() && identity.get(id).named()
                    ? identity.get(id).fullName() : "#" + id;
            System.out.println("    tick=" + log.tickAt(i) + "  " + name
                    + " [" + registry.get(id).typeId().key() + "] -- " + log.causeAt(i));
        }
        if (log.size() == 0) {
            System.out.println("    (nobody died this soak -- death stays rare)");
        }
        System.out.println("============================================================================");
    }

    private static List<Integer> idsOfType(ActorRegistry registry, String typeKey) {
        List<Integer> ids = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            if (registry.get(i).typeId().key().equals(typeKey)) {
                ids.add(i);
            }
        }
        return ids;
    }

    /** Roam stats for one beast: distinct cells, bounding box, longest single-cell stall. */
    private static final class BeastRoam {
        private final java.util.HashSet<Integer> visited = new java.util.HashSet<>();
        private int minX = Integer.MAX_VALUE;
        private int maxX = Integer.MIN_VALUE;
        private int minY = Integer.MAX_VALUE;
        private int maxY = Integer.MIN_VALUE;
        private int lastCell = Actor.NONE;
        private int run;
        private int maxRun;

        void observe(int cell) {
            visited.add(cell);
            minX = Math.min(minX, PackedPos.x(cell));
            maxX = Math.max(maxX, PackedPos.x(cell));
            minY = Math.min(minY, PackedPos.y(cell));
            maxY = Math.max(maxY, PackedPos.y(cell));
            run = cell == lastCell ? run + 1 : 1;
            lastCell = cell;
            maxRun = Math.max(maxRun, run);
        }
    }

    /** Actor-type/job composition — the report's headline numbers, printed deterministically. */
    private static void printComposition(ActorRegistry registry, JobRegistry jobs) {
        Map<String, Integer> byType = new LinkedHashMap<>();
        Map<String, Integer> byJob = new java.util.TreeMap<>();
        int villains = 0;
        int commuters = 0;
        int undifferentiated = 0; // serf.laborer + wastrel.streetlife (idle/undifferentiated labor)
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            byType.merge(a.typeId().key(), 1, Integer::sum);
            Job job = a.jobOrdinal() >= 0 ? jobs.get(a.jobOrdinal()) : null;
            String trueJobId = JobDisplay.trueJobId(job);
            byJob.merge(trueJobId, 1, Integer::sum);
            if (job != null && (trueJobId.equals("serf.laborer") || trueJobId.equals("wastrel.streetlife"))) {
                undifferentiated++;
            }
            if (JobDisplay.isSecret(job)) {
                villains++;
            }
            if (a.anchorCell() != a.cell()) {
                commuters++;   // spawned at home with a distinct work anchor
            }
        }
        System.out.println("composition by type: " + byType);
        System.out.println("composition by job (true): " + byJob);
        System.out.println("undifferentiated labor (serf.laborer + wastrel.streetlife): "
                + undifferentiated
                + "; specialised dock trades: maritime.sailor=" + byJob.getOrDefault("maritime.sailor", 0)
                + " trade.trader=" + byJob.getOrDefault("trade.trader", 0));
        System.out.println("secret villain jobs under covers: " + villains
                + "; actors with a distinct work anchor (commuters): " + commuters);
    }

    // ==================================================================================
    // Daily-life proof
    // ==================================================================================

    private static void printDailyLifeProof(ActorRegistry registry, JobRegistry jobs,
            Tracked commuter, Tracked patroller, Tracked wanderer, Tracked keeper,
            List<Tracked> beasts) {
        System.out.println();
        System.out.println("================ DAILY-LIFE MOVEMENT PROOF (real position deltas) ===========");

        if (commuter != null) {
            Actor a = registry.get(commuter.id);
            System.out.println();
            System.out.println("(a) COMMUTER  " + label(a, jobs) + "  home=" + xyz(commuter.spawnCell)
                    + " workplace(anchor)=" + xyz(a.anchorCell())
                    + "  (Chebyshev home<->work = "
                    + chebyshev(commuter.spawnCell, a.anchorCell()) + ")");
            for (long[] s : commuter.samples) {
                long tod = DailyRhythm.tickOfDay(s[0]);
                int cell = (int) s[1];
                String where = cell == a.anchorCell() ? "AT WORKPLACE"
                        : cell == commuter.spawnCell ? "AT HOME"
                        : "en route (d_work=" + chebyshev(cell, a.anchorCell())
                                + ", d_home=" + chebyshev(cell, commuter.spawnCell) + ")";
                System.out.println("    tick=" + s[0] + " (tod=" + tod + ", "
                        + (tod >= 1000 && tod < 11000 ? "ON SHIFT" : "off shift") + ")  pos="
                        + xyz(cell) + "  -> " + where);
            }
        }

        if (patroller != null) {
            System.out.println();
            System.out.println("(b) WATCH PATROL  actor#" + patroller.id + "  spawn/post="
                    + xyz(patroller.spawnCell));
            System.out.println("    visited bounding box over the run: x[" + patroller.minX + ","
                    + patroller.maxX + "] y[" + patroller.minY + "," + patroller.maxY
                    + "]  span=" + patroller.spanX() + "x" + patroller.spanY()
                    + "  (a point would be 0x0; a real beat spans the loop)");
            printSamples(patroller);
        }

        if (wanderer != null) {
            System.out.println();
            System.out.println("(c) WASTREL WANDER  actor#" + wanderer.id + "  spawn="
                    + xyz(wanderer.spawnCell));
            System.out.println("    visited bounding box over the run: x[" + wanderer.minX + ","
                    + wanderer.maxX + "] y[" + wanderer.minY + "," + wanderer.maxY
                    + "]  span=" + wanderer.spanX() + "x" + wanderer.spanY());
            int maxDisplacement = 0;
            for (long[] s : wanderer.samples) {
                int d = chebyshev(wanderer.spawnCell, (int) s[1]);
                maxDisplacement = Math.max(maxDisplacement, d);
                long tod = DailyRhythm.tickOfDay(s[0]);
                System.out.println("    tick=" + s[0] + " (tod=" + tod + ", "
                        + (tod >= 2000 && tod < 10000 ? "roaming" : "home-ward at night")
                        + ")  pos=" + xyz((int) s[1]) + "  displacement from spawn="
                        + d + " tiles");
            }
            System.out.println("    max sampled displacement from spawn: " + maxDisplacement
                    + " tiles (wanders by day, returns to the hut at night)");
        }

        if (keeper != null && !beasts.isEmpty()) {
            System.out.println();
            System.out.println("(d) OWNER-FOLLOW  Keeper actor#" + keeper.id + " spawn="
                    + xyz(keeper.spawnCell));
            for (int s = 0; s < keeper.samples.size(); s++) {
                long tick = keeper.samples.get(s)[0];
                int keeperCell = (int) keeper.samples.get(s)[1];
                StringBuilder line = new StringBuilder();
                line.append("    tick=").append(tick).append("  keeper=").append(xyz(keeperCell))
                        .append(" (moved ").append(chebyshev(keeper.spawnCell, keeperCell))
                        .append(" from spawn)");
                for (Tracked b : beasts) {
                    int beastCell = sampleAt(b, tick);
                    if (beastCell != Actor.NONE) {
                        line.append("  | ").append(b.label).append("=").append(xyz(beastCell))
                                .append(" d_keeper=").append(chebyshev(beastCell, keeperCell));
                    }
                }
                System.out.println(line);
            }
        }
        System.out.println("============================================================================");
    }

    private static void printSamples(Tracked t) {
        for (long[] s : t.samples) {
            System.out.println("    tick=" + s[0] + " (tod=" + DailyRhythm.tickOfDay(s[0]) + ")  pos="
                    + xyz((int) s[1]));
        }
    }

    // ==================================================================================
    // Tracked-actor identification
    // ==================================================================================

    /**
     * First Serf whose work anchor differs from its home cell AND whose workplace is one of
     * the waterfront anchors (y < 32 in map space) — a genuine dockworker commuter, the
     * "waterfront visibly works" proof subject.
     */
    private static Tracked trackCommuter(ActorRegistry registry, HomeRegistry homes) {
        Tracked fallback = null;
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (!a.typeId().key().equals("serf") || a.homeId() == Actor.NONE) {
                continue;
            }
            int homeCell = homes.get(a.homeId()).homeCell();
            if (a.anchorCell() == homeCell) {
                continue;
            }
            if (fallback == null) {
                fallback = new Tracked(i, "commuter", homeCell);
            }
            if (PackedPos.y(a.anchorCell()) < 32 + 32) {   // CHUNK_SIZE_Y pad + waterfront rows
                return new Tracked(i, "dock commuter", homeCell);
            }
        }
        return fallback;
    }

    private static int firstOfType(ActorRegistry registry, String typeKey) {
        for (int i = 0; i < registry.size(); i++) {
            if (registry.get(i).typeId().key().equals(typeKey)) {
                return i;
            }
        }
        return Actor.NONE;
    }

    /** First Wastrel that was not deliberately displaced as a demo mover. */
    private static int firstWanderer(ActorRegistry registry, List<Integer> movers) {
        for (int i = 0; i < registry.size(); i++) {
            if (registry.get(i).typeId().key().equals("wastrel") && !movers.contains(i)) {
                return i;
            }
        }
        return firstOfType(registry, "wastrel");
    }

    private static Tracked track(ActorRegistry registry, int id, String label) {
        if (id == Actor.NONE) {
            return null;
        }
        return new Tracked(id, label, registry.get(id).cell());
    }

    private static void addIfPresent(List<Tracked> out, Tracked... tracks) {
        for (Tracked t : tracks) {
            if (t != null) {
                out.add(t);
            }
        }
    }

    /** A tracked actor's spawn cell, per-tick visited bounding box, and sampled positions. */
    private static final class Tracked {
        private final int id;
        private final String label;
        private final int spawnCell;
        private int minX = Integer.MAX_VALUE;
        private int maxX = Integer.MIN_VALUE;
        private int minY = Integer.MAX_VALUE;
        private int maxY = Integer.MIN_VALUE;
        private final List<long[]> samples = new ArrayList<>();

        Tracked(int id, String label, int spawnCell) {
            this.id = id;
            this.label = label;
            this.spawnCell = spawnCell;
        }

        void observe(int cell) {
            minX = Math.min(minX, PackedPos.x(cell));
            maxX = Math.max(maxX, PackedPos.x(cell));
            minY = Math.min(minY, PackedPos.y(cell));
            maxY = Math.max(maxY, PackedPos.y(cell));
        }

        void sample(long tick, int cell) {
            samples.add(new long[] {tick, cell});
        }

        int spanX() {
            return maxX - minX;
        }

        int spanY() {
            return maxY - minY;
        }
    }

    private static int sampleAt(Tracked t, long tick) {
        for (long[] s : t.samples) {
            if (s[0] == tick) {
                return (int) s[1];
            }
        }
        return Actor.NONE;
    }

    private static String label(Actor a, JobRegistry jobs) {
        Job job = a.jobOrdinal() >= 0 ? jobs.get(a.jobOrdinal()) : null;
        return "actor#" + a.id() + " " + a.typeId().key() + " [" + JobDisplay.trueJobId(job) + "]";
    }

    private static int chebyshev(int cellA, int cellB) {
        return Math.max(Math.abs(PackedPos.x(cellA) - PackedPos.x(cellB)),
                Math.abs(PackedPos.y(cellA) - PackedPos.y(cellB)));
    }

    // ==================================================================================
    // Roster / mover / graph listings
    // ==================================================================================

    private static void printRoster(ActorRegistry registry, HomeRegistry homes, JobRegistry jobs,
            IdentityRegistry identity) {
        System.out.println();
        System.out.printf("%-3s %-24s %-18s %-22s %-18s %-18s %-5s %-13s %-13s %-13s %-10s %s%n",
                "id", "name", "epithet", "type", "job(true)", "presents", "home", "homeCell",
                "anchorCell", "position", "goalState", "needs(H/R/C/S/D)");
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            Job job = actor.jobOrdinal() >= 0 ? jobs.get(actor.jobOrdinal()) : null;
            String trueJob = JobDisplay.trueJobId(job);
            String presented = JobDisplay.presentedJobId(job);
            String cover = JobDisplay.isSecret(job) ? "  <-- secret" : "";
            IdentityRegistry.Identity who = identity.get(i);
            Home home = homes.get(actor.homeId());
            short[] needs = actor.needsSnapshot();
            System.out.printf(
                    "%-3d %-24s %-18s %-22s %-18s %-18s %-5d %-13s %-13s %-13s %-10s %d/%d/%d/%d/%d%s%n",
                    actor.id(), who.fullName(), who.epithet(), actor.typeId().key(), trueJob,
                    presented, actor.homeId(), xyz(home.homeCell()), xyz(actor.anchorCell()),
                    xyz(actor.cell()), actor.goalState(),
                    needs[0], needs[1], needs[2], needs[3], needs[4], cover);
        }
        System.out.println("homes baked: " + homes.size());
        int namedCount = 0;
        int notableCount = 0;
        for (int i = 0; i < identity.size(); i++) {
            if (identity.get(i).named()) {
                namedCount++;
            }
            if (identity.get(i).notableId() != null) {
                notableCount++;
            }
        }
        System.out.println("identity table (NameForge): " + namedCount + " named of "
                + identity.size() + " souls (" + notableCount
                + " authored notables; ferals/mice/cats deliberately nameless)");
    }

    /**
     * Template-bio legibility sample: the first FORGED (non-notable) soul of each actor type,
     * ascending id — deterministic, so twin runs stay byte-identical.
     */
    private static void printBioSamples(ActorRegistry registry, IdentityRegistry identity) {
        System.out.println();
        System.out.println("identity bio samples (first forged soul of each type):");
        java.util.Set<String> seenTypes = new java.util.HashSet<>();
        for (int i = 0; i < registry.size(); i++) {
            IdentityRegistry.Identity who = identity.get(i);
            if (who.notableId() != null || who.bio().isBlank()) {
                continue;
            }
            if (!seenTypes.add(registry.get(i).typeId().key())) {
                continue;
            }
            System.out.println("  actor#" + i + " " + who.fullName()
                    + (who.epithet().isBlank() ? "" : " \"" + who.epithet() + "\"")
                    + " [" + registry.get(i).typeId().key() + "]: " + who.bio());
        }
    }

    /** The Forty Notables listing: authored identity, binding, and the full bio. */
    private static void printNotables(ActorRegistry registry, HomeRegistry homes,
            JobRegistry jobs, IdentityRegistry identity) {
        System.out.println();
        System.out.println("================ THE FORTY NOTABLES (authored, bound by spawn site) =========");
        for (int i = 0; i < registry.size(); i++) {
            IdentityRegistry.Identity who = identity.get(i);
            if (who.notableId() == null) {
                continue;
            }
            Actor actor = registry.get(i);
            Job job = actor.jobOrdinal() >= 0 ? jobs.get(actor.jobOrdinal()) : null;
            int homeCell = actor.homeId() == Actor.NONE ? actor.cell()
                    : homes.get(actor.homeId()).homeCell();
            System.out.println("  [" + who.notableId() + "] " + who.fullName() + ", "
                    + who.epithet() + "  -- actor#" + i + " [" + actor.typeId().key() + " / "
                    + JobDisplay.presentedJobId(job) + "] home=" + xyz(homeCell));
            System.out.println("      " + who.bio());
        }
        System.out.println("============================================================================");
    }

    private static void printMoversAfter(DocksPopulation population, long tick,
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

    private static boolean hasFlag(String[] args, String flag) {
        for (String arg : args) {
            if (flag.equals(arg)) {
                return true;
            }
        }
        return false;
    }
}
