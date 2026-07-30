package com.trojia.sim.actor.spell;

/**
 * WHICH (axis x time-shape) PAIRINGS ARE REAL — the table that makes it impossible to author a
 * crafting that resolves, charges a resist, narrates success and changes nothing.
 *
 * <p><b>The defect this exists to kill.</b> Three axes times three modes is nine pairings, and
 * only four of them ever moved anything: the sim reads a held TEMPERATURE row and a held
 * ATTRIBUTE row by summing live slots, and it writes hit points on an INSTANT or an OVER_TIME
 * dose. The other five — temperature INSTANT, temperature OVER_TIME, vitality WHILE_ACTIVE,
 * attribute INSTANT, attribute OVER_TIME — loaded fine, resolved fine, were charged their full
 * difficulty and toasted "the link opens; it takes", and then did nothing at all. In a system
 * whose whole premise is that a player will COMPOSE combinations, a silently dead combination is
 * the worst defect available. So the illegal pairings are REFUSED, by name, at the moment a
 * component is constructed — which is the raws loader for an authored spell and the record's own
 * canonical constructor for anything composed in Java.
 *
 * <p><b>Why the four survivors are the right four, in canon's terms.</b> Canon has exactly one
 * operation — energy moved along a link — and the endpoint decides what the operation looks like
 * in time:
 * <ul>
 *   <li><b>Heat is a state a link HOLDS.</b> Eric links into a campfire and keeps drawing (L505);
 *       Gerik's arm is warm while the link is open. A body carries no thermal store in this sim
 *       — its offset from ambient IS the live sum of its held rows — so a one-off or a dosed
 *       temperature has nowhere to land. {@link EffectMode#WHILE_ACTIVE} only.</li>
 *   <li><b>A wound is an EVENT.</b> "too much energy hurts... you're applying it to a small part
 *       of his body" (L448), delivered at once or dose by dose ("lots of tiny transfers", L457).
 *       Hit points are a written number, and nothing anywhere reads a held vitality row, so
 *       {@link EffectMode#WHILE_ACTIVE} is refused: mending is not a state you hold, it is a
 *       change you make. {@link EffectMode#INSTANT} and {@link EffectMode#OVER_TIME}.</li>
 *   <li><b>An attribute is DERIVED, never banked.</b> PROGRESSION-SPEC §5 recomputes MGT/AGI/VIG/
 *       WIT from the skill table on every read, so there is no field for a one-off to write; a
 *       crafting can only lean on the number while the link holds. {@link EffectMode#WHILE_ACTIVE}
 *       only — and it is bounded, see {@link ActiveEffects#ATTRIBUTE_MODIFIER_LIMIT}.</li>
 * </ul>
 *
 * <p><b>The second silent family: a duration too short to deliver.</b> Legality is not enough.
 * A trickle doses once per {@link ActiveEffects#OVER_TIME_PERIOD_TICKS}, so a component authored
 * for 1–9 ticks delivered ZERO doses at full price; a held warmth pays REST once per
 * {@link ActiveEffects#WARMTH_REST_PERIOD_TICKS}, so a warmth shorter than that cadence moved
 * nothing either. Every legal pairing therefore declares the shortest duration at which it
 * actually does something ({@link #minimumEffectiveTicks}), and anything under it is refused
 * with the same named error. There is no third way to be a fully-priced no-op: a magnitude of 0
 * is refused here too.
 *
 * <p>Pure integer/enum arithmetic. No state, no draws, no allocation on any hot path.
 */
public final class EffectPairing {

    private EffectPairing() {
    }

    /**
     * Whether this axis can sit in this shape of time at all — i.e. whether some code in the sim
     * actually reads or writes what the pairing would produce. The whole matrix, in one switch,
     * so there is exactly one place to change when a fourth axis arrives.
     */
    public static boolean isLegal(EffectKind kind, EffectMode mode) {
        return switch (kind) {
            // Heat and tuning are HELD: read by summing live rows, with no field to bank into.
            case TEMPERATURE, ATTRIBUTE -> mode == EffectMode.WHILE_ACTIVE;
            // A wound is DELIVERED: written into hit points, at once or dose by dose.
            case VITALITY -> mode == EffectMode.INSTANT || mode == EffectMode.OVER_TIME;
        };
    }

    /**
     * The shortest {@code durationTicks} at which a legal lingering pairing actually moves
     * something — the cadence it is delivered on. {@code 0} for {@link EffectMode#INSTANT}
     * (which carries no duration) and for an illegal pairing (which never gets this far).
     *
     * <p>A trickle under one dose period and a warmth under one REST cadence are the exact same
     * defect as an illegal pairing wearing a legal name, so they are refused the same way.
     */
    public static int minimumEffectiveTicks(EffectKind kind, EffectMode mode) {
        if (!isLegal(kind, mode) || mode == EffectMode.INSTANT) {
            return 0;
        }
        return switch (kind) {
            // A dose lands on the period; anything shorter delivers none of them.
            case VITALITY -> ActiveEffects.OVER_TIME_PERIOD_TICKS;
            // Held warmth pays REST on its own cadence; anything shorter never reaches a payment.
            case TEMPERATURE -> ActiveEffects.WARMTH_REST_PERIOD_TICKS;
            // A held nudge is IN FORCE from the tick it is filed: every check reads it at once.
            case ATTRIBUTE -> 1;
        };
    }

    /**
     * The authored ceiling on one component's magnitude in this axis's own units, or
     * {@link Integer#MAX_VALUE} where the axis is bounded structurally somewhere else.
     *
     * <p>Only {@link EffectKind#ATTRIBUTE} is bounded here, and it is bounded because it is the
     * axis that reaches furthest: a live attribute row is folded into
     * {@code SkillTrackRegistry.attribute()}, which is the one function EVERY check in the game
     * reads. {@link EffectKind#VITALITY} needs no authored ceiling — {@link
     * ActiveEffects#VITALITY_FLOOR} holds every body at 1 hp no matter what is authored — and
     * {@link EffectKind#TEMPERATURE} needs none either, since a big offset simply prices itself
     * out through {@link SpellCost}.
     */
    public static int magnitudeCeiling(EffectKind kind) {
        return kind == EffectKind.ATTRIBUTE
                ? ActiveEffects.ATTRIBUTE_MODIFIER_LIMIT
                : Integer.MAX_VALUE;
    }

    /**
     * Throws {@link IllegalArgumentException} naming exactly what is wrong, or returns quietly
     * if this component can actually do something. Called from {@link EffectComponent}'s
     * canonical constructor, which every authoring route goes through — the raws loader wraps
     * the message into a {@code SpellRawsValidationException} carrying the JSON field path, and
     * a Java caller composing a definition by hand gets the same sentence.
     */
    public static void require(EffectKind kind, EffectMode mode, int magnitude,
            int durationTicks) {
        if (!isLegal(kind, mode)) {
            throw new IllegalArgumentException(illegalReason(kind, mode));
        }
        if (magnitude == 0) {
            throw new IllegalArgumentException("a component that moves nothing is not a crafting: "
                    + kind + " magnitude 0 would resolve, be charged and change nothing");
        }
        int ceiling = magnitudeCeiling(kind);
        if (Math.abs(magnitude) > ceiling) {
            throw new IllegalArgumentException("the " + kind + " axis is bounded at +/-" + ceiling
                    + " and this component authors " + magnitude
                    + "; a live attribute row is read by every check in the game, so the public"
                    + " shelf cannot teach a big one (ActiveEffects.ATTRIBUTE_MODIFIER_LIMIT)");
        }
        int minimum = minimumEffectiveTicks(kind, mode);
        if (durationTicks < minimum) {
            throw new IllegalArgumentException("a " + kind + " " + mode + " component needs at"
                    + " least " + minimum + " ticks to deliver anything and this one authors "
                    + durationTicks + "; below that it would resolve, be charged and change"
                    + " nothing");
        }
    }

    /**
     * Why this pairing is refused, in the sim's own terms — the message a content author reads.
     * Package-private: the two doors into the table ({@link EffectComponent}'s constructor and
     * {@link ActiveEffects#add}) both live here, and nothing outside the package should be
     * formatting refusals of its own.
     */
    static String illegalReason(EffectKind kind, EffectMode mode) {
        String why = switch (kind) {
            case TEMPERATURE -> "body heat is a state a link HOLDS open: a body carries no"
                    + " thermal store, its offset from ambient IS the sum of its live held rows,"
                    + " so a " + mode + " temperature has nowhere to land";
            case VITALITY -> "a wound is an EVENT, not a state a link holds: hit points are a"
                    + " written number and nothing in the sim reads a held vitality row, so a "
                    + mode + " vitality would resolve and change nothing";
            case ATTRIBUTE -> "attributes are DERIVED from the skill table on every read"
                    + " (PROGRESSION-SPEC section 5), so there is no field for a " + mode
                    + " attribute to write; a crafting can only lean on the number while the"
                    + " link holds";
        };
        return "the " + kind + " axis cannot be worked " + mode + " -- " + why
                + ". Legal modes for " + kind + ": " + legalModes(kind);
    }

    /** The modes this axis may actually be authored in, as a readable list. */
    private static String legalModes(EffectKind kind) {
        StringBuilder out = new StringBuilder();
        for (EffectMode mode : EffectMode.values()) {
            if (isLegal(kind, mode)) {
                out.append(out.length() == 0 ? "" : ", ").append(mode);
            }
        }
        return out.toString();
    }
}
