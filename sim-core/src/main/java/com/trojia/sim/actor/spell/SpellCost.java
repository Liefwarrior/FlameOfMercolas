package com.trojia.sim.actor.spell;

/**
 * WHY WEAK MAGIC STAYS WEAK — the whole balance engine, in one integer sum, quoted out of the
 * novel.
 *
 * <p>Gerik's lecture is already a cost model written as prose: "the more you transfer the more
 * is lost to nature, also the further it travels the more is lost" (L452), and "it would be
 * less taxing to do lots of tiny transfers than one big one" (L457). Both clauses are priced
 * here as additions to the difficulty a crafting is checked against, so a big effect at a
 * distance is unaffordable to a laborer's hand WITHOUT any magnitude clamp, any hardcoded
 * ceiling, or any per-spell tuning.
 *
 * <p>That is what makes the vocabulary safe to open up: a spell nobody has written yet cannot
 * be accidentally overpowered, because the same sum prices it the moment it exists. Author a
 * "harm 9 at range 12" and its own resist buries it; the raws need no reviewer.
 *
 * <p>Pure integer arithmetic on a definition and a distance — no state, no draws, no reads.
 */
public final class SpellCost {

    /**
     * Resist added per tile between caster and target (L452, "the further it travels the more
     * is lost"). Every tile is worth four tenths of a skill point at the {@code SkillChecks}
     * exchange rate, so a reach of four already costs a trained hand more than it can spare.
     */
    public static final int RESIST_PER_TILE = 4;

    /** Resist added per transfer point moved (L452, "the more you transfer the more is lost"). */
    public static final int RESIST_PER_TRANSFER_POINT = 1;

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
     */
    public static final int RESIST_UNBRIDGED = 20;

    private SpellCost() {
    }

    /**
     * The difficulty this crafting is actually checked against at this distance: the authored
     * base, plus the distance bleed, the transfer bleed, the spread and (for an unbridged link)
     * canon's gift tax. This is the number {@code SkillChecks.linkcraftPermille} takes and the
     * number the on-screen check line quotes, so what a player sees is what the sim rolled.
     */
    public static int resistFor(SpellDefinition spell, int distance) {
        int resist = spell.baseResist()
                + RESIST_PER_TILE * Math.max(0, distance)
                + RESIST_PER_TRANSFER_POINT * spell.transferPoints()
                + RESIST_PER_AREA_TILE * spell.areaRadius();
        if (spell.targetShape() == TargetShape.RANGED) {
            resist += RESIST_UNBRIDGED;
        }
        return resist;
    }
}
