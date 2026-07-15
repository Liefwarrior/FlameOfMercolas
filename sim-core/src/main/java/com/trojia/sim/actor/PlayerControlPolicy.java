package com.trojia.sim.actor;

/**
 * Play mode's direct-control override (PLAY-MODE-SPEC.md §5.2): while
 * {@link StatusBit#PLAYER_CONTROLLED} is set on an actor, the observer's {@code PlayModeInput}
 * steers that actor directly (via {@link Actor#setPlayerMoveTarget(int)}) instead of its own
 * needs/job AI. Scores above every ordinary AI band (RETURN_HOME/SEEK_FOOD's observed ~1305
 * ceiling) so a played actor's own AI never fights the player, but deliberately BELOW
 * {@link HeldPolicy}'s 5000 and {@link ExecutedPolicy}'s 6000 sentinels: a played actor who
 * gets arrested still gets held (holding a key does not spring you from custody), and a played
 * Skyrunner hanged on a second offense stays permanently inert like anyone else. Player input
 * augments an actor's agency — it does not grant immunity from the justice system.
 */
public final class PlayerControlPolicy implements BehaviorPolicy {

    /** Above every ordinary AI band; below HELD (5000) / EXECUTED (6000) — see class doc. */
    private static final int PLAYER_CONTROL_SCORE = 2000;

    @Override
    public PolicyId id() {
        return PolicyId.PLAYER_CONTROL;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        return self.hasStatus(StatusBit.PLAYER_CONTROLLED) ? PLAYER_CONTROL_SCORE : 0;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        int target = self.playerMoveTargetCell();
        if (target != Actor.NONE) {
            self.stepToward(target, true, ctx::isWalkable, ctx.occupancy());
            // Consume the intent so a stale target never re-fires after the driver pauses
            // (the observer re-arms it every frame a movement key is held, §5.2).
            self.setPlayerMoveTarget(Actor.NONE);
        }
        self.setLastReasonCode(ReasonCode.PLAYER_CONTROLLED);
    }
}
