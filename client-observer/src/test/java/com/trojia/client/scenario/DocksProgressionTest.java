package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorsSystem;
import com.trojia.sim.actor.BankLedger;
import com.trojia.sim.actor.CivicFixtures;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.RestrictedZoneTable;
import com.trojia.sim.actor.SkillLevelLog;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.io.WorldHasher;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint-1 DoD over the real baked docks (the progression engine LIVE): a scripted THREE-DAY
 * (72,000-tick) headless run shows citizens levelling streetwise through the scavenge path
 * with a fully reconstructable trail — the {@link ReasonCode#SCAVENGED_FOOD} stamps observed
 * during the run plus the {@link SkillLevelLog} rows naming (tick, actor, skill, newLevel) —
 * while the faction ledger visibly remembers the run's justice and commerce; and the
 * {@code ActorsSystem} chunk passes the persisted triad MID-PROGRESSION (banked grains, live
 * satiation, moved standings — not a clean slate).
 */
class DocksProgressionTest {

    private static final int THREE_DAYS = 72_000; // 3 x DailyRhythm.DAY

    @Test
    void threeDocksDaysLevelStreetwiseWithATrailAndTheLedgerRemembers() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        ActorRegistry registry = population.registry();
        SkillTrackRegistry tracks = population.system().skillTracks();
        assertTrue(tracks.isWired(), "the docks bake wires the committed 16-skill universe");

        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));

        // The trail observers: who was seen scavenging (the ReasonCode stamps), and every
        // streetwise level-up row harvested from the client-seam ring BEFORE it can be
        // overwritten (rows are read tick-by-tick off the monotonic counter).
        HashSet<Integer> seenScavenging = new HashSet<>();
        List<long[]> streetwiseLevelUps = new ArrayList<>(); // {tick, actorId, newLevel}
        SkillLevelLog log = tracks.levelLog();
        long harvested = 0;

        for (int t = 1; t <= THREE_DAYS; t++) {
            driver.requestStep();
            for (int i = 0; i < registry.size(); i++) {
                if (registry.get(i).lastReasonCode() == ReasonCode.SCAVENGED_FOOD) {
                    seenScavenging.add(i);
                }
            }
            long total = log.totalRecorded();
            int fresh = (int) Math.min(total - harvested, log.size());
            for (int r = log.size() - fresh; r < log.size(); r++) {
                if (log.skillRawAt(r) == tracks.streetwiseRaw()) {
                    streetwiseLevelUps.add(new long[] {log.tickAt(r), log.actorIdAt(r),
                            log.newLevelAt(r)});
                }
            }
            harvested = total;
        }

        // ---- the levelling DoD: streetwise rose, on someone seen scavenging ----
        assertTrue(!streetwiseLevelUps.isEmpty(),
                "three docks days must level streetwise for someone (the scavenge path)");
        boolean trailReconstructs = false;
        boolean citizenLevelled = false;
        for (long[] row : streetwiseLevelUps) {
            int actorId = (int) row[1];
            if (seenScavenging.contains(actorId)) {
                trailReconstructs = true;
            }
            String type = registry.get(actorId).typeId().key();
            if (type.equals("serf") || type.equals("wastrel")) {
                citizenLevelled = true;
            }
            assertTrue(tracks.level(actorId, tracks.streetwiseRaw()) >= 1,
                    "a logged level-up must persist on the live track");
        }
        assertTrue(trailReconstructs, "the ReasonCode trail (SCAVENGED_FOOD stamps) must "
                + "name a streetwise leveller — the WHY is reconstructable");
        assertTrue(citizenLevelled, "the levellers include the citizen mass (serf/wastrel)");

        // ---- the district trains itself by living: contests taught hands and hides ----
        int trained = 0;
        for (int i = 0; i < registry.size(); i++) {
            if (tracks.level(i, tracks.openHandRaw()) > 0
                    || tracks.level(i, tracks.gritRaw()) > 0) {
                trained++;
            }
        }
        assertTrue(trained > 0, "push contests must have taught open_hand/grit somewhere");

        // ---- the ledger remembers: justice stained standings, commerce built them ----
        var standings = population.system().factionStandings();
        assertTrue(standings.isWired());
        int watchStained = 0;
        int merchantsWarmed = 0;
        int merchants = standings.factions().rawId("merchants");
        for (int i = 0; i < registry.size(); i++) {
            watchStained += standings.watchStanding(i) < 0 ? 1 : 0;
            merchantsWarmed += standings.standingOf(i, merchants) > 0 ? 1 : 0;
        }
        assertTrue(watchStained > 0,
                "three days of docks justice must stain someone's Watch standing");
        assertTrue(merchantsWarmed > 0,
                "three days of provisioning must warm the Merchants to the paying mass");
    }

    @Test
    void theChunkPassesThePersistedTriadMidProgression() throws IOException {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));
        for (int t = 0; t < 2_000; t++) {
            driver.requestStep();
        }
        SkillTrackRegistry tracks = population.system().skillTracks();
        // MID-progression, by assertion not hope: XP has actually flowed when we snapshot.
        assertTrue(tracks.levelLog().totalRecorded() > 0,
                "2000 docks ticks must produce real level-ups before the snapshot");

        byte[] first = serialize(population.system());
        // Since Sprint 3 the QuestLog frame rides this chunk too, so the loading system must
        // be built against the SAME bake-compiled quest raws (the skillTracks contract).
        ActorsSystem reloaded = new ActorsSystem(loaded.worldSeed(), population.typeStats(),
                population.jobs(), new ActorRegistry(), new HomeRegistry(),
                new RelationshipRegistry(), new ItemsLiteRegistry(), new BankLedger(), null,
                CivicFixtures.ofJustice(Actor.NONE, RestrictedZoneTable.EMPTY),
                DocksPopulation.freshSkillTracks(), DocksPopulation.freshFactionStandings(),
                population.system().questRegistry());
        reloaded.load(new DataInputStream(new ByteArrayInputStream(first)));
        byte[] second = serialize(reloaded);

        assertArrayEquals(first, second, "serialize -> load -> serialize must be byte-identical "
                + "with live progression + standing state aboard");
        assertEquals(hash(population.system()), hash(reloaded), "hashInto must match after load");
        assertEquals(tracks.levelLog().totalRecorded(),
                reloaded.skillTracks().levelLog().totalRecorded());
    }

    private static byte[] serialize(ActorsSystem system) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        system.serialize(new DataOutputStream(bytes));
        return bytes.toByteArray();
    }

    private static long hash(ActorsSystem system) {
        WorldHasher hasher = new WorldHasher();
        system.hashInto(hasher.sectionSink(system.id()));
        return hasher.sectionHash(system.id());
    }
}
