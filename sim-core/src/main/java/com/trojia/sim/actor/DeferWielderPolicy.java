package com.trojia.sim.actor;

/**
 * {@code DEFER_WIELDER} (ACTORS-SPEC.md §1.3): EMERGENCY-band posture when the
 * presented Wielder is in sight — holds position and emits a presentation-only
 * bark. One PolicyId serves every posture variant (the raws {@code posture}
 * param distinguishes them, §1.3); this foundation ships the single
 * UNCONDITIONAL_COMPLIANCE-shaped behavior (hold + bark) common to all of
 * them — richer per-posture branching (approach/kneel/petition) is a later
 * extension that does not change this class's shape.
 *
 * <p>Types with no deference row (Animal, Feral — INDIFFERENT, §4.8/§4.9)
 * simply omit this policy from their stack; {@link ActorTypeStats#hasDeferWielder()}
 * is the raws-declared belt-and-suspenders guard for types that do include it
 * with a zero radius by mistake.
 */
public final class DeferWielderPolicy implements BehaviorPolicy {

    @Override
    public PolicyId id() {
        return PolicyId.DEFER_WIELDER;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        ActorTypeStats stats = self.stats();
        if (!stats.hasDeferWielder()) {
            return 0;
        }
        if (ctx.wielderId() == self.id()) {
            return 0; // the Wielder does not defer to themselves
        }
        int wielderCell = ctx.wielderCell();
        if (wielderCell == Actor.NONE) {
            return 0; // no Wielder currently spawned/presented
        }
        if (ActorGeometry.chebyshev(self.cell(), wielderCell) > stats.deferWielderRadius()) {
            return 0;
        }
        return stats.deferWielderPriority();
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        // Presentation-only bark (§2.2): the draw is consumed, never read by any state.
        ctx.draw(ActorRngStream.ACTOR_BARK, self.id(), ctx.nextDrawIndex(self.id()));
        self.setLastReasonCode(ReasonCode.DEFERENCE);
    }
}
