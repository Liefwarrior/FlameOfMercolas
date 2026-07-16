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
    void scoresNonZeroWhenHomeButNotYetRecovered() {
        // The oscillation-hysteresis fix (NeedThresholds.RECOVERED): merely crossing back
        // above LOW while at home must NOT drop this score to 0 — otherwise GOAL_PURSUE
        // immediately re-wins and walks the actor straight back out, forever.
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int homeId = ctx.homes().addHome(actor.cell());
        actor.setHomeId(homeId);
        actor.applyNeedDelta(Need.HUNGER, -9000); // drive HUNGER to 0 (well below LOW)
        assertTrue(Policies.SEEK_FOOD.score(actor, ctx) > 0, "still critical — must keep winning at home");

        actor.applyNeedDelta(Need.HUNGER, 3500); // 0 -> 3500: above LOW (3000), still below RECOVERED (6000)
        assertTrue(Policies.SEEK_FOOD.score(actor, ctx) > 0,
                "above LOW but below RECOVERED — must still win, or the actor oscillates back out");
    }

    @Test
    void scoresZeroWhenHomeAndRecovered() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int homeId = ctx.homes().addHome(actor.cell());
        actor.setHomeId(homeId);
        actor.applyNeedDelta(Need.HUNGER, -3000); // 9000 -> 6000, exactly RECOVERED
        assertEquals(0, Policies.SEEK_FOOD.score(actor, ctx), "comfortably recovered — releases back to GOAL_PURSUE");
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
    void actWalksToTheHomeLarderEatsAndReleases() {
        // The economy-loop rework: HUNGER no longer recovers passively at home — the actor must
        // EAT a FOOD. A stocked home larder is the free source; the hungry actor A*-walks to it,
        // eats one (+EAT_RESTORE, sunk), and SEEK_FOOD releases once comfortably RECOVERED.
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 0);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int home = PackedPos.pack(0, 0, 1);
        actor.setHomeId(ctx.homes().addHome(home));
        ctx.items().addOnCell(home, ItemKinds.FOOD, 5); // a stocked larder
        actor.applyNeedDelta(Need.HUNGER, -8100); // below CRITICAL

        for (int i = 0; i < 200 && Policies.SEEK_FOOD.score(actor, ctx) > 0; i++) {
            actor.tick(ctx);
        }
        assertEquals(0, Policies.SEEK_FOOD.score(actor, ctx),
                "ate from the home larder and released back to the job");
        assertTrue(actor.need(Need.HUNGER) >= NeedThresholds.RECOVERED,
                "eating restored HUNGER comfortably above RECOVERED, was " + actor.need(Need.HUNGER));
        assertTrue(ActorGeometry.chebyshev(actor.cell(), home) <= FoodEconomy.EAT_REACH,
                "must have reached within EAT_REACH of the home larder to eat");
        assertTrue(ctx.items().countOnCellOfKind(home, ItemKinds.FOOD) < 5,
                "a FOOD must have been consumed (sunk) from the larder");
    }

    @Test
    void eatingIsTheOnlyWayHungerRecovers() {
        // With the passive at-home recovery deleted, an actor sitting on its home cell with NO
        // reachable FOOD (empty larder, no shop, no commons) must NEVER recover — HUNGER only
        // falls. This is what makes starvation possible (the whole point of the pass).
        ActorRegistry registry = new ActorRegistry();
        int home = PackedPos.pack(0, 0, 1);
        Actor actor = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), home);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        actor.setHomeId(ctx.homes().addHome(home)); // stands ON its home cell, empty larder
        actor.applyNeedDelta(Need.HUNGER, -6100); // 9000 -> 2900, low
        int before = actor.need(Need.HUNGER);

        for (int i = 0; i < 100; i++) {
            actor.tick(ctx);
        }
        assertTrue(actor.need(Need.HUNGER) < before,
                "no food anywhere -> HUNGER must only fall, never recover at home (was " + before
                        + ", now " + actor.need(Need.HUNGER) + ")");
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
