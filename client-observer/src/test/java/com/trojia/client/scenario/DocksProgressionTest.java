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
        // overwritten (rows are read tick-by-tick off the monotonic counter). Sprint 5:
        // the same harvest also collects every CIVIC-ONLY job-skill row (fieldcraft/
        // seacraft/kit_keeping — skills with NO award site outside the job seams), the
        // awards wave's own reconstructable trail.
        int fieldcraft = tracks.skills().id("fieldcraft").raw();
        int seacraft = tracks.skills().id("seacraft").raw();
        int kitKeeping = tracks.skills().id("kit_keeping").raw();
        int channeling = tracks.skills().id("channeling").raw();
        HashSet<Integer> seenScavenging = new HashSet<>();
        List<long[]> streetwiseLevelUps = new ArrayList<>(); // {tick, actorId, newLevel}
        List<long[]> jobSkillLevelUps = new ArrayList<>();   // {tick, actorId, skillRaw}
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
                int raw = log.skillRawAt(r);
                if (raw == fieldcraft || raw == seacraft || raw == kitKeeping) {
                    jobSkillLevelUps.add(new long[] {log.tickAt(r), log.actorIdAt(r), raw});
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

        // ================================================================
        // Sprint 5 — the awards wave DoD (every job trains its trade)
        // ================================================================
        var jobs = population.jobs();

        // (a) Coverage: >= 60% of employed citizens (bound to a TRAINING job) hold their
        // job's skill at >= 1 after three days of ordinary work.
        int employed = 0;
        int holdingTheirTrade = 0;
        List<Integer> anchorWorkerLevels = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            int ordinal = registry.get(i).jobOrdinal();
            if (ordinal < 0) {
                continue;
            }
            var params = jobs.get(ordinal).params();
            if (!params.trains()) {
                continue;
            }
            employed++;
            int level = tracks.level(i, params.trainSkillRaw());
            if (level >= 1) {
                holdingTheirTrade++;
            }
            // The full-time anchor-cycle trades (no-cooldown workplace quotas): the rate
            // math's reference population. TEND_PLOT (farm cooldown), TEND_BEASTS (wander)
            // and PATROL_ROUTE (waypoint cadence, cp 10) are deliberately outside the band.
            switch (params.goalKind()) {
                case HAUL_WORK, CREW_SHIP, STALL_CYCLE, VEND_WARES, ALMS_CYCLE, CARRY_RUN ->
                        anchorWorkerLevels.add(level);
                default -> { }
            }
        }
        assertTrue(employed > 100, "sanity: the docks employ a real training workforce");
        assertTrue(holdingTheirTrade * 100 >= employed * 60,
                "three days must teach >= 60% of the employed their trade (>= level 1): "
                        + holdingTheirTrade + "/" + employed);

        // (b) The visible-but-not-inflationary band: median full-time anchor-worker level
        // at day 3. S5's cp-tuning loop signed off 5..10 under the exact-anchor-cell work
        // model, where a crowded crew had ONE working member and everyone else ringed the
        // cell earning nothing. S6's WORK_REACH (whole crews work in parallel) + the
        // working-city station spread turned those ring-idlers into workers, and the
        // deterministic day-3 median moved 8 -> 13 — the same souls, actually working.
        // Band re-baselined 5..15 (measured 13, slack both ways); the deliberate-rebaseline
        // note rides the S6 WORLD report for sign-off.
        anchorWorkerLevels.sort(null);
        int median = anchorWorkerLevels.get(anchorWorkerLevels.size() / 2);
        assertTrue(median >= 5 && median <= 15,
                "median anchor-worker job-skill must sit in the visible 5..15 band at day 3, "
                        + "got " + median + " over " + anchorWorkerLevels.size() + " workers");

        // (c) Zero beast awards: no beast body ever banks a grain of the civic trades
        // (fieldcraft/seacraft/kit_keeping/channeling have NO award site outside the job
        // seams and quest raws touch none of them; streetwise is excluded from this probe —
        // its scavenge/quest sites are citizen paths, not wave seams).
        for (int i = 0; i < registry.size(); i++) {
            String type = registry.get(i).typeId().key();
            if (!(type.equals("feral") || type.equals("cat") || type.equals("mouse")
                    || type.equals("animal"))) {
                continue;
            }
            for (int raw : new int[] {fieldcraft, seacraft, kitKeeping, channeling}) {
                assertEquals(0, tracks.level(i, raw),
                        type + "#" + i + " must never level a civic trade");
                assertEquals(0, tracks.progressGrains(i, raw),
                        type + "#" + i + " must never bank a civic-trade grain");
            }
        }

        // (d) The wave's trail reconstructs: every harvested civic-only job-skill row names
        // an actor whose BOUND job trains exactly that skill — the WHY of every level-up is
        // the job that taught it.
        //
        // S8 narrows this claim by exactly one deliberate exception, and it is worth stating
        // rather than papering over: FIELDCRAFT now has a SECOND legitimate award site, the
        // cull verb (a soul who takes a scalp learns something about handling a carcass,
        // whatever its day job). So a soul who has culled may hold fieldcraft grains its
        // bound job never taught it. Every OTHER civic-trade level-up still has to
        // reconstruct to the job that taught it, and a soul that has never culled has to
        // reconstruct even in fieldcraft — which is what keeps this a real check.
        assertTrue(!jobSkillLevelUps.isEmpty(),
                "three days of work must produce fieldcraft/seacraft/kit_keeping level-ups");
        int cullTaughtRows = 0;
        for (long[] row : jobSkillLevelUps) {
            int actorId = (int) row[1];
            int ordinal = registry.get(actorId).jobOrdinal();
            assertTrue(ordinal >= 0, "a job-skill leveller must hold a job, actor#" + actorId);
            if ((int) row[2] == fieldcraft
                    && population.system().scalpsTakenBy(actorId) > 0) {
                cullTaughtRows++;
                continue; // the cull verb taught this one, and the report says who culled
            }
            assertEquals((int) row[2], jobs.get(ordinal).params().trainSkillRaw(),
                    "actor#" + actorId + "'s level-up must reconstruct to its own bound job");
        }
        assertTrue(cullTaughtRows < jobSkillLevelUps.size(),
                "the cull exception must not swallow the whole check — every row cannot be "
                        + "a culler's fieldcraft row");
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
        // S6: the fishing-spot registry frame-guards its zone count, so the load side
        // wires the same baked zone table (DocksPopulation.loadSideFixtures).
        ActorsSystem reloaded = new ActorsSystem(loaded.worldSeed(), population.typeStats(),
                population.jobs(), new ActorRegistry(), new HomeRegistry(),
                new RelationshipRegistry(), new ItemsLiteRegistry(), new BankLedger(), null,
                DocksPopulation.loadSideFixtures(),
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
