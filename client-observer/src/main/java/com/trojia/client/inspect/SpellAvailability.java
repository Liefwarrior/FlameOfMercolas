package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.DailyRhythm;
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
    public static final String UNLEARNED_PREFIX = "You have not read deeply enough -- Linkcraft ";

    /** The hand is still full of the last link. */
    public static final String COOLDOWN_PREFIX = "Your hands are still on the last link -- ready in ";

    /** Nothing this crafting could reach. */
    public static final String NO_TARGET = "Nothing within reach of that link.";

    private SpellAvailability() {
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
     * through. Checked in the sim's own order — can this body work a crafting at all, has it
     * read this one, is its hand free, is anything in reach — so the reason on screen is the
     * reason the sim would have hit.
     */
    public static String refusal(ActorRegistry registry, SpellRegistry spells,
            SkillTrackRegistry tracks, Actor caster, int spellRaw, long tick) {
        if (!spells.isValidRaw(spellRaw)) {
            return NO_TARGET;
        }
        SpellDefinition spell = spells.get(spellRaw);
        if (!SpellVerb.isLiterate(caster)) {
            return ILLITERATE;
        }
        if (!SpellVerb.canCast(caster, tracks, spells, spellRaw)) {
            return UNLEARNED_PREFIX + spell.minLevel() + " for " + spell.displayName() + ".";
        }
        long left = cooldownTicksLeft(caster, tick);
        if (left > 0) {
            return cooldownLine(left);
        }
        if (SpellVerb.targetInReach(caster, registry, spell) == Actor.NONE) {
            return NO_TARGET;
        }
        return null;
    }
}
