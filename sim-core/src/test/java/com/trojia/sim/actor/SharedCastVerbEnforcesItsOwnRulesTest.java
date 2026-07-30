package com.trojia.sim.actor;

import com.trojia.sim.actor.spell.ActiveEffects;
import com.trojia.sim.actor.spell.EffectComponent;
import com.trojia.sim.actor.spell.EffectKind;
import com.trojia.sim.actor.spell.EffectMode;
import com.trojia.sim.actor.spell.SpellDefinition;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.actor.spell.SpellVerb;
import com.trojia.sim.actor.spell.TargetShape;
import com.trojia.sim.actor.type.AnimalActor;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;

/**
 * THE SHARED VERB ENFORCES ITS OWN CONTRACT — every rule {@code SpellVerb} documents, refused by
 * {@code SpellVerb}, charging nothing.
 *
 * <p><b>What used to happen.</b> {@code resolveCast} is documented as THE ONE CAST PATH, shared
 * by the play-mode verb and by anything that grows an AI caster later. It enforced none of the
 * rules it documents: not the {@code minLevel} literacy tier it names in its own class comment,
 * not the literacy gate beside it, not the cast latch it stamps two lines later, and not the reach
 * that is canon's whole bridge rule. All four lived in {@code PlayerControlPolicy} and in the
 * client's availability read — so the BUTTON was safe and the shared verb was not. Anything that
 * called it directly could work a crafting its body had never read, on a body across the ward,
 * with its hand still full of the last link. It also priced a {@link TargetShape#SELF} crafting
 * aimed at somebody else at distance 0, which made the SELF shape the cheapest way to reach.
 *
 * <p><b>What this file pins.</b> One case per rule, each asserting the full refusal contract: the
 * cast returns false, the reason code names the rule that stopped it, and the caster is charged
 * NOTHING — no use-XP, no latch, no row in the effect table.
 */
final class SharedCastVerbEnforcesItsOwnRulesTest {

    private static final int Z = 10;
    private static final int HERE = PackedPos.pack(50, 50, Z);
    private static final int NEXT_DOOR = PackedPos.pack(51, 50, Z);
    private static final int ACROSS_THE_WARD = PackedPos.pack(80, 50, Z);
    private static final int UPSTAIRS = PackedPos.pack(51, 50, Z + 3);

    // ==================================================================
    // One case per rule
    // ==================================================================

    /** A gull cannot use a library — and now cannot get past the shared verb either. */
    @Test
    void aBodyThatCannotReadIsRefusedByTheVerbAndNotByTheButtonAlone() {
        ActorRegistry registry = new ActorRegistry();
        Actor gull = registry.spawn(AnimalActor.TYPE,
                ActorTestFixtures.statsWithFaction(AnimalActor.TYPE, "animals"), HERE);
        Actor neighbour = spawn(registry, NEXT_DOOR);
        VerbContext ctx = new VerbContext(registry, shelf());

        assertFalse(SpellVerb.isLiterate(gull), "the fixture must actually bake an illiterate");
        assertRefusedAndFree(gull, ctx, raw(ctx, "touch_warmth"), neighbour.id(),
                ReasonCode.CANNOT_READ_A_CRAFTING);
    }

    /** The restricted edition is on the top shelf, for the verb as well as for the bar. */
    @Test
    void aCraftingTheReaderHasNotGotDeepEnoughForIsRefusedByTheVerb() {
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor neighbour = spawn(registry, NEXT_DOOR);
        VerbContext ctx = new VerbContext(registry, shelf());
        int deep = raw(ctx, "deep_touch_warmth");

        assertFalse(SpellVerb.canCast(caster, ctx.tracks, ctx.spells(), deep));
        assertRefusedAndFree(caster, ctx, deep, neighbour.id(), ReasonCode.CRAFTING_UNREAD);

        // ...and it comes down the moment the reader is deep enough for it.
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(), 5);
        assertNull(SpellVerb.refusalFor(caster, registry, ctx.tracks, ctx.spells(), deep,
                neighbour.id(), ctx.tick()), "the gate is a gate, not a wall");
    }

    /** The latch the verb STAMPS is now the latch the verb READS. */
    @Test
    void aHandStillOnTheLastLinkIsRefusedByTheVerb() {
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor neighbour = spawn(registry, NEXT_DOOR);
        VerbContext ctx = new VerbContext(registry, shelf());
        ctx.setTick(1_000L);
        caster.setCastUntilTick(1_600L);

        int grainsBefore = ctx.tracks.progressGrains(caster.id(), ctx.tracks.linkcraftRaw());
        assertFalse(SpellVerb.resolveCast(caster, ctx, raw(ctx, "touch_warmth"),
                neighbour.id()));
        assertEquals(ReasonCode.CRAFTING_HAND_LATCHED, caster.lastReasonCode());
        assertEquals(1_600L, caster.castUntilTick(),
                "a refused cast does not re-stamp the latch it was refused for");
        assertEquals(grainsBefore, ctx.tracks.progressGrains(caster.id(),
                ctx.tracks.linkcraftRaw()), "and pays no use-XP");
        assertEquals(0, ctx.effects.liveCount());
    }

    /** Canon's bridge rule, as a rule of the verb: an arm's length is an arm's length. */
    @Test
    void aBodyOutOfTheLinksReachIsRefusedByTheVerb() {
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor faraway = spawn(registry, ACROSS_THE_WARD);
        VerbContext ctx = new VerbContext(registry, shelf());

        assertRefusedAndFree(caster, ctx, raw(ctx, "touch_warmth"), faraway.id(),
                ReasonCode.NO_LINK_TO_TARGET);
    }

    /** A body on another floor is not touchable, whatever the map distance reads. */
    @Test
    void aBodyOnAnotherFloorIsRefusedByTheVerb() {
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor upstairs = spawn(registry, UPSTAIRS);
        VerbContext ctx = new VerbContext(registry, shelf());

        assertRefusedAndFree(caster, ctx, raw(ctx, "touch_warmth"), upstairs.id(),
                ReasonCode.NO_LINK_TO_TARGET);
    }

    /**
     * A SELF-SHAPED CRAFTING AIMED AT SOMEBODY ELSE. This used to resolve — and be priced at
     * distance 0, because the distance term read the SHAPE instead of the geometry — which made
     * the SELF shape the cheapest reach in the vocabulary. It is a refusal now, and the pricing
     * reads the real gap.
     */
    @Test
    void aSelfCraftingAimedAtSomebodyElseIsRefusedRatherThanPricedAtZero() {
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor neighbour = spawn(registry, NEXT_DOOR);
        VerbContext ctx = new VerbContext(registry, shelf());
        int inward = raw(ctx, "self_warmth");

        assertRefusedAndFree(caster, ctx, inward, neighbour.id(), ReasonCode.NO_LINK_TO_TARGET);
        // ...and on its own caster it still works, at the gap that actually exists (zero).
        assertNull(SpellVerb.refusalFor(caster, registry, ctx.tracks, ctx.spells(), inward,
                caster.id(), ctx.tick()));
    }

    /** An unknown spell raw never reaches the registry lookup that would throw on it. */
    @Test
    void anUnknownSpellRawIsRefusedRatherThanThrown() {
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        VerbContext ctx = new VerbContext(registry, shelf());

        assertRefusedAndFree(caster, ctx, 9_999, caster.id(), ReasonCode.NO_LINK_TO_TARGET);
        assertRefusedAndFree(caster, ctx, Actor.NONE, caster.id(), ReasonCode.NO_LINK_TO_TARGET);
    }

    /** A dead caster works nothing, the {@code CullVerb} precondition idiom. */
    @Test
    void aDeadCasterIsRefusedByTheVerb() {
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor neighbour = spawn(registry, NEXT_DOOR);
        VerbContext ctx = new VerbContext(registry, shelf());
        caster.setStatus(StatusBit.DEAD, true);

        assertFalse(SpellVerb.resolveCast(caster, ctx, raw(ctx, "touch_warmth"),
                neighbour.id()));
        assertEquals(ReasonCode.NO_LINK_TO_TARGET, caster.lastReasonCode());
        assertEquals(0L, caster.castUntilTick());
        assertEquals(0, ctx.effects.liveCount());
    }

    /**
     * THE LADDER IS THE SAME LADDER. Every refusal the verb can name is a refusal the verb
     * REFUSES — this walks the whole ladder in one place so a rule added to
     * {@link SpellVerb#refusalFor} tomorrow cannot be a rule {@code resolveCast} skips.
     */
    @Test
    void everyReasonTheLadderCanNameIsAReasonTheCastActuallyRefuses() {
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor neighbour = spawn(registry, NEXT_DOOR);
        VerbContext ctx = new VerbContext(registry, shelf());
        int touch = raw(ctx, "touch_warmth");

        // Free hand, read deeply enough, neighbour in reach: the ladder passes.
        assertNull(SpellVerb.refusalFor(caster, registry, ctx.tracks, ctx.spells(), touch,
                neighbour.id(), ctx.tick()));

        // Every state that makes the ladder speak also makes the cast refuse, with that reason.
        caster.setCastUntilTick(ctx.tick() + 50);
        ReasonCode said = SpellVerb.refusalFor(caster, registry, ctx.tracks, ctx.spells(), touch,
                neighbour.id(), ctx.tick());
        assertNotNull(said);
        assertFalse(SpellVerb.resolveCast(caster, ctx, touch, neighbour.id()));
        assertEquals(said, caster.lastReasonCode(),
                "the verb must stamp the code its own ladder named");
    }

    // ==================================================================
    // helpers
    // ==================================================================

    /** The refusal contract in one place: refused, named, and charged nothing at all. */
    private static void assertRefusedAndFree(Actor caster, VerbContext ctx, int spellRaw,
            int targetId, ReasonCode expected) {
        int linkcraft = ctx.tracks.linkcraftRaw();
        int grainsBefore = ctx.tracks.progressGrains(caster.id(), linkcraft);
        long latchBefore = caster.castUntilTick();

        assertFalse(SpellVerb.resolveCast(caster, ctx, spellRaw, targetId),
                "the shared verb must refuse this");
        assertEquals(expected, caster.lastReasonCode(), "and name the rule that stopped it");
        assertEquals(grainsBefore, ctx.tracks.progressGrains(caster.id(), linkcraft),
                "a refusal pays no use-XP");
        assertEquals(latchBefore, caster.castUntilTick(), "a refusal stamps no latch");
        assertEquals(0, ctx.effects.liveCount(), "a refusal files no row");
    }

    /**
     * A three-row shelf composed in Java, so this file tests the VERB rather than the shipped
     * content: a self-target warmth, its touch twin, and a touch twin gated deep.
     */
    private static SpellRegistry shelf() {
        List<EffectComponent> warmth = List.of(new EffectComponent(EffectKind.TEMPERATURE,
                EffectMode.WHILE_ACTIVE, 15, 0, 600));
        return SpellRegistry.of(List.of(
                new SpellDefinition("self_warmth", "Self Warmth", "linkcraft", 0, 0, 200,
                        TargetShape.SELF, 0, 0, warmth),
                new SpellDefinition("touch_warmth", "Touch Warmth", "linkcraft", 0, 0, 200,
                        TargetShape.TOUCH, 1, 0, warmth),
                new SpellDefinition("deep_touch_warmth", "Deep Touch Warmth", "linkcraft", 5, 0,
                        200, TargetShape.TOUCH, 1, 0, warmth)));
    }

    private static int raw(VerbContext ctx, String key) {
        return ctx.spells().rawOf(key);
    }

    private static Actor spawn(ActorRegistry registry, int cell) {
        return registry.spawn(Serf.TYPE, ActorTestFixtures.statsWithDefer(Serf.TYPE, true), cell);
    }

    /** A context with a wired skill table, a real effect table and a composed spell universe. */
    private static final class VerbContext extends NoOpActorContext {
        final SkillTrackRegistry tracks = new SkillTrackRegistry(
                com.trojia.sim.progression.SkillRawsLoader.load(RawsDir.locate()));
        final ActiveEffects effects = new ActiveEffects();
        private final SpellRegistry spells;

        VerbContext(ActorRegistry registry, SpellRegistry spells) {
            super(registry);
            this.spells = spells;
            tracks.bindActiveEffects(effects);
        }

        @Override
        public SkillTrackRegistry skillTracks() {
            return tracks;
        }

        @Override
        public SpellRegistry spells() {
            return spells;
        }

        @Override
        public ActiveEffects activeEffects() {
            return effects;
        }
    }
}
