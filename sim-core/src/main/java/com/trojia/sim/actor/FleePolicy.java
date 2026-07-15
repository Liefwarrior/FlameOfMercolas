package com.trojia.sim.actor;

import com.trojia.sim.world.Dir;
import com.trojia.sim.world.PackedPos;

/**
 * {@code FLEE} (ACTORS-SPEC.md §1.3): EMERGENCY-band panic response. This
 * foundation milestone drives the trigger purely off the SAFETY need band
 * (§3.1's "event-driven... low SAFETY powers FLEE") since no fire/violence
 * stimulus system exists yet in sim-core — a documented scope trim. The
 * escape heading is a named {@code actor.fleeJitter} draw rather than an
 * away-vector from a concrete hazard cell (also a trim of the same gap);
 * both trims are removed by wiring a real danger-cell signal into
 * {@link ActorContext} once THERMAL/fire lands.
 */
public final class FleePolicy implements BehaviorPolicy {

    /** WEST/EAST/NORTH/SOUTH only — actors move on one z-level (top-down grid). */
    private static final Dir[] HEADINGS = {Dir.WEST, Dir.EAST, Dir.NORTH, Dir.SOUTH};

    @Override
    public PolicyId id() {
        return PolicyId.FLEE;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        if (NeedThresholds.isCritical(self.need(Need.SAFETY))) {
            return self.stats().fleeEmergencyPriority();
        }
        return 0;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        long draw = ctx.draw(ActorRngStream.ACTOR_FLEE_JITTER, self.id(), ctx.nextDrawIndex(self.id()));
        Dir heading = HEADINGS[(int) Long.remainderUnsigned(draw, HEADINGS.length)];
        int x = clamp(PackedPos.x(self.cell()) + heading.dx(), PackedPos.X_MASK);
        int y = clamp(PackedPos.y(self.cell()) + heading.dy(), PackedPos.Y_MASK);
        int fled = PackedPos.pack(x, y, PackedPos.z(self.cell()));
        self.stepToward(fled, true, ctx::isWalkable, ctx.occupancy()); // FLEE ignores the leash (§1.3, §2.5)
        self.setLastReasonCode(ReasonCode.SAFETY_CRITICAL);
    }

    private static int clamp(int coordinate, int max) {
        if (coordinate < 0) {
            return 0;
        }
        return Math.min(coordinate, max);
    }
}
