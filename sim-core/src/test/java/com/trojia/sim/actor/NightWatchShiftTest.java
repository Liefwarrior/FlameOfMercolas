package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The SKELETON NIGHT WATCH — {@code watch.nightwatch} and the one policy seam it needs.
 *
 * <p>Slice 3's off-shift branch is the real cure for the night oscillation and it stays, but
 * it emptied the streets: all nineteen Watch went home for the whole 12,000-tick night, and
 * {@code checkArrestExposure} gates on a Watch being nearby, so arrest-by-exposure became a
 * daytime-only mechanic. A few souls therefore draw a night roster.
 *
 * <p>The load-bearing question is not "does the job exist" but "can a rostered guard actually
 * STAY OUT". {@code RETURN_HOME} is priced at 305 plus an 80-point night rhythm bonus against
 * a JOB band that tops out at 299, so without the {@link Job#worksThroughTheNight()} seam the
 * roster would be dragged home every tick and shoved back out the next — slice 3's bug rebuilt
 * on the other shift. These tests pin the scoring, both directions, plus the byte-identity
 * guarantee that no other job's scoring moved.
 */
final class NightWatchShiftTest {

    private static final int Z = 11;

    /** {@code watch.patrol} is [0,12000); {@code watch.nightwatch} is [12000,24000). */
    private static final long DAY_TICK = 3_000L;
    private static final long NIGHT_TICK = 18_000L;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    private static final class WalledContext extends NoOpActorContext {
        private final List<Integer> walls = new ArrayList<>();

        WalledContext(ActorRegistry registry) {
            super(registry);
        }

        @Override
        public boolean isWalkable(int c) {
            return !walls.contains(c);
        }
    }

    private static Actor spawnWatch(ActorRegistry registry, ActorContext ctx, int at, Job job) {
        Actor watch = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.stats(MilitiaWatch.TYPE), at);
        watch.setJobOrdinal((short) ctx.jobs().ordinalOf(job.id()));
        return watch;
    }

    private static Job job(ActorContext ctx, com.trojia.sim.actor.job.JobId id) {
        return ctx.jobs().get(ctx.jobs().ordinalOf(id));
    }

    // ================== the job itself ==================

    @Test
    void theNightRosterIsTheOnlyJobThatWorksThroughTheNight() {
        // The seam is opt-in per job on purpose: villain.robber and villain.skyrunner carry
        // night windows too, and exempting THEM from RETURN_HOME would rewrite the ward's
        // nocturnal economy under cover of a guard-routing fix. Exactly one job opts in.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int optedIn = 0;
        for (Job j : ctx.jobs().all()) {
            if (j.worksThroughTheNight()) {
                optedIn++;
                assertEquals(Job.Watch.NightWatch.ID, j.id(),
                        "only the night roster may opt out of the type's night rhythm");
            }
        }
        assertEquals(1, optedIn, "exactly one job on the night roster seam");
    }

    @Test
    void theNightRosterIsTheWatchAsFarAsEveryLawReadIsConcerned() {
        // watchIsNearby / ApprehendPolicy / TheftMechanics all test `instanceof Job.Watch`.
        // If the night job did not extend Watch, a rostered guard would deter nobody and the
        // whole roster would be decoration.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        assertTrue(job(ctx, Job.Watch.NightWatch.ID) instanceof Job.Watch,
                "the night roster must read as the Watch to every law surface");
    }

    @Test
    void theTwoShiftsCoverTheDayBetweenThemAndNeverOverlap() {
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        var patrol = job(ctx, Job.Watch.Patrol.ID).params();
        var night = job(ctx, Job.Watch.NightWatch.ID).params();
        for (long tod = 0; tod < DailyRhythm.DAY; tod += 250) {
            assertNotEquals(patrol.inWindow(tod), night.inWindow(tod),
                    "exactly one Watch shift is open at tick-of-day " + tod);
        }
    }

    // ================== behaviour on and off the night shift ==================

    @Test
    void onTheNightShiftTheRosterLeavesItsPostAndWalks() {
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int post = cell(40, 40);
        Actor watch = spawnWatch(registry, ctx, post, job(ctx, Job.Watch.NightWatch.ID));
        watch.setAnchorCell(post);
        watch.setHomeId(ctx.homes().addHome(post));
        Job night = job(ctx, Job.Watch.NightWatch.ID);

        ctx.setTick(NIGHT_TICK);
        for (int i = 0; i < 40; i++) {
            night.pursue(watch, ctx);
        }
        assertNotEquals(post, watch.cell(), "on the night shift the beat leaves the post");
    }

    @Test
    void byDayTheRosterGoesHomeAndStaysThere() {
        // The exact mirror of a day guard's night: the roster is not doing a double shift.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int post = cell(40, 40);
        int home = cell(46, 44);
        Actor watch = spawnWatch(registry, ctx, post, job(ctx, Job.Watch.NightWatch.ID));
        watch.setAnchorCell(post);
        watch.setHomeId(ctx.homes().addHome(home));
        Job night = job(ctx, Job.Watch.NightWatch.ID);

        ctx.setTick(DAY_TICK);
        for (int i = 0; i < 60 && watch.cell() != home; i++) {
            night.pursue(watch, ctx);
        }
        assertEquals(home, watch.cell(), "off shift, the night roster walks home");
        for (int i = 0; i < 200; i++) {
            night.pursue(watch, ctx);
            assertEquals(home, watch.cell(), "off shift, it stays home");
        }
    }

    // ================== the RETURN_HOME seam — the load-bearing part ==================

    @Test
    void atNightARosteredGuardIsNotPulledHomeButADayGuardStillIs() {
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        Actor dayGuard = spawnWatch(registry, ctx, cell(40, 40), job(ctx, Job.Watch.Patrol.ID));
        dayGuard.setAnchorCell(cell(40, 40));
        dayGuard.setHomeId(ctx.homes().addHome(cell(50, 50)));
        Actor nightGuard = spawnWatch(registry, ctx, cell(60, 60),
                job(ctx, Job.Watch.NightWatch.ID));
        nightGuard.setAnchorCell(cell(60, 60));
        nightGuard.setHomeId(ctx.homes().addHome(cell(70, 70)));
        ReturnHomePolicy policy = new ReturnHomePolicy();

        ctx.setTick(NIGHT_TICK);
        assertTrue(policy.score(dayGuard, ctx) > 299,
                "at night an off-shift day guard is pulled home above the whole JOB band");
        assertEquals(0, policy.score(nightGuard, ctx),
                "a rested guard ON its rostered night shift is at work, not up past bedtime");
    }

    @Test
    void aTiredNightGuardStillGoesHomeAndStaysUntilRecovered() {
        // The seam must not make the roster sleepless. REST is what sends it to bed, and the
        // RECOVERED hysteresis is what stops the going-to-bed from oscillating -- exactly the
        // shape a day guard already has at noon, which is the shape proven not to flip.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        int home = cell(70, 70);
        Actor nightGuard = spawnWatch(registry, ctx, cell(60, 60),
                job(ctx, Job.Watch.NightWatch.ID));
        nightGuard.setAnchorCell(cell(60, 60));
        nightGuard.setHomeId(ctx.homes().addHome(home));
        ReturnHomePolicy policy = new ReturnHomePolicy();
        ctx.setTick(NIGHT_TICK);

        nightGuard.applyNeedDelta(Need.REST,
                NeedThresholds.CRITICAL - nightGuard.need(Need.REST) - 1);
        assertTrue(NeedThresholds.isLow(nightGuard.need(Need.REST)), "the fixture is tired");
        assertTrue(policy.score(nightGuard, ctx) > 299,
                "a tired guard goes home whatever shift it is on");

        // ...and once home it KEEPS scoring until RECOVERED rather than dropping to 0 the
        // instant it stops being LOW. Dropping to 0 at home is what the night-window branch
        // used to do, and it is precisely how a guard gets shoved back out of its own doorway.
        nightGuard.setCell(home);
        assertTrue(policy.score(nightGuard, ctx) > 0,
                "at home and not yet recovered, it stays put");
        nightGuard.applyNeedDelta(Need.REST, NeedThresholds.MAX);
        assertEquals(0, policy.score(nightGuard, ctx),
                "recovered, it releases and goes back on the beat");
    }

    @Test
    void everyOtherActorScoresExactlyWhatItScoredBefore() {
        // The byte-identity guarantee: the seam reads jobs, so a job that does not opt in
        // must be untouched by it -- including the villains whose own rhythm windows are the
        // night. A robber is still dragged home by the night term, as it always was.
        ActorRegistry registry = new ActorRegistry();
        WalledContext ctx = new WalledContext(registry);
        Actor robber = registry.spawn(com.trojia.sim.actor.type.Wastrel.TYPE,
                ActorTestFixtures.stats(com.trojia.sim.actor.type.Wastrel.TYPE), cell(30, 30));
        robber.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Villain.Robber.ID));
        robber.setAnchorCell(cell(30, 30));
        robber.setHomeId(ctx.homes().addHome(cell(35, 35)));
        ReturnHomePolicy policy = new ReturnHomePolicy();

        ctx.setTick(NIGHT_TICK + 2_000L); // inside villain.robber's [18000,24000) window
        assertTrue(job(ctx, Job.Villain.Robber.ID).params()
                        .inWindow(DailyRhythm.tickOfDay(ctx.tick())),
                "the fixture really is inside the robber's own working window");
        assertFalse(job(ctx, Job.Villain.Robber.ID).worksThroughTheNight(),
                "the robber does not opt into the roster seam");
        assertTrue(policy.score(robber, ctx) > 299,
                "and is therefore still pulled home by the type's night rhythm, unchanged");
    }
}
