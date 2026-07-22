package com.trojia.sim.actor;

/**
 * {@code HOUSE_ARREST} (density revisit, Eli's shove-riot correction): the Watch caught this
 * actor shoving in a riot and sent it HOME for a full day — "forces them to go and sleep for
 * 24h". While {@link StatusBit#HOUSE_ARREST} is set this policy scores a fixed
 * {@value #HOUSE_ARREST_SCORE} — just under {@code HELD}'s 5000 (real custody supersedes house
 * arrest), above {@code PLAYER_CONTROL}'s 2000 and every NEED band — and {@code act()} walks
 * the offender to its home cell (leash-ignoring, the {@code RETURN_HOME} precedent) and holds
 * it there. Standing on the home cell is sleeping: {@code Actor.recoverRestAtHome} restores
 * REST every tick of the sentence.
 *
 * <p><b>House arrest FEEDS (the custody-starvation landmine the map pass flagged):</b> unlike
 * the K34 cells, home confinement is at the actor's own hearth — each held tick it eats from
 * its carried ration or its home-cell larder whenever HUNGER is not recovered (the
 * {@link SeekFoodPolicy} step-1/step-3 sources, sink-accounted identically), so a provisioned
 * citizen serves its day without joining the starvation margin. The periodic system-side
 * provisioning ({@code ActorsSystem.runFoodProvision}) keeps a solvent arrestee's pantry
 * stocked regardless of which policy wins its ticks.
 *
 * <p>At {@code houseArrestUntilTick} the sentence ends: the bit clears and the goal state
 * resets exactly like {@link HeldPolicy}'s release, so the actor's job re-derives a fresh
 * target next tick. Guards are never house-arrested ({@link ApprehendPolicy}'s riot branch
 * exempts the Watch/Wielder/beasts), so no guard stack needs this policy.
 */
public final class HouseArrestPolicy implements BehaviorPolicy {

    /** Just under HELD's 5000 (custody supersedes), far above PLAYER_CONTROL 2000. */
    private static final int HOUSE_ARREST_SCORE = 4500;

    /** The fixed sentence: exactly one day (DailyRhythm.DAY = 24,000 ticks = 24h at 1 tick/s). */
    public static final long HOUSE_ARREST_TICKS = 24_000L;

    @Override
    public PolicyId id() {
        return PolicyId.HOUSE_ARREST;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        return self.hasStatus(StatusBit.HOUSE_ARREST) ? HOUSE_ARREST_SCORE : 0;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        if (ctx.tick() >= self.houseArrestUntilTick()) {
            release(self);
            return;
        }
        if (self.homeId() == Actor.NONE) {
            // Homeless degradation (world-less bootstrap): hold in place — the HeldPolicy shape.
            self.setLastReasonCode(ReasonCode.UNDER_HOUSE_ARREST);
            return;
        }
        int homeCell = ctx.homes().get(self.homeId()).homeCell();
        if (self.cell() != homeCell) {
            self.stepAlongRoute(homeCell, true, ctx::isWalkable, ctx.occupancy());
        }
        eatAtHearth(self, ctx);
        self.setLastReasonCode(ReasonCode.UNDER_HOUSE_ARREST);
    }

    /**
     * Eats one FOOD per held tick while HUNGER is below RECOVERED, from carried ration first,
     * then the home-cell larder once within reach — the same two free SeekFood sources, with the
     * identical sink accounting, so FOOD conservation stays exact and a fed citizen never
     * starves through its sentence.
     */
    private static void eatAtHearth(Actor self, ActorContext ctx) {
        if (NeedThresholds.isRecovered(self.need(Need.HUNGER))) {
            return;
        }
        ItemsLiteRegistry items = ctx.items();
        if (items.countCarriedOfKind(self.id(), ItemKinds.FOOD) > 0) {
            items.takeCarried(self.id(), ItemKinds.FOOD, 1);
            ctx.recordFoodEaten(1);
            self.applyNeedDelta(Need.HUNGER, FoodEconomy.EAT_RESTORE);
            return;
        }
        int homeCell = ctx.homes().get(self.homeId()).homeCell();
        if (ActorGeometry.chebyshev(self.cell(), homeCell) <= FoodEconomy.EAT_REACH
                && items.countOnCellOfKind(homeCell, ItemKinds.FOOD) > 0) {
            items.takeOnCell(homeCell, ItemKinds.FOOD, 1);
            ctx.recordFoodEaten(1);
            self.applyNeedDelta(Need.HUNGER, FoodEconomy.EAT_RESTORE);
        }
    }

    /** The HeldPolicy-shaped release: clear the bit, reset the goal machine, reason-code it. */
    private void release(Actor self) {
        self.setStatus(StatusBit.HOUSE_ARREST, false);
        self.setGoalState(GoalState.SELECTING);
        self.setGoalTarget(TargetKind.NONE, Actor.NONE);
        self.setGoalWorkTicks(0);
        self.setGoalCooldown(0);
        self.setLastReasonCode(ReasonCode.RELEASED_FROM_HOUSE_ARREST);
    }
}
