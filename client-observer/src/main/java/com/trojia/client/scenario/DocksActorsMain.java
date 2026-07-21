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
        }

        printRoster(registry, homes, jobs);
        printMoversAfter(population, driver.currentTick(), movers);
        printGraphSample(homes, relationships);
        System.out.println("items minted (placeholder ids + quantities, §11.2): "
                + population.items().size());
        printEconomyProof(population);
        printFoodConservation(population);
        printStarvationByClass(registry);
        printSerfStarvationByBand(registry, jobs);
        printMoneyGateProof(population, jobs);
        printJusticeReport(population, jobs);
        printBeastReport(population, gullIds, catIds, mouseIds, gullRoam, huntCounters);
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
