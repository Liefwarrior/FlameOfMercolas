package com.trojia.sim.actor;

/**
 * {@code RETURN_HOME} (ACTORS-SPEC.md §11.1): the sleep-at-home NEED-band
 * policy. Scores when REST need urgency is at/below {@code LOW}, OR the
 * type's night rhythm window is open, AND the actor is not already at its
 * {@code homeCell}. At the home cell during the day it keeps scoring — the
 * oscillation-hysteresis fix, {@link NeedThresholds#RECOVERED}'s javadoc —
 * until REST is comfortably {@code RECOVERED}, not merely the instant it
 * stops being {@code LOW}; at night it is 0 at home regardless (§1.2's
 * "0 = not applicable" convention), since {@code pursueAtAnchor}'s own
 * off-shift target already keeps the actor there through the night.
 *
 * <p>Score is {@code priority + urgencyBonus (+ rhythmBonus if night)} — the
 * urgency-bonus term (§3.3) was added by the needs-hierarchy pass alongside
 * {@link SeekFoodPolicy}; raws validation exhaustively checks every one of
 * the 4 (HUNGER band x REST band) combinations for every type — not just the
 * same-band pairs — so HUNGER's SEEK_FOOD score is always {@code >=} this
 * policy's excluding the night-rhythm term (a deliberate exclusion — see the
 * design notes on why the rhythm term isn't part of that guarantee).
 */
public final class ReturnHomePolicy implements BehaviorPolicy {

    @Override
    public PolicyId id() {
        return PolicyId.RETURN_HOME;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        if (self.homeId() == Actor.NONE) {
            return 0; // not yet baked (defensive — every actor has a home post-bake, §11.4 step 5)
        }
        Home home = ctx.homes().get(self.homeId());
        int rest = self.need(Need.REST);
        long tickOfDay = DailyRhythm.tickOfDay(ctx.tick());
        ActorTypeStats stats = self.stats();
        boolean night = tickOfDay >= stats.nightWindowStart() && tickOfDay < stats.nightWindowEnd();
        if (self.cell() == home.homeCell()) {
            if (night) {
                return 0; // pursueAtAnchor's own off-shift target already keeps the actor
                          // home through the night regardless of this policy's score — not
                          // susceptible to the oscillation this gate exists to prevent
            }
            // Gated on RECOVERED, not merely "not LOW" (the oscillation fix,
            // NeedThresholds.RECOVERED's javadoc — the SEEK_FOOD counterpart to this):
            // without it, one recovery tick nudging REST just above LOW drops this score
            // to 0 the same tick GOAL_PURSUE re-wins, walking the actor straight back out
            // and repeating forever.
            if (NeedThresholds.isRecovered(rest)) {
                return 0;
            }
            NeedConfig cfg = stats.need(Need.REST);
            int urgencyBonus = NeedThresholds.isCritical(rest) ? cfg.critBonus() : cfg.lowBonus();
            return stats.returnHomePriority() + urgencyBonus;
        }
        boolean restLow = NeedThresholds.isLow(rest);
        if (!restLow && !night) {
            return 0;
        }
        NeedConfig cfg = stats.need(Need.REST);
        int urgencyBonus = restLow ? (NeedThresholds.isCritical(rest) ? cfg.critBonus() : cfg.lowBonus()) : 0;
        return stats.returnHomePriority() + urgencyBonus + (night ? stats.returnHomeRhythmBonus() : 0);
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        Home home = ctx.homes().get(self.homeId());
        // §2.5's leash list (FLEE/APPREHEND/RECAPTURE) predates the Home addendum;
        // RETURN_HOME must ignore it too whenever home != anchor (a Serf's tenement
        // is routinely outside the work anchor's leash) or the walk home could never
        // complete — a resolved-here consistency gap, not a spec contradiction.
        self.stepAlongRoute(home.homeCell(), true, ctx::isWalkable);
        // !isRecovered rather than isLow for legibility consistency with the score()
        // hysteresis gate above (cosmetic — doesn't change which reason fires, since
        // score() already guarantees this act() only runs while the REST branch applies).
        boolean restNotRecovered = !NeedThresholds.isRecovered(self.need(Need.REST));
        self.setLastReasonCode(restNotRecovered ? ReasonCode.NEED_REST_LOW : ReasonCode.RHYTHM_NIGHT_HOME);
    }
}
