package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Direct coverage of {@link FleePolicy} — the EMERGENCY-band panic/safety
 * response — previously untested anywhere in the suite. Covers the score
 * gate, the world-edge clamp (an actor at the map boundary must never
 * produce an out-of-range coordinate), the deliberate leash bypass, and
 * fixed-seed determinism of the jitter draw.
 */
final class FleePolicyTest {

    private static Actor serfAt(ActorRegistry registry, int x, int y, int z, int leashRadius) {
        ActorTypeStats stats = ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, leashRadius);
        return registry.spawn(Serf.TYPE, stats, PackedPos.pack(x, y, z));
    }

    @Test
    void scoresZeroWhenSafetyIsNotCritical() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 50, 50, 1, 24);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        assertEquals(0, Policies.FLEE.score(actor, ctx));
    }

    @Test
    void scoresTheFleeEmergencyPriorityWhenSafetyIsCritical() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 50, 50, 1, 24);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        // fixture SAFETY starts at 10000 (need(6)); drive it below CRITICAL (1000).
        actor.applyNeedDelta(Need.SAFETY, -(10_000 - (NeedThresholds.CRITICAL - 1)));

        assertTrue(NeedThresholds.isCritical(actor.need(Need.SAFETY)), "sanity: SAFETY is critical");
        assertEquals(actor.stats().fleeEmergencyPriority(), Policies.FLEE.score(actor, ctx));
    }

    @Test
    void actMovesTheActorEvenWhenEveryDirectionExceedsTheLeash() {
        ActorRegistry registry = new ActorRegistry();
        // leashRadius=0: any single step away from the anchor (== spawn cell) would be
        // refused by a leash-respecting stepToward. FLEE must move anyway (§1.3, §2.5).
        Actor actor = serfAt(registry, 2000, 2000, 1, 0);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int before = actor.cell();

        Policies.FLEE.act(actor, ctx);

        assertNotEquals(before, actor.cell(), "FLEE must ignore the leash and actually move");
        assertEquals(ReasonCode.SAFETY_CRITICAL, actor.lastReasonCode());
    }

    @Test
    void actAtTheOriginCornerNeverProducesANegativeCoordinate() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0, 3, 24);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        Policies.FLEE.act(actor, ctx);

        assertTrue(PackedPos.x(actor.cell()) >= 0 && PackedPos.x(actor.cell()) <= PackedPos.X_MASK);
        assertTrue(PackedPos.y(actor.cell()) >= 0 && PackedPos.y(actor.cell()) <= PackedPos.Y_MASK);
        assertEquals(3, PackedPos.z(actor.cell()), "fleeing never changes z");
    }

    @Test
    void actAtTheFarCornerNeverExceedsTheMaxCoordinate() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, PackedPos.X_MASK, PackedPos.Y_MASK, 3, 24);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        Policies.FLEE.act(actor, ctx);

        assertTrue(PackedPos.x(actor.cell()) >= 0 && PackedPos.x(actor.cell()) <= PackedPos.X_MASK);
        assertTrue(PackedPos.y(actor.cell()) >= 0 && PackedPos.y(actor.cell()) <= PackedPos.Y_MASK);
    }

    @Test
    void jitterDrawIsReproducibleAcrossIdenticalSetups() {
        ActorRegistry registryA = new ActorRegistry();
        Actor actorA = serfAt(registryA, 2000, 2000, 1, 24);
        NoOpActorContext ctxA = new NoOpActorContext(registryA);

        ActorRegistry registryB = new ActorRegistry();
        Actor actorB = serfAt(registryB, 2000, 2000, 1, 24);
        NoOpActorContext ctxB = new NoOpActorContext(registryB);

        Policies.FLEE.act(actorA, ctxA);
        Policies.FLEE.act(actorB, ctxB);

        assertEquals(actorA.cell(), actorB.cell(),
                "identical (worldSeed, tick, actorId, drawIndex) must pick the same heading");
    }
}
