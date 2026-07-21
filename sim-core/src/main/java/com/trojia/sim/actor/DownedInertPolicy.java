package com.trojia.sim.actor;

/**
 * {@code DOWNED_INERT} (living-docks beast pass): caught prey holds still. A mouse a predator
 * caught is {@link StatusBit#DOWNED} with a live {@code downedTimer}; while down it must not
 * wander, flee, or sleep-walk home — it lies where it was caught until {@code Actor#auditStatus}
 * counts the timer to zero and clears DOWNED (the existing revive machinery — no new scalar,
 * no removal: {@code ActorRegistry} never removes an actor). On revival the ordinary stack
 * resumes; the den is the wander anchor, so the relative-improvement leash rule walks a mouse
 * dropped outside its leash back home one cell at a time — an ordinary walk, never a teleport.
 *
 * <p>Score 5500 sits in the documented custody ladder — above {@code HELD} (5000), below
 * {@code EXECUTED} (6000) — so nothing else in a prey stack can outrank being down, and a
 * hypothetical executed actor stays permanently inert. {@code act()} is a no-op (the
 * {@link ExecutedPolicy} shape). In the Mouse stack only — nothing else goes DOWNED-with-timer
 * today, so the blast radius is zero.
 */
public final class DownedInertPolicy implements BehaviorPolicy {

    /** Above HELD (5000), below EXECUTED (6000) — the documented override ladder. */
    private static final int DOWNED_SCORE = 5500;

    @Override
    public PolicyId id() {
        return PolicyId.DOWNED_INERT;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        return self.hasStatus(StatusBit.DOWNED) && !self.hasStatus(StatusBit.EXECUTED)
                ? DOWNED_SCORE : 0;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        // Inert until auditStatus' downedTimer countdown clears DOWNED — then the stack resumes.
    }
}
