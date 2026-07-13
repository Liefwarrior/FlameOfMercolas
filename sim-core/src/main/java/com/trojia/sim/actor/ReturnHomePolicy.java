package com.trojia.sim.actor;

/**
 * {@code RETURN_HOME} (ACTORS-SPEC.md §11.1): the new sleep-at-home NEED-band
 * policy. Scores when REST need urgency is at/below {@code LOW}, OR the
 * type's night rhythm window is open, AND the actor is not already at its
 * {@code homeCell} — mutual exclusivity with any future {@code REST} policy
 * is by construction (score 0 the instant {@code cell == homeCell}, the
 * §1.2 "0 = not applicable" convention), not by new interruption logic.
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
        if (self.cell() == home.homeCell()) {
            return 0; // already home — not applicable (§11.1)
        }
        boolean restLow = NeedThresholds.isLow(self.need(Need.REST));
        long tickOfDay = DailyRhythm.tickOfDay(ctx.tick());
        ActorTypeStats stats = self.stats();
        boolean night = tickOfDay >= stats.nightWindowStart() && tickOfDay < stats.nightWindowEnd();
        if (!restLow && !night) {
            return 0;
        }
        return stats.returnHomePriority() + (night ? stats.returnHomeRhythmBonus() : 0);
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        Home home = ctx.homes().get(self.homeId());
        // §2.5's leash list (FLEE/APPREHEND/RECAPTURE) predates the Home addendum;
        // RETURN_HOME must ignore it too whenever home != anchor (a Serf's tenement
        // is routinely outside the work anchor's leash) or the walk home could never
        // complete — a resolved-here consistency gap, not a spec contradiction.
        self.stepToward(home.homeCell(), true);
        boolean restLow = NeedThresholds.isLow(self.need(Need.REST));
        self.setLastReasonCode(restLow ? ReasonCode.NEED_REST_LOW : ReasonCode.RHYTHM_NIGHT_HOME);
    }
}
