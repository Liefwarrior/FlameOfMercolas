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
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.PackedPos;

import java.util.ArrayList;
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

    public static void main(String[] args) {
        int ticks = parseTicks(args, 50_000);
        boolean perf = hasFlag(args, "--perf");

        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());

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

        // Sample at a work-window tick (tod 5000) and a deep-night tick (tod 20000) of each day.
        List<Long> sampleTicks = new ArrayList<>();
        for (long d = 0; d * DailyRhythm.DAY + 20_000 <= ticks; d++) {
            sampleTicks.add(d * DailyRhythm.DAY + 5_000);
            sampleTicks.add(d * DailyRhythm.DAY + 20_000);
        }

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
        }

        printRoster(registry, homes, jobs);
        printMoversAfter(population, driver.currentTick(), movers);
        printGraphSample(homes, relationships);
        System.out.println("items minted (placeholder ids + quantities, §11.2): "
                + population.items().size());
        printEconomyProof(population);
        printDailyLifeProof(registry, jobs, commuter, patroller, wanderer, keeper, beasts);
        if (perf) {
            // Wall-clock timing — printed only under --perf so plain runs stay byte-identical.
            double avgMillis = tickNanos / 1e6 / ticks;
            System.out.println();
            System.out.printf("PERF: %d ticks in %.1f ms wall-clock (engine tick only) -> "
                            + "avg %.4f ms/tick at %d actors (observer FAST budget: 25 ms/tick)%n",
                    ticks, tickNanos / 1e6, avgMillis, registry.size());
        }
    }

    /**
     * Money-conservation proof (Phase-2): after the run, the hard invariant
     * {@code totalRoyals() == vault COIN count} must still hold, and the closed COIN supply
     * (minted == vault + loose-on-persons + sunk) must be intact — wages and any counter traffic
     * only ever MOVED Royals, never minted them.
     */
    private static void printEconomyProof(DocksPopulation population) {
        var bank = population.bankAccounts();
        var items = population.items();
        int vaultCell = DocksPopulation.bankVaultChestCell();
        long totalRoyals = bank.totalRoyals();
        int vaultCoins = items.countOnCellOfKind(vaultCell, ItemKinds.COIN);
        int mintedCoin = items.totalOfKind(ItemKinds.COIN);
        int sunkCoin = items.sunkOfKind(ItemKinds.COIN);
        int looseCoin = mintedCoin - vaultCoins - sunkCoin; // coins carried on persons
        System.out.println();
        System.out.println("================ MONEY CONSERVATION (closed supply) ========================");
        System.out.println("  accounts open: " + bank.accountCount()
                + " (per-actor + 1 employer pool);  totalRoyals=" + totalRoyals
                + "  vaultCOIN=" + vaultCoins
                + "  -> invariant totalRoyals==vaultCOIN: " + (totalRoyals == vaultCoins));
        System.out.println("  COIN supply: minted=" + mintedCoin + " == vault(" + vaultCoins
                + ") + loose(" + looseCoin + ") + sunk(" + sunkCoin + "): "
                + (mintedCoin == vaultCoins + looseCoin + sunkCoin));
        System.out.println("============================================================================");
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

    private static void printRoster(ActorRegistry registry, HomeRegistry homes, JobRegistry jobs) {
        System.out.println();
        System.out.printf("%-3s %-22s %-18s %-18s %-5s %-13s %-13s %-13s %-10s %s%n",
                "id", "type", "job(true)", "presents", "home", "homeCell", "anchorCell", "position",
                "goalState", "needs(H/R/C/S/D)");
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            Job job = actor.jobOrdinal() >= 0 ? jobs.get(actor.jobOrdinal()) : null;
            String trueJob = JobDisplay.trueJobId(job);
            String presented = JobDisplay.presentedJobId(job);
            String cover = JobDisplay.isSecret(job) ? "  <-- secret" : "";
            Home home = homes.get(actor.homeId());
            short[] needs = actor.needsSnapshot();
            System.out.printf("%-3d %-22s %-18s %-18s %-5d %-13s %-13s %-13s %-10s %d/%d/%d/%d/%d%s%n",
                    actor.id(), actor.typeId().key(), trueJob, presented, actor.homeId(),
                    xyz(home.homeCell()), xyz(actor.anchorCell()), xyz(actor.cell()), actor.goalState(),
                    needs[0], needs[1], needs[2], needs[3], needs[4], cover);
        }
        System.out.println("homes baked: " + homes.size());
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
