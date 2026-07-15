package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@code HELD} (ARREST-SPEC addendum): the in-custody EMERGENCY-band override. Verifies the
 * score sentinel (far above every observed NEED-band ceiling, so raws tuning elsewhere can
 * never accidentally outrank an arrest), the leash-ignoring escort to
 * {@link ActorContext#arrestHoldCell()}, and the release-and-reset transition once the drawn
 * sentence elapses — mirroring {@link GoalPursuePolicy}'s own {@code renew()} reset shape.
 */
final class HeldPolicyTest {

    private static final int Z = 11;

    private static Actor wastrelAt(ActorRegistry registry, int x, int y) {
        ActorTypeStats stats = ActorTestFixtures.stats(Wastrel.TYPE);
        return registry.spawn(Wastrel.TYPE, stats, PackedPos.pack(x, y, Z));
    }

    private static final class HoldCellContext extends NoOpActorContext {
        private final int holdCell;

        HoldCellContext(ActorRegistry registry, int holdCell) {
            super(registry);
            this.holdCell = holdCell;
        }

        @Override
        public int arrestHoldCell() {
            return holdCell;
        }
    }

    @Test
    void scoreIsZeroWhenNotHeld() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = wastrelAt(registry, 10, 10);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        assertEquals(0, Policies.HELD.score(actor, ctx));
    }

    @Test
    void scoreIsAFarAboveEveryObservedBandSentinelWhenHeld() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = wastrelAt(registry, 10, 10);
        actor.setStatus(StatusBit.HELD, true);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        // RETURN_HOME/SEEK_FOOD's observed ceiling is ~1305 (305 + up to a 1000 crit bonus,
        // ACTORS-SPEC.md's needs-hierarchy pass); this sentinel must clear it by a wide margin.
        assertTrue(Policies.HELD.score(actor, ctx) > 2000);
    }

    @Test
    void actWalksTowardTheHoldCellWhileTheSentenceIsStillPending() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = wastrelAt(registry, 10, 10);
        actor.setStatus(StatusBit.HELD, true);
        actor.setHeldUntilTick(5_000L);
        int holdCell = PackedPos.pack(20, 10, Z);
        HoldCellContext ctx = new HoldCellContext(registry, holdCell);
        ctx.setTick(100L);

        Policies.HELD.act(actor, ctx);

        assertEquals(PackedPos.pack(11, 10, Z), actor.cell(), "one leash-ignoring step toward the cell");
        assertTrue(actor.hasStatus(StatusBit.HELD), "still serving the sentence");
        assertEquals(ReasonCode.HELD_IN_CUSTODY, actor.lastReasonCode());
    }

    @Test
    void actStaysInPlaceWhenNoHoldCellIsWired() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = wastrelAt(registry, 10, 10);
        actor.setStatus(StatusBit.HELD, true);
        actor.setHeldUntilTick(5_000L);
        NoOpActorContext ctx = new NoOpActorContext(registry); // arrestHoldCell() == Actor.NONE
        ctx.setTick(100L);

        Policies.HELD.act(actor, ctx);

        assertEquals(PackedPos.pack(10, 10, Z), actor.cell(), "no fixture wired -> hold in place");
    }

    @Test
    void actReleasesAndFullyResetsGoalStateOnceTheSentenceElapses() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = wastrelAt(registry, 10, 10);
        actor.setStatus(StatusBit.HELD, true);
        actor.setHeldUntilTick(5_000L);
        actor.setGoalState(GoalState.PURSUING);
        actor.setGoalTarget(TargetKind.CELL, PackedPos.pack(99, 99, Z));
        actor.setGoalWorkTicks(7);
        actor.setGoalCooldown(123);
        HoldCellContext ctx = new HoldCellContext(registry, PackedPos.pack(20, 10, Z));
        ctx.setTick(5_000L); // sentence has fully elapsed

        Policies.HELD.act(actor, ctx);

        assertFalse(actor.hasStatus(StatusBit.HELD));
        assertEquals(GoalState.SELECTING, actor.goalState());
        assertEquals(TargetKind.NONE, actor.goalTargetKind());
        assertEquals(Actor.NONE, actor.goalTargetKey());
        assertEquals(0, actor.goalWorkTicks());
        assertEquals(0, actor.goalCooldown());
        assertEquals(ReasonCode.RELEASED_FROM_CUSTODY, actor.lastReasonCode());
        assertEquals(PackedPos.pack(10, 10, Z), actor.cell(), "release itself is not a movement step");
    }
}
