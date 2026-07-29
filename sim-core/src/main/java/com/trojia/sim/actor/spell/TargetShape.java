package com.trojia.sim.actor.spell;

/**
 * WHERE a crafting can reach — Daggerfall's target axis, with canon's own boundary drawn
 * through the middle of it.
 *
 * <p>"A crafting requires a link between the two entities, normally this would be an arm or a
 * blade" (L459); making one WITHOUT a physical bridge takes the gift, and "almost nobody can do
 * this." So {@link #TOUCH} is what the public shelf teaches and {@link #RANGED} is the tier
 * above it. Range is not a balance dial somebody set to 1 — it is the line between a docks
 * laborer and a sky knight, and the vocabulary supports both so the content can grant one and
 * withhold the other.
 */
public enum TargetShape {

    /** The caster's own body. No link to forge: you are already touching yourself. */
    SELF,

    /** An adjacent body — the arm or the blade. Chebyshev 1, same z. */
    TOUCH,

    /**
     * A body up to the spell's authored {@code range} away with no bridge at all. Canon's
     * gifted tier; every unit of distance also bleeds the transfer ({@link SpellCost}), so a
     * long reach prices itself out of a common hand without any extra rule.
     */
    RANGED;

    /** The shape with this ordinal; throws on an unknown ordinal. */
    public static TargetShape of(int ordinal) {
        TargetShape[] all = values();
        if (ordinal < 0 || ordinal >= all.length) {
            throw new IllegalArgumentException("unknown TargetShape ordinal " + ordinal);
        }
        return all[ordinal];
    }
}
