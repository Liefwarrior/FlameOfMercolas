package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.inspect.PersonNames;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.SpellAvailability;
import com.trojia.client.inspect.SpellBar;
import com.trojia.client.inspect.SpellFeedbackTracker;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorGeometry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.spell.SpellDefinition;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.actor.spell.SpellVerb;
import com.trojia.sim.actor.spell.TargetShape;

import java.util.List;

/**
 * Polls the CRAFTINGS BAR (Simple Magic — the buttons down the right of the screen): a left
 * click inside a button arms the played soul's cast intent for that crafting, and {@code X}
 * repeats the last one worked. The SIM resolves it next tick inside
 * {@code PlayerControlPolicy.act} through the same shared {@code SpellVerb.resolveCast} any
 * future AI caster will use — LINKCRAFT XP on the attempt, the {@code check.linkcraft} draw
 * against the difficulty {@code SpellCost} computes, the components landed on success, the
 * per-caster latch stamped either way.
 *
 * <p><b>The click is the whole verb.</b> Eli's brief was "click on an actor and have three
 * different effects": select a body with the ordinary inspector click, then click a crafting.
 * Which body a crafting lands on is the sim's own {@link SpellVerb#targetInReach} rule (self
 * for a SELF crafting, the lowest-id body within the link's reach otherwise) — the same rule
 * the AI would use, so the button path and any future policy path can never disagree.
 *
 * <p>{@code X} was unbound (the verify-free-then-bind rule; taken keys: SPACE F PERIOD G T P I
 * C E N J L M R K B WASD arrows brackets ESC). Mirrors {@link CullInput}'s shape exactly: a thin
 * {@code Gdx.input} wrapper around the deterministic {@link #applyCast} seam the
 * scripted-playtest tape drives keyless.
 */
public final class SpellInput {

    /** The arming toast for a crafting worked on your own body. */
    public static final String ARM_SELF_PREFIX = "You turn the link inward -- ";

    /** The arming toast for a crafting worked on somebody else, who gets named. */
    public static final String ARM_TOUCH_PREFIX = "You reach toward ";

    /** {@code X} pressed with nothing worked yet this session. */
    public static final String NOTHING_TO_REPEAT = "No crafting to repeat yet.";

    private SpellInput() {
    }

    /**
     * Applies one frame's craftings-bar input. Returns {@code true} if this call consumed the
     * frame's left click (a button was hit) — the caller then skips the world-picking input for
     * that frame, so a click on the bar never also reselects whatever tile is behind it.
     *
     * @param lastCast a one-slot memory of the last crafting worked, for the {@code X} repeat
     */
    public static boolean poll(PlayModeState playMode, ActorRegistry registry,
            IdentityRegistry identity, SpellRegistry spells, SkillTrackRegistry tracks,
            List<SpellBar.Button> buttons, ToastQueue toasts, SpellFeedbackTracker feedback,
            long tick, float viewportHeightPx, LastCast lastCast) {
        if (Gdx.input.isKeyJustPressed(Input.Keys.X)) {
            if (lastCast.spellRaw == Actor.NONE) {
                toasts.add(NOTHING_TO_REPEAT);
            } else {
                applyCast(playMode, registry, identity, spells, tracks, lastCast.spellRaw,
                        toasts, feedback, tick);
            }
        }
        if (!Gdx.input.isButtonJustPressed(Input.Buttons.LEFT)) {
            return false;
        }
        int hit = SpellBar.hitTestScreen(buttons, Gdx.input.getX(), Gdx.input.getY(),
                viewportHeightPx);
        if (hit == Actor.NONE) {
            return false;
        }
        lastCast.spellRaw = hit;
        applyCast(playMode, registry, identity, spells, tracks, hit, toasts, feedback, tick);
        return true;
    }

    /** A one-slot memory of the crafting {@code X} repeats. Plain mutable holder, client-side. */
    public static final class LastCast {
        /** The last crafting a button press armed, or {@link Actor#NONE}. */
        public int spellRaw = Actor.NONE;
    }

    /**
     * The deterministic cast application (the {@code applyCull} convention): arms the sim-side
     * intent, toasts the attempt, and arms the feedback tracker so the outcome, what it did and
     * the check line all land as toasts after the resolving tick. A no-op while Play mode is
     * inactive, except for the one toast that says so.
     *
     * <p><b>A press that cannot work is refused HERE, with the reason that applies</b>
     * ({@link SpellAvailability}) — nothing is armed and no reaching-out is narrated for it.
     * The cull verb shipped the other way round once and it read as a broken verb; this one
     * does not repeat that.
     */
    public static void applyCast(PlayModeState playMode, ActorRegistry registry,
            IdentityRegistry identity, SpellRegistry spells, SkillTrackRegistry tracks,
            int spellRaw, ToastQueue toasts, SpellFeedbackTracker feedback, long tick) {
        if (!playMode.active()) {
            toasts.add(SpellAvailability.NO_BODY);
            return;
        }
        Actor caster = registry.get(playMode.playedActorId());
        String refusal = SpellAvailability.refusal(registry, spells, tracks, caster, spellRaw,
                tick);
        if (refusal != null) {
            toasts.add(refusal);
            return;
        }
        SpellDefinition spell = spells.get(spellRaw);
        int target = SpellVerb.targetInReach(caster, registry, spell);
        caster.setPlayerSpellIntent(spellRaw, target);
        toasts.add(armLine(spell, registry, identity, target));
        feedback.arm(spellRaw, distanceTo(caster, registry, spell, target));
    }

    /**
     * "You reach toward Onna Tidewatcher -- Sting..." — the target NAMED, so Eli's
     * click-an-actor-and-see-something-happen-to-them reads as exactly that. Named as the ward
     * sees them (presented identity — the Persona rule, the {@code TheftInput} precedent).
     */
    private static String armLine(SpellDefinition spell, ActorRegistry registry,
            IdentityRegistry identity, int target) {
        if (spell.targetShape() == TargetShape.SELF) {
            return ARM_SELF_PREFIX + spell.displayName() + "...";
        }
        return ARM_TOUCH_PREFIX + PersonNames.fullNameOf(
                registry.get(target).identity().presentedId(), registry, identity)
                + " -- " + spell.displayName() + "...";
    }

    /** The gap the sim will price this cast at — 0 for a SELF crafting. */
    private static int distanceTo(Actor caster, ActorRegistry registry, SpellDefinition spell,
            int target) {
        if (spell.targetShape() == TargetShape.SELF || target == Actor.NONE) {
            return 0;
        }
        return ActorGeometry.chebyshev(caster.cell(), registry.get(target).cell());
    }
}
