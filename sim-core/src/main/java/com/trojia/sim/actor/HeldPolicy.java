package com.trojia.sim.actor;

/**
 * {@code HELD} (ARREST-SPEC addendum, DECISIONS.md hanging-supersedes-arrest ruling): the
 * in-custody EMERGENCY-band override for an ordinary (non-Skyrunner) Villain caught by the
 * Watch. Scores a fixed sentinel — deliberately far above every other observed score band
 * (RETURN_HOME's ~1305 ceiling included) so raws tuning elsewhere can never accidentally
 * outrank an arrest — whenever {@link StatusBit#HELD} is set, else {@code 0}. {@code act()}
 * walks the leash-ignoring escort to {@link ActorContext#arrestHoldCell()} until the drawn
 * sentence (§{@code JobBehaviors.checkArrestExposure}) elapses, then releases: clears
 * {@code HELD} and resets the goal/target state exactly the way {@link GoalPursuePolicy}'s
 * own {@code renew()} does, so the actor's job re-derives a fresh, validated target next
 * tick instead of resuming whatever it was mid-doing at arrest.
 */
public final class HeldPolicy implements BehaviorPolicy {

    /** Above every other policy's observed score (RETURN_HOME's ~1305 ceiling) by construction. */
    private static final int HELD_SCORE = 5000;

    @Override
    public PolicyId id() {
        return PolicyId.HELD;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        return self.hasStatus(StatusBit.HELD) ? HELD_SCORE : 0;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        if (ctx.tick() >= self.heldUntilTick()) {
            release(self);
            return;
        }
        // Escort to the prison cell assigned at arrest (Phase-2 STEP C, Pass 10); fall back to the
        // single well-known K34 cell when no multi-cell registry is wired (or it was full), and to
        // "hold in place" when neither is set (the world-less/unwired degradation the scalar had).
        int holdCell = self.assignedHoldCell() != Actor.NONE
                ? self.assignedHoldCell()
                : ctx.arrestHoldCell();
        if (holdCell != Actor.NONE) {
            self.stepAlongRoute(holdCell, true, ctx::isWalkable, ctx.occupancy());
        }
        self.setLastReasonCode(ReasonCode.HELD_IN_CUSTODY);
    }

    private void release(Actor self) {
        self.setStatus(StatusBit.HELD, false);
        self.setAssignedHoldCell(Actor.NONE); // free the prison-cell slot for the next arrest
        self.setGoalState(GoalState.SELECTING);
        self.setGoalTarget(TargetKind.NONE, Actor.NONE);
        self.setGoalWorkTicks(0);
        self.setGoalCooldown(0);
        self.setLastReasonCode(ReasonCode.RELEASED_FROM_CUSTODY);
    }
}
