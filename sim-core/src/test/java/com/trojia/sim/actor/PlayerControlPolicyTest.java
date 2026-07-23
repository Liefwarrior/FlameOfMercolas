package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Play mode's direct-control override (PLAY-MODE-SPEC.md §5.2): {@link PlayerControlPolicy}'s
 * score gate, its consuming {@code act()}, and {@link Actor#setActAs(int)}'s Persona seam
 * (§5.3). Mirrors {@link HeldPolicyTest}'s shape.
 */
final class PlayerControlPolicyTest {

    private static final int Z = 11;

    private static Actor serfAt(ActorRegistry registry, int x, int y) {
        ActorTypeStats stats = ActorTestFixtures.stats(Serf.TYPE);
        return registry.spawn(Serf.TYPE, stats, PackedPos.pack(x, y, Z));
    }

    @Test
    void scoreIsZeroWhenNotPlayerControlled() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 10);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        assertEquals(0, Policies.PLAYER_CONTROL.score(actor, ctx));
    }

    @Test
    void scoreOutranksOrdinaryAiBandsButStaysBelowHeldAndExecuted() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 10);
        actor.setStatus(StatusBit.PLAYER_CONTROLLED, true);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        int score = Policies.PLAYER_CONTROL.score(actor, ctx);
        // RETURN_HOME/SEEK_FOOD's observed ceiling is ~1305 (ACTORS-SPEC's needs-hierarchy
        // pass); PLAYER_CONTROL must clear it, but a played actor must still be arrestable/
        // hangable (HELD=5000, EXECUTED=6000 stay strictly above it).
        assertTrue(score > 1305, "must outrank every ordinary AI band: " + score);
        assertTrue(score < 5000, "must NOT outrank HELD (arrest still holds a played actor): " + score);
    }

    @Test
    void actStepsTowardThePendingTargetThenConsumesIt() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 10);
        actor.setStatus(StatusBit.PLAYER_CONTROLLED, true);
        actor.setPlayerMoveTarget(PackedPos.pack(11, 10, Z));
        NoOpActorContext ctx = new NoOpActorContext(registry);

        Policies.PLAYER_CONTROL.act(actor, ctx);

        assertEquals(PackedPos.pack(11, 10, Z), actor.cell(), "one leash-ignoring step toward the target");
        assertEquals(Actor.NONE, actor.playerMoveTargetCell(), "the intent must be consumed, not re-fired");
        assertEquals(ReasonCode.PLAYER_CONTROLLED, actor.lastReasonCode());
    }

    @Test
    void actResolvesThePickpocketIntentAndConsumesIt() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 10);
        Actor mark = serfAt(registry, 11, 10);
        actor.setStatus(StatusBit.PLAYER_CONTROLLED, true);
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            private final CrimeLog log = new CrimeLog(8);

            @Override
            public CrimeLog crimeLog() {
                return log;
            }
        };
        ctx.items().addCarried(mark.id(), ItemKinds.COIN, 2);

        actor.setPlayerPickpocketTarget(mark.id());
        Policies.PLAYER_CONTROL.act(actor, ctx);

        assertEquals(Actor.NONE, actor.playerPickpocketTargetId(),
                "the intent must be consumed, not re-fired");
        assertTrue(actor.lastReasonCode() == ReasonCode.PICKPOCKETED
                        || actor.lastReasonCode() == ReasonCode.CAUGHT_STEALING,
                "an adjacent attempt resolves and keeps its outcome stamp: "
                        + actor.lastReasonCode());
        assertEquals(1, ctx.crimeLog().totalRecorded(), "the attempt is logged");

        // An out-of-reach intent is consumed but resolves to no attempt at all.
        Actor far = serfAt(registry, 30, 30);
        actor.setPlayerPickpocketTarget(far.id());
        Policies.PLAYER_CONTROL.act(actor, ctx);
        assertEquals(ReasonCode.PLAYER_CONTROLLED, actor.lastReasonCode(),
                "no reach -> no attempt -> the ordinary play-mode stamp");
        assertEquals(1, ctx.crimeLog().totalRecorded(), "nothing new logged");
    }

    @Test
    void actResolvesTheEatIntentThroughTheSeekFoodChainAndConsumesIt() {
        // Sprint 4: the played-actor eat verb. A hungry played soul with a carried ration
        // eats it (step 1 of SeekFoodPolicy's in-reach chain) with the ordinary stamp.
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 10);
        actor.setHomeId(0); // quoteFor/eatInReach read the home cell
        actor.setStatus(StatusBit.PLAYER_CONTROLLED, true);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.homes().addHome(PackedPos.pack(10, 10, Z));
        ctx.items().addCarried(actor.id(), ItemKinds.FOOD, 1);
        actor.applyNeedDelta(Need.HUNGER, -8000);
        int hungerBefore = actor.need(Need.HUNGER);

        actor.setPlayerEatIntent(true);
        Policies.PLAYER_CONTROL.act(actor, ctx);

        assertFalse(actor.playerEatIntent(), "the intent must be consumed, not re-fired");
        assertTrue(actor.need(Need.HUNGER) > hungerBefore, "the meal restored HUNGER");
        assertEquals(0, ctx.items().countCarriedOfKind(actor.id(), ItemKinds.FOOD),
                "the ration was sunk");
        assertEquals(ReasonCode.ATE_FOOD, actor.lastReasonCode(),
                "the ordinary eat stamp survives the tick");

        // Nothing in reach: the intent is consumed and the miss is stamped legibly.
        actor.setPlayerEatIntent(true);
        Policies.PLAYER_CONTROL.act(actor, ctx);
        assertFalse(actor.playerEatIntent());
        assertEquals(ReasonCode.NO_MEAL_IN_REACH, actor.lastReasonCode(),
                "an empty-handed eat press reads as the refusal toast");
    }

    @Test
    void actWithNoPendingTargetIsAHarmlessNoOpThatStillSetsTheReason() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 10);
        actor.setStatus(StatusBit.PLAYER_CONTROLLED, true);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        int cellBefore = actor.cell();

        Policies.PLAYER_CONTROL.act(actor, ctx);

        assertEquals(cellBefore, actor.cell());
        assertEquals(ReasonCode.PLAYER_CONTROLLED, actor.lastReasonCode());
    }

    @Test
    void heldStillDominatesAPlayerControlledActorOnWastrelsFullStack() {
        // The adversarial-parity check HeldDominatesWholeStackTest's own doc flags as worth
        // adding once PLAYER_CONTROL exists: HELD must still win even with PLAYER_CONTROLLED
        // also set (a played actor cannot walk out of custody by holding a key).
        ActorRegistry registry = new ActorRegistry();
        ActorTypeStats stats = ActorTestFixtures.stats(Wastrel.TYPE);
        Actor actor = registry.spawn(Wastrel.TYPE, stats, PackedPos.pack(10, 10, Z));
        NoOpActorContext ctx = new NoOpActorContext(registry);
        actor.setStatus(StatusBit.PLAYER_CONTROLLED, true);
        actor.setPlayerMoveTarget(PackedPos.pack(11, 10, Z));
        actor.setStatus(StatusBit.HELD, true);
        actor.setHeldUntilTick(50_000L);
        ctx.setTick(1_000L);

        actor.tick(ctx);

        assertEquals(ReasonCode.HELD_IN_CUSTODY, actor.lastReasonCode(),
                "HELD (5000) must still outrank PLAYER_CONTROL (2000) even when both are set");
    }

    @Test
    void executedStillDominatesAPlayerControlledActorOnWastrelsFullStack() {
        ActorRegistry registry = new ActorRegistry();
        ActorTypeStats stats = ActorTestFixtures.stats(Wastrel.TYPE);
        Actor actor = registry.spawn(Wastrel.TYPE, stats, PackedPos.pack(10, 10, Z));
        NoOpActorContext ctx = new NoOpActorContext(registry);
        actor.setStatus(StatusBit.PLAYER_CONTROLLED, true);
        actor.setPlayerMoveTarget(PackedPos.pack(11, 10, Z));
        actor.setStatus(StatusBit.EXECUTED, true);
        int cellBefore = actor.cell();
        ctx.setTick(1_000L);

        actor.tick(ctx);

        assertEquals(cellBefore, actor.cell(),
                "EXECUTED (6000) must still outrank PLAYER_CONTROL (2000) and never move the actor");
    }

    // ---- setActAs (Persona seam, §5.3) -------------------------------------------------

    @Test
    void setActAsChangesPresentedIdButNeverTrueId() {
        ActorRegistry registry = new ActorRegistry();
        Actor player = serfAt(registry, 10, 10);
        Actor other = serfAt(registry, 20, 20);
        int trueIdBefore = player.identity().trueId();

        player.setActAs(other.id());

        assertEquals(trueIdBefore, player.identity().trueId(), "true identity never changes");
        assertEquals(other.id(), player.identity().presentedId());
        assertTrue(player.identity().isDisguised());
    }

    @Test
    void setActAsWithOwnIdDropsTheDisguise() {
        ActorRegistry registry = new ActorRegistry();
        Actor player = serfAt(registry, 10, 10);
        Actor other = serfAt(registry, 20, 20);
        player.setActAs(other.id());
        assertTrue(player.identity().isDisguised());

        player.setActAs(player.id());

        assertFalse(player.identity().isDisguised());
        assertEquals(player.identity().trueId(), player.identity().presentedId());
    }

    @Test
    void freshlySpawnedActorHasNoPendingPlayerMoveTarget() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 10, 10);
        assertEquals(Actor.NONE, actor.playerMoveTargetCell());
    }
}
