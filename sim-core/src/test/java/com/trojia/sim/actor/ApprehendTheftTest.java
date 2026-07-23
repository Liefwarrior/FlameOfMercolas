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

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint-2 justice-from-theft (the DoD staging): a WITNESSED CrimeLog row is the whole stim
 * — a guard senses it district-wide (same z), chases the BODY, and corrects with no
 * courtesy warning: fine + 1-day custody stamped {@code ARRESTED_FOR_THEFT} (the trail), or
 * the maim/hang escalation for a TRUE-job Skyrunner caught red-handed. Serving, claiming
 * and the crime window keep one correction per crime and one guard per culprit.
 */
final class ApprehendTheftTest {

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
        throw new IllegalStateException("content/raws not found above "
                + Path.of("").toAbsolutePath());
    }

    /** Ctx double with a live crime log + wired standings + correction accounting. */
    private static final class CrimeContext extends NoOpActorContext {
        private final CrimeLog crimeLog = new CrimeLog(64);
        private final FactionStandings standings = new FactionStandings(FACTIONS);
        long theftArrests;
        long skyrunnerEscalations;

        CrimeContext(ActorRegistry registry) {
            super(registry);
        }

        @Override
        public CrimeLog crimeLog() {
            return crimeLog;
        }

        @Override
        public FactionStandings factionStandings() {
            return standings;
        }

        @Override
        public void recordTheftCorrection(boolean skyrunnerEscalated) {
            if (skyrunnerEscalated) {
                skyrunnerEscalations++;
            } else {
                theftArrests++;
            }
        }
    }

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    private record Stage(ActorRegistry registry, CrimeContext ctx, Actor guard, Actor thief) {
    }

    private static Stage stage(int guardX, boolean skyrunnerThief) {
        ActorRegistry registry = new ActorRegistry();
        CrimeContext ctx = new CrimeContext(registry);
        Actor guard = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.stats(MilitiaWatch.TYPE), cell(guardX, 50));
        guard.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
        Actor thief = registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                cell(50, 50));
        thief.setJobOrdinal((short) ctx.jobs().ordinalOf(
                skyrunnerThief ? Job.Villain.Skyrunner.ID : Job.Wastrel.Streetlife.ID));
        return new Stage(registry, ctx, guard, thief);
    }

    @Test
    void aWitnessedTheftDrawsTheGuardAndLandsFinePlusTheftCustody() {
        Stage s = stage(51, false);
        s.ctx().setTick(100); // % SENSE_PERIOD == 0: a sense-cadence tick
        s.ctx().crimeLog().record(95, cell(50, 50), s.thief().id(),
                s.thief().identity().presentedId(), 99, true);

        assertTrue(Policies.APPREHEND.score(s.guard(), s.ctx()) > 0,
                "a witnessed crime row must open a case at the sense cadence");

        Policies.APPREHEND.act(s.guard(), s.ctx()); // adjacent: contact + correction in one act

        assertTrue(s.thief().hasStatus(StatusBit.HELD), "theft correction jails the body");
        assertEquals(ReasonCode.ARRESTED_FOR_THEFT, s.thief().lastReasonCode(),
                "the trail stamp distinguishes theft custody from a loiter arrest");
        assertEquals(100 + 24_000L, s.thief().heldUntilTick(), "the fixed 1-day sentence");
        assertFalse(s.thief().hasStatus(StatusBit.MOVE_ALONG),
                "a caught thief gets no move-along courtesy");
        assertTrue(s.ctx().crimeLog().servedAt(0), "the correction serves the row");
        assertEquals(Actor.NONE, s.guard().apprehendTargetId(), "case closed");
        assertEquals(1, s.ctx().theftArrests);
        assertTrue(s.ctx().standings.watchStanding(s.thief().identity().presentedId()) < 0,
                "the arrest + fine stain the presented identity's Watch standing");
    }

    @Test
    void aSkyrunnerCaughtRedHandedRidesTheMaimThenHangEscalation() {
        Stage s = stage(51, true);
        CrimeContext ctx = s.ctx();
        Actor guard = s.guard();
        Actor thief = s.thief();

        ctx.setTick(100);
        ctx.crimeLog().record(95, cell(50, 50), thief.id(),
                thief.identity().presentedId(), 99, true);
        Policies.APPREHEND.act(guard, ctx);

        assertTrue(thief.hasStatus(StatusBit.MAIMED), "1st catch: the hand comes off");
        assertFalse(thief.hasStatus(StatusBit.HELD), "a maim is not custody");
        assertEquals(1, ctx.skyrunnerEscalations);
        assertTrue(ctx.crimeLog().servedAt(0), "served — no re-sense off the same crime");

        // A SECOND witnessed theft: the second catch hangs.
        ctx.setTick(200);
        ctx.crimeLog().record(195, cell(50, 50), thief.id(),
                thief.identity().presentedId(), 99, true);
        Policies.APPREHEND.act(guard, ctx);
        assertTrue(thief.hasStatus(StatusBit.EXECUTED), "2nd catch: the gibbet");
        assertEquals(2, ctx.skyrunnerEscalations);
    }

    @Test
    void servedUnwitnessedStaleAndCrossZRowsNeverOpenACase() {
        Stage s = stage(51, false);
        CrimeContext ctx = s.ctx();
        ctx.setTick(10_000);
        // Unwitnessed (a clean lift): never sensed.
        ctx.crimeLog().record(9_990, cell(50, 50), s.thief().id(),
                s.thief().identity().presentedId(), 99, false);
        assertEquals(Actor.NONE, ApprehendPolicy.senseCrimeCulprit(s.guard(), ctx));
        // Stale (outside the crime window): word went cold.
        ctx.crimeLog().record(10_000 - ApprehendPolicy.CRIME_WINDOW_TICKS - 1, cell(50, 50),
                s.thief().id(), s.thief().identity().presentedId(), 99, true);
        assertEquals(Actor.NONE, ApprehendPolicy.senseCrimeCulprit(s.guard(), ctx));
        // Cross-z: word travels, boots do not.
        ctx.crimeLog().record(9_995, PackedPos.pack(50, 50, Z + 2), s.thief().id(),
                s.thief().identity().presentedId(), 99, true);
        s.thief().setCell(PackedPos.pack(50, 50, Z + 2));
        assertEquals(Actor.NONE, ApprehendPolicy.senseCrimeCulprit(s.guard(), ctx));
        // Back on the guard's plane: the fresh witnessed row IS sensed...
        s.thief().setCell(cell(50, 50));
        ctx.crimeLog().record(9_996, cell(50, 50), s.thief().id(),
                s.thief().identity().presentedId(), 99, true);
        assertEquals(s.thief().id(), ApprehendPolicy.senseCrimeCulprit(s.guard(), ctx));
        // ...and serving it closes the book.
        ctx.crimeLog().markServed(s.thief().id());
        assertEquals(Actor.NONE, ApprehendPolicy.senseCrimeCulprit(s.guard(), ctx));
    }

    @Test
    void oneGuardPerCulpritTheSecondGuardSkipsAClaimedCase() {
        Stage s = stage(51, false);
        CrimeContext ctx = s.ctx();
        Actor secondGuard = s.registry().spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.stats(MilitiaWatch.TYPE), cell(53, 50));
        secondGuard.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));

        ctx.setTick(100);
        ctx.crimeLog().record(95, cell(50, 50), s.thief().id(),
                s.thief().identity().presentedId(), 99, true);
        assertEquals(s.thief().id(), ApprehendPolicy.senseCrimeCulprit(s.guard(), ctx));

        s.guard().setApprehendTargetId(s.thief().id()); // guard 1 claims the case
        assertEquals(Actor.NONE, ApprehendPolicy.senseCrimeCulprit(secondGuard, ctx),
                "a claimed culprit is nobody else's case (no guard dog-pile)");
    }

    @Test
    void aDistantCrimeStartsAChaseNotAnInstantCorrection() {
        Stage s = stage(70, false); // 20 tiles out: beyond contact, within the district
        CrimeContext ctx = s.ctx();
        ctx.setTick(100);
        ctx.crimeLog().record(95, cell(50, 50), s.thief().id(),
                s.thief().identity().presentedId(), 99, true);

        int before = s.guard().cell();
        Policies.APPREHEND.act(s.guard(), ctx);
        assertEquals(s.thief().id(), s.guard().apprehendTargetId(), "the case is open");
        assertFalse(s.thief().hasStatus(StatusBit.HELD), "no correction at 20 tiles");
        assertTrue(s.guard().cell() != before, "the guard is walking the case down");
        assertEquals(ReasonCode.APPREHENDING, s.guard().lastReasonCode());
    }
}
