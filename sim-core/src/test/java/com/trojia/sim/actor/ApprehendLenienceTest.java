package com.trojia.sim.actor;

import com.trojia.sim.actor.faction.FactionRawsLoader;
import com.trojia.sim.actor.faction.FactionRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The faction ledger's ONE behavior read (Sprint 1): {@code ApprehendPolicy}'s
 * first-contact warn-vs-fine decision rides the {@code watch.lenience} named draw,
 * thresholded by the offender's PRESENTED Watch standing. A clean citizen is ALWAYS warned
 * (permille 1000 — the pre-faction baseline in outcome); a staged crime spree measurably
 * hardens subsequent Watch treatment (the DoD probe): the same contact situation now
 * sometimes skips the courtesy and lands the immediate fine + arrest.
 */
final class ApprehendLenienceTest {

    private static final int Z = 11;
    private static final FactionRegistry FACTIONS = FactionRawsLoader.load(committedRawsRoot());

    private static Path committedRawsRoot() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws not found above " + Path.of("").toAbsolutePath());
    }

    // ======================================================================
    // The pure threshold
    // ======================================================================

    @Test
    void leniencePermilleIsCleanAtNonNegativeStandingAndErodesToTheFloor() {
        assertEquals(1000, ApprehendPolicy.warnLeniencePermille(0),
                "a clean citizen is always warned — the pre-faction baseline");
        assertEquals(1000, ApprehendPolicy.warnLeniencePermille(75),
                "positive standing never exceeds the customary courtesy");
        assertEquals(800, ApprehendPolicy.warnLeniencePermille(-20), "one arrest: 20% harder");
        assertEquals(400, ApprehendPolicy.warnLeniencePermille(-60));
        assertEquals(250, ApprehendPolicy.warnLeniencePermille(-100),
                "even the most-hated draws the 1-in-4 floor");
    }

    // ======================================================================
    // The DoD probe: a staged crime spree changes Watch treatment
    // ======================================================================

    /** Ctx double with a WIRED standing ledger (the lenience read's live path). */
    private static final class StandingContext extends NoOpActorContext {
        private final FactionStandings standings;

        StandingContext(ActorRegistry registry, FactionStandings standings) {
            super(registry);
            this.standings = standings;
        }

        @Override
        public FactionStandings factionStandings() {
            return standings;
        }
    }

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /**
     * Stages one first-contact correction at {@code tick} against an offender whose Watch
     * standing is pre-seeded to {@code watchStanding}, and reports whether the guard
     * WARNED (true) or skipped straight to the fine + arrest (false). Fresh world per
     * trial: the decision under test is exactly one draw.
     */
    private static boolean stagedContactWarns(long tick, int watchStanding) {
        ActorRegistry registry = new ActorRegistry();
        FactionStandings standings = new FactionStandings(FACTIONS);
        StandingContext ctx = new StandingContext(registry, standings);
        ctx.setTick(tick);

        Actor guard = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.stats(MilitiaWatch.TYPE), cell(51, 50));
        guard.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
        Actor offender = registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                cell(30, 30));
        offender.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Wastrel.Streetlife.ID));
        offender.setCell(cell(52, 50)); // wandered in: the staff exemption must not apply
        ctx.setRestrictedZones(new RestrictedZoneTable(List.of(
                new RestrictedZone(Job.Trade.Trader.ID, Actor.NONE, new int[] {cell(52, 50)}))));
        standings.adjust(offender.identity().presentedId(), FACTIONS.rawId("watch"),
                watchStanding);

        Policies.APPREHEND.act(guard, ctx); // adjacent: lock + first contact in one act

        boolean warned = offender.hasStatus(StatusBit.MOVE_ALONG);
        if (warned) {
            assertTrue(!offender.hasStatus(StatusBit.HELD), "a warning is not an arrest");
            assertEquals(ReasonCode.WARNED_MOVE_ALONG, offender.lastReasonCode());
        } else {
            assertTrue(offender.hasStatus(StatusBit.HELD),
                    "lenience denied must mean the immediate fine + arrest");
            assertEquals(ReasonCode.ARRESTED, offender.lastReasonCode());
        }
        return warned;
    }

    @Test
    void aCleanCitizenIsAlwaysWarnedFirst() {
        for (long t = 1; t <= 60; t++) {
            assertTrue(stagedContactWarns(t, 0),
                    "standing 0 must always draw the warning (tick " + t + ")");
        }
    }

    @Test
    void aCrimeSpreeMeasurablyHardensWatchTreatment() {
        // Three arrests' worth of standing (-60 -> lenience 400 permille): over 60 staged
        // first contacts the Watch must BOTH still warn sometimes AND now sometimes skip
        // straight to the fine + arrest — the measurable treatment shift.
        int warns = 0;
        int immediateArrests = 0;
        for (long t = 1; t <= 60; t++) {
            if (stagedContactWarns(t, -60)) {
                warns++;
            } else {
                immediateArrests++;
            }
        }
        assertTrue(immediateArrests > 0,
                "a spree offender must sometimes be corrected without the courtesy warning");
        assertTrue(warns > 0, "lenience erodes, it does not vanish (floor > 0)");
        assertTrue(immediateArrests > 60 / 4,
                "at 400 permille lenience the harder treatment must dominate visibly: "
                        + immediateArrests + "/60 immediate");
    }
}
