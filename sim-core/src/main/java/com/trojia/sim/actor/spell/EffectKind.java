package com.trojia.sim.actor.spell;

/**
 * THE EFFECT AXES — the whole vocabulary a crafting can be built out of, and the only part of
 * magic that is Java. Everything else (which axis, how much, how long, how far, how wide, how
 * likely) is a number in {@code content/raws/spells/spells.json}, so the fourth spell costs no
 * code at all.
 *
 * <p><b>Append-only</b> (the {@code ReasonCode} / {@code ActorRngStream} discipline): the
 * ordinal is written into every save by {@link ActiveEffects}, so a new axis goes on the END.
 *
 * <p><b>Canon.</b> There is exactly one operation in the novel's magic — energy moved along a
 * link — and these three are the three places it can land. Gerik heals a wound with "heat
 * energy from the enemies" (L445) and the same transfer into a small part of a body simply
 * "hurts" (L449): warmth and harm are ONE verb with the endpoint changed, which is why
 * {@link #TEMPERATURE} and {@link #VITALITY} are separate axes rather than separate spells. The
 * third is the royal mage's line at Eric's birth (L98) — untrained magic "can be expressed as
 * unnatural intelligence, or in strength, or reflexes" — i.e. a stat nudge, quoted.
 *
 * <p><b>Sign is the whole of opposition.</b> Every axis reads a SIGNED magnitude: positive
 * gives, negative takes. Warm/chill, mend/scald, steady/sap are the same row of raws with a
 * minus sign, so no "opposite" axis ever needs writing.
 */
public enum EffectKind {

    /**
     * Body heat, in <b>deci-Kelvin of offset from ambient</b> (the project's temperature unit,
     * ARCHITECTURE §1.1 #13). Positive warms, negative chills. Read as a live sum over the
     * caster's active {@link EffectMode#WHILE_ACTIVE} slots — there is no separate per-actor
     * field to persist, so the offset can never drift out of step with the effects that made it.
     */
    TEMPERATURE(10),

    /**
     * The body's own condition, in <b>hit points</b>. Negative harms (a scald, a welt), positive
     * mends. Clamped into {@code [VITALITY_FLOOR, the type's authored hp]} — public-shelf
     * craftings bruise and blister; they do not kill. That ceiling is canon, not timidity: it
     * takes "the energy of twenty full crafters" pooled into an already-lit torch to catch one
     * guard alight (L567), and a common crafter carries roughly a thousandth of a Luxerne's
     * throughput (L516).
     */
    VITALITY(1),

    /**
     * A derived attribute (MGT/AGI/VIG/WIT), in <b>attribute points</b>, for as long as the
     * effect holds. {@code param} is the {@code AttributeId} ordinal. Positive steadies,
     * negative saps. Because the whole game reads attributes through one function, a +1 AGI
     * lands in the shove contest, the lift, the cast and the cull at once — the standing
     * Morrowind steer, taken literally.
     */
    ATTRIBUTE(2);

    /**
     * How many of this axis's own units make ONE point of "transfer" for costing purposes
     * ({@link SpellCost}). Ten deci-Kelvin is a degree; one hit point is one point; two
     * attribute points are worth one. Without this the axes could not share a cost model, and
     * the cost model is what makes "weak" enforce itself on spells nobody has written yet.
     */
    private final int unitsPerTransferPoint;

    EffectKind(int unitsPerTransferPoint) {
        this.unitsPerTransferPoint = unitsPerTransferPoint;
    }

    public int unitsPerTransferPoint() {
        return unitsPerTransferPoint;
    }

    /** The axis with this ordinal (save/load path); throws on an unknown ordinal. */
    public static EffectKind of(int ordinal) {
        EffectKind[] all = values();
        if (ordinal < 0 || ordinal >= all.length) {
            throw new IllegalArgumentException("unknown EffectKind ordinal " + ordinal);
        }
        return all[ordinal];
    }
}
