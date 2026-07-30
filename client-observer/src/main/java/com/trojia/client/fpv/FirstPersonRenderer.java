package com.trojia.client.fpv;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.trojia.client.art.TileArtResolver;
import com.trojia.client.atlas.TileAtlas;
import com.trojia.client.render.AmbientLight;
import com.trojia.client.render.DepthVision;
import com.trojia.client.render.LampGlowMap;
import com.trojia.client.render.WorldRenderer;
import com.trojia.client.sprite.SpriteSheet;

import java.util.List;

/**
 * Draws a {@link FirstPersonPlanner} plan. That is all it does, and the shortness of that
 * sentence is the design: every decision about what is visible was already made somewhere a
 * test can read, so the only thing left here is a texture lookup, a colour multiply and a
 * quad.
 *
 * <p><b>Why this needs no new batch type.</b> {@code SpriteBatch.draw(Texture, float[], int,
 * int)} takes four raw vertices of {@code (x, y, packedColour, u, v)} and pushes them straight
 * into the mesh, so an arbitrary quad — a wall face receding to a vanishing point, a floor
 * trapezoid — is the same call as a sprite. No {@code PolygonSpriteBatch}, no mesh pipeline,
 * no shader: the first-person frame goes through the exact batch, atlas and nearest-filtered
 * 16px art the tile view uses. Mapping across those quads is affine rather than perspective
 * correct, which is the small texture wobble on a grazing floor noted in the planner.
 *
 * <p><b>Lighting is the same lighting.</b> Depth is {@link DepthVision#shade} — the one curve
 * the tile view's look-down and its depth actors already share, so a floor one band down is
 * the same shade of recessed whichever key you last pressed. What that curve is <em>fed</em> is
 * {@link BandGeometry#lookDownDepth}: bands strictly below the eye's own continuous height,
 * counted off that height rather than off the sim's band index. Above the eye it is zero,
 * because a look-down recession curve has nothing to say about a roof. Lamp pools come from the
 * same
 * precomputed {@link LampGlowMap} with the same lerp, and night water gets the same cool pull
 * as the top-down overlay. The only thing here that has no top-down counterpart is
 * {@link #faceShade}, and it cannot cause a disagreement because the tile view never draws a
 * vertical face at all.
 *
 * <p>Caller owns {@code batch}'s begin/end and the y-up viewport projection, exactly as
 * {@code WorldRenderer.draw} assumes.
 */
public final class FirstPersonRenderer {

    /**
     * Per-face brightness. The oldest trick in first-person rendering and still the best value
     * for money: with one flat texture per material and no lighting model, two walls meeting at
     * a corner are invisible as a corner unless their faces differ. North/south faces sit a
     * touch darker than east/west, and a roof reads brightest because the sky is above it.
     */
    private static float faceShade(Face face) {
        return switch (face) {
            case TOP -> 1f;
            case NORTH, SOUTH -> 0.78f;
            case WEST, EAST -> 0.90f;
            case BOTTOM -> UNDERSIDE_SHADE;
            case BILLBOARD -> 1f;
        };
    }

    /** A ceiling — the underside of the slab above — is lit by nothing and reads as such. */
    private static final float UNDERSIDE_SHADE = 0.55f;

    private static final float ALPHA_Q8_ONE = 256f;

    private final TileAtlas atlas;
    private final SpriteSheet sheet;
    private final LampGlowMap lamps;

    /** One reusable quad: 4 vertices of (x, y, packedColour, u, v). */
    private final float[] vertices = new float[20];

    public FirstPersonRenderer(TileAtlas atlas, SpriteSheet sheet, LampGlowMap lamps) {
        this.atlas = atlas;
        this.sheet = sheet;
        this.lamps = lamps;
    }

    /**
     * Draws the sky band and then the plan, in plan order (farthest first).
     *
     * @param plan       the frame's quads from {@link FirstPersonPlanner#plan}
     * @param eye        the projection they were planned with (for the horizon line)
     * @param whitePixel a 1x1 opaque region, for the sky band
     * @param ambient    the frame's day/night light
     * @param alpha      global opacity, for the mode cross-fade (1 = fully first-person)
     */
    public void draw(SpriteBatch batch, List<ViewQuad> plan, EyeProjection eye,
            TextureRegion whitePixel, AmbientLight ambient, float alpha) {
        if (alpha <= 0f) {
            return;
        }
        drawSky(batch, eye, whitePixel, ambient, alpha);
        float lampF = ambient.lampFactor();
        float eyeHeight = eye.eyeHeight();
        for (ViewQuad quad : plan) {
            if (quad instanceof ViewQuad.Terrain terrain) {
                drawTerrain(batch, terrain, ambient, lampF, alpha, eyeHeight);
            } else if (quad instanceof ViewQuad.Billboard billboard) {
                drawBillboard(batch, billboard, ambient, lampF, alpha, eyeHeight);
            }
        }
        batch.setColor(Color.WHITE);
    }

    /**
     * The frame's backdrop: sky above the horizon, ground haze below it, together covering
     * every pixel of the viewport before a single quad is drawn (see {@link SkyBand} for why
     * both halves are painted rather than just the top one). Nothing else in this class is
     * allowed to assume anything was cleared behind it.
     */
    private void drawSky(SpriteBatch batch, EyeProjection eye, TextureRegion whitePixel,
            AmbientLight ambient, float alpha) {
        float width = eye.viewportWidthPx();
        float horizon = Math.max(0f, Math.min(eye.viewportHeightPx(), eye.horizonY()));
        if (horizon > 0f) {
            SkyBand.Rgb haze = SkyBand.groundHazeAt(ambient);
            batch.setColor(haze.r(), haze.g(), haze.b(), alpha);
            batch.draw(whitePixel, 0f, 0f, width, horizon);
        }
        float skyHeight = eye.viewportHeightPx() - horizon;
        if (skyHeight > 0f) {
            SkyBand.Rgb sky = SkyBand.colorAt(ambient);
            batch.setColor(sky.r(), sky.g(), sky.b(), alpha);
            batch.draw(whitePixel, 0f, horizon, width, skyHeight);
        }
        batch.setColor(Color.WHITE);
    }

    private void drawTerrain(SpriteBatch batch, ViewQuad.Terrain q, AmbientLight ambient,
            float lampF, float alpha, float eyeHeight) {
        TextureRegion region = atlas.region(q.regionName(), q.variant(), 0);
        float ambR = ambient.r();
        float ambG = ambient.g();
        float ambB = ambient.b();
        if (q.fluid()) {
            // The same "night water sits darker and cooler than the land" pull the top-down
            // overlay applies, so the harbour is the same harbour at midnight in both views.
            ambR *= 1f - WorldRenderer.WATER_NIGHT_COOL_R * lampF;
            ambG *= 1f - WorldRenderer.WATER_NIGHT_COOL_G * lampF;
        }
        float[] lit = litAmbient(ambR, ambG, ambB, lampF, q.tileX(), q.tileY(), q.bandZ());
        DepthVision.Shade shade =
                DepthVision.shade(BandGeometry.lookDownDepth(eyeHeight, q.bandZ()));
        float face = faceShade(q.face());
        float r = lit[0] * shade.r() * face;
        float g = lit[1] * shade.g() * face;
        float b = lit[2] * shade.b() * face;
        int tint = q.tintRgb();
        if (tint != TileArtResolver.NO_TINT) {
            r *= ((tint >> 16) & 0xFF) / 255f;
            g *= ((tint >> 8) & 0xFF) / 255f;
            b *= (tint & 0xFF) / 255f;
        }
        float a = alpha * q.alphaQ8() / ALPHA_Q8_ONE;
        push(batch, region, q.corners(), q.uvs(), Color.toFloatBits(r, g, b, a));
    }

    /**
     * One actor: a flat sprite, square to the camera, in the light of the cell it stands on.
     * Sprites draw sharp and dim-only through the depth treatment — the same ruling the
     * top-down depth pass made after rendering the alternative side by side, because at 16px
     * a figure is mostly edge and blurring it dissolves "that is a person down there" into a
     * smudge.
     */
    private void drawBillboard(SpriteBatch batch, ViewQuad.Billboard q, AmbientLight ambient,
            float lampF, float alpha, float eyeHeight) {
        TextureRegion region = sheet.region(q.sprite());
        float[] lit = litAmbient(ambient.r(), ambient.g(), ambient.b(), lampF,
                q.tileX(), q.tileY(), q.bandZ());
        DepthVision.Shade shade =
                DepthVision.shade(BandGeometry.lookDownDepth(eyeHeight, q.bandZ()));
        float r = lit[0] * shade.r();
        float g = lit[1] * shade.g();
        float b = lit[2] * shade.b();
        if (q.corpse()) {
            r *= CORPSE_DIM_R;
            g *= CORPSE_DIM_G;
            b *= CORPSE_DIM_B;
        }
        push(batch, region, q.corners(), FirstPersonPlanner.FULL_UVS,
                Color.toFloatBits(r, g, b, alpha));
    }

    /** The top-down corpse dim, carried over verbatim so the dead read the same either way. */
    private static final float CORPSE_DIM_R = 0.52f;
    private static final float CORPSE_DIM_G = 0.44f;
    private static final float CORPSE_DIM_B = 0.44f;

    private final float[] litScratch = new float[3];

    /** Ambient lifted toward the cell's lamp-glow colour — {@code WorldRenderer.lit}'s lerp. */
    private float[] litAmbient(float ambR, float ambG, float ambB, float lampFactor,
            int x, int y, int z) {
        float r = ambR;
        float g = ambG;
        float b = ambB;
        if (lampFactor > 0f) {
            int glow = lamps.glow(x, y, z);
            if (glow != 0) {
                float s = ((glow >>> 24) & 0xFF) / 255f * lampFactor;
                r += (((glow >> 16) & 0xFF) / 255f - r) * s;
                g += (((glow >> 8) & 0xFF) / 255f - g) * s;
                b += ((glow & 0xFF) / 255f - b) * s;
            }
        }
        litScratch[0] = r;
        litScratch[1] = g;
        litScratch[2] = b;
        return litScratch;
    }

    /**
     * Pushes one arbitrary quad into the batch. Corner order is
     * {@code (bl, tl, tr, br)} in the region's own orientation and the UVs are region-space
     * fractions, so a near-plane-clipped face keeps the part of its texture it kept of itself.
     */
    private void push(SpriteBatch batch, TextureRegion region, float[] corners, float[] uvs,
            float packedColour) {
        float u0 = region.getU();
        float v0 = region.getV();   // libGDX: v is the region's TOP edge
        float du = region.getU2() - u0;
        float dv = region.getV2() - v0;
        for (int i = 0; i < 4; i++) {
            int o = i * 5;
            vertices[o] = corners[i * 2];
            vertices[o + 1] = corners[i * 2 + 1];
            vertices[o + 2] = packedColour;
            vertices[o + 3] = u0 + uvs[i * 2] * du;
            vertices[o + 4] = v0 + uvs[i * 2 + 1] * dv;
        }
        Texture texture = region.getTexture();
        batch.draw(texture, vertices, 0, 20);
    }
}
