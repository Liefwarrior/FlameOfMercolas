package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The sleep-at-home behavior (ACTORS-SPEC.md §11.1, tests A51/A52's shape):
 * {@code RETURN_HOME} triggers on REST-low or night-window, is a no-op once
 * home, and an interrupted walk resumes toward the same target.
 */
final class ReturnHomePolicyTest {

    private static Actor serfAt(ActorRegistry registry, int x, int y) {
        return registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), PackedPos.pack(x, y, 1));
    }

    @Test
    void scoresZeroWithoutABakedHome() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        assertEquals(0, Policies.RETURN_HOME.score(actor, ctx));
    }

    @Test
    void scoresZeroWhenAlreadyHome() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int homeId = ctx.homes().addHome(actor.cell());
        actor.setHomeId(homeId);
        assertEquals(0, Policies.RETURN_HOME.score(actor, ctx));
    }

    @Test
    void staysNonZeroAtHomeDuringTheDayUntilRestIsComfortablyRecovered() {
        // The oscillation-hysteresis fix (NeedThresholds.RECOVERED), symmetric with
        // SeekFoodPolicyTest's equivalent case: merely crossing back above LOW while at home
        // (daytime, no night window) must NOT drop this score to 0, or GOAL_PURSUE immediately
        // re-wins and walks the actor straight back out, forever.
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(1_000L); // daytime for the fixture's [12000,24000) night window
        int homeId = ctx.homes().addHome(actor.cell());
        actor.setHomeId(homeId);

        actor.applyNeedDelta(Need.REST, -9000); // drive REST to 0 (well below LOW)
        assertTrue(Policies.RETURN_HOME.score(actor, ctx) > 0, "still critical — must keep winning at home");

        actor.applyNeedDelta(Need.REST, 3500); // 0 -> 3500: above LOW (3000), still below RECOVERED (6000)
        assertTrue(Policies.RETURN_HOME.score(actor, ctx) > 0,
                "above LOW but below RECOVERED — must still win, or the actor oscillates back out");

        actor.applyNeedDelta(Need.REST, 2500); // 3500 -> 6000, exactly RECOVERED
        assertEquals(0, Policies.RETURN_HOME.score(actor, ctx), "comfortably recovered — releases back to GOAL_PURSUE");
    }

    @Test
    void scoresZeroAtHomeDuringTheNightWindowRegardlessOfRestHysteresis() {
        // The night-window branch is deliberately untouched by the hysteresis fix (design
        // note): pursueAtAnchor's own off-shift target already keeps the actor home through
        // the night, so this policy stays 0 at home at night even with REST below RECOVERED.
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(15_000L); // inside the fixture's [12000,24000) night window
        int homeId = ctx.homes().addHome(actor.cell());
        actor.setHomeId(homeId);

        actor.applyNeedDelta(Need.REST, -9000); // drive REST to 0 (well below LOW and RECOVERED)
        assertEquals(0, Policies.RETURN_HOME.score(actor, ctx), "at home at night — not applicable regardless of REST");
    }

    @Test
    void triggersWhenRestCrossesLowEvenOutsideTheNightWindow() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(1_000L); // daytime for the fixture's [12000,24000) night window
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);

        assertEquals(0, Policies.RETURN_HOME.score(actor, ctx), "REST is full, no window: not applicable");

        actor.applyNeedDelta(Need.REST, -6100); // 9000 -> 2900, below LOW (3000), not below CRITICAL (1000)
        int score = Policies.RETURN_HOME.score(actor, ctx);
        assertEquals(actor.stats().returnHomePriority() + actor.stats().need(Need.REST).lowBonus(), score,
                "REST-low outside the window scores the base priority plus the low urgency bonus");
    }

    @Test
    void triggersDuringTheNightWindowEvenWithFullRest() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(15_000L); // inside the fixture's [12000,24000) night window
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);

        int score = Policies.RETURN_HOME.score(actor, ctx);
        assertEquals(actor.stats().returnHomePriority() + actor.stats().returnHomeRhythmBonus(), score);
    }

    @Test
    void actIsDeterministicGreedyWalkThatArrivesHome() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 0);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(15_000L);
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);

        // speedTicksPerStep is 1 in the fixture, so every act() call moves one cell.
        for (int i = 0; i < 10 && actor.cell() != PackedPos.pack(0, 0, 1); i++) {
            Policies.RETURN_HOME.act(actor, ctx);
        }
        assertEquals(PackedPos.pack(0, 0, 1), actor.cell());
        assertEquals(0, Policies.RETURN_HOME.score(actor, ctx), "score drops to 0 the instant it arrives");
    }

    @Test
    void wholeStackPrefersReturnHomeOverGoalPursueDuringTheNightWindow() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 0);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(15_000L);
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);
        actor.setJobOrdinal((short) ctx.jobs().ordinalOf(
                com.trojia.sim.actor.job.Job.Serf.Laborer.ID));

        actor.tick(ctx);
        assertTrue(actor.cell() != PackedPos.pack(10, 0, 1), "the actor must have stepped toward home");
    }
}
