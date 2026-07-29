package com.trojia.sim.actor.spell;

import java.util.List;

/**
 * A CRAFTING, entirely as data: which skill governs it, how deeply you must have read to have
 * it at all, how hard it is, how long before the hand is free again, how far and how wide it
 * reaches, and the ordered list of {@link EffectComponent}s it lands. Nothing here is
 * special-cased anywhere in Java — {@link SpellVerb} resolves any well-formed definition,
 * which is the point of building the vocabulary before the spells.
 *
 * <p>Daggerfall's five axes all live here: <b>effect</b> and <b>magnitude</b> and
 * <b>duration</b> in the components, <b>chance</b> as {@code baseResist} against the caster's
 * skill check, and <b>area</b> as {@code targetShape}/{@code range}/{@code areaRadius}.
 *
 * @param key          the raws id ({@code "warm_the_hands"})
 * @param displayName  the ward-facing name ({@code "Warm the Hands"})
 * @param skillKey     the raws id of the skill that gates and grows with this crafting
 * @param minLevel     the level in that skill below which this crafting is simply not known —
 *                     the literacy tier made mechanical (canon's public-issue shelf vs the
 *                     restricted edition on the top shelf, L2472)
 * @param baseResist   the authored difficulty the check is measured against, BEFORE distance,
 *                     transfer and spread are priced in ({@link SpellCost})
 * @param cooldownTicks ticks before this caster may work another crafting at all
 * @param targetShape  where it can reach ({@link TargetShape})
 * @param range        Chebyshev reach for {@link TargetShape#RANGED}; 0 for SELF, 1 for TOUCH
 * @param areaRadius   Chebyshev radius around the target that also catches it; 0 = the target
 *                     alone
 * @param components   the ordered parts, applied in list order
 */
public record SpellDefinition(String key, String displayName, String skillKey, int minLevel,
        int baseResist, int cooldownTicks, TargetShape targetShape, int range, int areaRadius,
        List<EffectComponent> components) {

    public SpellDefinition {
        java.util.Objects.requireNonNull(key, "key");
        java.util.Objects.requireNonNull(displayName, "displayName");
        java.util.Objects.requireNonNull(skillKey, "skillKey");
        java.util.Objects.requireNonNull(targetShape, "targetShape");
        components = List.copyOf(java.util.Objects.requireNonNull(components, "components"));
        if (components.isEmpty()) {
            throw new IllegalArgumentException("a spell with no components does nothing: " + key);
        }
        if (minLevel < 0 || baseResist < 0 || cooldownTicks < 0 || range < 0 || areaRadius < 0) {
            throw new IllegalArgumentException("spell " + key + " carries a negative field");
        }
        if (targetShape == TargetShape.SELF && range != 0) {
            throw new IllegalArgumentException("a SELF spell has no range: " + key);
        }
        if (targetShape == TargetShape.TOUCH && range != 1) {
            throw new IllegalArgumentException("a TOUCH spell reaches exactly 1: " + key);
        }
        if (targetShape == TargetShape.RANGED && range < 1) {
            throw new IllegalArgumentException("a RANGED spell needs a range >= 1: " + key);
        }
    }

    /** The reach this spell's target must fall inside: 0 for SELF, else {@link #range}. */
    public int reach() {
        return targetShape == TargetShape.SELF ? 0 : range;
    }

    /** The longest {@code durationTicks} any part holds — what the toast quotes. */
    public int longestDurationTicks() {
        int longest = 0;
        for (int i = 0; i < components.size(); i++) {
            longest = Math.max(longest, components.get(i).durationTicks());
        }
        return longest;
    }

    /** Total transfer points across every part — the magnitude half of the cost (L452/L457). */
    public int transferPoints() {
        int total = 0;
        for (int i = 0; i < components.size(); i++) {
            total += components.get(i).transferPoints();
        }
        return total;
    }

    /** How many slots {@link ActiveEffects} needs for one landing (the non-INSTANT parts). */
    public int lingeringPartCount() {
        int count = 0;
        for (int i = 0; i < components.size(); i++) {
            if (components.get(i).mode() != EffectMode.INSTANT) {
                count++;
            }
        }
        return count;
    }
}
