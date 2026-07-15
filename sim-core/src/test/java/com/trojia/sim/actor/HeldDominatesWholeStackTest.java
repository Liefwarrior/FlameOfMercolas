package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Whole-stack proof (alongside {@link NeedBandOutranksJobBandTest} and
 * {@link CrossBandNeedsHierarchyTest}'s cross-band checks) that {@code HELD}/{@code EXECUTED}
 * dominate every other policy in {@code Wastrel.STACK} even under the single most adversarial
 * need state available (every need pinned to its most critical end) — a real
 * {@link Actor#tick} call, not just an isolated {@code score()} comparison.
 */
final class HeldDominatesWholeStackTest {

    private static final int Z = 11;

    private static Actor extremeNeedWastrel(ActorRegistry registry, ActorContext ctx) {
        ActorTypeStats stats = ActorTestFixtures.stats(Wastrel.TYPE);
        Actor actor = registry.spawn(Wastrel.TYPE, stats, PackedPos.pack(10, 10, Z));
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, Z));
        actor.setHomeId(homeId);
        for (Need need : Need.values()) {
            actor.applyNeedDelta(need, -actor.need(need)); // drive every need to 0 (worst case)
        }
        return actor;
    }

    @Test
    void heldWinsSelectionOverEveryNeedAndJobBandPolicy() {
        ActorRegistry registry = new ActorRegistry();
        NoOpActorContext ctx = new NoOpActorContext(registry);
        Actor actor = extremeNeedWastrel(registry, ctx);
        actor.setJobOrdinal((short) 0); // some bound job, irrelevant which
        actor.setStatus(StatusBit.HELD, true);
        actor.setHeldUntilTick(50_000L);
        ctx.setTick(1_000L);

        actor.tick(ctx);

        assertEquals(ReasonCode.HELD_IN_CUSTODY, actor.lastReasonCode(),
                "HELD must win the whole-stack decision even with every need at its worst");
    }

    @Test
    void executedWinsSelectionOverHeldItself() {
        ActorRegistry registry = new ActorRegistry();
        NoOpActorContext ctx = new NoOpActorContext(registry);
        Actor actor = extremeNeedWastrel(registry, ctx);
        actor.setStatus(StatusBit.HELD, true); // simulate a stale HELD bit alongside EXECUTED
        actor.setStatus(StatusBit.EXECUTED, true);
        actor.setStatus(StatusBit.DOWNED, true);
        int cellBefore = actor.cell();
        ctx.setTick(1_000L);

        actor.tick(ctx);

        assertEquals(cellBefore, actor.cell(), "EXECUTED must win selection and never move the actor");
    }
}
