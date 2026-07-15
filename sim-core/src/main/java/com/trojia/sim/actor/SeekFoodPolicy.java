package com.trojia.sim.actor;

/**
 * {@code SEEK_FOOD} (ACTORS-SPEC.md §3.3, the needs-hierarchy pass): the
 * HUNGER-urgency NEED-band policy — Eli's "food/water first" directive,
 * mapped onto the single HUNGER slot (this sim has no separate WATER need,
 * §3.1). Mirrors {@link ReturnHomePolicy} in shape: scores {@code priority +
 * urgencyBonus} once HUNGER crosses {@link NeedThresholds#LOW}, walks the
 * actor home to recover (no food-establishment routing exists yet — a
 * deliberate scope cut, see {@link Actor#recoverHungerAtHome}), and — the
 * oscillation-hysteresis fix, {@link NeedThresholds#RECOVERED}'s javadoc —
 * keeps winning at home until HUNGER is comfortably {@code RECOVERED}, not
 * merely the instant it stops being {@code LOW}, so a single at-home recovery
 * tick can never flip this score back to 0 and immediately release the actor
 * to {@code GOAL_PURSUE}.
 *
 * <p>Raws validation ({@code ActorRawsLoader}) guarantees, for every actor
 * type, {@code seekFoodPriority >= returnHomePriority} and — checked
 * exhaustively across all 4 (HUNGER band x REST band) combinations, not just
 * the same-band pairs — that this policy's score is provably {@code >=}
 * {@link ReturnHomePolicy}'s whenever both are active, at any mix of urgency
 * (excluding RETURN_HOME's night-rhythm term, a deliberate exclusion — see
 * the design notes). Stack position (this policy
 * declared immediately before {@code RETURN_HOME} in every stack) resolves
 * the residual exact-tie case via {@link PolicyStack#selectIndex}'s
 * documented earlier-position tie-break.
 */
public final class SeekFoodPolicy implements BehaviorPolicy {

    @Override
    public PolicyId id() {
        return PolicyId.SEEK_FOOD;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        if (self.homeId() == Actor.NONE) {
            return 0; // not yet baked (defensive — every actor has a home post-bake, §11.4 step 5)
        }
        Home home = ctx.homes().get(self.homeId());
        int hunger = self.need(Need.HUNGER);
        if (self.cell() == home.homeCell()) {
            // Gated on RECOVERED, not merely "not LOW" (the oscillation fix,
            // NeedThresholds.RECOVERED's javadoc): keep winning — and thus keep the actor
            // standing on the home cell where recoverHungerAtHome is unconditionally
            // climbing HUNGER — until comfortably recovered, then release back to
            // GOAL_PURSUE. Without this, one recovery tick nudging HUNGER just above LOW
            // drops this score to 0 the same tick GOAL_PURSUE re-wins, walking the actor
            // straight back out and repeating forever.
            if (NeedThresholds.isRecovered(hunger)) {
                return 0;
            }
            NeedConfig cfg = self.stats().need(Need.HUNGER);
            int urgencyBonus = NeedThresholds.isCritical(hunger) ? cfg.critBonus() : cfg.lowBonus();
            return self.stats().seekFoodPriority() + urgencyBonus;
        }
        if (!NeedThresholds.isLow(hunger)) {
            return 0;
        }
        NeedConfig cfg = self.stats().need(Need.HUNGER);
        int urgencyBonus = NeedThresholds.isCritical(hunger) ? cfg.critBonus() : cfg.lowBonus();
        return self.stats().seekFoodPriority() + urgencyBonus;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        Home home = ctx.homes().get(self.homeId());
        // Ignores the leash for the same reason RETURN_HOME does (§11.1): home is routinely
        // outside the work anchor's leash, and a hungry actor must still be able to walk there.
        self.stepAlongRoute(home.homeCell(), true, ctx::isWalkable, ctx.occupancy());
        self.setLastReasonCode(ReasonCode.NEED_HUNGER_LOW);
    }
}
