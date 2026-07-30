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
 * <p><b>Two gates, both canon, and the SPELL names the skill for both.</b> Every
 * {@link SpellDefinition} carries its governing skill as a raws key, and that skill decides
 * whether you may work the crafting at all ({@link SpellDefinition#minLevel} — the public shelf
 * is "overly general and skimmed over most of the details" and the real edition is on the top
 * shelf, L2472) AND how well it goes (the {@code check.linkcraft} draw through the ONE
 * {@link SkillChecks#successPermille} function). Nothing in this file hardcodes LINKCRAFT: a
 * crafting authored against a different skill tomorrow is gated by it, checked against it and
 * grows it, with no code change. The difficulty it is measured against is likewise not authored
 * per spell but computed from what the crafting actually moves and how far ({@link SpellCost}),
 * which is Gerik's lecture in integers.
 *
 * <p><b>What a cast may never do.</b> Put a body on the ground. {@link ActiveEffects}'s
 * vitality floor holds every target at 1 hp or better, so no crafting can spend the ecology's
 * budget, trip the death feed or feed the justice pipeline. Weak is not a tuning choice here —
 * it is a structural property of the only code that can write a hit point, and the same is true
 * of the ATTRIBUTE axis: {@link ActiveEffects#ATTRIBUTE_MODIFIER_LIMIT} holds the one axis that
 * reaches every check in the game to a couple of points however many rows are stacked on it.
 *
 * <p><b>What a cast may never BE.</b> A lie. Two guards run here before anything is charged —
 * the bodies are alive, and the effect table has room for what this crafting will file — and a
 * third ran long before, at load: every component is an (axis x time-shape) pairing that some
 * code in this sim actually reads ({@link EffectPairing}). A cast that resolves, charges a
 * resist and narrates success can no longer have changed nothing.
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
     * Factions that never work a crafting. The public shelf is a SHELF — "issued to the public
     * because it was overly general and skimmed over most of the battles and details" (L2472) —
     * so the gate is literacy, and a gull does not have it. Stated as data-shaped constants rather
     * than a type list so a new beast type inherits the exclusion for free — the
     * {@code CullVerb.NON_CULLING_FACTIONS} precedent.
     */
    private static final String[] ILLITERATE_FACTIONS = {"animals", "feral"};

    private SpellVerb() {
    }

    /**
     * Whether {@code self} could work {@code spellRaw} at all right now, ignoring targets and
     * the latch: it can read, it is alive, the spell exists, and it has read deeply enough
     * ({@link SpellDefinition#minLevel} against its live level in the spell's OWN declared
     * skill). Pure reads.
     */
    public static boolean canCast(Actor self, SkillTrackRegistry tracks, SpellRegistry spells,
            int spellRaw) {
        if (!spells.isValidRaw(spellRaw) || self.isDead() || !isLiterate(self)) {
            return false;
        }
        SpellDefinition spell = spells.get(spellRaw);
        return tracks.level(self.id(), tracks.rawOfSkill(spell.skillKey())) >= spell.minLevel();
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
     * WHY THIS CAST WOULD BE REFUSED, or {@code null} when it may go through — the ONE refusal
     * ladder, shared by {@link #resolveCast} and by every surface that wants to grey a button or
     * word a toast before anything is armed.
     *
     * <p><b>This exists because the rules used to live outside the verb.</b>
     * {@code resolveCast} is documented as THE ONE CAST PATH, shared by the play-mode verb and by
     * any AI caster grown later — and it enforced none of its own contract: not the level gate it
     * documents in its own class comment, not literacy, not the latch it stamps, and not the reach
     * that is canon's whole bridge rule. Every one of those lived in {@code PlayerControlPolicy}
     * and in the client's availability read, which meant the button was safe and the shared verb
     * was not: a test, a queued intent or the first AI caster could work a crafting its body had
     * never read, from across the ward, with its hand still full of the last link. The rules live
     * here now, and the callers ask.
     *
     * <p>Ordered so the reason a surface shows is the reason the sim would have hit: a crafting
     * that exists, hands that read, a reader deep enough, a free hand, a body in reach. Pure
     * reads — no draws, no writes, nothing that could move a tick hash.
     *
     * <p>The one refusal NOT in this ladder is {@link ReasonCode#NO_ROOM_FOR_CRAFTING}: it is a
     * property of the district's effect table rather than of this caster and this target, and it
     * is checked inside {@link #resolveCast} where the table is in hand.
     */
    public static ReasonCode refusalFor(Actor self, ActorRegistry registry,
            SkillTrackRegistry tracks, SpellRegistry spells, int spellRaw, int targetId,
            long tick) {
        if (!spells.isValidRaw(spellRaw) || self.isDead()) {
            return ReasonCode.NO_LINK_TO_TARGET;
        }
        if (!isLiterate(self)) {
            return ReasonCode.CANNOT_READ_A_CRAFTING;
        }
        SpellDefinition spell = spells.get(spellRaw);
        if (tracks.level(self.id(), tracks.rawOfSkill(spell.skillKey())) < spell.minLevel()) {
            return ReasonCode.CRAFTING_UNREAD;
        }
        if (tick < self.castUntilTick()) {
            return ReasonCode.CRAFTING_HAND_LATCHED;
        }
        if (!inReach(self, registry, spell, targetId)) {
            return ReasonCode.NO_LINK_TO_TARGET;
        }
        return null;
    }

    /**
     * Whether {@code targetId} sits inside this crafting's own reach — canon's bridge rule as a
     * predicate: alive, on the same z, and no further than {@link SpellDefinition#reach}. A
     * {@link TargetShape#SELF} crafting reaches exactly one body, its caster's, and nothing else:
     * pointing one at a neighbour used to be accepted AND priced at distance 0, so a SELF-shaped
     * crafting was the cheapest way to reach across a room.
     */
    public static boolean inReach(Actor self, ActorRegistry registry, SpellDefinition spell,
            int targetId) {
        if (targetId < 0 || targetId >= registry.size()) {
            return false;
        }
        if (spell.targetShape() == TargetShape.SELF) {
            return targetId == self.id();
        }
        Actor body = registry.get(targetId);
        return !body.isDead()
                && PackedPos.z(body.cell()) == PackedPos.z(self.cell())
                && ActorGeometry.chebyshev(self.cell(), body.cell()) <= spell.reach();
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
     * <p><b>It enforces its own rules.</b> Every refusal runs through {@link #refusalFor} — the
     * one ladder the client's availability read now shares — so the shared verb refuses exactly
     * what the button refuses, and a caller that forgets a rule cannot get past one. Refusals
     * cost the caster nothing at all: no latch, no use-XP, no resist, no row.
     *
     * @return {@code true} if the crafting took
     */
    public static boolean resolveCast(Actor self, ActorContext ctx, int spellRaw, int targetId) {
        // EVERY RULE THIS VERB DOCUMENTS, ENFORCED BY THIS VERB, before anything is charged: the
        // spell exists, the body reads, the reader is deep enough, the hand is free, and the
        // target is a living body inside the link's reach. This is the shared public verb --
        // reached by the play-mode path, by a test, by a queued intent that outlived its target,
        // and by whatever AI caster is grown next -- so a rule that lives only in a caller is a
        // rule that only one caller has.
        ReasonCode refusal = refusalFor(self, ctx.registry(), ctx.skillTracks(), ctx.spells(),
                spellRaw, targetId, ctx.tick());
        if (refusal != null) {
            self.setLastReasonCode(refusal);
            return false;
        }
        SpellDefinition spell = ctx.spells().get(spellRaw);
        // ROOM, also before anything is charged. Filing an effect used to evict somebody else's
        // live row to make space, silently; now a cast that will not fit is refused out loud and
        // costs the caster nothing at all -- no latch, no XP, no resist.
        if (ctx.activeEffects().freeSlots() < rowsNeeded(spell, ctx, targetId)) {
            self.setLastReasonCode(ReasonCode.NO_ROOM_FOR_CRAFTING);
            return false;
        }
        self.setCastUntilTick(ctx.tick() + spell.cooldownTicks());
        SkillTrackRegistry tracks = ctx.skillTracks();
        // The crafting's OWN skill, named in its raws row -- never a hardcoded one. A spell
        // authored against a different skill tomorrow is gated by it, checked against it and
        // grows it, with no code change here.
        int skillRaw = tracks.rawOfSkill(spell.skillKey());
        tracks.award(self.id(), skillRaw, CAST_ATTEMPT_CP, spellRaw, ctx.tick());
        // The REAL gap, always. A SELF crafting reaches its own caster and nothing else (the
        // reach guard above), so this reads 0 for one without special-casing the shape -- and a
        // SELF-shaped crafting aimed across a room can no longer be priced as if it were not.
        int distance = ActorGeometry.chebyshev(self.cell(), ctx.registry().get(targetId).cell());
        long resist = SpellCost.resistFor(spell, distance);
        long draw = ctx.draw(ActorRngStream.CHECK_LINKCRAFT, self.id(),
                ctx.nextDrawIndex(self.id()));
        if (!SkillChecks.passes(draw,
                SkillChecks.craftingPermille(tracks, self.id(), skillRaw, resist))) {
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

    /**
     * How many {@link ActiveEffects} rows one resolution of this crafting would file: its
     * lingering parts, once for the target and once for every body its spread would also catch.
     * Read-only, draw-free, ascending-slot — the same scan {@link #spread} makes, run first so
     * the cast can be refused before it costs anybody anything.
     */
    private static int rowsNeeded(SpellDefinition spell, ActorContext ctx, int targetId) {
        int perBody = spell.lingeringPartCount();
        if (perBody == 0) {
            return 0;
        }
        int bodies = 1;
        if (spell.areaRadius() > 0) {
            ActorRegistry registry = ctx.registry();
            int centre = registry.get(targetId).cell();
            int centreZ = PackedPos.z(centre);
            for (int i = 0; i < registry.size(); i++) {
                Actor other = registry.get(i);
                if (i != targetId && !other.isDead() && PackedPos.z(other.cell()) == centreZ
                        && ActorGeometry.chebyshev(centre, other.cell()) <= spell.areaRadius()) {
                    bodies++;
                }
            }
        }
        return perBody * bodies;
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
