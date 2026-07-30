package com.trojia.sim.actor.spell;

/**
 * ONE PART of a crafting: an axis, a signed magnitude, a shape in time, and (where the axis
 * needs one) a selector. This is the atom a spellcrafting screen will let a player pick from —
 * a spell is nothing but an ordered list of these.
 *
 * <p><b>Every authoring route lands here.</b> The raws loader builds one of these per JSON
 * component, and a Java caller composing a definition by hand calls the same canonical
 * constructor — which is why {@link EffectPairing#require} lives in it. A part that would
 * resolve, be charged its full difficulty, narrate success and change nothing is refused at
 * construction, by name, and there is no way round it.
 *
 * @param kind          which axis this part moves ({@link EffectKind})
 * @param mode          how it sits in time ({@link EffectMode}) — must be a mode this axis can
 *                      actually be worked in ({@link EffectPairing#isLegal})
 * @param magnitude     signed, in the axis's own units: deci-Kelvin, hit points, attribute
 *                      points. Negative takes, positive gives. Never 0, and never past the
 *                      axis's own ceiling ({@link EffectPairing#magnitudeCeiling})
 * @param param         the axis's selector — the {@code AttributeId} ordinal for
 *                      {@link EffectKind#ATTRIBUTE}, {@code 0} for every axis that needs none
 * @param durationTicks how long it holds/trickles, in ticks; {@code 0} for
 *                      {@link EffectMode#INSTANT} (and required to be 0 there), and otherwise
 *                      at least {@link EffectPairing#minimumEffectiveTicks}
 */
public record EffectComponent(EffectKind kind, EffectMode mode, int magnitude, int param,
        int durationTicks) {

    public EffectComponent {
        java.util.Objects.requireNonNull(kind, "kind");
        java.util.Objects.requireNonNull(mode, "mode");
        if (mode == EffectMode.INSTANT && durationTicks != 0) {
            throw new IllegalArgumentException(
                    "an INSTANT component carries no duration: " + durationTicks);
        }
        if (param < 0) {
            throw new IllegalArgumentException("param must be >= 0: " + param);
        }
        // The no-silent-no-op gate: a legal (axis x time-shape) pairing, a non-zero magnitude
        // inside the axis's own ceiling, and a duration long enough for the pairing's cadence to
        // deliver its first dose. Everything a crafting can be is composed of these, so this one
        // call is what makes a fully-priced no-op unauthorable.
        EffectPairing.require(kind, mode, magnitude, durationTicks);
    }

    /**
     * How much this part actually MOVES, in axis-neutral transfer points — the number the cost
     * model prices (L452: "the more you transfer the more is lost to nature"). Never 0 for a
     * non-zero magnitude: a nudge below one whole point still costs a point.
     *
     * <p><b>Duration is priced on BOTH lingering shapes, not just the trickle.</b> An
     * {@link EffectMode#OVER_TIME} part pays for every dose it will deliver (L457's arithmetic,
     * run backwards). A {@link EffectMode#WHILE_ACTIVE} part pays for every period it holds the
     * link open — a coarser period than a dose ({@link SpellCost#HELD_PERIOD_TICKS} against
     * {@link ActiveEffects#OVER_TIME_PERIOD_TICKS}), because holding a link open is not the same
     * work as pushing fresh transfers through it, but priced all the same. Without this a held
     * effect cost exactly what a one-tick version of itself cost, and the claim that an
     * unreviewed spell cannot be accidentally strong was measurably false: an author could hold
     * a nudge for a week for the price of holding it for a heartbeat.
     */
    public int transferPoints() {
        int units = Math.abs(magnitude);
        if (units == 0) {
            return 0;
        }
        int perPeriod = Math.max(1, units / kind.unitsPerTransferPoint());
        return perPeriod * pricedPeriods();
    }

    /**
     * How many periods of this part's own shape it is charged for: doses for a trickle, holds
     * for a hold, one for an instant. Never 0 — the shortest legal duration still costs a period.
     */
    public int pricedPeriods() {
        return switch (mode) {
            case INSTANT -> 1;
            case OVER_TIME -> Math.max(1, durationTicks / ActiveEffects.OVER_TIME_PERIOD_TICKS);
            case WHILE_ACTIVE -> Math.max(1, durationTicks / SpellCost.HELD_PERIOD_TICKS);
        };
    }
}
