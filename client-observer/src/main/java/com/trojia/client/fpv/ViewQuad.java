package com.trojia.client.fpv;

import com.trojia.client.sprite.SpriteRef;

/**
 * One planned draw in the first-person frame: four screen-space corners, and a statement of
 * exactly which cell of the simulated world they came from.
 *
 * <p><b>This is the whole verification story.</b> The planner is a pure function
 * {@code (tiles, eye, yaw, viewport) -> ordered list of these}, and every element names its
 * {@code (tileX, tileY, bandZ)}. A headless test can therefore stand the eye in one spot,
 * take the plan, re-read every cell it names through a real {@code TileCursor}, and assert
 * the plan is not lying — no GL context, no framebuffer, no eyeballing a screenshot. That is
 * the reason the view is split planner/renderer at all: the renderer's entire job below is a
 * texture lookup, a colour multiply and a {@code batch.draw} of these corners, and it has no
 * opinions left to get wrong.
 *
 * <p><b>Corner order</b> is the {@code SpriteBatch} quad order, in the source region's own
 * orientation: {@code (bottom-left, top-left, top-right, bottom-right)}, eight floats,
 * screen px, y-up from the bottom-left. "Bottom" and "left" are the <em>texture's</em>, not
 * the screen's — a wall face seen from its far side still maps its region the same way round.
 *
 * <p><b>Draw order is the list order</b>, and the list is a painter's order: farthest first.
 * There is no depth buffer, which is the point — a depth buffer would put visibility on the
 * GPU where no test can read it.
 */
public sealed interface ViewQuad permits ViewQuad.Terrain, ViewQuad.Billboard {

    /** Screen-space corners: {@code x0,y0, x1,y1, x2,y2, x3,y3} (bl, tl, tr, br), y-up. */
    float[] corners();

    /** Distance from the eye to the surface's centre, world units (draw order / haze). */
    float distance();

    /** Signed band offset from the eye's own band: {@code +1} is one storey up. */
    int bandDelta();

    /** World x of the cell this came from. */
    int tileX();

    /** World y of the cell this came from. */
    int tileY();

    /**
     * Absolute world z (band) of the cell this came from. Absolute, never authored-relative —
     * the docks' authored {@code z:+11} quayside is band 19 here, because chunk layer 0 is the
     * void border ring.
     */
    int bandZ();

    /** Which face of a cell this quad shows. */
    Face face();

    /**
     * A face of one tile: a wall's side, a floor slab's top or underside, or a fluid surface.
     *
     * @param face       which face of the cell
     * @param fluid      {@code true} for a fluid surface (translucent), false for terrain
     * @param regionName the atlas region, resolved through the shared
     *                   {@link com.trojia.client.render.TilePlan} chain — the same name and
     *                   the same cosmetic variant the top-down view would draw for this cell
     * @param variant    the cosmetic variant cell within the region
     * @param tintRgb    the material's or fluid's optional secondary tint, or
     *                   {@code TileArtResolver.NO_TINT}
     * @param alphaQ8    Q8 opacity: 256 for terrain, the fluid's per-depth alpha for water
     * @param uvs        per-corner texture coordinates in the region's own {@code [0,1]}
     *                   square, {@code (0,0)} at its top-left, same order as {@code corners}.
     *                   Almost always the whole region; they exist because a face clipped
     *                   against the near plane keeps only part of its texture, and the
     *                   alternative — stretching the region over the surviving sliver — reads
     *                   as the wall smearing whenever you brush past a corner.
     */
    record Terrain(int tileX, int tileY, int bandZ, Face face, boolean fluid,
                   String regionName, int variant, int tintRgb, int alphaQ8,
                   int bandDelta, float distance, float[] corners, float[] uvs)
            implements ViewQuad {
    }

    /**
     * One actor, as a flat sprite squarely facing the camera — Daggerfall's billboards, which
     * is also just what this project's actor art already is: {@code SpriteIndex.forActor} has
     * never had directional frames, so nothing needed building for "living things are flat
     * sprites facing the camera" except the projection.
     *
     * @param actorId the registry id, so a test can name who it saw
     * @param sprite  the per-life sprite pick, identical to the one the top-down view draws
     * @param corpse  a dead body: squashed and blood-dimmed, the top-down treatment carried over
     */
    record Billboard(int actorId, int tileX, int tileY, int bandZ, SpriteRef sprite,
                     boolean corpse, int bandDelta, float distance, float[] corners)
            implements ViewQuad {

        @Override
        public Face face() {
            return Face.BILLBOARD;
        }
    }
}
