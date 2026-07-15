package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.actor.job.JobParams;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The crime-detection / arrest / hold / Skyrunner-escalation mechanic (Eli's 2026-07-14
 * directive, ARREST-SPEC addendum), exercised directly against {@link JobBehaviors
 * #checkArrestExposure} using the real committed {@code jobs.json}-bound
 * {@code Job.Villain}/{@code Job.Watch} instances (via {@link NoOpActorContext#jobs()}), with
 * the two new-arrest RNG streams pinned via {@link PinnedDrawContext} so "the draw succeeds/
 * fails" is a controlled precondition, not a hunt for a lucky seed.
 */
final class ArrestExposureTest {

    private static final int Z = 11;

    /** Pins {@code WATCH_ARREST_CHECK}/{@code WATCH_SENTENCE_LENGTH}; every other stream is real. */
    private static final class PinnedDrawContext extends NoOpActorContext {
        private final Long arrestCheckDraw;
        private final Long sentenceDraw;

        PinnedDrawContext(ActorRegistry registry, Long arrestCheckDraw, Long sentenceDraw) {
            super(registry);
            this.arrestCheckDraw = arrestCheckDraw;
            this.sentenceDraw = sentenceDraw;
        }

        @Override
        public long draw(ActorRngStream stream, int actorId, int drawIndex) {
            if (stream == ActorRngStream.WATCH_ARREST_CHECK && arrestCheckDraw != null) {
                return arrestCheckDraw;
            }
            if (stream == ActorRngStream.WATCH_SENTENCE_LENGTH && sentenceDraw != null) {
                return sentenceDraw;
            }
            return super.draw(stream, actorId, drawIndex);
        }
    }

    private static Actor spawnVillain(ActorRegistry registry, int x, int y, int z) {
        ActorTypeStats stats = ActorTestFixtures.stats(Wastrel.TYPE);
        return registry.spawn(Wastrel.TYPE, stats, PackedPos.pack(x, y, z));
    }

    private static Actor spawnBoundWatch(ActorRegistry registry, ActorContext ctx, int x, int y, int z) {
        ActorTypeStats stats = ActorTestFixtures.stats(MilitiaWatch.TYPE);
        Actor watch = registry.spawn(MilitiaWatch.TYPE, stats, PackedPos.pack(x, y, z));
        watch.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
        return watch;
    }

    private static Job.Villain boundVillainJob(ActorContext ctx, com.trojia.sim.actor.job.JobId id) {
        return (Job.Villain) ctx.jobs().get(ctx.jobs().ordinalOf(id));
    }

    // ======================================================================
    // Detection gating: same z + radius, no false positives
    // ======================================================================

    @Test
    void noArrestWhenNoWatchIsAnywhereNearby() {
        ActorRegistry registry = new ActorRegistry();
        Actor cutpurse = spawnVillain(registry, 50, 50, Z);
        PinnedDrawContext ctx = new PinnedDrawContext(registry, 0L, 0L); // would succeed if reached
        Job.Villain job = boundVillainJob(ctx, Job.Villain.Cutpurse.ID);

        boolean arrested = JobBehaviors.checkArrestExposure(cutpurse, ctx, job);

        assertFalse(arrested, "no Watch anywhere near -> never even draws");
        assertFalse(cutpurse.hasStatus(StatusBit.HELD));
    }

    @Test
    void noArrestWhenTheOnlyWatchIsOnADifferentZLevel() {
        ActorRegistry registry = new ActorRegistry();
        Actor cutpurse = spawnVillain(registry, 50, 50, Z);
        PinnedDrawContext ctx = new PinnedDrawContext(registry, 0L, 0L);
        spawnBoundWatch(registry, ctx, 50, 50, Z + 1); // same x/y, different z
        Job.Villain job = boundVillainJob(ctx, Job.Villain.Cutpurse.ID);

        boolean arrested = JobBehaviors.checkArrestExposure(cutpurse, ctx, job);

        assertFalse(arrested, "a Watch on a different z-level must never trigger detection");
        assertFalse(cutpurse.hasStatus(StatusBit.HELD));
    }

    @Test
    void watchExactlyAtTheDetectionRadiusStillTriggersADraw() {
        ActorRegistry registry = new ActorRegistry();
        Actor cutpurse = spawnVillain(registry, 50, 50, Z);
        PinnedDrawContext ctx = new PinnedDrawContext(registry, 0L, 0L); // draw 0 always "succeeds"
        spawnBoundWatch(registry, ctx, 58, 50, Z); // chebyshev == 8, the documented radius
        Job.Villain job = boundVillainJob(ctx, Job.Villain.Cutpurse.ID);

        boolean arrested = JobBehaviors.checkArrestExposure(cutpurse, ctx, job);

        assertTrue(arrested, "chebyshev == ARREST_DETECT_RADIUS must still count as in range");
        assertTrue(cutpurse.hasStatus(StatusBit.HELD));
    }

    @Test
    void watchOneCellBeyondTheDetectionRadiusNeverTriggers() {
        ActorRegistry registry = new ActorRegistry();
        Actor cutpurse = spawnVillain(registry, 50, 50, Z);
        PinnedDrawContext ctx = new PinnedDrawContext(registry, 0L, 0L);
        spawnBoundWatch(registry, ctx, 59, 50, Z); // chebyshev == 9, just outside
        Job.Villain job = boundVillainJob(ctx, Job.Villain.Cutpurse.ID);

        boolean arrested = JobBehaviors.checkArrestExposure(cutpurse, ctx, job);

        assertFalse(arrested);
        assertFalse(cutpurse.hasStatus(StatusBit.HELD));
    }

    // ======================================================================
    // Chance roll: exposed does not always mean caught
    // ======================================================================

    @Test
    void exposureWithoutAFavorableDrawDoesNotArrest() {
        ActorRegistry registry = new ActorRegistry();
        Actor cutpurse = spawnVillain(registry, 50, 50, Z);
        // 6554 is ARREST_CHANCE_Q16 itself -- the ">=" boundary is deliberately a miss.
        PinnedDrawContext ctx = new PinnedDrawContext(registry, 6554L, 0L);
        spawnBoundWatch(registry, ctx, 52, 50, Z);
        Job.Villain job = boundVillainJob(ctx, Job.Villain.Cutpurse.ID);

        boolean arrested = JobBehaviors.checkArrestExposure(cutpurse, ctx, job);

        assertFalse(arrested, "a draw landing exactly on the chance boundary must be a miss");
        assertFalse(cutpurse.hasStatus(StatusBit.HELD));
    }

    // ======================================================================
    // Ordinary arrest + hold: Robber/Cutpurse
    // ======================================================================

    @Test
    void ordinaryVillainCaughtNearTheWatchIsHeldWithADrawnSentence() {
        ActorRegistry registry = new ActorRegistry();
        Actor cutpurse = spawnVillain(registry, 50, 50, Z);
        PinnedDrawContext ctx = new PinnedDrawContext(registry, 0L, 0L); // sentence draw 0 -> floor
        ctx.setTick(1_000L);
        spawnBoundWatch(registry, ctx, 53, 51, Z);
        Job.Villain job = boundVillainJob(ctx, Job.Villain.Cutpurse.ID);
        Persona before = cutpurse.identity();

        boolean arrested = JobBehaviors.checkArrestExposure(cutpurse, ctx, job);

        assertTrue(arrested);
        assertTrue(cutpurse.hasStatus(StatusBit.HELD));
        assertFalse(cutpurse.hasStatus(StatusBit.EXECUTED));
        assertFalse(cutpurse.hasStatus(StatusBit.MAIMED), "only Skyrunners maim");
        assertEquals(1_000L + 24_000L, cutpurse.heldUntilTick(),
                "sentence draw 0 -> the 1-day floor (DailyRhythm.DAY)");
        assertEquals(1, cutpurse.offenseCount());
        assertEquals(ReasonCode.ARRESTED, cutpurse.lastReasonCode());
        assertEquals(before, cutpurse.identity(),
                "arrest is a mechanical sim-state transition, never a presented-identity change "
                        + "(DECISIONS.md's presentedId ruling)");
    }

    @Test
    void sentenceDrawIsBoundedToOneThroughThreeDays() {
        ActorRegistry registry = new ActorRegistry();
        Actor cutpurse = spawnVillain(registry, 50, 50, Z);
        PinnedDrawContext ctx = new PinnedDrawContext(registry, 0L, -1L); // max unsigned remainder
        ctx.setTick(0L);
        spawnBoundWatch(registry, ctx, 51, 50, Z);
        Job.Villain job = boundVillainJob(ctx, Job.Villain.Cutpurse.ID);

        JobBehaviors.checkArrestExposure(cutpurse, ctx, job);

        assertTrue(cutpurse.heldUntilTick() >= 24_000L && cutpurse.heldUntilTick() <= 72_000L,
                "1-3 days: [24000, 72000] ticks, DailyRhythm.DAY=24000");
    }

    @Test
    void alreadyHeldOrExecutedShortCircuitsWithoutRedrawing() {
        ActorRegistry registry = new ActorRegistry();
        Actor cutpurse = spawnVillain(registry, 50, 50, Z);
        cutpurse.setStatus(StatusBit.HELD, true);
        cutpurse.setHeldUntilTick(999L);
        PinnedDrawContext ctx = new PinnedDrawContext(registry, null, null); // any redraw is a bug
        Job.Villain job = boundVillainJob(ctx, Job.Villain.Cutpurse.ID);

        boolean result = JobBehaviors.checkArrestExposure(cutpurse, ctx, job);

        assertTrue(result);
        assertEquals(999L, cutpurse.heldUntilTick(), "must not redraw a sentence while already held");
    }

    // ======================================================================
    // Skyrunner escalation: maim, then hang
    // ======================================================================

    @Test
    void skyrunnerFirstOffenseIsMaimedNotHeld() {
        ActorRegistry registry = new ActorRegistry();
        Actor skyrunner = spawnVillain(registry, 50, 50, Z);
        PinnedDrawContext ctx = new PinnedDrawContext(registry, 0L, 0L);
        spawnBoundWatch(registry, ctx, 52, 50, Z);
        Job.Villain job = boundVillainJob(ctx, Job.Villain.Skyrunner.ID);

        boolean arrested = JobBehaviors.checkArrestExposure(skyrunner, ctx, job);

        assertFalse(arrested, "maiming is cosmetic only -- self resumes its own job, unheld");
        assertTrue(skyrunner.hasStatus(StatusBit.MAIMED));
        assertFalse(skyrunner.hasStatus(StatusBit.HELD));
        assertFalse(skyrunner.hasStatus(StatusBit.EXECUTED));
        assertFalse(skyrunner.hasStatus(StatusBit.DOWNED));
        assertEquals(1, skyrunner.offenseCount());
        assertEquals(ReasonCode.MAIMED_FIRST_OFFENSE, skyrunner.lastReasonCode());
    }

    @Test
    void skyrunnerSecondOffenseIsDownedAndExecutedPermanently() {
        ActorRegistry registry = new ActorRegistry();
        Actor skyrunner = spawnVillain(registry, 50, 50, Z);
        PinnedDrawContext ctx = new PinnedDrawContext(registry, 0L, 0L);
        spawnBoundWatch(registry, ctx, 52, 50, Z);
        Job.Villain job = boundVillainJob(ctx, Job.Villain.Skyrunner.ID);

        JobBehaviors.checkArrestExposure(skyrunner, ctx, job); // 1st: maimed
        boolean arrestedSecond = JobBehaviors.checkArrestExposure(skyrunner, ctx, job); // 2nd: hanged

        assertTrue(arrestedSecond, "the 2nd offense dominates the stack (EXECUTED) going forward");
        assertTrue(skyrunner.hasStatus(StatusBit.MAIMED), "the 1st offense's mark is permanent too");
        assertTrue(skyrunner.hasStatus(StatusBit.DOWNED));
        assertTrue(skyrunner.hasStatus(StatusBit.EXECUTED));
        assertFalse(skyrunner.hasStatus(StatusBit.HELD), "hanging replaces holding for Skyrunners");
        assertEquals(2, skyrunner.offenseCount());
        assertEquals(ReasonCode.EXECUTED_SECOND_OFFENSE, skyrunner.lastReasonCode());
    }

    // ======================================================================
    // The wander-dwell cadence hook
    // ======================================================================

    @Test
    void wanderDwellCompleteIsTrueOnlyAtTheThresholdTick() {
        ActorRegistry registry = new ActorRegistry();
        Actor cutpurse = spawnVillain(registry, 50, 50, Z);
        cutpurse.setGoalTarget(TargetKind.CELL, cutpurse.cell());
        JobParams params = new JobParams(com.trojia.sim.actor.job.GoalKind.LIFT_PURSE,
                200, 0, 24000, 0, 30, 3, com.trojia.sim.actor.job.RenewMode.COOLDOWN, 6000);

        cutpurse.setGoalWorkTicks(0);
        assertFalse(JobBehaviors.isWanderDwellComplete(cutpurse, params), "tick 1 of 30: not yet");

        cutpurse.setGoalWorkTicks(params.workTicksPerUnit() - 1);
        assertTrue(JobBehaviors.isWanderDwellComplete(cutpurse, params), "the 30th tick completes it");
    }

    @Test
    void wanderDwellCompleteIsFalseWhileStillWalkingToTheTarget() {
        ActorRegistry registry = new ActorRegistry();
        Actor cutpurse = spawnVillain(registry, 50, 50, Z);
        cutpurse.setGoalTarget(TargetKind.CELL, PackedPos.pack(60, 60, Z)); // not at cell yet
        JobParams params = new JobParams(com.trojia.sim.actor.job.GoalKind.LIFT_PURSE,
                200, 0, 24000, 0, 30, 3, com.trojia.sim.actor.job.RenewMode.COOLDOWN, 6000);
        cutpurse.setGoalWorkTicks(29);

        assertFalse(JobBehaviors.isWanderDwellComplete(cutpurse, params),
                "still en route to the target -- no dwell to complete yet");
    }
}
