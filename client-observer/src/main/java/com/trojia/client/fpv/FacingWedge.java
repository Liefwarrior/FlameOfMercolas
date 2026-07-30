package com.trojia.client.fpv;

import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;

/**
 * The arrowhead drawn on the driven actor in the <b>top-down</b> view, pointing along the
 * first-person view yaw.
 *
 * <p>It is here, in the first-person package, because it is not decoration — it is half of the
 * mode switch. {@code Actor.facing()} has always existed and has always printed on the
 * character sheet, but nothing ever rendered it, so before this a player pressing the
 * first-person key had no way of knowing which way they were about to be looking. Show the
 * direction on the map, let the player aim it before they switch, and the switch stops being
 * disorienting: you were already pointed that way.
 *
 * <p>It tracks the <em>view yaw</em>, not the sim's four-way facing, and that is deliberate.
 * The view yaw is what the first-person camera will actually use; the sim's facing is written
 * only on a committed step and takes the x component of a diagonal, so a wedge following it
 * would promise east and deliver north-east. The yaw is seeded from the facing once and is
 * the player's after that.
 *
 * <p>{@link #corners} is pure screen arithmetic and is where the test lives; the draw below is
 * three vertices and a repeat.
 */
public final class FacingWedge {

    /** How far the tip reaches from the tile's centre, as a fraction of the tile span. */
    private static final float TIP = 0.72f;

    /**
     * How far the two base corners sit from the centre, as a fraction of the tile span.
     * Together with {@link #SPREAD} this puts the whole chevron <em>ahead</em> of the body
     * rather than centred on it: a marker that covers the actor it is describing tells you
     * which way you are pointing at the price of hiding who is pointing.
     */
    private static final float BASE = 0.40f;

    /** Half-angle of the arrowhead, radians. */
    private static final float SPREAD = (float) Math.toRadians(33);

    private FacingWedge() {
    }

    /**
     * The arrowhead's screen-space corners: {@code x0,y0, x1,y1, x2,y2, x3,y3} with the fourth
     * repeating the third, so the quad's second triangle collapses and a plain
     * {@code SpriteBatch} draws a triangle.
     *
     * <p>Screen y is <b>up</b> (the libGDX viewport projection) while world y runs <b>south</b>,
     * so a world direction {@code (cos, sin)} points at screen {@code (cos, -sin)}. Getting
     * that flip wrong points the wedge at the player's back, which is the one bug this whole
     * marker exists to prevent.
     *
     * @param centreX screen x of the actor's tile centre
     * @param centreY screen y of the actor's tile centre, y-up
     * @param spanPx  the drawn tile edge in screen px
     * @param yaw     the view yaw (0 = EAST, increasing toward SOUTH)
     */
    public static float[] corners(float centreX, float centreY, float spanPx, float yaw) {
        float tipX = centreX + (float) Math.cos(yaw) * TIP * spanPx;
        float tipY = centreY - (float) Math.sin(yaw) * TIP * spanPx;
        float leftAngle = yaw - SPREAD;
        float rightAngle = yaw + SPREAD;
        float lx = centreX + (float) Math.cos(leftAngle) * BASE * spanPx;
        float ly = centreY - (float) Math.sin(leftAngle) * BASE * spanPx;
        float rx = centreX + (float) Math.cos(rightAngle) * BASE * spanPx;
        float ry = centreY - (float) Math.sin(rightAngle) * BASE * spanPx;
        return new float[] {tipX, tipY, lx, ly, rx, ry, rx, ry};
    }

    /** Draws a planned wedge as a flat-coloured triangle. */
    public static void draw(SpriteBatch batch, TextureRegion whitePixel, float[] corners,
            float packedColour) {
        float u = whitePixel.getU();
        float v = whitePixel.getV();
        float[] verts = new float[20];
        for (int i = 0; i < 4; i++) {
            int o = i * 5;
            verts[o] = corners[i * 2];
            verts[o + 1] = corners[i * 2 + 1];
            verts[o + 2] = packedColour;
            verts[o + 3] = u;
            verts[o + 4] = v;
        }
        batch.draw(whitePixel.getTexture(), verts, 0, 20);
    }
}
