package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.spell.SpellDefinition;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.actor.spell.SpellVerb;

/**
 * WHY THE LINK REFUSED — the four reasons a press on the craftings bar cannot become a cast,
 * told apart on screen instead of collapsed into one sentence.
 *
 * <p>This is the {@code CullAvailability} lesson applied before it had to be learned twice. The
 * sim resolves every refused cast as a single {@code NO_LINK_TO_TARGET} stamp, which would read
 * to a player as the verb being broken: press a button that is plainly lit, get "nothing within
 * reach" back, with a neighbour standing right there. So the reasons the CLIENT can see are
 * refused here, before anything is armed, with the reason that actually applies — and no
 * reaching-out is narrated for an attempt that never began.
 *
 * <p>Reads only: no sim writes, no draws, nothing that could move a tick hash. The sim's own
 * stamp still lands for the case only the sim can see (the neighbour stepped away between the
 * press and the resolving tick).
 */
public final class SpellAvailability {

    /** Nobody is being driven: the bar is a spectator's window until you step into a body. */
    public static final String NO_BODY = "Step into a body before you work a crafting.";

    /** This body does not read at all — a gull cannot use a library. */
    public static final String ILLITERATE = "Yours are not the hands for that work.";

    /** The reader has not got deep enough into the shelf for this one. */
    public static final String UNLEARNED_PREFIX = "You have not read deeply enough -- ";

    /** The hand is still full of the last link. */
    public static final String COOLDOWN_PREFIX = "Your hands are still on the last link -- ready in ";

    /** Nothing this crafting could reach. */
    public static final String NO_TARGET = "Nothing within reach of that link.";

    /**
     * The ward is already holding every crafting it can hold. Said out loud rather than quietly
     * evicting somebody else's live effect to make space (the sim's {@code NO_ROOM_FOR_CRAFTING}
     * stamp) — a held warmth that vanishes for reasons nobody can see is the same lie as a cast
     * that lands nothing.
     */
    public static final String NO_ROOM = "Too many craftings are already working; nothing else"
            + " will hold.";

    private SpellAvailability() {
    }

    /**
     * The display name of the skill a crafting is gated on — read off the SPELL's raws row, so
     * the refusal names whatever skill actually governs it. Degrades to the raws key when the
     * skill table is unwired.
     */
    private static String skillName(SkillTrackRegistry tracks, SpellDefinition spell) {
        int raw = tracks.rawOfSkill(spell.skillKey());
        return raw == Actor.NONE ? spell.skillKey() : tracks.skills().get(raw).displayName();
    }

    /** Ticks left on this actor's crafting latch at {@code tick}; {@code 0} when the hand is free. */
    public static long cooldownTicksLeft(Actor self, long tick) {
        return Math.max(0L, self.castUntilTick() - tick);
    }

    /**
     * The latch, sized in the clock the HUD already shows: the {@link DailyRhythm#DAY}-tick day
     * reads as 24h, so a 500-tick latch is half an hour of ward time. Rounds up to 1 min so a
     * nearly-spent latch never reads as "0 min" (the {@code CullAvailability} wording).
     */
    public static String cooldownLine(long ticksLeft) {
        long minutes = Math.max(1L, ticksLeft * 24 * 60 / DailyRhythm.DAY);
        return COOLDOWN_PREFIX + minutes + " min.";
    }

    /**
     * The sentence to toast INSTEAD of arming a cast, or {@code null} when the press may go
     * through.
     *
     * <p><b>This method owns the WORDING and nothing else.</b> It used to own the rules too — its
     * own literacy read, its own level comparison, its own latch arithmetic, its own reach probe —
     * running alongside a second copy in {@code PlayerControlPolicy} and a shared verb that
     * enforced neither. The ladder now lives in {@link SpellVerb#refusalFor}, the sim's own, and
     * this translates the {@link ReasonCode} it returns into the sentence a player reads. The two
     * cannot disagree about WHETHER a press is refused, because there is only one of them.
     */
    public static String refusal(ActorRegistry registry, SpellRegistry spells,
            SkillTrackRegistry tracks, Actor caster, int spellRaw, long tick) {
        if (!spells.isValidRaw(spellRaw)) {
            return NO_TARGET;
        }
        SpellDefinition spell = spells.get(spellRaw);
        // The body the sim would pick for this press -- the same rule the arming path uses, so
        // the reach clause below is asked about the body that would actually be linked to.
        int target = SpellVerb.targetInReach(caster, registry, spell);
        ReasonCode refusal = SpellVerb.refusalFor(caster, registry, tracks, spells, spellRaw,
                target, tick);
        if (refusal == null) {
            return null;
        }
        return switch (refusal) {
            case CANNOT_READ_A_CRAFTING -> ILLITERATE;
            case CRAFTING_UNREAD -> UNLEARNED_PREFIX + skillName(tracks, spell) + " "
                    + spell.minLevel() + " for " + spell.displayName() + ".";
            case CRAFTING_HAND_LATCHED -> cooldownLine(cooldownTicksLeft(caster, tick));
            case NO_ROOM_FOR_CRAFTING -> NO_ROOM;
            default -> NO_TARGET;
        };
    }
}
