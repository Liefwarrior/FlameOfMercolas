package com.trojia.sim.actor;

import com.trojia.sim.actor.spell.EffectComponent;
import com.trojia.sim.actor.spell.EffectKind;
import com.trojia.sim.actor.spell.EffectMode;
import com.trojia.sim.actor.spell.SpellCost;
import com.trojia.sim.actor.spell.SpellDefinition;
import com.trojia.sim.actor.spell.TargetShape;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * PAYING MORE MUST NEVER BUY BETTER ODDS — the cost model's one load-bearing property, swept
 * across the whole range a crafting may legally author rather than checked at one worked case.
 *
 * <p><b>What used to happen.</b> {@code SkillChecks.successPermille} built its raw permille in
 * {@code int}: {@code base + 10 * (score - resist)}. Every resist the shipped shelf produces is a
 * two-digit number, so nothing ever noticed — but the vocabulary deliberately puts NO ceiling on
 * magnitude for the temperature and vitality axes, on the argument that an outsized crafting
 * prices itself out instead of being refused. Past a resist of roughly 215 million that multiply
 * wrapped POSITIVE and the whole argument inverted: the most expensive crafting authorable became
 * the easiest one in the ward to land, pinned at the ceiling, with the check line printing the
 * honest resist next to the dishonest percentage. {@code EffectComponent.transferPoints} could
 * wrap on its own too (a big magnitude times a long duration), and a wrapped transfer count is a
 * NEGATIVE resist, which is the same lie arriving a step earlier.
 *
 * <p><b>What this file pins.</b> Monotonicity, as a property: over magnitude, over duration, over
 * distance and over spread, the computed difficulty never falls and the success odds never rise.
 * The sweeps run to {@link Integer#MAX_VALUE} and {@link Integer#MIN_VALUE} on both signs, which
 * is the actual legal range — {@code EffectPairing} bounds only the ATTRIBUTE axis.
 */
final class CostModelMonotonicityTest {

    /**
     * The magnitude ladder: every small value where the units-per-transfer-point division still
     * matters, then powers of two up to the int ceiling, then the two extremes themselves. The
     * old wrap sat around 215 million, so the ladder has to walk through and past that.
     */
    private static final int[] MAGNITUDE_LADDER = magnitudeLadder();

    private static int[] magnitudeLadder() {
        int[] ladder = new int[64];
        int at = 0;
        for (int small = 1; small <= 12; small++) {
            ladder[at++] = small;
        }
        for (int shift = 4; shift <= 30; shift++) {
            ladder[at++] = 1 << shift;
        }
        // Straddle the old int wrap point (the raw permille overflowed once 10*resist passed
        // 2^31, i.e. from a resist of 214,748,365 up).
        ladder[at++] = 214_748_364;
        ladder[at++] = 214_748_365;
        ladder[at++] = 300_000_000;
        ladder[at++] = 1_000_000_000;
        ladder[at++] = Integer.MAX_VALUE - 1;
        ladder[at++] = Integer.MAX_VALUE;
        int[] trimmed = new int[at];
        System.arraycopy(ladder, 0, trimmed, 0, at);
        java.util.Arrays.sort(trimmed);
        return trimmed;
    }

    // ==================================================================
    // Magnitude
    // ==================================================================

    /**
     * THE INVERTED CURVE, SWEPT AWAY. For both delivered vitality shapes and for held warmth, and
     * for BOTH signs, a bigger magnitude is a dearer crafting and a longer shot — never once the
     * other way round, anywhere on the ladder.
     */
    @Test
    void difficultyNeverFallsAndOddsNeverRiseAsTheMagnitudeGrows() {
        sweepMagnitude(EffectKind.VITALITY, EffectMode.INSTANT, 0);
        sweepMagnitude(EffectKind.VITALITY, EffectMode.OVER_TIME, 100);
        sweepMagnitude(EffectKind.TEMPERATURE, EffectMode.WHILE_ACTIVE, 600);
    }

    private static void sweepMagnitude(EffectKind kind, EffectMode mode, int durationTicks) {
        for (int sign : new int[] {1, -1}) {
            long previousResist = Long.MIN_VALUE;
            int previousOdds = Integer.MAX_VALUE;
            for (int step = 0; step < MAGNITUDE_LADDER.length; step++) {
                int magnitude = sign > 0
                        ? MAGNITUDE_LADDER[step]
                        : -MAGNITUDE_LADDER[step];
                SpellDefinition spell = touchSpell(kind, mode, magnitude, durationTicks, 0);
                long resist = SpellCost.resistFor(spell, 1);
                int odds = noviceOdds(resist);
                String where = kind + " " + mode + " magnitude " + magnitude;
                assertTrue(resist >= 0, where + ": a resist can never be negative -- " + resist);
                assertTrue(resist >= previousResist,
                        where + ": difficulty FELL from " + previousResist + " to " + resist
                                + " -- a crafting became cheaper by being bigger");
                assertTrue(odds <= previousOdds,
                        where + ": the odds ROSE from " + previousOdds + " to " + odds
                                + " permille -- a crafting became easier by costing more");
                previousResist = resist;
                previousOdds = odds;
            }
        }
        // The extreme magnitude an author can actually type, on both ends of the int range.
        for (int extreme : new int[] {Integer.MAX_VALUE, Integer.MIN_VALUE + 1,
                Integer.MIN_VALUE}) {
            SpellDefinition spell = touchSpell(kind, mode, extreme, durationTicks, 0);
            long resist = SpellCost.resistFor(spell, 1);
            assertTrue(resist >= 0, kind + " " + mode + " magnitude " + extreme
                    + ": resist wrapped negative (" + resist + ")");
            assertEquals(SkillChecks.LINKCRAFT_FLOOR_PERMILLE, noviceOdds(resist),
                    kind + " " + mode + " magnitude " + extreme
                            + ": the biggest transfer authorable must sit on the check's FLOOR,"
                            + " not its ceiling");
        }
    }

    /**
     * The exact shape of the old defect, stated as one comparison rather than as a sweep: the
     * biggest crafting a raws author can type must not be an easier crafting than a nettle-snap.
     */
    @Test
    void theBiggestCraftingAuthorableIsNotTheEasiestOneInTheWard() {
        SpellDefinition nettle = touchSpell(EffectKind.VITALITY, EffectMode.INSTANT, -1, 0, 0);
        SpellDefinition ruinous = touchSpell(EffectKind.VITALITY, EffectMode.INSTANT,
                Integer.MIN_VALUE, 0, 0);
        assertTrue(SpellCost.resistFor(ruinous, 1) > SpellCost.resistFor(nettle, 1),
                "the ruinous crafting must be the dearer one");
        assertTrue(noviceOdds(SpellCost.resistFor(ruinous, 1))
                        < noviceOdds(SpellCost.resistFor(nettle, 1)),
                "and the longer shot -- this is the assertion the int wrap used to fail");
    }

    // ==================================================================
    // Duration, distance and spread
    // ==================================================================

    /** A hold or a trickle that runs longer is dearer, all the way to the tick ceiling. */
    @Test
    void difficultyNeverFallsAsTheDurationGrows() {
        sweepDuration(EffectKind.VITALITY, EffectMode.OVER_TIME,
                com.trojia.sim.actor.spell.ActiveEffects.OVER_TIME_PERIOD_TICKS);
        sweepDuration(EffectKind.TEMPERATURE, EffectMode.WHILE_ACTIVE,
                com.trojia.sim.actor.spell.ActiveEffects.WARMTH_REST_PERIOD_TICKS);
    }

    private static void sweepDuration(EffectKind kind, EffectMode mode, int minimumTicks) {
        long previousResist = Long.MIN_VALUE;
        int previousOdds = Integer.MAX_VALUE;
        for (int ticks = minimumTicks; ticks > 0; ticks = nextDuration(ticks)) {
            SpellDefinition spell = touchSpell(kind, mode, 1_000_000, ticks, 0);
            long resist = SpellCost.resistFor(spell, 1);
            int odds = noviceOdds(resist);
            String where = kind + " " + mode + " for " + ticks + " ticks";
            assertTrue(resist >= previousResist, where + ": difficulty fell to " + resist);
            assertTrue(odds <= previousOdds, where + ": the odds rose to " + odds);
            previousResist = resist;
            previousOdds = odds;
        }
    }

    /** Doubles until it would leave the int range, then stops (returns a non-positive value). */
    private static int nextDuration(int ticks) {
        return ticks > Integer.MAX_VALUE / 2 ? -1 : ticks * 2;
    }

    /** Reaching further is dearer, every tile of the way out to the packed-position ceiling. */
    @Test
    void difficultyNeverFallsAsTheDistanceGrows() {
        SpellDefinition bolt = new SpellDefinition("bolt", "Bolt", "linkcraft", 0, 0, 0,
                TargetShape.RANGED, 4096, 0,
                List.of(new EffectComponent(EffectKind.VITALITY, EffectMode.INSTANT, -1, 0, 0)));
        long previousResist = Long.MIN_VALUE;
        for (int distance = 0; distance <= 4096; distance++) {
            long resist = SpellCost.resistFor(bolt, distance);
            assertTrue(resist >= previousResist,
                    "distance " + distance + ": difficulty fell to " + resist);
            previousResist = resist;
        }
    }

    /**
     * A SPREAD IS NEVER THE BARGAIN ROUTE TO AN UNBRIDGED LINK. The area axis reaches bodies the
     * caster is not touching, which is precisely what canon gates behind the gift (L459) — so it
     * pays the same tax a RANGED crafting pays, and a crowd version can never undercut the
     * single-target bolt that reaches the same distance.
     */
    @Test
    void spreadingAcrossACrowdIsNeverCheaperThanReachingOneBodyThatFar() {
        long previousResist = Long.MIN_VALUE;
        for (int radius = 0; radius <= 64; radius++) {
            SpellDefinition spread = new SpellDefinition("spread", "Spread", "linkcraft", 0, 0, 0,
                    TargetShape.TOUCH, 1, radius,
                    List.of(new EffectComponent(EffectKind.VITALITY, EffectMode.INSTANT, -1, 0,
                            0)));
            long resist = SpellCost.resistFor(spread, 1);
            assertTrue(resist >= previousResist,
                    "radius " + radius + ": difficulty fell to " + resist);
            previousResist = resist;
            if (radius == 0) {
                continue;
            }
            SpellDefinition bolt = new SpellDefinition("bolt", "Bolt", "linkcraft", 0, 0, 0,
                    TargetShape.RANGED, radius, 0,
                    List.of(new EffectComponent(EffectKind.VITALITY, EffectMode.INSTANT, -1, 0,
                            0)));
            assertTrue(resist >= SpellCost.resistFor(bolt, radius),
                    "a spread of radius " + radius + " (" + resist + ") undercuts the single bolt"
                            + " that reaches the same distance ("
                            + SpellCost.resistFor(bolt, radius)
                            + ") -- the unbridged tax has a hole in it");
        }
    }

    // ==================================================================
    // helpers
    // ==================================================================

    /** A one-component TOUCH crafting — the cheapest shape, so the sweep measures the axis only. */
    private static SpellDefinition touchSpell(EffectKind kind, EffectMode mode, int magnitude,
            int durationTicks, int areaRadius) {
        return new SpellDefinition("swept", "Swept", "linkcraft", 0, 0, 0, TargetShape.TOUCH, 1,
                areaRadius,
                List.of(new EffectComponent(kind, mode, magnitude, 0, durationTicks)));
    }

    /**
     * A novice's odds against a resist, off the UNWIRED table — a property of the CONTENT rather
     * than of any live actor, the {@code DocksActorsMain} shelf-report convention.
     */
    private static int noviceOdds(long resist) {
        return SkillChecks.craftingPermille(SkillTrackRegistry.UNWIRED, 0, Actor.NONE, resist);
    }
}
