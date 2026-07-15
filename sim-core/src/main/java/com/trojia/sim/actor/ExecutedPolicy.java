package com.trojia.sim.actor;

/**
 * {@code EXECUTED} (ARREST-SPEC addendum): the permanent EMERGENCY-band override for a
 * Skyrunner hanged on its 2nd offense (Eli's 2026-07-14 directive superseding
 * ACTORS-SPEC.md §2.7's "the Watch arrests, never executes" for Skyrunners specifically —
 * see the {@code DECISIONS.md} addendum). Scores even higher than {@link HeldPolicy} so an
 * executed actor can never re-enter custody/job selection; {@code act()} is a permanent
 * no-op — {@link ActorRegistry} has no removal path (its own class doc: "never removes an
 * actor"), so the actor stays resident, forever inert, at whatever cell it was executed on
 * (deliberately no travel to the gibbet — "don't invent combat mechanics, just represent the
 * fact of it"). {@link StatusBit#DOWNED} is set alongside {@link StatusBit#EXECUTED} at the
 * transition (never via {@code downedTimer}, so {@code Actor#auditStatus}'s decrement guard
 * never fires and it never clears), so any future rendering keyed off {@code DOWNED} (none
 * exists yet — client-observer has no status-bit-driven render accents today) will
 * automatically treat an executed actor consistently with a downed one. {@code EXECUTED}
 * stays the authoritative, distinct "permanently dead" bit so nothing reading
 * {@code hasStatus(DOWNED)} expecting eventual recovery is misled.
 */
public final class ExecutedPolicy implements BehaviorPolicy {

    /** Above {@link HeldPolicy}'s sentinel — an executed actor can never re-enter custody. */
    private static final int EXECUTED_SCORE = 6000;

    @Override
    public PolicyId id() {
        return PolicyId.EXECUTED;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        return self.hasStatus(StatusBit.EXECUTED) ? EXECUTED_SCORE : 0;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        // Permanently inert: no movement, no re-selection ever wins over this again.
    }
}
