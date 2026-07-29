package com.trojia.sim.actor.spell;

/**
 * ONE PART of a crafting: an axis, a signed magnitude, a shape in time, and (where the axis
 * needs one) a selector. This is the atom a spellcrafting screen will let a player pick from —
 * a spell is nothing but an ordered list of these.
 *
 * @param kind          which axis this part moves ({@link EffectKind})
 * @param mode          how it sits in time ({@link EffectMode})
 * @param magnitude     signed, in the axis's own units: deci-Kelvin, hit points, attribute
 *                      points. Negative takes, positive gives.
 * @param param         the axis's selector — the {@code AttributeId} ordinal for
 *                      {@link EffectKind#ATTRIBUTE}, {@code 0} for every axis that needs none
 * @param durationTicks how long it holds/trickles, in ticks; {@code 0} for
 *                      {@link EffectMode#INSTANT} (and required to be 0 there)
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
        if (mode != EffectMode.INSTANT && durationTicks <= 0) {
            throw new IllegalArgumentException(
                    "a " + mode + " component needs a positive durationTicks");
        }
        if (param < 0) {
            throw new IllegalArgumentException("param must be >= 0: " + param);
        }
    }

    /**
     * How much this part actually MOVES, in axis-neutral transfer points — the number the cost
     * model prices (L452: "the more you transfer the more is lost to nature"). A trickle pays
     * for every dose it will deliver, so a long harm-over-time is honestly dearer than a short
     * one. Never 0 for a non-zero magnitude: a nudge below one whole point still costs a point.
     */
    public int transferPoints() {
        int units = Math.abs(magnitude);
        if (units == 0) {
            return 0;
        }
        int perDose = Math.max(1, units / kind.unitsPerTransferPoint());
        if (mode != EffectMode.OVER_TIME) {
            return perDose;
        }
        int doses = Math.max(1, durationTicks / ActiveEffects.OVER_TIME_PERIOD_TICKS);
        return perDose * doses;
    }
}
