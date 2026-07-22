package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.NeedThresholds;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The beast-food-channel soak (living-docks beast pass, 30,000 ticks over the real baked
 * docks): gulls RANGE (distinct cells + bounding box + no long single-cell stalls — the
 * anti-oscillation DoD), the hunt loop actually turns (catches land, every caught mouse
 * revives on schedule and stands near its den), no beast starves, the citizen economy bars
 * hold (serf &le; 5% starved, middle class 0%, money + FOOD conservation), the roster stays
 * fixed at 691 and the occupancy cap holds throughout.
 */
class DocksBeastSoakTest {

    private static final int TICKS = 30_000;
    private static final int SAMPLE_PERIOD = 1_000;

    /**
     * Roam floors sized against the map, not the void: gull wander radius is 12 (leash 24 —
     * tuned down from a first-cut 48 because wide envelopes reach into crewed/walled
     * interiors where an occupancy wedge can outlast the hunger buffer), and the quay-edge
     * roosts lose half their envelope to harbor water. An oscillating gull scores 2-3
     * distinct cells in a 1x1-2x2 box, so these floors keep a ~50x regression margin.
     */
    private static final int MIN_GULL_DISTINCT_CELLS = 100;
    private static final int MIN_GULL_BBOX_SPAN = 16;
    /**
     * PASS 9 (density revisit) set this to 15000 while beasts could still be boxed by
     * occupancy-parked households; the fix pass RE-TIGHTENS it to 8000 as promised, now that
     * the push mechanic's squeeze-past swap + short pushee stagger let a crowd-locked beast
     * fight its way out and the hearth-gated wander keeps it out of bedrooms in the first
     * place (soak-observed gull maxima run ~600 under the fixed regime; the historical
     * failure signatures — an 8225-tick alcove pin, 3-10k condo boxes — all fail an 8000
     * cap, while a full night's roost sleep still passes). The anti-oscillation DoD is still
     * carried by the DISTINCT_CELLS and BBOX floors.
     */
    private static final int MAX_CONSECUTIVE_TICKS_ON_ONE_CELL = 8_000;
    private static final int MIN_STRAND_DISTINCT_CELLS = 10;
    private static final int MIN_TOTAL_CATCHES = 40;
    private static final int REVIVE_TICKS = 3_000; // BeastHuntPolicy.PREY_REVIVE_TICKS
    private static final int REVIVE_SLACK = 200;
    private static final int BEAST_HUNGER_FLOOR = 500;

    /** Per-beast roam tracker: distinct cells, bbox, longest single-cell stall. */
    private static final class Roam {
        final HashSet<Integer> visited = new HashSet<>();
        int minX = Integer.MAX_VALUE;
        int maxX = Integer.MIN_VALUE;
        int minY = Integer.MAX_VALUE;
        int maxY = Integer.MIN_VALUE;
        int lastCell = Actor.NONE;
        int run;
        int maxRun;
        int maxRunCell = Actor.NONE; // where the longest stall happened (failure forensics)

        void observe(int cell) {
            visited.add(cell);
            minX = Math.min(minX, PackedPos.x(cell));
            maxX = Math.max(maxX, PackedPos.x(cell));
            minY = Math.min(minY, PackedPos.y(cell));
            maxY = Math.max(maxY, PackedPos.y(cell));
            run = cell == lastCell ? run + 1 : 1;
            lastCell = cell;
            if (run > maxRun) {
                maxRunCell = cell;
            }
            maxRun = Math.max(maxRun, run);
        }
    }

    @Test
    void huntLoopTurnsGullsRangeNothingStarvesAndTheCitizenBarsHold() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        ActorRegistry registry = population.registry();
        assertEquals(691, registry.size(), "the beast-pass roster: 653 + 30 mice + 8 cats");

        List<Integer> gulls = new ArrayList<>();
        List<Integer> cats = new ArrayList<>();
        List<Integer> mice = new ArrayList<>();
        int mainBandZ = Integer.MIN_VALUE;
        for (int i = 0; i < registry.size(); i++) {
            switch (registry.get(i).typeId().key()) {
                case "feral" -> {
                    gulls.add(i);
                    mainBandZ = Math.max(mainBandZ, PackedPos.z(registry.get(i).cell()));
                }
                case "cat" -> cats.add(i);
                case "mouse" -> mice.add(i);
                default -> { }
            }
        }
        assertEquals(5, gulls.size());
        assertEquals(8, cats.size());
        assertEquals(30, mice.size());

        Map<Integer, Roam> roam = new HashMap<>();
        for (int id : gulls) {
            roam.put(id, new Roam());
        }
        Map<Integer, Integer> denOf = new HashMap<>();
        for (int id : mice) {
            denOf.put(id, population.homes().get(registry.get(id).homeId()).homeCell());
        }

        Map<Integer, Long> catchTick = new HashMap<>();
        Map<Integer, Boolean> wasDowned = new HashMap<>();
        for (int id : mice) {
            wasDowned.put(id, false);
        }
        int catches = 0;
        int revives = 0;
        long maxReviveLatency = 0;

        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));
        for (int t = 1; t <= TICKS; t++) {
            driver.requestStep();
            for (int id : gulls) {
                roam.get(id).observe(registry.get(id).cell());
            }
            for (int id : mice) {
                boolean downed = registry.get(id).hasStatus(StatusBit.DOWNED);
                if (downed && !wasDowned.get(id)) {
                    catches++;
                    catchTick.put(id, (long) t);
                } else if (!downed && wasDowned.get(id)) {
                    revives++;
                    long latency = t - catchTick.get(id);
                    maxReviveLatency = Math.max(maxReviveLatency, latency);
                    assertTrue(latency <= REVIVE_TICKS + REVIVE_SLACK,
                            "mouse#" + id + " took " + latency + " ticks to revive");
                }
                wasDowned.put(id, downed);
            }
            if (t % SAMPLE_PERIOD == 0) {
                assertOccupancyCapHolds(registry, "tick " + t);
                for (int id : gulls) {
                    assertTrue(registry.get(id).need(Need.HUNGER) >= BEAST_HUNGER_FLOOR,
                            "gull#" + id + " starving at tick " + t + " (HUNGER="
                                    + registry.get(id).need(Need.HUNGER) + ")");
                }
                for (int id : cats) {
                    assertTrue(registry.get(id).need(Need.HUNGER) >= BEAST_HUNGER_FLOOR,
                            "cat#" + id + " starving at tick " + t + " (HUNGER="
                                    + registry.get(id).need(Need.HUNGER) + ")");
                }
                for (int id : mice) {
                    assertTrue(registry.get(id).need(Need.HUNGER) >= NeedThresholds.LOW,
                            "mouse#" + id + " hungry at tick " + t + " (HUNGER="
                                    + registry.get(id).need(Need.HUNGER) + ")");
                }
            }
        }

        // ---- gull roam (the anti-oscillation DoD) ----
        for (int id : gulls) {
            Roam r = roam.get(id);
            boolean mainBand = PackedPos.z(registry.get(id).cell()) == mainBandZ;
            if (mainBand) {
                assertTrue(r.visited.size() >= MIN_GULL_DISTINCT_CELLS,
                        "gull#" + id + " visited only " + r.visited.size() + " distinct cells");
                assertTrue(r.maxX - r.minX >= MIN_GULL_BBOX_SPAN
                                && r.maxY - r.minY >= MIN_GULL_BBOX_SPAN,
                        "gull#" + id + " bbox " + (r.maxX - r.minX) + "x" + (r.maxY - r.minY)
                                + " under " + MIN_GULL_BBOX_SPAN + "x" + MIN_GULL_BBOX_SPAN);
            } else {
                assertTrue(r.visited.size() >= MIN_STRAND_DISTINCT_CELLS,
                        "strand gull#" + id + " visited only " + r.visited.size() + " cells");
            }
            assertTrue(r.maxRun <= MAX_CONSECUTIVE_TICKS_ON_ONE_CELL,
                    "gull#" + id + " stalled " + r.maxRun + " consecutive ticks on one cell at ("
                            + PackedPos.x(r.maxRunCell) + "," + PackedPos.y(r.maxRunCell) + ",z"
                            + PackedPos.z(r.maxRunCell) + ")");
        }

        // ---- the hunt loop turned ----
        assertTrue(catches >= MIN_TOTAL_CATCHES,
                "only " + catches + " catches over " + TICKS + " ticks");
        assertTrue(revives > 0, "no caught mouse ever revived");
        for (int id : mice) {
            Actor mouse = registry.get(id);
            if (mouse.hasStatus(StatusBit.DOWNED)) {
                continue; // a late catch may still be counting down at soak end
            }
            // Leash 8 + a full FLEE panic (leash-ignoring random walk, ~40 ticks at worst
            // before safety.recoverPerTick ends it) can leave a mouse ~2x its leash out at
            // an arbitrary soak-end instant; the relative-improvement leash rule then walks
            // it back. 20 pins "mice live at their dens" while tolerating one live panic.
            int distance = chebyshev(mouse.cell(), denOf.get(id));
            assertTrue(distance <= 20,
                    "mouse#" + id + " ended " + distance + " cells from its den "
                            + "(leash 8 + a bounded flee excursion)");
        }

        // ---- citizen bars (the no-regression gate) ----
        int serfTotal = 0;
        int serfStarved = 0;
        int middleStarved = 0;
        StringBuilder middleDetail = new StringBuilder(); // names the culprit on failure
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            String type = a.typeId().key();
            boolean starved = a.need(Need.HUNGER) == 0;
            if (type.equals("serf")) {
                serfTotal++;
                serfStarved += starved ? 1 : 0;
            } else if (type.equals("shopkeeper") || type.equals("militia_watch")
                    || type.equals("priest_of_the_flame") || type.equals("disciple_of_the_flame")
                    || type.equals("animal_keeper")) {
                middleStarved += starved ? 1 : 0;
                if (starved) {
                    middleDetail.append(' ').append(type).append('#').append(a.id())
                            .append("@(").append(PackedPos.x(a.cell())).append(',')
                            .append(PackedPos.y(a.cell())).append(",z")
                            .append(PackedPos.z(a.cell())).append(')');
                }
            }
        }
        assertTrue(100.0 * serfStarved / serfTotal <= 5.0,
                "serf starvation " + serfStarved + "/" + serfTotal + " breaches the 5% bar");
        assertEquals(0, middleStarved, "the middle class never starves:" + middleDetail);

        // ---- conservation identities ----
        var bank = population.bankAccounts();
        var items = population.items();
        long totalRoyals = bank.totalRoyals();
        int vaultCoins = items.countOnCellOfKind(DocksPopulation.bankVaultChestCell(),
                ItemKinds.COIN);
        assertEquals(totalRoyals, vaultCoins, "totalRoyals == vault COIN count");
        long minted = population.system().foodMinted();
        long eaten = population.system().foodEaten();
        assertEquals(minted, items.liveOfKind(ItemKinds.FOOD) + eaten,
                "FOOD minted == live + eaten (predation must not touch the item counts)");

        assertEquals(691, registry.size(), "the roster never shrinks (no removal path)");
    }

    private static void assertOccupancyCapHolds(ActorRegistry registry, String when) {
        Map<Integer, Integer> perCell = new HashMap<>();
        for (int i = 0; i < registry.size(); i++) {
            int count = perCell.merge(registry.get(i).cell(), 1, Integer::sum);
            assertTrue(count <= Actor.MAX_OCCUPANTS_PER_CELL,
                    "occupancy cap breached at " + when);
        }
    }

    private static int chebyshev(int cellA, int cellB) {
        return Math.max(Math.abs(PackedPos.x(cellA) - PackedPos.x(cellB)),
                Math.abs(PackedPos.y(cellA) - PackedPos.y(cellB)));
    }
}
