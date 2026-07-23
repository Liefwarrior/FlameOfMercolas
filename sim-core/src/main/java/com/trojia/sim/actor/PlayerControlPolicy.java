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

    /**
     * Skyrunning base award per committed ROOFTOP step (Sprint 1 progression wiring):
     * priced at PROGRESSION-SPEC §3.1's smallest Skyrunning row (the 25-cp "proximity
     * check" scale) — "rooftop runs" are the skill's own raws-listed covers. This is the
     * lead-ruled exception to §3.2's no-locomotion-XP rule, deliberately confined to the
     * PLAYED actor on baked {@link RooftopTable} cells and satiation-keyed per coarse roof
     * region ({@link #ROOF_REGION_SHIFT}), so circling one roofline decays to the 25%
     * floor (§3.3) while genuinely new rooftop territory pays full rate — a discoverable,
     * priced seam, exactly the north star's kind of exploitable.
     */
    static final int SKYRUNNING_ROOF_STEP_CP = 25;

    /** Roof satiation regions are {@code 16x16} tiles per z ({@code x>>4, y>>4, z}). */
    static final int ROOF_REGION_SHIFT = 4;

    @Override
    public void act(Actor self, ActorContext ctx) {
        // The tick's default legibility stamp lands FIRST, so a real pickpocket attempt
        // below overwrites it with its own outcome (PICKPOCKETED / CAUGHT_STEALING) and
        // that trail stamp survives the tick — while a no-attempt (empty or invalid
        // intent) leaves the ordinary PLAYER_CONTROLLED reading.
        self.setLastReasonCode(ReasonCode.PLAYER_CONTROLLED);
        // Pickpocket intent (Sprint 2's play-mode verb) resolves before movement — a lift
        // is a deliberate act, not a side effect of a movement key. Consumed
        // unconditionally (the §5.2 stale-intent rule); TheftMechanics validates
        // adjacency/eligibility itself.
        int markId = self.playerPickpocketTargetId();
        self.setPlayerPickpocketTarget(Actor.NONE);
        if (markId != Actor.NONE && markId >= 0 && markId < ctx.registry().size()) {
            TheftMechanics.pickpocket(self, ctx.registry().get(markId), ctx);
        }
        int target = self.playerMoveTargetCell();
        if (target != Actor.NONE) {
            int before = self.cell();
            self.stepToward(target, true, ctx::isWalkable, ctx.occupancy());
            // Consume the intent so a stale target never re-fires after the driver pauses
            // (the observer re-arms it every frame a movement key is held, §5.2).
            self.setPlayerMoveTarget(Actor.NONE);
            if (self.cell() != before && ctx.rooftops().contains(self.cell())) {
                // A committed step onto an authored roof plane: skyrunning use-XP for the
                // played actor (class constant doc). Context = the coarse roof region.
                ctx.skillTracks().award(self.id(), ctx.skillTracks().skyrunningRaw(),
                        SKYRUNNING_ROOF_STEP_CP, roofRegionKey(self.cell()), ctx.tick());
            }
        }
    }

    /** The §3.3 satiation context for a roof cell: its {@code (x>>4, y>>4, z)} region id. */
    static long roofRegionKey(int cell) {
        long rx = com.trojia.sim.world.PackedPos.x(cell) >> ROOF_REGION_SHIFT;
        long ry = com.trojia.sim.world.PackedPos.y(cell) >> ROOF_REGION_SHIFT;
        long rz = com.trojia.sim.world.PackedPos.z(cell);
        return (rz << 40) | (ry << 20) | rx;
    }
}
