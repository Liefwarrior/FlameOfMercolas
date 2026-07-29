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

    /** Chebyshev reach of the played soul's cast (spot water beside the stand, z±1 legal). */
    static final int PLAYER_CAST_REACH = 2;

    @Override
    public void act(Actor self, ActorContext ctx) {
        // The tick's default legibility stamp lands FIRST, so a real pickpocket attempt
        // below overwrites it with its own outcome (PICKPOCKETED / CAUGHT_STEALING) and
        // that trail stamp survives the tick — while a no-attempt (empty or invalid
        // intent) leaves the ordinary PLAYER_CONTROLLED reading.
        self.setLastReasonCode(ReasonCode.PLAYER_CONTROLLED);
        // Talk intent (Sprint 3 quests — the existing talk verb made sim-visible) resolves
        // FIRST: the fact of talking enters the quest log this tick so the QuestEngine's
        // TALK triggers can match it after tickAll; the presentation greet stays observer-
        // side and sim-silent. Consumed unconditionally (the §5.2 stale-intent rule);
        // reach is validated here (same z, chebyshev <= PICKPOCKET_REACH — the talk verb
        // and the lift share one adjacency shape).
        int talkId = self.playerTalkTargetId();
        self.setPlayerTalkTarget(Actor.NONE);
        if (talkId != Actor.NONE && talkId >= 0 && talkId < ctx.registry().size()
                && talkId != self.id()) {
            Actor listener = ctx.registry().get(talkId);
            if (com.trojia.sim.world.PackedPos.z(listener.cell())
                    == com.trojia.sim.world.PackedPos.z(self.cell())
                    && ActorGeometry.chebyshev(self.cell(), listener.cell())
                            <= TheftMechanics.PICKPOCKET_REACH) {
                ctx.questLog().noteTalk(self.id(), talkId, ctx.tick());
                self.setLastReasonCode(ReasonCode.TALKED);
            }
        }
        // Pickpocket intent (Sprint 2's play-mode verb) resolves before movement — a lift
        // is a deliberate act, not a side effect of a movement key. Consumed
        // unconditionally (the §5.2 stale-intent rule); TheftMechanics validates
        // adjacency/eligibility itself.
        int markId = self.playerPickpocketTargetId();
        self.setPlayerPickpocketTarget(Actor.NONE);
        if (markId != Actor.NONE && markId >= 0 && markId < ctx.registry().size()) {
            TheftMechanics.pickpocket(self, ctx.registry().get(markId), ctx);
        }
        // Eat intent (Sprint 4 — the played actor can finally feed itself): resolves one
        // pass of SeekFoodPolicy's eat-in-reach chain (carried ration, buyable counter at
        // the player's OWN quote, larder, commons, the broke's scavenge) with all its
        // ordinary side effects and reason stamps (ATE_FOOD / BOUGHT_FOOD /
        // SCAVENGED_FOOD). Consumed unconditionally (the §5.2 stale-intent rule); a miss
        // stamps NO_MEAL_IN_REACH so the client can toast the refusal.
        boolean eatIntent = self.playerEatIntent();
        self.setPlayerEatIntent(false);
        if (eatIntent && !SeekFoodPolicy.tryEatInReach(self, ctx)) {
            self.setLastReasonCode(ReasonCode.NO_MEAL_IN_REACH);
        }
        // Fish intent (Sprint 6 — the played soul can work the water too): one cast against
        // a live spot within reach that this soul actually PERCEIVES (the same per-actor
        // sight gate the AI fishers obey — an unseen spot is uncastable). Resolves through
        // the shared cast attempt: FISHING XP on the attempt, the check.fishing draw, FISH
        // minted on success (CAUGHT_FISH / FISH_GOT_AWAY). Consumed unconditionally (the
        // §5.2 stale-intent rule); a miss stamps NO_SPOT_IN_REACH for the client's toast.
        boolean fishIntent = self.playerFishIntent();
        self.setPlayerFishIntent(false);
        if (fishIntent) {
            int slot = ctx.fishingSpots().slotNear(self.cell(), PLAYER_CAST_REACH);
            if (slot != Actor.NONE && ctx.fishingSpots().visibleTo(slot, ctx.worldSeed(),
                    self.id(), ctx.skillTracks())) {
                com.trojia.sim.actor.job.JobBehaviors.resolveCastAttempt(self, ctx, slot, true);
            } else {
                self.setLastReasonCode(ReasonCode.NO_SPOT_IN_REACH);
            }
        }
        // Cull intent (S8 — the played soul can work the vermin bounty too): one attempt
        // against a DOWNED scalpable body within knife reach, through the same shared verb the
        // AI's work-event seam uses (FIELDCRAFT XP on the attempt, the check.cull draw, the
        // named scalp minted on success, the per-culler latch stamped either way). The body's
        // revive timer is never touched. Consumed unconditionally (the §5.2 stale-intent
        // rule); a miss stamps NO_QUARRY_IN_REACH so the client can toast the refusal, and a
        // still-latched hand reads the same way — you had your turn at that carcass.
        boolean cullIntent = self.playerCullIntent();
        self.setPlayerCullIntent(false);
        if (cullIntent) {
            int body = ctx.tick() < self.culledUntilTick() || !CullVerb.canCull(self)
                    ? Actor.NONE : CullVerb.quarryInReach(self, ctx);
            if (body != Actor.NONE) {
                CullVerb.resolveCull(self, ctx, body);
            } else {
                self.setLastReasonCode(ReasonCode.NO_QUARRY_IN_REACH);
            }
        }
        // Cast intent (Simple Magic — the spell bar down the right of the screen): ONE crafting
        // worked this tick, through the same shared verb any future AI caster will use
        // (LINKCRAFT XP on the attempt, the check.linkcraft draw against the difficulty
        // SpellCost computes, the components landed on success, the per-caster latch stamped
        // either way). The client sends the spell raw and, optionally, the body it clicked; a
        // NONE target lets the sim pick the spell's own lowest-id target in reach, so the
        // keyboard path and the click path resolve identically. Consumed unconditionally (the
        // §5.2 stale-intent rule); anything unworkable stamps NO_LINK_TO_TARGET for the toast.
        int spellRaw = self.playerSpellRaw();
        int spellTarget = self.playerSpellTargetId();
        self.setPlayerSpellIntent(Actor.NONE, Actor.NONE);
        if (spellRaw != Actor.NONE) {
            resolveCastIntent(self, ctx, spellRaw, spellTarget);
        }
        // Sell intent (S8 — the counter half of the playable loop): one pass of the shared
        // counter sale, item for coin, through the same BankVerbs exchange the AI fishers use.
        // Consumed unconditionally (the §5.2 stale-intent rule); the verb stamps SOLD_GOODS or
        // NO_BUYER_IN_REACH itself.
        boolean sellIntent = self.playerSellIntent();
        self.setPlayerSellIntent(false);
        if (sellIntent) {
            SellVerb.sellMaterialsInReach(self, ctx);
        }
        int target = self.playerMoveTargetCell();
        if (target != Actor.NONE) {
            int before = self.cell();
            if (com.trojia.sim.world.PackedPos.z(target)
                    != com.trojia.sim.world.PackedPos.z(before)) {
                // Sprint 4 (the climb): a cross-z move intent is the player taking a baked
                // stair/ramp connector — committed only across a ZLinkTable pair (an
                // arbitrary cross-z cell is a deterministic no-op), same speed gate,
                // occupancy cap and shove escape as any step.
                self.tryStepVertical(target, true, ctx::isWalkable, ctx.occupancy(),
                        ctx.zLinks());
            } else {
                self.stepToward(target, true, ctx::isWalkable, ctx.occupancy());
            }
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

    /**
     * Validates and resolves one crafting intent (Simple Magic). Every refusal the sim can see
     * — an unknown spell, an unread one, a latched hand, a body out of the link's reach — lands
     * as the single {@code NO_LINK_TO_TARGET} stamp; the client refuses the ones it can see
     * BEFORE arming, with the reason that actually applies ({@code SpellAvailability}, the
     * {@code CullAvailability} lesson).
     */
    private static void resolveCastIntent(Actor self, ActorContext ctx, int spellRaw,
            int requestedTarget) {
        var spells = ctx.spells();
        if (!com.trojia.sim.actor.spell.SpellVerb.canCast(self, ctx.skillTracks(), spells,
                spellRaw) || ctx.tick() < self.castUntilTick()) {
            self.setLastReasonCode(ReasonCode.NO_LINK_TO_TARGET);
            return;
        }
        var spell = spells.get(spellRaw);
        int target = requestedTarget == Actor.NONE
                ? com.trojia.sim.actor.spell.SpellVerb.targetInReach(self, ctx.registry(), spell)
                : requestedTarget;
        if (target == Actor.NONE || target < 0 || target >= ctx.registry().size()
                || !reachable(self, ctx, spell, target)) {
            self.setLastReasonCode(ReasonCode.NO_LINK_TO_TARGET);
            return;
        }
        com.trojia.sim.actor.spell.SpellVerb.resolveCast(self, ctx, spellRaw, target);
    }

    /** Whether {@code target} sits inside this crafting's own reach — the canon bridge rule. */
    private static boolean reachable(Actor self, ActorContext ctx,
            com.trojia.sim.actor.spell.SpellDefinition spell, int target) {
        if (spell.targetShape() == com.trojia.sim.actor.spell.TargetShape.SELF) {
            return target == self.id();
        }
        Actor body = ctx.registry().get(target);
        return !body.isDead()
                && com.trojia.sim.world.PackedPos.z(body.cell())
                        == com.trojia.sim.world.PackedPos.z(self.cell())
                && ActorGeometry.chebyshev(self.cell(), body.cell()) <= spell.reach();
    }

    /** The §3.3 satiation context for a roof cell: its {@code (x>>4, y>>4, z)} region id. */
    static long roofRegionKey(int cell) {
        long rx = com.trojia.sim.world.PackedPos.x(cell) >> ROOF_REGION_SHIFT;
        long ry = com.trojia.sim.world.PackedPos.y(cell) >> ROOF_REGION_SHIFT;
        long rz = com.trojia.sim.world.PackedPos.z(cell);
        return (rz << 40) | (ry << 20) | rx;
    }
}
