package com.trojia.sim.actor.job;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorContext;
import com.trojia.sim.actor.TargetKind;

/**
 * The shared generic goal mechanics every {@link Job} leaf delegates to
 * (ACTORS-SPEC.md §10.1's SELECT/PURSUE/COMPLETE steps): walk to the actor's
 * job anchor, "work" there for {@code workTicksPerUnit} ticks per progress
 * unit, complete at {@code unitsToComplete}. Leaves stay one-line thin
 * (mirroring the §1.4 thin-subclass convention applied to jobs, §10.2) by
 * calling these instead of each hand-rolling the same walk/tally loop; richer
 * per-goal-kind behavior (purse-lifting, route waylay, …) is a later
 * extension that replaces a leaf's call here without touching this class's
 * contract.
 */
public final class JobBehaviors {

    private JobBehaviors() {
    }

    /** SELECT: target the actor's own job anchor — draw-free (§10.1). */
    public static void selectAnchorTarget(Actor self, ActorContext ctx) {
        self.setGoalTarget(TargetKind.CELL, self.anchorCell());
        self.setGoalWorkTicks(0);
    }

    /** PURSUE: walk to the target; once there, accrue work ticks into progress units. */
    public static void pursueAtAnchor(Actor self, ActorContext ctx, JobParams params) {
        if (self.goalTargetKind() != TargetKind.CELL) {
            selectAnchorTarget(self, ctx);
            return;
        }
        int target = self.goalTargetKey();
        if (self.cell() != target) {
            self.stepToward(target);
            return;
        }
        int workTicks = self.goalWorkTicks() + 1;
        if (workTicks >= params.workTicksPerUnit()) {
            self.setGoalProgress((short) (self.goalProgress() + 1));
            self.setGoalWorkTicks(0);
        } else {
            self.setGoalWorkTicks(workTicks);
        }
    }

    /** COMPLETE check: pure, {@code goalProgress >= unitsToComplete}. */
    public static boolean isCompleteAtUnits(Actor self, JobParams params) {
        return self.goalProgress() >= params.unitsToComplete();
    }
}
