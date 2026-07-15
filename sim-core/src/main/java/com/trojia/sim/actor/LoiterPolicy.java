package com.trojia.sim.actor;

import com.trojia.sim.world.Dir;
import com.trojia.sim.world.PackedPos;

/**
 * {@code LOITER} (ACTORS-SPEC.md §1.3): the universal IDLE fallback — every
 * stack must end in one, score {@code >= 1} always, so policy selection never
 * returns "no winner" (§1.2). Dwells at/near the anchor with an occasional
 * deterministic shuffle.
 */
public final class LoiterPolicy implements BehaviorPolicy {

    private static final Dir[] HEADINGS = {Dir.WEST, Dir.EAST, Dir.NORTH, Dir.SOUTH};
    /** 1-in-16 shuffle chance per tick (placeholder, mirrors §6's shuffleChanceQ16 texture). */
    private static final int SHUFFLE_MODULUS = 16;

    @Override
    public PolicyId id() {
        return PolicyId.LOITER;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        return self.stats().loiterPriority();
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        long draw = ctx.draw(ActorRngStream.ACTOR_WANDER, self.id(), ctx.nextDrawIndex(self.id()));
        self.setLastReasonCode(ReasonCode.IDLE_DEFAULT);
        if (Long.remainderUnsigned(draw, SHUFFLE_MODULUS) != 0) {
            return; // no shuffle this tick
        }
        Dir heading = HEADINGS[(int) Long.remainderUnsigned(draw >>> 8, HEADINGS.length)];
        int x = clamp(PackedPos.x(self.cell()) + heading.dx(), PackedPos.X_MASK);
        int y = clamp(PackedPos.y(self.cell()) + heading.dy(), PackedPos.Y_MASK);
        self.stepToward(PackedPos.pack(x, y, PackedPos.z(self.cell())), false, ctx::isWalkable,
                ctx.occupancy());
    }

    private static int clamp(int coordinate, int max) {
        if (coordinate < 0) {
            return 0;
        }
        return Math.min(coordinate, max);
    }
}
