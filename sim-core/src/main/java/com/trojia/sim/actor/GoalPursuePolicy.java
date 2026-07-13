package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobParams;
import com.trojia.sim.actor.job.RenewMode;

/**
 * {@code GOAL_PURSUE} (ACTORS-SPEC.md §10.1): JOB-band delegate to the bound
 * {@link Job}'s SELECT/PURSUE/COMPLETE/RENEW lifecycle. Score is draw-free
 * (the job's raws priority + rhythm bonus); draws happen only inside the
 * job's own {@code selectTarget}/{@code pursue}.
 */
public final class GoalPursuePolicy implements BehaviorPolicy {

    @Override
    public PolicyId id() {
        return PolicyId.GOAL_PURSUE;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        if (self.jobOrdinal() < 0) {
            return 0; // no job bound yet (should not happen post-bake, defensive)
        }
        if (self.goalState() == GoalState.COOLDOWN) {
            return 0; // renewing; the always-applicable IDLE fallback wins meanwhile
        }
        Job job = ctx.jobs().get(self.jobOrdinal());
        JobParams params = job.params();
        boolean inWindow = params.inWindow(DailyRhythm.tickOfDay(ctx.tick()));
        return params.priority() + (inWindow ? params.rhythmBonus() : 0);
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        Job job = ctx.jobs().get(self.jobOrdinal());
        self.setLastReasonCode(ReasonCode.JOB_GOAL);
        switch (self.goalState()) {
            case SELECTING -> {
                job.selectTarget(self, ctx);
                self.setGoalState(GoalState.PURSUING);
            }
            case PURSUING -> {
                job.pursue(self, ctx);
                if (job.isComplete(self, ctx)) {
                    renew(self, job.params());
                }
            }
            case COOLDOWN -> {
                // unreachable in practice (score() is 0 in COOLDOWN); no-op defensively.
            }
        }
    }

    private void renew(Actor self, JobParams params) {
        self.setGoalProgress((short) 0);
        self.setGoalTarget(TargetKind.NONE, Actor.NONE);
        if (params.renewMode() == RenewMode.COOLDOWN) {
            self.setGoalCooldown(params.cooldownTicks());
            self.setGoalState(GoalState.COOLDOWN);
        } else {
            // IMMEDIATE and RHYTHM both land back in SELECTING in this foundation
            // milestone (§10.9 friction 1 — RHYTHM's window-gated re-select is a
            // later refinement; the job's own rhythmBonus already biases scoring).
            self.setGoalState(GoalState.SELECTING);
        }
    }
}
