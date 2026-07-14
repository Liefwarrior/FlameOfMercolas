package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The HUNGER-seeking behavior (the needs-hierarchy pass, ACTORS-SPEC.md
 * §3.3): {@code SEEK_FOOD} triggers once HUNGER crosses LOW, is a no-op once
 * home, and — the single most important case here — wins the whole-stack
 * tiebreak against {@code RETURN_HOME} even when both needs' urgency bonuses
 * are exactly equal (the fixture's HUNGER/REST lowBonus=250/critBonus=500 on
 * both), proving the "HUNGER always outranks REST" invariant holds by stack
 * order, not by coincidence of the raw numbers.
 */
final class SeekFoodPolicyTest {

    private static Actor serfAt(ActorRegistry registry, int x, int y) {
        return registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), PackedPos.pack(x, y, 1));
    }

    @Test
    void scoresZeroWithoutABakedHome() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        assertEquals(0, Policies.SEEK_FOOD.score(actor, ctx));
    }

    @Test
    void scoresZeroWhenAlreadyHome() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int homeId = ctx.homes().addHome(actor.cell());
        actor.setHomeId(homeId);
        actor.applyNeedDelta(Need.HUNGER, -9000); // drive HUNGER to 0 (well below LOW)
        assertEquals(0, Policies.SEEK_FOOD.score(actor, ctx), "already home — not applicable regardless of HUNGER");
    }

    @Test
    void scoresZeroWhenHungerIsNotLow() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);

        assertEquals(0, Policies.SEEK_FOOD.score(actor, ctx), "HUNGER starts full — not applicable");
    }

    @Test
    void scoresPriorityPlusLowBonusOnceHungerCrossesLow() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);

        actor.applyNeedDelta(Need.HUNGER, -6100); // 9000 -> 2900, below LOW (3000), not below CRITICAL (1000)
        int score = Policies.SEEK_FOOD.score(actor, ctx);
        assertEquals(actor.stats().seekFoodPriority() + actor.stats().need(Need.HUNGER).lowBonus(), score);
    }

    @Test
    void scoresPriorityPlusCritBonusOnceHungerCrossesCritical() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);

        actor.applyNeedDelta(Need.HUNGER, -8100); // 9000 -> 900, below CRITICAL (1000)
        int score = Policies.SEEK_FOOD.score(actor, ctx);
        assertEquals(actor.stats().seekFoodPriority() + actor.stats().need(Need.HUNGER).critBonus(), score);
    }

    @Test
    void actIsDeterministicGreedyWalkThatArrivesHome() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 0);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);
        actor.applyNeedDelta(Need.HUNGER, -8100); // below CRITICAL

        for (int i = 0; i < 10 && actor.cell() != PackedPos.pack(0, 0, 1); i++) {
            Policies.SEEK_FOOD.act(actor, ctx);
        }
        assertEquals(PackedPos.pack(0, 0, 1), actor.cell());
        assertEquals(0, Policies.SEEK_FOOD.score(actor, ctx), "score drops to 0 the instant it arrives");
    }

    @Test
    void tickingAtHomeRecoversHungerAboveLowSoTheActorEventuallyHeadsBackOut() {
        // Drives through the whole Actor#tick() template (decay + recoverHungerAtHome +
        // policy selection), not just act() directly, to prove the recovery loop actually
        // closes: hungry -> SEEK_FOOD walks home -> recoverHungerAtHome refills HUNGER ->
        // SEEK_FOOD's score drops back to 0. Mirrors the REST rationale in Actor.java's
        // recoverRestAtHome javadoc ("without this... RETURN_HOME pins the population home
        // forever") — the same freeze would happen to HUNGER without this recovery.
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 3, 0);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);
        actor.applyNeedDelta(Need.HUNGER, -8100); // 9000 -> 900, below CRITICAL

        for (int i = 0; i < 2_000 && actor.cell() != PackedPos.pack(0, 0, 1); i++) {
            actor.tick(ctx);
        }
        assertEquals(PackedPos.pack(0, 0, 1), actor.cell(), "must have walked all the way home");

        // One more tick while still at home: LOITER may fire (the winning policy once
        // SEEK_FOOD's score drops to 0), but recoverHungerAtHome runs unconditionally in
        // Actor#tick() based on position alone, independent of which policy wins — so HUNGER
        // must climb this tick regardless.
        int hungerJustArrived = actor.need(Need.HUNGER);
        actor.tick(ctx);
        assertTrue(actor.need(Need.HUNGER) > hungerJustArrived,
                "HUNGER must climb the very next tick at home (recoverHungerAtHome), was "
                        + hungerJustArrived + " now " + actor.need(Need.HUNGER));
    }

    @Test
    void wholeStackPrefersSeekFoodOverReturnHomeWhenBothAreEquallyLow() {
        // The fixture (ActorTestFixtures) authors HUNGER lowBonus/critBonus == REST
        // lowBonus/critBonus (250/500 both) — an exact tie in the score formula. This test
        // exploits that zero-margin case to prove the invariant holds by stack order
        // (SEEK_FOOD declared before RETURN_HOME in every stack), not just by coincidence
        // of the raw numbers happening to differ.
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 0);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(1_000L); // daytime — no night-rhythm term to muddy the comparison
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);

        actor.applyNeedDelta(Need.HUNGER, -6100); // 9000 -> 2900, low, not critical
        actor.applyNeedDelta(Need.REST, -6100);   // 9000 -> 2900, low, not critical (same delta)

        assertEquals(Policies.SEEK_FOOD.score(actor, ctx), Policies.RETURN_HOME.score(actor, ctx),
                "sanity: the fixture's equal bonuses really do produce an exact score tie");

        actor.tick(ctx);
        assertTrue(actor.cell() != PackedPos.pack(10, 0, 1), "the actor must have stepped toward home");
        assertEquals(ReasonCode.NEED_HUNGER_LOW, actor.lastReasonCode(),
                "SEEK_FOOD must win the tie by stack position, not RETURN_HOME");
    }

    @Test
    void wholeStackPrefersSeekFoodOverGoalPursueWhenHungerIsCritical() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 0);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(1_000L);
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);
        actor.setJobOrdinal((short) ctx.jobs().ordinalOf(
                com.trojia.sim.actor.job.Job.Serf.Laborer.ID));
        actor.applyNeedDelta(Need.HUNGER, -8100); // below CRITICAL

        actor.tick(ctx);
        assertTrue(actor.cell() != PackedPos.pack(10, 0, 1), "the actor must have stepped toward home");
        assertEquals(ReasonCode.NEED_HUNGER_LOW, actor.lastReasonCode());
    }
}
