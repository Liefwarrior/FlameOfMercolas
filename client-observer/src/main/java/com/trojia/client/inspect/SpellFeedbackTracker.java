package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.spell.ActiveEffects;
import com.trojia.sim.actor.spell.EffectComponent;
import com.trojia.sim.actor.spell.SpellCost;
import com.trojia.sim.actor.spell.SpellDefinition;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.actor.spell.TargetShape;

import java.util.function.IntSupplier;

/**
 * Narrates the played soul's CRAFTING outcome as toasts (the {@code CullFeedbackTracker}
 * pattern): after a button press arms the intent, the sim resolves it next tick and stamps
 * SPELL_WORKED / SPELL_FIZZLED / NO_LINK_TO_TARGET — this tracker reads the stamp off the
 * played body and says what happened in words.
 *
 * <p>A resolved attempt gets THREE toasts, in the order a player needs them: what happened, WHAT
 * IT DID (the crafting's own components, read straight off the raws — "+1.5 deg for 10 min", "-1
 * hp", "+1 AGI for 15 min"), and then the visible dice
 * ({@link CheckLineFormatter#spellLine}). The middle line is the one a spellcrafting screen will
 * eventually need anyway, and shipping it now means a player can tell two craftings apart
 * without reading the JSON.
 *
 * <p><b>Bounded wait</b> ({@link #PENDING_TICKS}): custody and the gibbet outrank player
 * control, so a never-resolving intent expires silently rather than toasting minutes later.
 * Zero sim writes; GL-free.
 */
public final class SpellFeedbackTracker {

    /** How many executed ticks an armed intent waits for its outcome stamp. */
    public static final int PENDING_TICKS = 10;

    public static final String WORKED = "The link opens; it takes.";
    public static final String FIZZLED = "The link will not hold.";
    public static final String NO_LINK = SpellAvailability.NO_TARGET;

    private final ActorRegistry registry;
    private final ToastQueue toasts;
    private final IntSupplier playedActorId;
    private final SkillTrackRegistry tracks;
    private final SpellRegistry spells;

    private int pendingTicks;
    private int armedSpellRaw = Actor.NONE;
    private int armedDistance;

    public SpellFeedbackTracker(ActorRegistry registry, ToastQueue toasts,
            IntSupplier playedActorId, SkillTrackRegistry tracks, SpellRegistry spells) {
        this.registry = registry;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
        this.tracks = tracks;
        this.spells = spells;
    }

    /**
     * Arms the outcome watch beside the intent, remembering WHICH crafting and at what distance
     * — the check line has to quote the resist the sim actually rolled against, and that is a
     * function of the gap between the two bodies at the moment of the press.
     */
    public void arm(int spellRaw, int distance) {
        this.pendingTicks = PENDING_TICKS;
        this.armedSpellRaw = spellRaw;
        this.armedDistance = distance;
    }

    /** The crafting currently awaiting its outcome, or {@link Actor#NONE}. */
    public int armedSpellRaw() {
        return armedSpellRaw;
    }

    /** Call once per executed tick, after the tick ran (the driver's after-tick seam). */
    public void afterTick(long tick) {
        if (pendingTicks <= 0) {
            return;
        }
        int played = playedActorId.getAsInt();
        if (played == Actor.NONE) {
            pendingTicks = 0; // play mode ended before the outcome landed
            return;
        }
        ReasonCode reason = registry.get(played).lastReasonCode();
        String line = outcomeLine(reason);
        if (line == null) {
            pendingTicks--;
            return;
        }
        toasts.add(line);
        if (reason == ReasonCode.SPELL_WORKED || reason == ReasonCode.SPELL_FIZZLED) {
            boolean worked = reason == ReasonCode.SPELL_WORKED;
            if (worked && spells.isValidRaw(armedSpellRaw)) {
                toasts.add(effectLine(spells.get(armedSpellRaw)));
            }
            String check = checkLine(played, worked);
            if (!check.isEmpty()) {
                toasts.add(check);
            }
        }
        pendingTicks = 0;
        armedSpellRaw = Actor.NONE;
    }

    /** The crafting's visible dice, against the resist the sim actually computed. */
    private String checkLine(int played, boolean worked) {
        if (!tracks.isWired() || !spells.isValidRaw(armedSpellRaw)) {
            return "";
        }
        SpellDefinition spell = spells.get(armedSpellRaw);
        return CheckLineFormatter.spellLine(tracks, played, spell.displayName(),
                SpellCost.resistFor(spell, armedDistance), worked);
    }

    /**
     * WHAT IT DID, read off the crafting's own components — one clause per part, joined. Pure
     * raws presentation: this method knows the three axes and nothing about any one spell, so a
     * crafting authored tomorrow narrates itself.
     */
    public static String effectLine(SpellDefinition spell) {
        StringBuilder out = new StringBuilder();
        out.append(spell.targetShape() == TargetShape.SELF ? "On you: " : "On them: ");
        for (int i = 0; i < spell.components().size(); i++) {
            if (i > 0) {
                out.append(", ");
            }
            out.append(clauseFor(spell.components().get(i)));
        }
        return out.toString();
    }

    /** One component in words: the signed amount, its unit, and how long it lasts. */
    private static String clauseFor(EffectComponent part) {
        int magnitude = part.magnitude();
        String sign = magnitude >= 0 ? "+" : "-";
        int size = Math.abs(magnitude);
        String amount = switch (part.kind()) {
            case TEMPERATURE -> sign + (size / 10) + "." + (size % 10) + " deg";
            case VITALITY -> sign + size + " hp";
            case ATTRIBUTE -> sign + size + " "
                    + com.trojia.sim.progression.AttributeId.values()[part.param()].name();
        };
        return amount + durationClause(part);
    }

    /** " for N min" / " every N min for M min" / "" — the ward clock, not raw ticks. */
    private static String durationClause(EffectComponent part) {
        return switch (part.mode()) {
            case INSTANT -> "";
            case WHILE_ACTIVE -> " for " + minutes(part.durationTicks()) + " min";
            case OVER_TIME -> " x" + Math.max(1,
                    part.durationTicks() / ActiveEffects.OVER_TIME_PERIOD_TICKS)
                    + " over " + minutes(part.durationTicks()) + " min";
        };
    }

    /** Ticks as ward minutes, rounded up to 1 so nothing ever reads as "0 min". */
    private static long minutes(int ticks) {
        return Math.max(1L,
                (long) ticks * 24 * 60 / com.trojia.sim.actor.DailyRhythm.DAY);
    }

    /** The toast for a crafting-outcome reason stamp, or {@code null} for any other reason. */
    public static String outcomeLine(ReasonCode reason) {
        if (reason == null) {
            return null;
        }
        return switch (reason) {
            case SPELL_WORKED -> WORKED;
            case SPELL_FIZZLED -> FIZZLED;
            case NO_LINK_TO_TARGET -> NO_LINK;
            default -> null;
        };
    }
}
