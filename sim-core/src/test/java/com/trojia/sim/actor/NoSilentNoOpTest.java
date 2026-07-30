package com.trojia.sim.actor;

import com.trojia.sim.actor.spell.ActiveEffects;
import com.trojia.sim.actor.spell.EffectKind;
import com.trojia.sim.actor.spell.EffectMode;
import com.trojia.sim.actor.spell.EffectPairing;
import com.trojia.sim.actor.spell.SpellRawsLoader;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.progression.AttributeId;
import com.trojia.sim.progression.SkillRegistry;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * THE BAR FOR THIS ROUND, WRITTEN AS A TEST: it must be IMPOSSIBLE to author a crafting that
 * resolves, is charged a resist, narrates "the link opens; it takes" and changes nothing.
 *
 * <p><b>What used to happen.</b> Three axes times three time-shapes is nine pairings, and the
 * loader accepted all nine. Only four of them were read by any code in the sim. The other five
 * — temperature INSTANT, temperature OVER_TIME, vitality WHILE_ACTIVE, attribute INSTANT,
 * attribute OVER_TIME — loaded clean, resolved clean, were charged their full computed
 * difficulty, took the caster's cooldown, earned use-XP, toasted success, and moved nothing at
 * all. Two more families did the same thing without being illegal: a trickle authored shorter
 * than one dose period delivered zero doses, and a held warmth shorter than one REST cadence
 * reached no payment. And a magnitude of 0 consumed a persisted slot to do nothing with.
 *
 * <p><b>What this file does.</b> It walks the whole nine-cell matrix and AUTHORS one crafting
 * per cell, as raws text, through the same loader the shipped list uses — the five that used to
 * lie are refused by name, and the four survivors load. Then it authors one of each remaining
 * silent family and shows the same refusal. Then it shuts the two Java-side doors: the record's
 * canonical constructor and {@link ActiveEffects#add}, which is public and takes a loose
 * {@code (kind, mode)} pair.
 *
 * <p>The positive half of the proof — that every SURVIVING pairing is observed changing the
 * world through the real verb — lives in {@code SpellcraftTest}, because that is where the wired
 * sim harness is.
 */
final class NoSilentNoOpTest {

    /** The skill universe the loader gates every {@code "skill"} key against. */
    private static final SkillRegistry SKILLS =
            com.trojia.sim.progression.SkillRawsLoader.load(RawsDir.locate());

    // ==================================================================
    // The nine-cell matrix, authored cell by cell
    // ==================================================================

    /**
     * EVERY ONE OF THE FIVE SILENT PAIRINGS, AUTHORED AND REFUSED. The loop is the point: it
     * does not hardcode which five are dead, it asks {@link EffectPairing#isLegal} and then
     * proves the loader agrees with it in both directions for all nine cells. A pairing that
     * some future axis makes real gets picked up here for free.
     */
    @Test
    void allNinePairingsAreAuthoredAndOnlyTheFourThatDoSomethingLoad() {
        List<String> refused = new ArrayList<>();
        List<String> accepted = new ArrayList<>();
        for (EffectKind kind : EffectKind.values()) {
            for (EffectMode mode : EffectMode.values()) {
                String cell = kind + "/" + mode;
                if (EffectPairing.isLegal(kind, mode)) {
                    SpellRegistry loaded = SpellRawsLoader.parse(SKILLS, oneComponentSpell(kind, mode));
                    assertNotNull(loaded.get(loaded.rawOf("under_test")),
                            cell + " is read by the sim and must be authorable");
                    accepted.add(cell);
                    continue;
                }
                RuntimeException thrown = assertThrows(RuntimeException.class,
                        () -> SpellRawsLoader.parse(SKILLS, oneComponentSpell(kind, mode)),
                        cell + " is read by nothing in the sim: authoring it used to resolve,"
                                + " charge a resist, toast success and change nothing");
                String message = String.valueOf(thrown.getMessage());
                assertTrue(message.contains(kind.name()) && message.contains(mode.name()),
                        cell + " must be refused BY NAME, not by a generic parse failure: "
                                + message);
                assertTrue(message.contains("components[0]"),
                        "and the refusal must point at the exact component: " + message);
                refused.add(cell);
            }
        }
        assertEquals(List.of("TEMPERATURE/WHILE_ACTIVE", "VITALITY/INSTANT", "VITALITY/OVER_TIME",
                        "ATTRIBUTE/WHILE_ACTIVE"), accepted,
                "the four survivors are the four the sim actually reads: heat and tuning are"
                        + " HELD (summed from live rows), a wound is DELIVERED (written to hp)");
        assertEquals(List.of("TEMPERATURE/INSTANT", "TEMPERATURE/OVER_TIME",
                        "VITALITY/WHILE_ACTIVE", "ATTRIBUTE/INSTANT", "ATTRIBUTE/OVER_TIME"),
                refused, "and these are the five that used to resolve, charge and lie");
    }

    // ==================================================================
    // The other two silent families: legal pairing, no delivery
    // ==================================================================

    /**
     * A TRICKLE TOO SHORT TO DOSE. {@code VITALITY OVER_TIME} is legal, but a dose lands once
     * per {@link ActiveEffects#OVER_TIME_PERIOD_TICKS}, so 1–9 ticks delivered none of them at
     * full price. The boundary is asserted from the constant, not from the literal 10, so
     * retuning the cadence cannot silently reopen the hole.
     */
    @Test
    void aTrickleShorterThanOneDosePeriodIsRefused() {
        int period = ActiveEffects.OVER_TIME_PERIOD_TICKS;
        RuntimeException thrown = assertThrows(RuntimeException.class,
                () -> SpellRawsLoader.parse(SKILLS, spell(EffectKind.VITALITY, EffectMode.OVER_TIME,
                        -1, period - 1)),
                "under one period a trickle delivers zero doses and is charged for all of them");
        assertTrue(String.valueOf(thrown.getMessage()).contains("at least " + period),
                "the refusal names the cadence it fell short of: " + thrown.getMessage());
        assertNotNull(SpellRawsLoader.parse(SKILLS, spell(EffectKind.VITALITY, EffectMode.OVER_TIME,
                        -1, period)),
                "exactly one period delivers exactly one dose, so exactly one period is legal");
    }

    /**
     * A HELD WARMTH TOO SHORT TO PAY. {@code TEMPERATURE WHILE_ACTIVE} is legal, but the only
     * thing a held offset does to the rest of the sim is pay REST once per
     * {@link ActiveEffects#WARMTH_REST_PERIOD_TICKS} — so a warmth shorter than that cadence
     * expired before it ever moved a number.
     */
    @Test
    void aHeldWarmthShorterThanOneRestCadenceIsRefused() {
        int cadence = ActiveEffects.WARMTH_REST_PERIOD_TICKS;
        assertThrows(RuntimeException.class,
                () -> SpellRawsLoader.parse(SKILLS, spell(EffectKind.TEMPERATURE,
                        EffectMode.WHILE_ACTIVE, 15, cadence - 1)),
                "a warmth that expires before its first REST payment moved nothing");
        assertNotNull(SpellRawsLoader.parse(SKILLS, spell(EffectKind.TEMPERATURE,
                        EffectMode.WHILE_ACTIVE, 15, cadence)),
                "one full cadence reaches one payment, so one full cadence is legal");
    }

    /**
     * A held ATTRIBUTE row is IN FORCE from the tick it is filed — every check reads the live
     * sum — so unlike the other two lingering shapes it has no cadence to fall short of. One
     * tick is a real, if brief, nudge, and the pairing table says so.
     */
    @Test
    void aHeldNudgeNeedsNoCadenceBecauseEveryCheckReadsItImmediately() {
        assertEquals(1, EffectPairing.minimumEffectiveTicks(EffectKind.ATTRIBUTE,
                EffectMode.WHILE_ACTIVE));
        assertNotNull(SpellRawsLoader.parse(SKILLS, spell(EffectKind.ATTRIBUTE,
                        EffectMode.WHILE_ACTIVE, 1, 1)),
                "a one-tick nudge is felt by anything that checks on that tick");
    }

    /** A component that moves nothing is not a crafting, whatever shape of time it sits in. */
    @Test
    void aMagnitudeOfZeroIsRefusedOnEveryAxis() {
        for (EffectKind kind : EffectKind.values()) {
            for (EffectMode mode : EffectMode.values()) {
                if (!EffectPairing.isLegal(kind, mode)) {
                    continue;
                }
                assertThrows(RuntimeException.class,
                        () -> SpellRawsLoader.parse(SKILLS, spell(kind, mode, 0,
                                EffectPairing.minimumEffectiveTicks(kind, mode))),
                        kind + "/" + mode + " magnitude 0 used to consume a persisted slot"
                                + " and do nothing with it");
            }
        }
    }

    // ==================================================================
    // Weak is structural on the attribute axis too
    // ==================================================================

    /**
     * THE ATTRIBUTE AXIS IS BOUNDED IN BOTH PLACES THAT COULD DISAGREE. It is the axis that
     * reaches furthest — a live row is folded into {@code SkillTrackRegistry.attribute()}, the
     * one function every check in the game reads — so the shallow public shelf may not teach a
     * big one. The loader refuses an authored magnitude past the limit, AND the live sum is
     * clamped, so stacking craftings cannot walk past it either.
     */
    @Test
    void theAttributeAxisIsBoundedByTheLoaderAndByTheLiveSum() {
        int limit = ActiveEffects.ATTRIBUTE_MODIFIER_LIMIT;
        assertNotNull(SpellRawsLoader.parse(SKILLS, spell(EffectKind.ATTRIBUTE,
                        EffectMode.WHILE_ACTIVE, limit, 100)),
                "the limit itself is authorable");
        RuntimeException thrown = assertThrows(RuntimeException.class,
                () -> SpellRawsLoader.parse(SKILLS, spell(EffectKind.ATTRIBUTE,
                        EffectMode.WHILE_ACTIVE, limit + 1, 100)),
                "one point past it is not, in either direction");
        assertTrue(String.valueOf(thrown.getMessage()).contains("bounded at +/-" + limit),
                "and the refusal says what the bound is: " + thrown.getMessage());
        assertThrows(RuntimeException.class,
                () -> SpellRawsLoader.parse(SKILLS, spell(EffectKind.ATTRIBUTE,
                        EffectMode.WHILE_ACTIVE, -(limit + 1), 100)),
                "a sap past the bound is the same defect with a minus sign");

        ActiveEffects effects = new ActiveEffects();
        int agi = AttributeId.AGI.ordinal();
        effects.add(7, EffectKind.ATTRIBUTE, EffectMode.WHILE_ACTIVE, agi, limit, 0L, 900L);
        effects.add(7, EffectKind.ATTRIBUTE, EffectMode.WHILE_ACTIVE, agi, limit, 0L, 900L);
        assertEquals(limit, effects.attributeModifier(7, agi),
                "two rows at the limit still read as the limit -- stacking is not a way round it");
        ActiveEffects sapped = new ActiveEffects();
        sapped.add(7, EffectKind.ATTRIBUTE, EffectMode.WHILE_ACTIVE, agi, -limit, 0L, 900L);
        sapped.add(7, EffectKind.ATTRIBUTE, EffectMode.WHILE_ACTIVE, agi, -limit, 0L, 900L);
        assertEquals(-limit, sapped.attributeModifier(7, agi), "and the same downward");
    }

    // ==================================================================
    // The Java-side doors
    // ==================================================================

    /**
     * The record's canonical constructor refuses the same five, so a caller composing a
     * definition in Java gets the same sentence a content author gets. There is no authoring
     * route that skips it — the loader calls this constructor too.
     */
    @Test
    void theRecordsOwnConstructorRefusesWhatTheLoaderRefuses() {
        for (EffectKind kind : EffectKind.values()) {
            for (EffectMode mode : EffectMode.values()) {
                if (EffectPairing.isLegal(kind, mode)) {
                    continue;
                }
                assertThrows(IllegalArgumentException.class,
                        () -> new com.trojia.sim.actor.spell.EffectComponent(kind, mode, 1, 0,
                                mode == EffectMode.INSTANT ? 0 : 100),
                        kind + "/" + mode + " must be unauthorable from Java as well as raws");
            }
        }
    }

    /**
     * {@link ActiveEffects#add} is public and takes a loose pair, so it is the one door into
     * the table that does not pass through the record. Filing a row nothing reads is a
     * fully-priced no-op wearing a different hat, and it is shut too.
     */
    @Test
    void filingARowNothingReadsIsRefusedAtTheTableItself() {
        ActiveEffects effects = new ActiveEffects();
        assertThrows(IllegalArgumentException.class,
                () -> effects.add(0, EffectKind.VITALITY, EffectMode.WHILE_ACTIVE, 0, -1, 0L, 90L),
                "nothing anywhere reads a held vitality row");
        assertThrows(IllegalArgumentException.class,
                () -> effects.add(0, EffectKind.TEMPERATURE, EffectMode.OVER_TIME, 0, 15, 0L, 90L),
                "and a body carries no thermal store for a dose to land in");
        assertEquals(ActiveEffects.SLOT_CAPACITY, effects.freeSlots(),
                "a refused filing takes no slot");
    }

    // ==================================================================
    // helpers
    // ==================================================================

    /** One spell, one component, at the shortest duration this pairing can legally carry. */
    private static String oneComponentSpell(EffectKind kind, EffectMode mode) {
        int magnitude = kind == EffectKind.TEMPERATURE ? 15 : 1;
        return spell(kind, mode, magnitude, Math.max(EffectPairing.minimumEffectiveTicks(kind,
                mode), mode == EffectMode.INSTANT ? 0 : 1));
    }

    /**
     * A whole spells file with one crafting called {@code under_test} in it — the same text a
     * content author would type, handed to the same loader the shipped list goes through.
     * {@code durationTicks} is omitted for {@link EffectMode#INSTANT}, which carries none.
     */
    private static String spell(EffectKind kind, EffectMode mode, int magnitude,
            int durationTicks) {
        String param = kind == EffectKind.ATTRIBUTE ? ", \"param\": \"AGI\"" : "";
        String duration = mode == EffectMode.INSTANT
                ? "" : ", \"durationTicks\": " + durationTicks;
        return """
                { "id": "spells", "spells": [
                  { "id": "under_test", "displayName": "Under Test", "skill": "linkcraft",
                    "target": "TOUCH", "range": 1,
                    "components": [ { "effect": "%s", "mode": "%s", "magnitude": %d%s%s } ] }
                ] }
                """.formatted(kind.name(), mode.name(), magnitude, param, duration);
    }
}
