package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.spell.SpellDefinition;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.actor.spell.TargetShape;

import java.util.ArrayList;
import java.util.List;

/**
 * THE CRAFTINGS BAR — the column of buttons down the RIGHT of the screen. GL-free: this class
 * decides only WHERE each button is, WHAT it says and WHETHER the played soul may press it;
 * {@code SpellBarRenderer} draws the rectangles it returns and {@code SpellInput} routes the
 * clicks.
 *
 * <p><b>The right edge is already occupied.</b> The character sheet holds a
 * {@value com.trojia.client.render.InspectorRenderer#PANEL_WIDTH}px column there whenever an
 * actor is selected — which, in Play mode, is always. Rather than move the sheet or bury the
 * bar under it, the bar docks against the sheet's left edge while the sheet is up and against
 * the window's right edge when it is not. Both readings are "buttons down the right side"; only
 * one of them is legible.
 *
 * <p><b>One row per crafting, in raw order</b> — which is the raws file's own alphabetical key
 * order, so the bar is a pure function of the content and two runs of the same build lay it out
 * identically. A crafting the soul has not read deeply enough for still gets a row, greyed with
 * its gate on it: knowing what is on the top shelf is half of wanting to reach it.
 */
public final class SpellBar {

    /** Button width, px — wide enough for the longest authored name plus its gate marker. */
    public static final float BUTTON_WIDTH = 208f;
    /** Button height, px. */
    public static final float BUTTON_HEIGHT = 26f;
    /** Vertical gap between two buttons, px. */
    public static final float BUTTON_GAP = 4f;
    /** Margin from the window edge (and from the sheet's edge when it is up), px. */
    public static final float MARGIN = 8f;
    /** Height reserved above the first button for the bar's header, px. */
    public static final float HEADER_HEIGHT = 22f;

    /** The bar's header. */
    public static final String HEADER = "CRAFTINGS";

    /** One laid-out button, in the y-up bottom-left origin the renderer draws in. */
    public record Button(int spellRaw, float x, float y, float width, float height) {

        /** Whether a y-up point falls inside this button. */
        public boolean contains(float px, float py) {
            return px >= x && px <= x + width && py >= y && py <= y + height;
        }
    }

    private SpellBar() {
    }

    /**
     * The x of the bar's left edge: tucked against the character sheet's left edge while the
     * sheet is up, against the window's right edge otherwise.
     */
    public static float leftEdge(float viewportWidthPx, boolean sheetShowing) {
        float rightEdge = viewportWidthPx - MARGIN
                - (sheetShowing ? com.trojia.client.render.InspectorRenderer.PANEL_WIDTH : 0f);
        return rightEdge - BUTTON_WIDTH;
    }

    /** The y of the header's baseline block top — the bar hangs down from here. */
    public static float topEdge(float viewportHeightPx) {
        return viewportHeightPx - MARGIN;
    }

    /**
     * Lays out one button per crafting, top-down from {@link #topEdge}. Returns an empty list
     * for an empty spell universe (a bake with no raws draws no bar at all).
     */
    public static List<Button> layout(SpellRegistry spells, float viewportWidthPx,
            float viewportHeightPx, boolean sheetShowing) {
        List<Button> buttons = new ArrayList<>();
        float x = leftEdge(viewportWidthPx, sheetShowing);
        float cursorY = topEdge(viewportHeightPx) - HEADER_HEIGHT;
        for (int raw = 0; raw < spells.size(); raw++) {
            cursorY -= BUTTON_HEIGHT;
            buttons.add(new Button(raw, x, cursorY, BUTTON_WIDTH, BUTTON_HEIGHT));
            cursorY -= BUTTON_GAP;
        }
        return buttons;
    }

    /**
     * The crafting under a y-up point, or {@link Actor#NONE}. The renderer's coordinates, so a
     * caller holding top-down mouse pixels flips first ({@link #hitTestScreen}).
     */
    public static int hitTest(List<Button> buttons, float px, float py) {
        for (int i = 0; i < buttons.size(); i++) {
            if (buttons.get(i).contains(px, py)) {
                return buttons.get(i).spellRaw();
            }
        }
        return Actor.NONE;
    }

    /**
     * The crafting under a TOP-DOWN mouse position ({@code Gdx.input.getX()/getY()}), or
     * {@link Actor#NONE} — the one place the two y conventions meet, so no call site has to
     * remember the flip.
     */
    public static int hitTestScreen(List<Button> buttons, int mouseX, int mouseY,
            float viewportHeightPx) {
        return hitTest(buttons, mouseX, viewportHeightPx - mouseY);
    }

    /**
     * The row's text: the crafting's name, its reach, and — when the reader is not deep enough
     * — the level that would bring it down off the top shelf.
     */
    public static String label(SpellRegistry spells, SkillTrackRegistry tracks, int actorId,
            int spellRaw) {
        SpellDefinition spell = spells.get(spellRaw);
        String reach = spell.targetShape() == TargetShape.SELF ? "self" : "touch";
        if (spell.targetShape() == TargetShape.RANGED) {
            reach = spell.range() + " away";
        }
        String text = spell.displayName() + "  (" + reach + ")";
        if (!known(spells, tracks, actorId, spellRaw)) {
            return text + "  [Lv " + spell.minLevel() + "]";
        }
        return text;
    }

    /**
     * Whether {@code actorId} has read deeply enough for this crafting to be theirs at all —
     * measured against the crafting's OWN declared skill, not a hardcoded one.
     */
    public static boolean known(SpellRegistry spells, SkillTrackRegistry tracks, int actorId,
            int spellRaw) {
        if (actorId == Actor.NONE) {
            return false;
        }
        SpellDefinition spell = spells.get(spellRaw);
        return tracks.level(actorId, tracks.rawOfSkill(spell.skillKey())) >= spell.minLevel();
    }

    /**
     * WHAT IS CURRENTLY ON THIS BODY — the footer under the last button: every live crafted
     * effect the played soul is carrying, summarised. Without it a held +1 AGI is invisible the
     * moment its toast fades, which makes the stat axis feel like it did nothing.
     *
     * <p>Reads the live table by summing rows, exactly as the sim does, so the footer and the
     * checks can never disagree. Returns {@code ""} when nothing is held (the renderer then
     * draws no footer at all).
     */
    public static String heldLine(com.trojia.sim.actor.spell.ActiveEffects effects, int actorId,
            long tick) {
        if (actorId == Actor.NONE) {
            return "";
        }
        StringBuilder out = new StringBuilder();
        int warmth = effects.temperatureOffsetDeciK(actorId);
        if (warmth != 0) {
            out.append(warmth > 0 ? "+" : "-")
               .append(Math.abs(warmth) / 10).append('.').append(Math.abs(warmth) % 10)
               .append(" deg");
        }
        for (com.trojia.sim.progression.AttributeId attribute
                : com.trojia.sim.progression.AttributeId.values()) {
            int nudge = effects.attributeModifier(actorId, attribute.ordinal());
            if (nudge == 0) {
                continue;
            }
            if (out.length() > 0) {
                out.append(", ");
            }
            out.append(nudge > 0 ? "+" : "-").append(Math.abs(nudge)).append(' ')
               .append(attribute.name());
        }
        int rows = effects.liveCountOn(actorId);
        if (out.length() == 0) {
            return rows == 0 ? "" : "held: " + rows + " working";
        }
        return "held: " + out;
    }

    /** The line under the header: what the bar is waiting for. */
    public static String hint(boolean playModeActive) {
        return playModeActive
                ? "click a crafting  (X repeats)"
                : "press P to step into a body";
    }
}
