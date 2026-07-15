package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@code EXECUTED} (ARREST-SPEC addendum): the permanent, no-removal "hanged" terminal state
 * for a Skyrunner's 2nd offense. {@link ActorRegistry} has no removal path, so the contract is
 * a permanently inert, dominant policy — never movement, never re-selection loss to anything
 * else — rather than actual removal from the sim.
 */
final class ExecutedPolicyTest {

    private static final int Z = 11;

    private static Actor wastrelAt(ActorRegistry registry, int x, int y) {
        ActorTypeStats stats = ActorTestFixtures.stats(Wastrel.TYPE);
        return registry.spawn(Wastrel.TYPE, stats, PackedPos.pack(x, y, Z));
    }

    @Test
    void scoreIsZeroWhenNotExecuted() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = wastrelAt(registry, 10, 10);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        assertEquals(0, Policies.EXECUTED.score(actor, ctx));
    }

    @Test
    void scoreOutranksHeldSoAnExecutedSkyrunnerCanNeverReenterCustody() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = wastrelAt(registry, 10, 10);
        actor.setStatus(StatusBit.EXECUTED, true);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        // Force both bits on (the escalation path always sets both, but this isolates the
        // score comparison itself): EXECUTED must win selection over HELD by construction.
        actor.setStatus(StatusBit.HELD, true);
        int executedScore = Policies.EXECUTED.score(actor, ctx);
        int heldScore = Policies.HELD.score(actor, ctx);

        assertTrue(executedScore > heldScore,
                "EXECUTED must dominate HELD so a hanged actor can never be re-selected into custody");
    }

    @Test
    void actIsAPermanentNoOp() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = wastrelAt(registry, 10, 10);
        actor.setStatus(StatusBit.EXECUTED, true);
        actor.setStatus(StatusBit.DOWNED, true);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ReasonCode before = actor.lastReasonCode();

        Policies.EXECUTED.act(actor, ctx);

        assertEquals(PackedPos.pack(10, 10, Z), actor.cell(), "no movement, ever");
        assertEquals(before, actor.lastReasonCode(), "act() does not even touch the reason code");
        assertTrue(actor.hasStatus(StatusBit.DOWNED), "DOWNED persists forever (no downedTimer set)");
        assertTrue(actor.hasStatus(StatusBit.EXECUTED));
    }
}
