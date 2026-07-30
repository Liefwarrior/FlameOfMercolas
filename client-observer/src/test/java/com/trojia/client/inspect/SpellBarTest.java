package com.trojia.client.inspect;

import com.trojia.client.boot.RepoPaths;
import com.trojia.client.render.InspectorRenderer;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.spell.ActiveEffects;
import com.trojia.sim.actor.spell.EffectKind;
import com.trojia.sim.actor.spell.EffectMode;
import com.trojia.sim.actor.spell.SpellRawsLoader;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.progression.AttributeId;
import com.trojia.sim.progression.SkillRawsLoader;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * THE BUTTONS DOWN THE RIGHT OF THE SCREEN, laid out and hit-tested headless (no GL).
 *
 * <p>The load-bearing assertion here is the one about the character sheet: Eli asked for
 * buttons down the right side, and the right side already holds a 430px sheet. A bar that
 * technically satisfies the request while sitting underneath the sheet satisfies nobody, so the
 * docking rule is pinned as a test rather than left to a renderer's arithmetic.
 */
class SpellBarTest {

    private static final float W = 1600f;
    private static final float H = 900f;

    private static SpellRegistry shippedSpells() {
        return SpellRawsLoader.load(RepoPaths.locate("content", "raws"));
    }

    private static SkillTrackRegistry wiredTracks() {
        return new SkillTrackRegistry(SkillRawsLoader.load(RepoPaths.locate("content", "raws")));
    }

    @Test
    void theBarNeverDrawsUnderneathTheCharacterSheet() {
        float withSheet = SpellBar.leftEdge(W, true);
        float withoutSheet = SpellBar.leftEdge(W, false);
        assertTrue(withSheet + SpellBar.BUTTON_WIDTH <= W - InspectorRenderer.PANEL_WIDTH,
                "with the sheet up, the bar's right edge must clear the sheet's left edge");
        assertTrue(withoutSheet + SpellBar.BUTTON_WIDTH <= W,
                "with no sheet, the bar still sits inside the window");
        assertTrue(withoutSheet > withSheet,
                "closing the sheet slides the bar back out to the window edge");
    }

    @Test
    void oneButtonPerCraftingStackedTopDownAndNoneOverlap() {
        SpellRegistry spells = shippedSpells();
        List<SpellBar.Button> buttons = SpellBar.layout(spells, W, H, true);
        assertEquals(spells.size(), buttons.size(), "every crafting gets a row");
        for (int i = 0; i < buttons.size(); i++) {
            assertEquals(i, buttons.get(i).spellRaw(), "rows follow raw order");
            assertTrue(buttons.get(i).y() >= 0f, "the bar fits the window at 900px tall");
            if (i > 0) {
                assertTrue(buttons.get(i).y() + buttons.get(i).height()
                                <= buttons.get(i - 1).y(),
                        "rows stack downward without overlapping");
            }
        }
        assertTrue(buttons.get(0).y() + buttons.get(0).height()
                        <= SpellBar.topEdge(H) - SpellBar.HEADER_HEIGHT,
                "the first row hangs below the header");
    }

    @Test
    void anEmptySpellUniverseDrawsNoBarAtAll() {
        assertTrue(SpellBar.layout(SpellRegistry.EMPTY, W, H, false).isEmpty());
    }

    @Test
    void clicksResolveToTheRowUnderTheCursorAndMissElsewhere() {
        SpellRegistry spells = shippedSpells();
        List<SpellBar.Button> buttons = SpellBar.layout(spells, W, H, true);
        SpellBar.Button third = buttons.get(2);

        assertEquals(third.spellRaw(),
                SpellBar.hitTest(buttons, third.x() + 4f, third.y() + 4f));
        assertEquals(Actor.NONE, SpellBar.hitTest(buttons, 10f, 10f),
                "a click out in the world is not a click on the bar");

        // ...and the top-down mouse convention flips exactly once, here and nowhere else.
        int mouseY = (int) (H - (third.y() + third.height() / 2f));
        assertEquals(third.spellRaw(),
                SpellBar.hitTestScreen(buttons, (int) (third.x() + 4f), mouseY, H));
    }

    @Test
    void aCraftingYouCannotYetReadStillGetsARowWithItsGateOnIt() {
        SpellRegistry spells = shippedSpells();
        SkillTrackRegistry tracks = wiredTracks();
        int deep = spells.rawOf("sap_the_step");
        int shallow = spells.rawOf("warm_the_hands");

        assertFalse(SpellBar.known(spells, tracks, 0, deep));
        assertTrue(SpellBar.label(spells, tracks, 0, deep).contains("[Lv "),
                "knowing what is on the top shelf is half of wanting to reach it");
        assertTrue(SpellBar.known(spells, tracks, 0, shallow));
        assertFalse(SpellBar.label(spells, tracks, 0, shallow).contains("[Lv "));
        assertTrue(SpellBar.label(spells, tracks, 0, shallow).contains("self"),
                "the row says how far the link reaches -- Warm the Hands reaches your own");
        assertTrue(SpellBar.label(spells, tracks, 0, spells.rawOf("sting")).contains("touch"),
                "...and a bridged crafting says so too");
    }

    @Test
    void theFooterNamesEveryEffectTheBodyIsCarrying() {
        ActiveEffects effects = new ActiveEffects();
        assertEquals("", SpellBar.heldLine(effects, 0, 100L),
                "an unmagicked soul's bar ends at its last button");

        effects.add(0, EffectKind.TEMPERATURE, EffectMode.WHILE_ACTIVE, 0, 15, 10L, 900L);
        effects.add(0, EffectKind.ATTRIBUTE, EffectMode.WHILE_ACTIVE,
                AttributeId.AGI.ordinal(), 1, 10L, 900L);
        String held = SpellBar.heldLine(effects, 0, 100L);
        assertTrue(held.contains("+1.5 deg"), held);
        assertTrue(held.contains("+1 AGI"), held);
        assertEquals("", SpellBar.heldLine(effects, 1, 100L),
                "somebody else's warmth is not on your bar");
        assertEquals("", SpellBar.heldLine(effects, Actor.NONE, 100L));
    }

    @Test
    void theHintSaysWhatTheBarIsWaitingFor() {
        assertNotEquals(SpellBar.hint(true), SpellBar.hint(false));
        assertTrue(SpellBar.hint(false).contains("P"),
                "a spectator is told how to take the wheel");
    }
}
