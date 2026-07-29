package com.trojia.sim.actor.spell;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorContext;
import com.trojia.sim.actor.ActorGeometry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorRngStream;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillChecks;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.world.PackedPos;

/**
 * THE ONE CAST PATH — the whole of "work a crafting", for every spell that exists and every
 * spell that ever will. There is no per-spell branch anywhere in this file: it reads a
 * {@link SpellDefinition} off the registry and does what the data says, which is the entire
 * reason the vocabulary was built before the spells.
 *
 * <p><b>The shape is the ward's, not magic's.</b> Deliberately the {@code CullVerb} silhouette,
 * line for line: a pure {@code canCast} raws predicate, a read-only draw-free
 * {@code targetInReach} probe, and ONE {@code resolveCast} shared by every caller. XP lands on
 * the ATTEMPT, not the win. The latch is stamped either way, because the attempt is what costs
 * the time. Both branches set a reason code. A cast therefore reads on screen exactly like a
 * cull or a cast of a line — same check line, same toast idiom, same visible dice.
 *
 * <p><b>Two gates, both canon.</b> The skill decides whether you may work a crafting at all
 * ({@link SpellDefinition#minLevel} — the public shelf is "overly general and skimmed over most
 * of the details" and the real edition is on the top shelf, L2472) and how well it goes (the
 * {@code check.linkcraft} draw through the ONE {@link SkillChecks#successPermille} function).
 * The difficulty it is measured against is not authored per spell but computed from what the
 * crafting actually moves and how far ({@link SpellCost}), which is Gerik's lecture in integers.
 *
 * <p><b>What a cast may never do.</b> Put a body on the ground. {@link ActiveEffects}'s
 * vitality floor holds every target at 1 hp or better, so no crafting can spend the ecology's
 * budget, trip the death feed or feed the justice pipeline. Weak is not a tuning choice here —
 * it is a structural property of the only code that can write a hit point.
 */
public final class SpellVerb {

    /**
     * Linkcraft cp per cast ATTEMPT — worked or fizzled, exactly like the fishing cast's and the
     * cull's attempt-XP. Priced at the same scale so no verb is the obviously better grind.
     * Satiated per SPELL (the context key is the spell's raw index), so grinding one crafting
     * decays to the §3.3 floor while a crafting you have not worked before still pays full: the
     * ward rewards a broad reader, which is the premise of the whole system.
     */
    public static final int CAST_ATTEMPT_CP = 60;

    /**
     * Factions that never work a crafting. The libraries were opened "to any who can READ", so
     * the gate is literacy and a gull does not have it. Stated as data-shaped constants rather
     * than a type list so a new beast type inherits the exclusion for free — the
     * {@code CullVerb.NON_CULLING_FACTIONS} precedent.
     */
    private static final String[] ILLITERATE_FACTIONS = {"animals", "feral"};

    private SpellVerb() {
    }

    /**
     * Whether {@code self} could work {@code spellRaw} at all right now, ignoring targets and
     * the latch: it can read, it is alive, the spell exists, and it has read deeply enough
     * ({@link SpellDefinition#minLevel} against its live linkcraft level). Pure reads.
     */
    public static boolean canCast(Actor self, SkillTrackRegistry tracks, SpellRegistry spells,
            int spellRaw) {
        if (!spells.isValidRaw(spellRaw) || self.isDead() || !isLiterate(self)) {
            return false;
        }
        return tracks.level(self.id(), tracks.linkcraftRaw())
                >= spells.get(spellRaw).minLevel();
    }

    /** Whether this body belongs to a faction that reads (the class doc's literacy gate). */
    public static boolean isLiterate(Actor self) {
        String faction = self.stats().factionId();
        for (String illiterate : ILLITERATE_FACTIONS) {
            if (illiterate.equals(faction)) {
                return false;
            }
        }
        return true;
    }

    /**
     * The body this spell would land on from where {@code self} is standing, or
     * {@link Actor#NONE}: itself for a {@link TargetShape#SELF} crafting, otherwise the
     * LOWEST-id living body within the spell's reach on the same z (the district's universal
     * tiebreak — the {@code AdjacentTargets} / {@code TheftMechanics.nearestMark} convention).
     * Read-only and draw-free, so a caller may probe before committing.
     */
    public static int targetInReach(Actor self, ActorRegistry registry, SpellDefinition spell) {
        if (spell.targetShape() == TargetShape.SELF) {
            return self.id();
        }
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        for (int i = 0; i < registry.size(); i++) {
            if (i == self.id()) {
                continue;
            }
            Actor other = registry.get(i);
            if (other.isDead() || PackedPos.z(other.cell()) != selfZ) {
                continue;
            }
            if (ActorGeometry.chebyshev(selfCell, other.cell()) <= spell.reach()) {
                return i;
            }
        }
        return Actor.NONE;
    }

    /**
     * Resolves ONE cast of {@code spellRaw} on {@code targetId} — the single resolution path
     * shared by the play-mode verb and by anything that grows an AI caster later (the
     * {@code resolveCull} precedent). Linkcraft cp on the attempt, the latch stamped either
     * way, the {@code check.linkcraft} named draw against {@link SpellCost}'s computed resist,
     * and on success every component landed on the target and on anything inside the spell's
     * area.
     *
     * @return {@code true} if the crafting took
     */
    public static boolean resolveCast(Actor self, ActorContext ctx, int spellRaw, int targetId) {
        SpellDefinition spell = ctx.spells().get(spellRaw);
        self.setCastUntilTick(ctx.tick() + spell.cooldownTicks());
        SkillTrackRegistry tracks = ctx.skillTracks();
        tracks.award(self.id(), tracks.linkcraftRaw(), CAST_ATTEMPT_CP, spellRaw, ctx.tick());
        int distance = spell.targetShape() == TargetShape.SELF
                ? 0
                : ActorGeometry.chebyshev(self.cell(), ctx.registry().get(targetId).cell());
        int resist = SpellCost.resistFor(spell, distance);
        long draw = ctx.draw(ActorRngStream.CHECK_LINKCRAFT, self.id(),
                ctx.nextDrawIndex(self.id()));
        if (!SkillChecks.passes(draw, SkillChecks.linkcraftPermille(tracks, self.id(), resist))) {
            self.setLastReasonCode(ReasonCode.SPELL_FIZZLED);
            return false;
        }
        land(spell, ctx, targetId);
        if (spell.areaRadius() > 0) {
            spread(spell, ctx, targetId);
        }
        self.setLastReasonCode(ReasonCode.SPELL_WORKED);
        return true;
    }

    /** Lands every component of {@code spell} on one body. */
    private static void land(SpellDefinition spell, ActorContext ctx, int targetId) {
        Actor target = ctx.registry().get(targetId);
        ActiveEffects effects = ctx.activeEffects();
        for (int i = 0; i < spell.components().size(); i++) {
            EffectComponent part = spell.components().get(i);
            if (part.mode() == EffectMode.INSTANT) {
                ActiveEffects.applyOnce(target, part.kind(), part.param(), part.magnitude());
            } else {
                effects.add(targetId, part.kind(), part.mode(), part.param(), part.magnitude(),
                        ctx.tick(), ctx.tick() + part.durationTicks());
            }
        }
    }

    /**
     * Lands the same components on every OTHER living body within {@link
     * SpellDefinition#areaRadius} of the target, same z, ascending id. The area axis exists in
     * the vocabulary so a spellcrafting screen can offer it; the ward's own list authors it at
     * 0 everywhere, because a common hand cannot afford the spread its resist costs.
     */
    private static void spread(SpellDefinition spell, ActorContext ctx, int targetId) {
        ActorRegistry registry = ctx.registry();
        int centre = registry.get(targetId).cell();
        int centreZ = PackedPos.z(centre);
        for (int i = 0; i < registry.size(); i++) {
            if (i == targetId) {
                continue;
            }
            Actor other = registry.get(i);
            if (other.isDead() || PackedPos.z(other.cell()) != centreZ) {
                continue;
            }
            if (ActorGeometry.chebyshev(centre, other.cell()) <= spell.areaRadius()) {
                land(spell, ctx, i);
            }
        }
    }
}
