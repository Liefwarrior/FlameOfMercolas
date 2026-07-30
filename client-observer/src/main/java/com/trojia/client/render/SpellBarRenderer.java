package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.trojia.client.hud.HudPanel;
import com.trojia.client.hud.icons.IconAtlas;
import com.trojia.client.inspect.SpellAvailability;
import com.trojia.client.inspect.SpellBar;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.spell.SpellRegistry;

import java.util.List;

/**
 * Draws the craftings bar — one DF-black plate per crafting down the right of the screen, in
 * the layout {@link SpellBar} computed. Pure drawing: every rectangle, every string and every
 * gate decision comes from the GL-free classes, so what the tests assert is what the screen
 * shows.
 *
 * <p>Three states, told apart at a glance because a bar you cannot read is a bar you will not
 * use: <b>ready</b> (gold, the reward hue the sheet's headers already use), <b>not yours yet</b>
 * (grey, with the level that would unlock it on the row), and <b>hand still busy</b> (dimmed,
 * with the latch counting down in the header). The caller owns {@code batch}'s begin/end and
 * the y-up, bottom-left projection.
 */
public final class SpellBarRenderer {

    /** Gold — a crafting you can work right now. */
    private static final Color READY = new Color(1f, 0.86f, 0.20f, 1f);
    /** Grey — on the shelf, but not yet yours. */
    private static final Color UNLEARNED = new Color(0.45f, 0.45f, 0.50f, 1f);
    /** Dimmed gold — yours, but the hand is still on the last link. */
    private static final Color LATCHED = new Color(0.55f, 0.48f, 0.22f, 1f);
    /** The header's own colour. */
    private static final Color HEADER = new Color(0.86f, 0.90f, 0.98f, 1f);
    /** Plate fill behind each button, a touch lighter than the panel black so rows separate. */
    private static final Color PLATE = new Color(0.09f, 0.09f, 0.11f, 1f);

    /** Text inset from the plate's left edge, px. */
    private static final float TEXT_INSET_PX = 8f;

    /**
     * Draws the header, the hint and every button.
     *
     * @param buttons the layout from {@link SpellBar#layout} (already positioned for whether
     *                the character sheet is up)
     * @param casterId the played soul, or {@link Actor#NONE} when nobody is being driven
     */
    public void draw(SpriteBatch batch, BitmapFont font, IconAtlas icons, SpellRegistry spells,
            SkillTrackRegistry tracks, ActorRegistry registry,
            com.trojia.sim.actor.spell.ActiveEffects effects, int casterId,
            List<SpellBar.Button> buttons, float viewportWidthPx, float viewportHeightPx,
            boolean sheetShowing, long tick) {
        if (buttons.isEmpty()) {
            return;
        }
        font.getData().setScale(1f);
        float x = SpellBar.leftEdge(viewportWidthPx, sheetShowing);
        float headerBottom = SpellBar.topEdge(viewportHeightPx) - SpellBar.HEADER_HEIGHT;

        long latchLeft = casterId == Actor.NONE ? 0L
                : SpellAvailability.cooldownTicksLeft(registry.get(casterId), tick);

        HudPanel.draw(batch, icons.whitePixel(), x, headerBottom,
                SpellBar.BUTTON_WIDTH, SpellBar.HEADER_HEIGHT);
        font.setColor(HEADER);
        String header = latchLeft > 0
                ? SpellBar.HEADER + "  (" + SpellAvailability.cooldownLine(latchLeft)
                        .replace(SpellAvailability.COOLDOWN_PREFIX, "ready in ") + ")"
                : SpellBar.HEADER + "  -- " + SpellBar.hint(casterId != Actor.NONE);
        font.draw(batch, header, x + TEXT_INSET_PX,
                headerBottom + SpellBar.HEADER_HEIGHT - 5f);

        for (int i = 0; i < buttons.size(); i++) {
            SpellBar.Button button = buttons.get(i);
            HudPanel.draw(batch, icons.whitePixel(), button.x(), button.y(), button.width(),
                    button.height(), PLATE);
            boolean known = SpellBar.known(spells, tracks, casterId, button.spellRaw());
            font.setColor(!known ? UNLEARNED : latchLeft > 0 ? LATCHED : READY);
            font.draw(batch, SpellBar.label(spells, tracks, casterId, button.spellRaw()),
                    button.x() + TEXT_INSET_PX, button.y() + button.height() - 7f);
        }

        // The footer: what this body is currently carrying. Drawn only when something is held,
        // so an unmagicked soul's bar ends at its last button.
        String held = SpellBar.heldLine(effects, casterId, tick);
        if (!held.isEmpty()) {
            SpellBar.Button last = buttons.get(buttons.size() - 1);
            float footerBottom = last.y() - SpellBar.BUTTON_GAP - SpellBar.HEADER_HEIGHT;
            HudPanel.draw(batch, icons.whitePixel(), x, footerBottom, SpellBar.BUTTON_WIDTH,
                    SpellBar.HEADER_HEIGHT);
            font.setColor(READY);
            font.draw(batch, held, x + TEXT_INSET_PX,
                    footerBottom + SpellBar.HEADER_HEIGHT - 5f);
        }
        font.setColor(Color.WHITE);
    }
}
