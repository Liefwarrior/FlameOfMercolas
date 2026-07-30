package com.trojia.sim.actor;

import com.trojia.sim.actor.spell.ActiveEffects;
import com.trojia.sim.actor.spell.EffectKind;
import com.trojia.sim.actor.spell.EffectMode;
import com.trojia.sim.actor.spell.SpellCost;
import com.trojia.sim.actor.spell.SpellDefinition;
import com.trojia.sim.actor.spell.SpellRawsLoader;
import com.trojia.sim.actor.spell.SpellRawsValidationException;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.actor.spell.SpellVerb;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.progression.AttributeId;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.progression.SkillRegistry;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * SIMPLE MAGIC, tested as a VOCABULARY rather than as a list of spells.
 *
 * <p>The load-bearing test in this file is
 * {@link #twoSpellsNobodyAuthoredWorkWithNoJavaAtAll()}: two craftings that exist in no raws
 * file and in no Java class, composed at runtime out of nothing but a JSON string, cast into
 * the live sim, and observed changing the world. If that ever needs a code change, the
 * vocabulary is wrong and the spellcrafting sprint is a rewrite.
 *
 * <p>Everything else here pins the properties that make the vocabulary safe to open up: the
 * cost model prices distance, transfer and spread (so an unreviewed spell cannot be strong),
 * the sign is the whole of opposition (so mend and harm are one row), the vitality floor is
 * structural (so no crafting can spend the ecology's budget), and a held attribute nudge is
 * felt by the ONE function every check in the game already reads.
 */
final class SpellcraftTest {

    /**
     * The skill universe every crafting in this file is gated against — a required argument to
     * the loader, because a spell names the skill that gates it, checks it and grows with it.
     */
    private static final SkillRegistry SKILLS = SkillRawsLoader.load(locateRawsDir());

    private static final int Z = 10;
    private static final int HERE = PackedPos.pack(50, 50, Z);
    private static final int NEXT_DOOR = PackedPos.pack(51, 50, Z);
    private static final int FOUR_AWAY = PackedPos.pack(54, 50, Z);

    /** A context with a real wired skill table, a real effect table, and a spell universe. */
    private static final class MagicContext extends NoOpActorContext {
        final SkillTrackRegistry tracks =
                new SkillTrackRegistry(SkillRawsLoader.load(locateRawsDir()));
        final ActiveEffects effects = new ActiveEffects();
        private SpellRegistry spells;

        MagicContext(ActorRegistry registry, SpellRegistry spells) {
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

    // ==================================================================
    // THE PROOF
    // ==================================================================

    /**
     * TWO CRAFTINGS NOBODY AUTHORED. Neither appears in {@code content/raws/spells/spells.json};
     * neither has a class, a branch, a constant or a line of Java anywhere. They are typed here
     * as raws text — a different axis, a different magnitude, a different duration, a different
     * time-shape, a different reach — handed to the same loader and the same verb the shipped
     * list uses, and they work.
     *
     * <p>The first is Eli's own test case, verbatim: <em>harm 3 for 10 ticks at range 4</em>.
     * The second is his other: <em>chill 2 for 200 ticks on self</em>. Between them they cover
     * every axis of the tuple the shipped list does not: an unbridged link, a real distance, a
     * trickle, and a self-target hold.
     */
    @Test
    void twoSpellsNobodyAuthoredWorkWithNoJavaAtAll() {
        SpellRegistry invented = SpellRawsLoader.parse(SKILLS, """
                {
                  "id": "spells",
                  "spells": [
                    {
                      "id": "probing_scald",
                      "displayName": "Probing Scald",
                      "skill": "linkcraft",
                      "target": "RANGED",
                      "range": 4,
                      "components": [
                        { "effect": "VITALITY", "mode": "OVER_TIME",
                          "magnitude": -3, "durationTicks": 10 }
                      ]
                    },
                    {
                      "id": "deep_chill",
                      "displayName": "Deep Chill",
                      "skill": "linkcraft",
                      "target": "SELF",
                      "components": [
                        { "effect": "TEMPERATURE", "mode": "WHILE_ACTIVE",
                          "magnitude": -20, "durationTicks": 200 }
                      ]
                    }
                  ]
                }
                """);
        assertEquals(2, invented.size(), "two craftings, written as data and nothing else");

        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor victim = spawn(registry, FOUR_AWAY);
        MagicContext ctx = new MagicContext(registry, invented);
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(), 40);

        // ---- "harm 3 for 10 ticks at range 4" ----------------------------------------
        int scaldRaw = invented.rawOf("probing_scald");
        assertEquals(4, ActorGeometry.chebyshev(caster.cell(), victim.cell()),
                "the victim is a genuine four tiles off, with no arm and no blade between");
        short hpBefore = victim.hp();
        castUntilItWorks(caster, ctx, scaldRaw, victim.id());
        assertEquals(hpBefore, victim.hp(),
                "a trickle lands nothing on the tick it is filed -- the first dose is a period away");

        // Ten ticks later the dose lands, through the table's ordinary cadence.
        long start = ctx.tick();
        for (long t = start + 1; t <= start + ActiveEffects.OVER_TIME_PERIOD_TICKS; t++) {
            ctx.setTick(t);
            ctx.effects.tick(registry, t);
        }
        assertEquals(hpBefore - 3, victim.hp(),
                "harm 3, delivered at range 4, by a spell that exists only as JSON");

        // ---- "chill 2 for 200 ticks on self" -----------------------------------------
        int chillRaw = invented.rawOf("deep_chill");
        caster.setCastUntilTick(0L);
        castUntilItWorks(caster, ctx, chillRaw, caster.id());
        assertEquals(-20, ctx.effects.temperatureOffsetDeciK(caster.id()),
                "two degrees of chill held on the caster's own body");

        long chilledAt = ctx.tick();
        ctx.setTick(chilledAt + 199);
        ctx.effects.tick(registry, chilledAt + 199);
        assertEquals(-20, ctx.effects.temperatureOffsetDeciK(caster.id()),
                "still cold one tick before it lapses");
        ctx.setTick(chilledAt + 200);
        ctx.effects.tick(registry, chilledAt + 200);
        assertEquals(0, ctx.effects.temperatureOffsetDeciK(caster.id()),
                "and exactly zero after -- a held value cannot outlive its own row");
    }

    // ==================================================================
    // The three seed axes, off the SHIPPED raws
    // ==================================================================

    @Test
    void theShippedListCoversTheThreeAxesAtLevelZero() {
        SpellRegistry shipped = SpellRawsLoader.load(locateRawsDir());
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor neighbour = spawn(registry, NEXT_DOOR);
        MagicContext ctx = new MagicContext(registry, shipped);

        for (String key : new String[] {"warm_the_hands", "sting", "steady_the_hand"}) {
            assertNotEquals(-1, shipped.rawOf(key), key + " must be on the public shelf");
            assertEquals(0, shipped.get(shipped.rawOf(key)).minLevel(),
                    key + " must be readable by anyone who can read at all");
            assertTrue(SpellVerb.canCast(caster, ctx.tracks, shipped, shipped.rawOf(key)),
                    "an unschooled docker must be able to attempt " + key);
        }

        // TEMPERATURE (on your own hands -- no link to forge to yourself)
        castUntilItWorks(caster, ctx, shipped.rawOf("warm_the_hands"), caster.id());
        assertEquals(15, ctx.effects.temperatureOffsetDeciK(caster.id()));

        // VITALITY
        caster.setCastUntilTick(0L);
        short before = neighbour.hp();
        castUntilItWorks(caster, ctx, shipped.rawOf("sting"), neighbour.id());
        assertEquals(before - 1, neighbour.hp(), "one hit point: the smallest transfer there is");

        // ATTRIBUTE
        caster.setCastUntilTick(0L);
        int agiBefore = ctx.tracks.attribute(caster.id(), AttributeId.AGI);
        castUntilItWorks(caster, ctx, shipped.rawOf("steady_the_hand"), caster.id());
        assertEquals(agiBefore + 1, ctx.tracks.attribute(caster.id(), AttributeId.AGI),
                "a held nudge is felt through the ONE attribute function the whole game reads");
    }

    @Test
    void everyShippedCraftingIsBridgedBecauseCanonGatesTheUnbridgedLink() {
        SpellRegistry shipped = SpellRawsLoader.load(locateRawsDir());
        for (SpellDefinition spell : shipped.all()) {
            assertTrue(spell.reach() <= 1,
                    spell.key() + " reaches past an arm's length; L459 gates that behind the gift");
            assertEquals(0, spell.areaRadius(),
                    spell.key() + " spreads across a crowd, which the public shelf never teaches");
        }
    }

    // ==================================================================
    // The properties that make the vocabulary safe to open up
    // ==================================================================

    @Test
    void distanceAndTransferBothBleedIntoTheDifficulty() {
        SpellRegistry invented = SpellRawsLoader.parse(SKILLS, """
                {
                  "id": "spells",
                  "spells": [
                    { "id": "small", "displayName": "Small", "skill": "linkcraft",
                      "target": "RANGED", "range": 8,
                      "components": [ { "effect": "VITALITY", "mode": "INSTANT",
                                        "magnitude": -1 } ] },
                    { "id": "big", "displayName": "Big", "skill": "linkcraft",
                      "target": "RANGED", "range": 8,
                      "components": [ { "effect": "VITALITY", "mode": "INSTANT",
                                        "magnitude": -6 } ] }
                  ]
                }
                """);
        SpellDefinition small = invented.get(invented.rawOf("small"));
        SpellDefinition big = invented.get(invented.rawOf("big"));

        assertTrue(SpellCost.resistFor(small, 6) > SpellCost.resistFor(small, 1),
                "the further it travels the more is lost (L454)");
        assertTrue(SpellCost.resistFor(big, 1) > SpellCost.resistFor(small, 1),
                "the more you transfer the more is lost (L454)");
        assertEquals(5, SpellCost.resistFor(big, 1) - SpellCost.resistFor(small, 1),
                "five extra hit points of transfer cost five points of difficulty");

        // ...and a trickle pays for every dose it will deliver, so duration is priced too.
        SpellRegistry trickles = SpellRawsLoader.parse(SKILLS, """
                {
                  "id": "spells",
                  "spells": [
                    { "id": "brief", "displayName": "Brief", "skill": "linkcraft",
                      "target": "TOUCH",
                      "components": [ { "effect": "VITALITY", "mode": "OVER_TIME",
                                        "magnitude": -1, "durationTicks": 10 } ] },
                    { "id": "long", "displayName": "Long", "skill": "linkcraft",
                      "target": "TOUCH",
                      "components": [ { "effect": "VITALITY", "mode": "OVER_TIME",
                                        "magnitude": -1, "durationTicks": 100 } ] }
                  ]
                }
                """);
        assertTrue(SpellCost.resistFor(trickles.get(trickles.rawOf("long")), 1)
                        > SpellCost.resistFor(trickles.get(trickles.rawOf("brief")), 1),
                "ten doses cost ten times one dose (L457's arithmetic, run backwards)");
    }

    @Test
    void noCraftingCanEverPutABodyOnTheGround() {
        SpellRegistry murderous = SpellRawsLoader.parse(SKILLS, """
                {
                  "id": "spells",
                  "spells": [
                    { "id": "annihilate", "displayName": "Annihilate", "skill": "linkcraft",
                      "target": "TOUCH",
                      "components": [ { "effect": "VITALITY", "mode": "INSTANT",
                                        "magnitude": -9999 } ] }
                  ]
                }
                """);
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor victim = spawn(registry, NEXT_DOOR);
        MagicContext ctx = new MagicContext(registry, murderous);
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(), 100);

        castUntilItWorks(caster, ctx, murderous.rawOf("annihilate"), victim.id());
        assertEquals(ActiveEffects.VITALITY_FLOOR, victim.hp(),
                "the floor is structural: the only code that writes a hit point enforces it");
        assertFalse(victim.isDead(), "so magic never touches the ecology or the death feed");
    }

    @Test
    void theSignIsTheWholeOfOpposition() {
        SpellRegistry shipped = SpellRawsLoader.load(locateRawsDir());
        SpellDefinition scald = shipped.get(shipped.rawOf("scald"));
        SpellDefinition knit = shipped.get(shipped.rawOf("knit_the_skin"));
        assertEquals(scald.components().get(0).kind(), knit.components().get(0).kind());
        assertEquals(scald.components().get(0).mode(), knit.components().get(0).mode());
        assertEquals(-scald.components().get(0).magnitude(),
                knit.components().get(0).magnitude(),
                "mending is harming with a minus sign -- not a second system");
    }

    @Test
    void aHeldNudgeShiftsEveryCheckAndThenStopsExactlyOnTime() {
        SpellRegistry shipped = SpellRawsLoader.load(locateRawsDir());
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor other = spawn(registry, NEXT_DOOR);
        MagicContext ctx = new MagicContext(registry, shipped);
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(), 20);

        int pushBefore = SkillChecks.pushContestPermille(ctx.tracks, caster.id(), other.id());
        castUntilItWorks(caster, ctx, shipped.rawOf("steady_the_hand"), caster.id());
        int pushAfter = SkillChecks.pushContestPermille(ctx.tracks, caster.id(), other.id());
        assertEquals(pushBefore + SkillChecks.POINTS_TO_PERMILLE, pushAfter,
                "a steadied hand shoves better -- the nudge reaches a system magic never mentions");

        long castAt = ctx.tick();
        int duration = shipped.get(shipped.rawOf("steady_the_hand")).longestDurationTicks();
        ctx.setTick(castAt + duration);
        ctx.effects.tick(registry, castAt + duration);
        assertEquals(pushBefore,
                SkillChecks.pushContestPermille(ctx.tracks, caster.id(), other.id()),
                "and stops on the exact tick, because the modifier IS the row");
    }

    @Test
    void aCraftingYouHaveNotReadDeeplyEnoughIsNotYours() {
        SpellRegistry shipped = SpellRawsLoader.load(locateRawsDir());
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        MagicContext ctx = new MagicContext(registry, shipped);
        int deep = shipped.rawOf("sap_the_step");
        assertTrue(shipped.get(deep).minLevel() > 0, "the deep row must be gated at all");
        assertFalse(SpellVerb.canCast(caster, ctx.tracks, shipped, deep),
                "the restricted edition is on the top shelf (L2472)");
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(),
                shipped.get(deep).minLevel());
        assertTrue(SpellVerb.canCast(caster, ctx.tracks, shipped, deep),
                "...and comes down when the reader is deep enough for it");
    }

    @Test
    void everyAttemptTrainsTheSkillEvenTheOnesThatFizzle() {
        SpellRegistry shipped = SpellRawsLoader.load(locateRawsDir());
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor neighbour = spawn(registry, NEXT_DOOR);
        MagicContext ctx = new MagicContext(registry, shipped);
        int linkcraft = ctx.tracks.linkcraftRaw();
        assertEquals(0, ctx.tracks.progressGrains(caster.id(), linkcraft));

        SpellVerb.resolveCast(caster, ctx, shipped.rawOf("warm_the_hands"), caster.id());
        assertTrue(ctx.tracks.progressGrains(caster.id(), linkcraft) > 0,
                "XP on the ATTEMPT -- the fishing cast and the cull already settled this");
        assertTrue(caster.castUntilTick() > ctx.tick(),
                "and the hand is latched either way: the attempt is what costs the time");
    }

    @Test
    void malformedCraftingsFailLoudlyRatherThanVanishingFromTheBar() {
        assertThrows(RuntimeException.class, () -> SpellRawsLoader.parse(SKILLS, """
                { "id": "spells", "spells": [
                  { "id": "nonsense", "displayName": "Nonsense", "skill": "linkcraft",
                    "target": "TOUCH",
                    "components": [ { "effect": "TEMPERATURE", "mode": "WHILE_ACTIVE",
                                      "magnitude": 5, "param": "AGI", "durationTicks": 10 } ] }
                ] }
                """), "a param on an axis that takes none is a content bug, not a shrug");

        assertThrows(RuntimeException.class, () -> SpellRawsLoader.parse(SKILLS, """
                { "id": "spells", "spells": [
                  { "id": "nameless", "displayName": "Nameless", "skill": "linkcraft",
                    "target": "TOUCH",
                    "components": [ { "effect": "ATTRIBUTE", "mode": "WHILE_ACTIVE",
                                      "magnitude": 1, "durationTicks": 10 } ] }
                ] }
                """), "an ATTRIBUTE part must say WHICH attribute");

        assertThrows(RuntimeException.class, () -> SpellRawsLoader.parse(SKILLS, """
                { "id": "spells", "spells": [
                  { "id": "unreachable", "displayName": "Unreachable", "skill": "linkcraft",
                    "target": "RANGED",
                    "components": [ { "effect": "VITALITY", "mode": "INSTANT",
                                      "magnitude": -1 } ] }
                ] }
                """), "a RANGED crafting must declare how far it reaches");

        SpellRawsValidationException typo = assertThrows(SpellRawsValidationException.class,
                () -> SpellRawsLoader.parse(SKILLS, """
                { "id": "spells", "spells": [
                  { "id": "mistyped", "displayName": "Mistyped", "skill": "linkraft",
                    "target": "SELF",
                    "components": [ { "effect": "TEMPERATURE", "mode": "WHILE_ACTIVE",
                                      "magnitude": 15, "durationTicks": 600 } ] }
                ] }
                """), "a misspelt skill key is a crafting with no gate, no dice and no XP");
        assertTrue(typo.getMessage().contains("linkraft"),
                "the refusal names the key that is wrong: " + typo.getMessage());
        assertTrue(typo.getMessage().contains("spells[0].skill"),
                "and the exact field path: " + typo.getMessage());
        assertTrue(typo.getMessage().contains("linkcraft"),
                "and the skills the ward actually has: " + typo.getMessage());
    }

    /**
     * A MISSPELT SKILL KEY USED TO LOAD CLEAN, AND EVERYTHING DOWNSTREAM AGREED WITH IT.
     * {@code SkillTrackRegistry.rawOfSkill} answers {@link Actor#NONE} for a key it does not
     * know, {@code level()} reads 0 off {@code NONE}, so the {@code minLevel} gate passed for
     * everybody; {@code award()} trained nothing; and the check ran against a skill that did not
     * exist. The typo was invisible in every direction — the crafting simply worked, for anyone,
     * forever, and grew nobody. This pins that the loader is now where that stops.
     */
    @Test
    void aSkillKeyTheRegistryDoesNotKnowIsRefusedRatherThanSilentlyUngated() {
        SkillTrackRegistry tracks = new SkillTrackRegistry(SkillRawsLoader.load(locateRawsDir()));
        assertEquals(Actor.NONE, tracks.rawOfSkill("linkraft"),
                "the misspelling really does resolve to nothing...");
        assertEquals(0, tracks.level(0, tracks.rawOfSkill("linkraft")),
                "...and nothing reads as level 0, which is what made the gate vanish");

        assertThrows(SpellRawsValidationException.class, () -> SpellRawsLoader.parse(SKILLS, """
                { "id": "spells", "spells": [
                  { "id": "ungated", "displayName": "Ungated", "skill": "linkraft",
                    "minLevel": 99, "target": "SELF",
                    "components": [ { "effect": "TEMPERATURE", "mode": "WHILE_ACTIVE",
                                      "magnitude": 15, "durationTicks": 600 } ] }
                ] }
                """), "a minLevel of 99 in a skill nobody has is a gate that opens for everyone");

        // Every shipped crafting names a skill the registry knows -- the same check, run over
        // the content that actually ships.
        for (SpellDefinition spell : SpellRawsLoader.load(locateRawsDir()).all()) {
            assertNotEquals(Actor.NONE, tracks.rawOfSkill(spell.skillKey()),
                    spell.key() + " names a skill the ward does not have: " + spell.skillKey());
        }
    }

    @Test
    void theTableExpiresRowsForABodyThatDied() {
        SpellRegistry shipped = SpellRawsLoader.load(locateRawsDir());
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor neighbour = spawn(registry, NEXT_DOOR);
        MagicContext ctx = new MagicContext(registry, shipped);
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(), 20);
        castUntilItWorks(caster, ctx, shipped.rawOf("lend_the_warmth"), neighbour.id());
        assertEquals(15, ctx.effects.temperatureOffsetDeciK(neighbour.id()));

        neighbour.setStatus(StatusBit.DEAD, true);
        ctx.effects.tick(registry, ctx.tick() + 1);
        assertEquals(0, ctx.effects.temperatureOffsetDeciK(neighbour.id()),
                "a corpse stops being warm");
    }

    /**
     * THE SPELL NAMES ITS OWN SKILL. Every raws row carries a {@code "skill"} key, and this
     * crafting names CHANNELING rather than linkcraft — so channeling is what gates it, what the
     * check reads, and what the attempt grows. If any of that were hardcoded, the raws field
     * would be a lie sitting in the content directory waiting to be believed.
     */
    @Test
    void aCraftingAuthoredAgainstADifferentSkillIsGatedAndGrownByThatSkill() {
        SpellRegistry channelled = SpellRawsLoader.parse(SKILLS, """
                { "id": "spells", "spells": [
                  { "id": "fill_the_stone", "displayName": "Fill the Stone",
                    "skill": "channeling", "minLevel": 3, "target": "SELF",
                    "components": [ { "effect": "TEMPERATURE", "mode": "WHILE_ACTIVE",
                                      "magnitude": 10, "durationTicks": 100 } ] } ] }
                """);
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        MagicContext ctx = new MagicContext(registry, channelled);
        int raw = channelled.rawOf("fill_the_stone");
        int channeling = ctx.tracks.rawOfSkill("channeling");
        int linkcraft = ctx.tracks.linkcraftRaw();
        assertNotEquals(channeling, linkcraft, "the two skills must actually be different");

        // Deep in the WRONG skill: still not yours.
        ctx.tracks.seedLevel(caster.id(), linkcraft, 50);
        assertFalse(SpellVerb.canCast(caster, ctx.tracks, channelled, raw),
                "linkcraft does not unlock a crafting the raws gate on channeling");

        // Deep enough in the RIGHT one: yours.
        ctx.tracks.seedLevel(caster.id(), channeling, 3);
        assertTrue(SpellVerb.canCast(caster, ctx.tracks, channelled, raw));

        int channelingBefore = ctx.tracks.progressGrains(caster.id(), channeling);
        SpellVerb.resolveCast(caster, ctx, raw, caster.id());
        assertTrue(ctx.tracks.progressGrains(caster.id(), channeling) > channelingBefore,
                "and the attempt grows CHANNELING, because that is what the row says");
    }

    @Test
    void axisUnitsKeepTheAxesComparable() {
        assertEquals(10, EffectKind.TEMPERATURE.unitsPerTransferPoint(),
                "ten deci-Kelvin is a degree");
        assertEquals(1, EffectKind.VITALITY.unitsPerTransferPoint());
        assertEquals(2, EffectKind.ATTRIBUTE.unitsPerTransferPoint());
    }

    // ==================================================================
    // NO SILENT NO-OPS — the positive half
    // ==================================================================

    /**
     * EVERY SURVIVING PAIRING, CAST FOR REAL, OBSERVED MOVING SOMETHING. {@code NoSilentNoOpTest}
     * proves the five dead pairings can no longer be authored; this proves the four that CAN be
     * authored all reach live sim state through the real verb, at the shortest duration the
     * pairing table lets them carry. Between the two files the system can state the property it
     * is built on: <b>a crafting that gets past the loader is authored to do something.</b> That
     * is a LOAD-time property of the row -- a fizzle still changes nothing, and so does a wound
     * on a body already at the vitality floor.
     *
     * <p>Each of the four is authored here as raws text at its own minimum, cast into a wired
     * sim, and then read back through the function the rest of the game reads it through — hit
     * points, the held temperature offset, the REST need, and the push contest.
     */
    @Test
    void everySurvivingPairingIsObservedChangingTheWorld() {
        SpellRegistry authored = SpellRawsLoader.parse(SKILLS, """
                {
                  "id": "spells",
                  "spells": [
                    { "id": "delivered_at_once", "displayName": "Delivered At Once",
                      "skill": "linkcraft", "target": "TOUCH", "range": 1,
                      "components": [ { "effect": "VITALITY", "mode": "INSTANT",
                                        "magnitude": -2 } ] },
                    { "id": "delivered_by_dose", "displayName": "Delivered By Dose",
                      "skill": "linkcraft", "target": "TOUCH", "range": 1,
                      "components": [ { "effect": "VITALITY", "mode": "OVER_TIME",
                                        "magnitude": -1, "durationTicks": 10 } ] },
                    { "id": "held_warmth", "displayName": "Held Warmth",
                      "skill": "linkcraft", "target": "TOUCH", "range": 1,
                      "components": [ { "effect": "TEMPERATURE", "mode": "WHILE_ACTIVE",
                                        "magnitude": 20, "durationTicks": 50 } ] },
                    { "id": "held_nudge", "displayName": "Held Nudge",
                      "skill": "linkcraft", "target": "TOUCH", "range": 1,
                      "components": [ { "effect": "ATTRIBUTE", "mode": "WHILE_ACTIVE",
                                        "magnitude": 1, "param": "AGI",
                                        "durationTicks": 1 } ] }
                  ]
                }
                """);
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor subject = spawn(registry, NEXT_DOOR);
        MagicContext ctx = new MagicContext(registry, authored);
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(), 100);

        // VITALITY x INSTANT -- a wound, delivered at once, written to hit points.
        int hpBefore = subject.hp();
        castUntilItWorks(caster, ctx, authored.rawOf("delivered_at_once"), subject.id());
        assertEquals(hpBefore - 2, subject.hp(), "an instant wound is an hp write, on the tick");

        // VITALITY x OVER_TIME -- a wound, delivered dose by dose on its own cadence.
        int hpBeforeTrickle = subject.hp();
        castUntilItWorks(caster, ctx, authored.rawOf("delivered_by_dose"), subject.id());
        long castAt = ctx.tick();
        for (long t = castAt + 1; t <= castAt + ActiveEffects.OVER_TIME_PERIOD_TICKS; t++) {
            ctx.effects.tick(registry, t);
        }
        assertEquals(hpBeforeTrickle - 1, subject.hp(),
                "one period delivers exactly one dose -- the shortest legal trickle is not a"
                        + " no-op, which is exactly why anything shorter is refused");

        // TEMPERATURE x WHILE_ACTIVE -- a held offset, read by summing live rows, paying REST.
        assertEquals(0, ctx.effects.temperatureOffsetDeciK(subject.id()));
        // Off the ceiling first: the need is saturating, and a body already fully rested would
        // absorb the payment silently -- which would make this assertion pass for the wrong
        // reason if warmth ever stopped paying at all.
        subject.applyNeedDelta(Need.REST, -5_000);
        int restBefore = subject.need(Need.REST);
        castUntilItWorks(caster, ctx, authored.rawOf("held_warmth"), subject.id());
        assertEquals(20, ctx.effects.temperatureOffsetDeciK(subject.id()),
                "the body's offset from ambient IS the live sum of its held rows");
        long warmedAt = ctx.tick();
        for (long t = warmedAt + 1; t <= warmedAt + ActiveEffects.WARMTH_REST_PERIOD_TICKS; t++) {
            ctx.effects.tick(registry, t);
        }
        assertNotEquals(restBefore, subject.need(Need.REST),
                "and one full cadence reaches one REST payment -- the shortest legal warmth"
                        + " moves a number the rest of the sim reads");

        // ATTRIBUTE x WHILE_ACTIVE -- in force from the tick it is filed, felt by every check.
        int pushBefore = SkillChecks.pushContestPermille(ctx.tracks, subject.id(), caster.id());
        castUntilItWorks(caster, ctx, authored.rawOf("held_nudge"), subject.id());
        assertEquals(pushBefore + SkillChecks.POINTS_TO_PERMILLE,
                SkillChecks.pushContestPermille(ctx.tracks, subject.id(), caster.id()),
                "a one-tick nudge is a real nudge on the tick it is filed");
    }

    /**
     * A CAST ON A CORPSE COSTS NOTHING AND STAMPS NOTHING. {@code resolveCast} is the shared
     * public verb — an AI policy, a queued intent that outlived its target, or a test can reach
     * it without pre-validating — and a dead body is not a body a link can be forged to. Refused
     * before the latch, the use-XP or the resist, the {@code CullVerb} precondition idiom.
     */
    @Test
    void aCastOnACorpseIsRefusedBeforeItCostsAnything() {
        SpellRegistry shipped = SpellRawsLoader.load(locateRawsDir());
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor victim = spawn(registry, NEXT_DOOR);
        MagicContext ctx = new MagicContext(registry, shipped);
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(), 100);
        victim.setStatus(StatusBit.DEAD, true);

        int linkcraft = ctx.tracks.linkcraftRaw();
        int grainsBefore = ctx.tracks.progressGrains(caster.id(), linkcraft);
        assertFalse(SpellVerb.resolveCast(caster, ctx, shipped.rawOf("lend_the_warmth"),
                victim.id()), "no link is forged to a corpse");
        assertEquals(ReasonCode.NO_LINK_TO_TARGET, caster.lastReasonCode());
        assertEquals(grainsBefore, ctx.tracks.progressGrains(caster.id(), linkcraft),
                "and nothing is charged: no XP,");
        assertEquals(0L, caster.castUntilTick(), "no latch on the hand,");
        assertEquals(0, ctx.effects.liveCount(), "and no row filed on the dead");
    }

    /**
     * A CAST WITH NOWHERE TO PUT ITS ROW IS REFUSED OUT LOUD. The table used to evict the
     * soonest-expiring row to make space, which meant one actor's crafting could delete
     * another's live warmth with nothing said to anybody. Now the room is reserved before
     * anything is charged, and the player is told.
     */
    @Test
    void aCastWithNoRoomLeftIsRefusedRatherThanEvictingSomebodyElsesEffect() {
        SpellRegistry shipped = SpellRawsLoader.load(locateRawsDir());
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        MagicContext ctx = new MagicContext(registry, shipped);
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(), 100);
        for (int s = 0; s < ActiveEffects.SLOT_CAPACITY; s++) {
            ctx.effects.add(caster.id(), EffectKind.TEMPERATURE, EffectMode.WHILE_ACTIVE, 0, 15,
                    0L, 10_000L);
        }
        assertEquals(0, ctx.effects.freeSlots());

        int grainsBefore = ctx.tracks.progressGrains(caster.id(), ctx.tracks.linkcraftRaw());
        assertFalse(SpellVerb.resolveCast(caster, ctx, shipped.rawOf("warm_the_hands"),
                caster.id()), "a crafting with nowhere to land does not land");
        assertEquals(ReasonCode.NO_ROOM_FOR_CRAFTING, caster.lastReasonCode(),
                "and says so, with a stamp the toast tracker can read");
        assertEquals(ActiveEffects.SLOT_CAPACITY, ctx.effects.liveCount(),
                "every row that was already working is still working");
        assertEquals(grainsBefore,
                ctx.tracks.progressGrains(caster.id(), ctx.tracks.linkcraftRaw()),
                "and the refusal cost the caster nothing at all");
    }

    // ==================================================================
    // helpers
    // ==================================================================

    private static Actor spawn(ActorRegistry registry, int cell) {
        return registry.spawn(Serf.TYPE, ActorTestFixtures.statsWithDefer(Serf.TYPE, true), cell);
    }

    /**
     * Presses the button until the link opens — the {@code PlayableScalpLoopTest} idiom. Every
     * check in this game has a ceiling under 1000 on purpose, so a test that asserts one
     * successful cast has to be willing to fail a few first. Advances the tick each attempt
     * (which re-phases the named draw) and clears the latch, since pacing is not what is under
     * test here.
     */
    private static void castUntilItWorks(Actor caster, MagicContext ctx, int spellRaw,
            int targetId) {
        for (int attempt = 0; attempt < 60; attempt++) {
            ctx.setTick(ctx.tick() + 1);
            caster.setCastUntilTick(0L);
            if (SpellVerb.resolveCast(caster, ctx, spellRaw, targetId)) {
                return;
            }
        }
        throw new AssertionError("60 attempts and the link never opened for spell raw "
                + spellRaw + " -- the check family is mis-tuned");
    }

    private static Path locateRawsDir() {
        return RawsDir.locate();
    }
}
