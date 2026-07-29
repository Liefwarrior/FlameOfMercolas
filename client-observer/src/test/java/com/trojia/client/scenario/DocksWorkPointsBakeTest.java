package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.PatrolRouteTable;
import com.trojia.sim.actor.ZLinkTable;
import com.trojia.sim.actor.ZReachability;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.Walkability;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * S6 work-point expansion audit (Eli's bugs 3/4): the destination monoculture the 48k
 * observer soak measured — 50 laborers ringing the Ropewalk's ONE cell, ~121 laborers
 * with no work point at all, a warehouse where nothing ever happened — is pinned FIXED
 * at bake time. Also the carter-rounds circuits (each carter's anchor must bind a
 * {@code workRounds} circuit or it silently degrades to the plain laborer cycle) and the
 * fisher crew on the fishbone pier.
 */
class DocksWorkPointsBakeTest {

    private static FixtureWorldLoader.Loaded loaded;
    private static DocksPopulation population;
    private static ZReachability reach;

    @BeforeAll
    static void bake() {
        loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        // The mid-Tarwalk sidewalk hub (96,33) — the S6 observer survey's own flood seed.
        int hub = PackedPos.pack(Coords.CHUNK_SIZE_X + 96, Coords.CHUNK_SIZE_Y + 33,
                Coords.CHUNK_SIZE_Z + 11);
        reach = ZReachability.flood(loaded.world(), ZLinkTable.extract(loaded.world()), hub);
    }

    /** Every working soul's anchor is real ground it can actually get to. */
    @Test
    void everyWorkerAnchorIsWalkableAndReachable() {
        TileCursor cursor = loaded.world().cursor();
        ActorRegistry registry = population.registry();
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            String job = jobKey(actor);
            if (!isStationHand(actor) && !job.equals("serf.carter")
                    && !job.equals("maritime.fisher") && !job.equals("maritime.sailor")
                    && !job.equals("serf.farmer")) {
                continue;
            }
            int anchor = actor.anchorCell();
            assertTrue(Walkability.isWalkable(cursor.moveTo(anchor)),
                    "actor#" + i + " (" + job + ") anchors unwalkable ground " + at(anchor));
            // Bunk-at-site non-commuters (home == anchor == the site, e.g. the ship crews
            // spawned aboard hulls across the water) never walk to their anchor — street
            // reachability is only the contract for a commuting worker.
            boolean bunksAtAnchor = actor.homeId() != Actor.NONE
                    && population.homes().get(actor.homeId()).homeCell() == anchor;
            assertTrue(bunksAtAnchor || reach.reachable(anchor),
                    "actor#" + i + " (" + job + ") anchors unreachable ground " + at(anchor)
                            + " - the timber-yard bug all over again");
        }
    }

    /** The three warehouse single-cell anchors carry NO laborer any more — crews spread. */
    @Test
    void theWarehouseCrewsSpreadOffTheirSingleAnchorCells() {
        Map<Integer, Integer> laborersPerAnchor = laborersPerAnchor();
        for (int[] site : new int[][] {{36, 85}, {90, 40}, {88, 87}}) {
            int cell = worldCell(site[0], site[1], 11);
            assertEquals(0, laborersPerAnchor.getOrDefault(cell, 0),
                    "site (" + site[0] + "," + site[1] + ") must not anchor laborers any more"
                            + " (the 50-on-one-cell monoculture)");
        }
        // The Ropewalk's 50-hand gang (4 staff + 10 hired + 34 bunk + 2 hovel-cycle hands
        // — the survey's own "50 laborers on one cell") lands 8-9 per station.
        int ropewalkTotal = 0;
        for (int[] station : new int[][] {{10, 85}, {20, 85}, {30, 85},
                {45, 85}, {55, 85}, {64, 85}}) {
            int count = laborersPerAnchor.getOrDefault(worldCell(station[0], station[1], 11), 0);
            assertTrue(count >= 8 && count <= 9, "ropewalk station (" + station[0] + ","
                    + station[1] + ") must host 8-9 of the 50 hands, hosts " + count);
            ropewalkTotal += count;
        }
        assertEquals(50, ropewalkTotal, "the full rope gang stays in the shed");
    }

    /** The almshouse works now: no laborer is left anchored around the Mission bunkroom. */
    @Test
    void theMissionsIndigentLodgersAllGotWork() {
        Map<Integer, Integer> laborersPerAnchor = laborersPerAnchor();
        int bunks = worldCell(85, 78, 11);
        for (Map.Entry<Integer, Integer> entry : laborersPerAnchor.entrySet()) {
            int dx = Math.abs(PackedPos.x(entry.getKey()) - PackedPos.x(bunks));
            int dy = Math.abs(PackedPos.y(entry.getKey()) - PackedPos.y(bunks));
            boolean atBunks = PackedPos.z(entry.getKey()) == PackedPos.z(bunks)
                    && Math.max(dx, dy) <= 3;
            assertTrue(!atBunks, "a laborer still anchors the Mission bunkroom at "
                    + at(entry.getKey()) + " - the almshouse pool was not put to work");
        }
    }

    /** 16 carters, every one bound to a rounds circuit through its anchor. */
    @Test
    void everyCarterBindsARoundsCircuit() {
        PatrolRouteTable rounds = PatrolRouteTable.of(DocksPopulation.workRounds());
        assertEquals(3, rounds.routeCount(), "east + west + timber circuits");
        ActorRegistry registry = population.registry();
        int[] perCircuit = new int[3];
        int carters = 0;
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (!jobKey(actor).equals("serf.carter")) {
                continue;
            }
            carters++;
            int circuit = rounds.routeContaining(actor.anchorCell());
            assertTrue(circuit >= 0, "carter actor#" + i + " anchor " + at(actor.anchorCell())
                    + " binds no circuit - it would degrade to the plain laborer cycle");
            perCircuit[circuit]++;
        }
        assertEquals(16, carters, "the authored carter crew");
        assertEquals(6, perCircuit[0], "east circuit crew");
        assertEquals(6, perCircuit[1], "west circuit crew");
        assertEquals(4, perCircuit[2], "timber circuit crew");
        // Every stop of every circuit is walkable, reachable ground.
        TileCursor cursor = loaded.world().cursor();
        for (int r = 0; r < rounds.routeCount(); r++) {
            for (int w = 0; w < rounds.waypointCount(r); w++) {
                int stop = rounds.waypoint(r, w);
                assertTrue(Walkability.isWalkable(cursor.moveTo(stop)),
                        "circuit " + r + " stop " + w + " at " + at(stop) + " not walkable");
                assertTrue(reach.reachable(stop),
                        "circuit " + r + " stop " + w + " at " + at(stop) + " not reachable");
            }
        }
    }

    /** Ten fishers, two per fishbone stand — the dead pier gets its daily crew. */
    @Test
    void theFishbonePierGetsItsFisherCrew() {
        ActorRegistry registry = population.registry();
        Map<Integer, Integer> perStand = new HashMap<>();
        int fishers = 0;
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (!jobKey(actor).equals("maritime.fisher")) {
                continue;
            }
            fishers++;
            perStand.merge(actor.anchorCell(), 1, Integer::sum);
        }
        assertEquals(10, fishers, "the authored fisher crew");
        for (int[] stand : new int[][] {{75, 23}, {75, 9}, {75, 17}, {75, 12}, {77, 20}}) {
            assertEquals(2, perStand.getOrDefault(worldCell(stand[0], stand[1], 11), 0),
                    "fishbone stand (" + stand[0] + "," + stand[1] + ") must host 2 fishers");
        }
    }

    /** The guardhouse garrison pair no longer beats from adjacent anchors. */
    @Test
    void theGarrisonPairBeatsFromSeparatedAnchors() {
        ActorRegistry registry = population.registry();
        boolean westBeatManned = false;
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (actor.typeId().key().equals("militia_watch")
                    && actor.anchorCell() == worldCell(102, 83, 11)) {
                westBeatManned = true;
            }
        }
        assertTrue(westBeatManned, "the second garrison watch must beat from the watch"
                + " room's west end (the S6 beat de-overlap)");
    }

    /**
     * The station-hand jobs: plain {@code serf.laborer} plus the four S8 craft-yard trades.
     * A ropewalker is a laborer standing at a Ropewalk station — the S8 promotion changed its
     * job LABEL and its yield, not where it stands or what this test is about (the ward's
     * worst monoculture was 50 hands on one cell). Counting only {@code serf.laborer} here
     * after S8 would silently stop counting the very gang these assertions exist to watch.
     */
    private static boolean isStationHand(Actor actor) {
        String job = jobKey(actor);
        return job.equals("serf.laborer") || job.equals("serf.ropewalker")
                || job.equals("serf.tarhand") || job.equals("serf.cooper")
                || job.equals("serf.salter");
    }

    private Map<Integer, Integer> laborersPerAnchor() {
        ActorRegistry registry = population.registry();
        Map<Integer, Integer> counts = new HashMap<>();
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (isStationHand(actor)) {
                counts.merge(actor.anchorCell(), 1, Integer::sum);
            }
        }
        return counts;
    }

    private static String jobKey(Actor actor) {
        if (actor.jobOrdinal() < 0) {
            return "";
        }
        Job job = population.jobs().get(actor.jobOrdinal());
        return job.id().value();
    }

    private static int worldCell(int x, int y, int z) {
        return PackedPos.pack(Coords.CHUNK_SIZE_X + x, Coords.CHUNK_SIZE_Y + y,
                Coords.CHUNK_SIZE_Z + z);
    }

    private static String at(int cell) {
        return "(" + (PackedPos.x(cell) - Coords.CHUNK_SIZE_X) + ","
                + (PackedPos.y(cell) - Coords.CHUNK_SIZE_Y) + ",z+"
                + (PackedPos.z(cell) - Coords.CHUNK_SIZE_Z) + ")";
    }
}
