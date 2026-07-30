package com.trojia.sim.actor.spell;

/**
 * WHY WEAK MAGIC STAYS WEAK — the whole balance engine, in one integer sum, quoted out of the
 * novel.
 *
 * <p>Gerik's lecture is already a cost model written as prose: "the more you transfer the more
 * is lost to nature, also the further it travels the more is lost" (L454), and "it would be
 * less taxing to do lots of tiny transfers than one big one" (L457). Both clauses are priced
 * here as additions to the difficulty a crafting is checked against, so a big effect at a
 * distance is unaffordable to a laborer's hand WITHOUT any magnitude clamp, any hardcoded
 * ceiling, or any per-spell tuning.
 *
 * <p>That is what makes the vocabulary safe to open up: a spell nobody has written yet cannot
 * be accidentally overpowered, because the same sum prices it the moment it exists. Author a
 * "harm 9 at range 12" and its own resist buries it; the raws need no reviewer.
 *
 * <p><b>All four spendable axes are priced.</b> Magnitude and distance are canon's own two
 * (L454); spread is the third; DURATION is the fourth, charged inside
 * {@link EffectComponent#transferPoints} for a trickle's doses AND for a hold's periods
 * ({@link #HELD_PERIOD_TICKS}). A held crafting used to be the hole in that argument: it cost
 * exactly the same at one tick as at ten thousand, and WHILE_ACTIVE is the mode every
 * temperature and every attribute crafting uses.
 *
 * <p>Pure integer arithmetic on a definition and a distance — no state, no draws, no reads.
 */
public final class SpellCost {

    /**
     * Resist added per tile between caster and target (L454, "the further it travels the more
     * is lost"). Every tile is worth four tenths of a skill point at the {@code SkillChecks}
     * exchange rate, so a reach of four already costs a trained hand more than it can spare.
     */
    public static final int RESIST_PER_TILE = 4;

    /** Resist added per transfer point moved (L454, "the more you transfer the more is lost"). */
    public static final int RESIST_PER_TRANSFER_POINT = 1;

    /**
     * Ticks of {@link EffectMode#WHILE_ACTIVE} hold that count as ONE priced period
     * ({@link EffectComponent#pricedPeriods}). Duration is the fourth thing a crafting can spend,
     * and before this constant existed it was the one thing nothing charged for: a held effect
     * cost the same whether it lasted a tick or a week, so the whole "an unreviewed spell cannot
     * be accidentally strong" argument had a hole in it exactly the size of the mode every
     * temperature and attribute crafting uses.
     *
     * <p>Deliberately much coarser than {@link ActiveEffects#OVER_TIME_PERIOD_TICKS}: L457 says
     * many small transfers beat one big one, so the trickle is the cheap-per-dose shape and the
     * hold is the continuous one. Holding a link open bleeds (L454's clause read over time), but
     * it is not the same work as pushing a fresh transfer through it every ten ticks. At this
     * size the shipped ten-minute warmth pays two periods and a fifteen-minute nudge pays three,
     * while a crafting authored to hold for a day prices itself straight through the check's
     * floor.
     */
    public static final int HELD_PERIOD_TICKS = 300;

    /**
     * Resist added per tile of area radius: one link split across a crowd is thinner than one
     * link. Sized above {@link #RESIST_PER_TILE} because spreading is harder than reaching.
     */
    public static final int RESIST_PER_AREA_TILE = 6;

    /**
     * Resist added for a crafting worked with NO physical bridge — canon's own gate. "A crafting
     * requires a link between the two entities, normally this would be an arm or a blade"
     * (L459); doing without one takes the gift, and "almost nobody can do this." So every
     * {@link TargetShape#RANGED} spell pays this on top of its distance, which is why the
     * ward's public-shelf list contains none.
     *
     * <p><b>A spread pays it too, and it used to slip past.</b> A crafting with an
     * {@link SpellDefinition#areaRadius} reaches bodies the caster is not touching, by
     * definition: the arm or the blade bridges to the TARGET, and nothing at all bridges to the
     * neighbours the spread also catches. Charging the tax only on {@link TargetShape#RANGED}
     * made an area crafting the cheap way to do the thing canon says almost nobody can do — a
     * TOUCH spell spreading five tiles cost 30 while the same reach as a single-target bolt cost
     * 40, so the crowd version was the bargain. Both unbridged shapes pay the same gift tax now,
     * once each.
     */
    public static final int RESIST_UNBRIDGED = 20;

    /**
     * The widest difficulty this model will report. Not a balance number — a saturation point.
     *
     * <p>Nothing clamps magnitude on the temperature and vitality axes (that is the whole
     * argument of this class: an outsized crafting prices itself out instead of being refused),
     * so {@link SpellDefinition#transferPoints} is genuinely unbounded from the raws' side, and
     * the sum below has to be able to survive an author typing a magnitude of two billion held
     * for a year. Beyond this point the crafting is already pinned at
     * {@code SkillChecks.LINKCRAFT_FLOOR_PERMILLE} many orders of magnitude over, so saturating
     * costs no legibility — and it keeps the subtraction inside
     * {@code SkillChecks.successPermille} in a width that cannot wrap, which is the defect this
     * constant exists to make unreachable.
     */
    public static final long RESIST_CEILING = 1_000_000_000L;

    private SpellCost() {
    }

    /**
     * The difficulty this crafting is actually checked against at this distance: the authored
     * base, plus the distance bleed, the transfer bleed (which now includes duration, on both
     * lingering shapes), the spread and (for either unbridged shape) canon's gift tax. This is
     * the number {@code SkillChecks.craftingPermille} takes and the number the on-screen check
     * line quotes, so what a player sees is what the sim rolled.
     *
     * <p><b>Computed in {@code long} and saturated at {@link #RESIST_CEILING}.</b> Every term
     * here is a product of two authored integers, and the transfer term is a product of a
     * magnitude and a duration — so in {@code int} this sum wrapped, and a wrapped resist is a
     * NEGATIVE resist, which is a crafting that got easier by costing more. Monotonic by
     * construction now: no input to this function can make the result smaller.
     */
    public static long resistFor(SpellDefinition spell, int distance) {
        long resist = spell.baseResist()
                + (long) RESIST_PER_TILE * Math.max(0, distance)
                + (long) RESIST_PER_TRANSFER_POINT * spell.transferPoints()
                + (long) RESIST_PER_AREA_TILE * spell.areaRadius();
        if (spell.targetShape() == TargetShape.RANGED) {
            resist += RESIST_UNBRIDGED;
        }
        if (spell.areaRadius() > 0) {
            resist += RESIST_UNBRIDGED;
        }
        return Math.min(RESIST_CEILING, resist);
    }
}
