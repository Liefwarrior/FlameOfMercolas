package com.trojia.client.fpv;

import com.trojia.client.art.TileArtResolver;
import com.trojia.client.atlas.RegionCatalog;
import com.trojia.client.render.DepthVision;
import com.trojia.client.render.TilePlan;
import com.trojia.sim.fluid.FluidRegistry;
import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.world.TileForm;

import java.util.ArrayList;
import java.util.List;

/**
 * <b>What the eye can see, decided without a graphics context.</b> A pure function of
 * {@code (tile lanes, eye position, yaw, viewport)} to an ordered list of {@link ViewQuad}s;
 * {@link FirstPersonRenderer} then does nothing but look up textures and draw them. Every
 * decision that could be wrong — which cells, which bands, which faces, in what order, at what
 * screen coordinates — is made here, where a JUnit test can read it.
 *
 * <h2>The engineering choice, and what was rejected</h2>
 *
 * <p>This is a <b>per-cell perspective projection with painter-ordered quads and billboards</b>
 * — 2D trickery in the Doom sense (there is no depth buffer, no mesh, no matrix stack; the
 * whole scene is textured quads in a sprite batch) but with a real pinhole projection instead
 * of a column raycast.
 *
 * <p><b>Rejected: a Wolf3D/Doom column raycaster.</b> It is the obvious answer and it is the
 * wrong one here, for the reason Eli named when he asked for the Z-axis: Doom could not put a
 * room over a room, and this ward is three walk planes with roof slums above and a flooded
 * harbour below. A raycaster can be extended to resolve a z-stack per traversed cell, but its
 * floors and ceilings become a per-pixel pass no headless test can read, and the extension's
 * span-occlusion bookkeeping is far harder to prove correct than "here are the quads, and here
 * is the cell each one came from".
 *
 * <p><b>Rejected: real 3D — meshes, a projection matrix, a depth buffer.</b> Visibility would
 * live in the GPU, and this project's iron rule is that the verification surface is headless.
 * It would also fight the pixel-snapped 2D pipeline and the whole 16px art path for nothing
 * this view can use.
 *
 * <p><b>What it costs.</b> No true pitch — looking up is a horizon shear
 * ({@link EyeProjection}). Painter's algorithm means overdraw: everything not back-facing,
 * buried, off-screen or hidden under your own feet is drawn, near over far. Texture mapping
 * across a quad is affine, so a floor tile at a grazing angle warps slightly — the PS1 wobble
 * — which at one quad per tile stays small. And wall faces reuse the plan-view {@code wall}
 * region, because no per-face art exists in either shipped pack: the front and the side of a
 * building look the same. That last one is a content gap, not an engine one — the resolver
 * already keys on a form token, so adding a {@code face} token to {@code art-mapping.json}
 * would light it up with no renderer change.
 *
 * <h2>Painter's order: Chebyshev rings, outward-in</h2>
 *
 * <p>Cells are visited in descending {@code max(|dx|, |dy|)} from the eye's cell. That is a
 * correct back-to-front order for unit boxes on a grid seen from a point, because along any
 * ray from the eye the Chebyshev ring is monotonically non-decreasing — a nearer cell can
 * never be occluded by one in a farther ring. It also needs no sort: the ring index IS the
 * order, so the plan comes out in draw order and is stable frame to frame.
 *
 * <p>Within a cell, faces are ordered by how far their surface sits from the eyeline, farthest
 * first, so the harbour bed draws under its own water and a floor draws under the roof that
 * hides it. Occupants come last in their cell, so an actor stands on the floor rather than in
 * it.
 *
 * <h2>The vertical rule is the top-down look-down rule, rotated</h2>
 *
 * <p>Each visible column is scanned outward from the eye's band. <b>Downward</b> it stops at
 * the first cell that would draw something — a solid form, or pooled fluid — capped at
 * {@link DepthVision#MAX_LOOKDOWN} bands. That stop condition is <em>exactly</em>
 * {@code WorldRenderer.cellDrawsSomething} and that reach is exactly the top-down look-down's
 * own, so the deepest band this view shows through a column is the same band
 * {@code DepthVision.visibleBelowZ} shows the top-down view through the same column. The two
 * views cannot disagree about how far down you can see, because they are running one rule.
 * (It is also sound rather than merely convenient: for any band below the eye's own, the eye
 * is strictly above that cell's ceiling plane, so the sight line to anything beneath it must
 * pass through it.)
 *
 * <p><b>Upward</b> there is no such shortcut and the code does not pretend otherwise — a wall
 * at first-floor height does not hide the second floor from someone standing in the street,
 * so every band up to {@link #BANDS_ABOVE} is scanned. What does stop the scan early is the
 * frame itself: {@link EyeProjection#highestVisibleHeight} gives the highest world height that
 * can land on screen for a column at that distance, and the scan stops there. That is an exact
 * cutoff, not a budget — and it is why a nearby column costs one or two bands while only the
 * far ones pay for the whole stack.
 *
 * <p>Depth shading is {@link DepthVision#shade} — one curve, both cameras (applied by the
 * renderer). Everything here is presentation-only, a pure read of the tile lanes; nothing is
 * written, nothing feeds {@code WorldHasher}, and the same eye in the same world plans the
 * same frame on every run and machine.
 */
public final class FirstPersonPlanner {

    /** How far, in tiles, the plan reaches horizontally. */
    public static final int DRAW_RADIUS_TILES = 20;

    /** How many bands below the eye's own the column scan may reach — the top-down
     * look-down's own reach, so the two views see equally deep. */
    public static final int BANDS_BELOW = DepthVision.MAX_LOOKDOWN;

    /** Hard cap on how many bands above the eye's own a column scan may reach. Six covers the
     * ward's tallest authored stack (the docks' roof slums sit five bands over the quay); in
     * practice the on-screen cutoff stops most columns far short of it. */
    public static final int BANDS_ABOVE = 6;

    /** Default UVs for an unclipped quad, in region space: (bl, tl, tr, br). Shared — the
     * common case must not allocate. */
    static final float[] FULL_UVS = {0f, 1f, 0f, 0f, 1f, 0f, 1f, 1f};

    /** The top-down corpse squash, carried over so a body reads the same in both views. */
    public static final float CORPSE_SQUASH = 0.45f;

    private static final int VOID = TileForm.VOID.ordinal();
    private static final int OPEN = TileForm.OPEN.ordinal();
    private static final int WALL = TileForm.WALL.ordinal();

    /** Opaque Q8 alpha. */
    private static final int OPAQUE_Q8 = 256;

    private final MaterialRegistry materials;
    private final FluidRegistry fluids;
    private final TileArtResolver artResolver;
    private final RegionCatalog catalog;

    // Scratch reused across cells within a plan call (single render thread, like every other
    // renderer here). Nothing escapes: quads are copied into the output list.
    private final List<ViewQuad.Terrain> cellFaces = new ArrayList<>(8);
    private final float[] faceHeights = new float[16];
    /** Sutherland-Hodgman buffers: up to 6 vertices of (worldX, worldY, worldHeight, u, v). */
    private final float[] clipIn = new float[6 * 5];
    private final float[] clipOut = new float[6 * 5];
    private final float[] screenX = new float[6];
    private final float[] screenY = new float[6];
    private boolean lastClipTrimmed;

    public FirstPersonPlanner(MaterialRegistry materials, FluidRegistry fluids,
            TileArtResolver artResolver, RegionCatalog catalog) {
        this.materials = materials;
        this.fluids = fluids;
        this.artResolver = artResolver;
        this.catalog = catalog;
    }

    /**
     * Plans one frame.
     *
     * @param eye     the frame's projection (eye position, yaw, viewport)
     * @param eyeBand the absolute world z the eye's actor is standing on
     * @param sight   the tile lanes
     * @param actors  who is standing where
     * @return the frame's quads, farthest first — draw them in this order
     */
    public List<ViewQuad> plan(EyeProjection eye, int eyeBand, CellSight sight,
            ActorSight actors) {
        List<ViewQuad> out = new ArrayList<>(768);
        int eyeCellX = (int) Math.floor(eye.eyeX());
        int eyeCellY = (int) Math.floor(eye.eyeY());
        float maxDepth = DRAW_RADIUS_TILES + 1.5f;
        for (int ring = DRAW_RADIUS_TILES; ring >= 1; ring--) {
            // WITHIN a ring the order matters too, and it is not free. A sight line crosses
            // at most two cells of a ring, they are always neighbours in the same edge row or
            // the same side column, and the farther of the two is always the one further
            // along that row or column from the eye's own axis. So each group is walked from
            // its corners inward — which is a distance sort with no sort. The two edge rows
            // come before the two side columns because the only place a sight line crosses
            // one of each is at a ring's corner, and there the corner cell (an edge-row cell)
            // is the farther one.
            for (int m = ring; m >= 0; m--) {
                maybeColumn(out, eye, eyeBand, sight, actors, eyeCellX - m, eyeCellY - ring,
                        maxDepth);
                maybeColumn(out, eye, eyeBand, sight, actors, eyeCellX - m, eyeCellY + ring,
                        maxDepth);
                if (m != 0) {
                    maybeColumn(out, eye, eyeBand, sight, actors, eyeCellX + m, eyeCellY - ring,
                            maxDepth);
                    maybeColumn(out, eye, eyeBand, sight, actors, eyeCellX + m, eyeCellY + ring,
                            maxDepth);
                }
            }
            for (int m = ring - 1; m >= 0; m--) {
                maybeColumn(out, eye, eyeBand, sight, actors, eyeCellX - ring, eyeCellY - m,
                        maxDepth);
                maybeColumn(out, eye, eyeBand, sight, actors, eyeCellX + ring, eyeCellY - m,
                        maxDepth);
                if (m != 0) {
                    maybeColumn(out, eye, eyeBand, sight, actors, eyeCellX - ring, eyeCellY + m,
                            maxDepth);
                    maybeColumn(out, eye, eyeBand, sight, actors, eyeCellX + ring, eyeCellY + m,
                            maxDepth);
                }
            }
        }
        // The eye's own cell last: nearest, and never frustum-tested (its centre can sit
        // behind the eye plane mid-stride).
        planColumn(out, eye, eyeBand, sight, actors, eyeCellX, eyeCellY, 1f);
        return out;
    }

    private void maybeColumn(List<ViewQuad> out, EyeProjection eye, int eyeBand, CellSight sight,
            ActorSight actors, int cx, int cy, float maxDepth) {
        if (!eye.mayBeVisible(cx + 0.5f, cy + 0.5f, maxDepth)) {
            return;
        }
        float nearDepth = Math.max(EyeProjection.NEAR_PLANE,
                eye.depthOf(cx + 0.5f, cy + 0.5f) - 1f);
        planColumn(out, eye, eyeBand, sight, actors, cx, cy, nearDepth);
    }

    /**
     * Plans one {@code (x, y)} column: the eye's own band and downward to the first cell that
     * draws, then upward until the frame runs out of sky (see the class javadoc's vertical
     * rule).
     *
     * @param nearDepth forward distance to the column's near side, for the upward cutoff
     */
    private void planColumn(List<ViewQuad> out, EyeProjection eye, int eyeBand, CellSight sight,
            ActorSight actors, int cx, int cy, float nearDepth) {
        for (int b = eyeBand; b >= eyeBand - BANDS_BELOW; b--) {
            if (!planCell(out, eye, eyeBand, sight, actors, cx, cy, b)) {
                break;
            }
        }
        float ceiling = eye.highestVisibleHeight(nearDepth);
        for (int b = eyeBand + 1; b <= eyeBand + BANDS_ABOVE; b++) {
            if (BandGeometry.floorHeight(b) > ceiling) {
                break; // this band and everything over it is off the top of the frame
            }
            planCell(out, eye, eyeBand, sight, actors, cx, cy, b);
        }
    }

    /**
     * Emits one cell's faces and occupants.
     *
     * @return {@code true} if a downward scan may continue past this cell — i.e. the cell is
     *         empty air with no pooled fluid. Anything else stops the descent, whether it
     *         draws (a slab, a wall, water) or not (the void border).
     */
    private boolean planCell(List<ViewQuad> out, EyeProjection eye, int eyeBand, CellSight sight,
            ActorSight actors, int cx, int cy, int b) {
        if (!sight.moveTo(cx, cy, b)) {
            return false;
        }
        int form = sight.form();
        if (form == VOID) {
            return false;
        }
        int fluidBits = sight.fluidBits();
        int materialLane = sight.materialId();
        int fluidDepth = TilePlan.fluidDepth(fluidBits);
        int bandDelta = b - eyeBand;

        cellFaces.clear();
        if (form == WALL) {
            planWall(eye, sight, cx, cy, b, materialLane, bandDelta);
        } else if (form != OPEN) {
            // A floor slab is a plane at the base of its band, with no authored thickness and
            // no side art: from above it is the ground, from below it is the ceiling of the
            // band under it (there is no CEILING form in this world — a ceiling IS the slab
            // above). RAMP and STAIR draw the same way; see Face's javadoc for why no slope.
            float h = BandGeometry.floorHeight(b);
            Face face = eye.eyeHeight() >= h ? Face.TOP : Face.BOTTOM;
            TilePlan.BaseTilePlan plan = TilePlan.base(TileForm.ofOrdinal(form), materialLane,
                    cx, cy, b, materials, artResolver, catalog);
            addHorizontal(eye, cx, cy, b, face, false, plan.regionName(), plan.variant(),
                    plan.materialTintRgb(), OPAQUE_Q8, bandDelta, h);
        }
        if (fluidDepth > 0) {
            planFluidSurface(eye, sight, cx, cy, b, fluidBits, fluidDepth, bandDelta);
        }
        flushCellFaces(out);
        planOccupants(out, eye, actors, cx, cy, b, bandDelta);
        return form == OPEN && fluidDepth == 0;
    }

    /**
     * Appends this cell's faces farthest-from-the-eyeline first. Insertion sort over at most
     * a handful of faces — no comparator, no allocation, and stable, so two faces at the same
     * height keep emission order and the plan stays byte-stable.
     */
    private void flushCellFaces(List<ViewQuad> out) {
        int n = cellFaces.size();
        for (int i = 1; i < n; i++) {
            ViewQuad.Terrain q = cellFaces.get(i);
            float key = faceHeights[i];
            int j = i - 1;
            while (j >= 0 && faceHeights[j] < key) {
                cellFaces.set(j + 1, cellFaces.get(j));
                faceHeights[j + 1] = faceHeights[j];
                j--;
            }
            cellFaces.set(j + 1, q);
            faceHeights[j + 1] = key;
        }
        out.addAll(cellFaces);
        cellFaces.clear();
    }

    /**
     * A solid cube: the four sides, plus its roof or its underside. A face is emitted only
     * when it is front-facing (the eye is outside its plane) and its neighbour is see-through
     * — so a wall buried in the granite substrate emits nothing at all, which is what makes a
     * 192x128x16 world affordable to stand inside.
     */
    private void planWall(EyeProjection eye, CellSight sight, int cx, int cy, int b,
            int materialLane, int bandDelta) {
        TilePlan.BaseTilePlan plan = TilePlan.base(TileForm.WALL, materialLane, cx, cy, b,
                materials, artResolver, catalog);
        String region = plan.regionName();
        int variant = plan.variant();
        int tint = plan.materialTintRgb();
        float bottom = BandGeometry.floorHeight(b);
        float top = BandGeometry.ceilingHeight(b);

        if (eye.eyeX() < cx && seeThrough(sight, cx - 1, cy, b)) {
            addSide(eye, cx, cy, b, Face.WEST, region, variant, tint, bandDelta,
                    cx, cy + 1, cx, cy, bottom, top);
        }
        if (eye.eyeX() > cx + 1 && seeThrough(sight, cx + 1, cy, b)) {
            addSide(eye, cx, cy, b, Face.EAST, region, variant, tint, bandDelta,
                    cx + 1, cy, cx + 1, cy + 1, bottom, top);
        }
        if (eye.eyeY() < cy && seeThrough(sight, cx, cy - 1, b)) {
            addSide(eye, cx, cy, b, Face.NORTH, region, variant, tint, bandDelta,
                    cx, cy, cx + 1, cy, bottom, top);
        }
        if (eye.eyeY() > cy + 1 && seeThrough(sight, cx, cy + 1, b)) {
            addSide(eye, cx, cy, b, Face.SOUTH, region, variant, tint, bandDelta,
                    cx + 1, cy + 1, cx, cy + 1, bottom, top);
        }
        if (eye.eyeHeight() > top && seeThrough(sight, cx, cy, b + 1)) {
            addHorizontal(eye, cx, cy, b, Face.TOP, false, region, variant, tint, OPAQUE_Q8,
                    bandDelta, top);
        }
        if (eye.eyeHeight() < bottom && seeThrough(sight, cx, cy, b - 1)) {
            addHorizontal(eye, cx, cy, b, Face.BOTTOM, false, region, variant, tint, OPAQUE_Q8,
                    bandDelta, bottom);
        }
    }

    /**
     * The harbour's defining decision, and the one the top-down view never had to make: where
     * is the surface of a body of water.
     *
     * <p>A fluid cell draws exactly one horizontal translucent surface, and only when nothing
     * is pooled in the cell above it — so the harbour's two-band water column is one surface
     * with the bed under it, not two stacked sheets. The surface sits {@code depth/8} of the
     * way up its own band ({@link BandGeometry#fluidSurfaceHeight}), which puts full-depth
     * harbour water just under the lip of the quay above it and the bathhouse's depth-2 pool
     * ankle-deep on its own floor, from one rule. Alpha comes from the pack's own per-depth
     * curve through the same {@link TilePlan#fluid} chain the top-down overlay uses, so
     * shallow water reads thin and deep water reads solid in both views.
     */
    private void planFluidSurface(EyeProjection eye, CellSight sight, int cx, int cy, int b,
            int fluidBits, int fluidDepth, int bandDelta) {
        if (sight.moveTo(cx, cy, b + 1) && TilePlan.fluidDepth(sight.fluidBits()) > 0) {
            return; // submerged: the surface belongs to the cell above
        }
        TilePlan.FluidOverlay overlay = TilePlan.fluid(fluidBits, cx, cy, b, fluids, artResolver,
                catalog);
        if (overlay == null) {
            return;
        }
        addHorizontal(eye, cx, cy, b, Face.TOP, true, overlay.regionName(), overlay.variant(),
                overlay.tintRgb(), overlay.alphaQ8(), bandDelta,
                BandGeometry.fluidSurfaceHeight(b, fluidDepth));
    }

    /** Whether a neighbouring cell lets a wall face show: anything but another wall or the
     * void border (a floor slab is a plane, not a filled cell, so a wall beside a floor still
     * shows its face). */
    private static boolean seeThrough(CellSight sight, int x, int y, int z) {
        if (!sight.moveTo(x, y, z)) {
            return false;
        }
        int f = sight.form();
        return f != WALL && f != VOID;
    }

    /**
     * Actors, as flat sprites turned square to the camera, standing on their band's floor
     * slab. Emitted after their cell's faces so a figure stands on the ground rather than in
     * it. The top-down view's stack-cascade nudge is deliberately not carried over: occupancy
     * is capped at one per cell sim-side, and the cascade only ever existed to stop two
     * co-located sprites hiding each other on a flat map.
     */
    private void planOccupants(List<ViewQuad> out, EyeProjection eye, ActorSight actors,
            int cx, int cy, int b, int bandDelta) {
        List<ActorSight.Mark> here = actors.at(cx, cy, b);
        if (here.isEmpty()) {
            return;
        }
        float centreX = cx + 0.5f;
        float centreY = cy + 0.5f;
        float depth = eye.depthOf(centreX, centreY);
        if (depth < EyeProjection.NEAR_PLANE) {
            return;
        }
        float lateral = eye.lateralOf(centreX, centreY);
        float sx = eye.screenX(lateral, depth);
        float base = BandGeometry.floorHeight(b);
        for (ActorSight.Mark mark : here) {
            float height = BandGeometry.ACTOR_HEIGHT * mark.sprite().cellsH()
                    * (mark.corpse() ? CORPSE_SQUASH : 1f);
            float halfW = eye.halfWidthPx(mark.sprite().cellsW(), depth);
            float yBottom = eye.screenY(base, depth);
            float yTop = eye.screenY(base + height, depth);
            float[] corners = {
                sx - halfW, yBottom,
                sx - halfW, yTop,
                sx + halfW, yTop,
                sx + halfW, yBottom,
            };
            float dh = base + height / 2f - eye.eyeHeight();
            out.add(new ViewQuad.Billboard(mark.actorId(), cx, cy, b, mark.sprite(),
                    mark.corpse(), bandDelta,
                    (float) Math.sqrt(depth * depth + lateral * lateral + dh * dh), corners));
        }
    }

    // ------------------------------------------------------------------ quad emission

    /**
     * A vertical face: the 2D segment {@code (ax, ay) -> (bx, by)} extruded from {@code bottom}
     * to {@code top}. Every caller passes the viewer's right-hand end as {@code A}, so the
     * region maps the same way round on all four sides of a building.
     */
    private void addSide(EyeProjection eye, int cx, int cy, int b, Face face, String region,
            int variant, int tint, int bandDelta, float ax, float ay, float bx, float by,
            float bottom, float top) {
        set(clipIn, 0, bx, by, bottom, 0f, 1f);
        set(clipIn, 1, bx, by, top, 0f, 0f);
        set(clipIn, 2, ax, ay, top, 1f, 0f);
        set(clipIn, 3, ax, ay, bottom, 1f, 1f);
        emitClipped(eye, 4, cx, cy, b, face, false, region, variant, tint, OPAQUE_Q8, bandDelta,
                (ax + bx) / 2f, (ay + by) / 2f, (bottom + top) / 2f);
    }

    /**
     * A horizontal face: the cell's unit square at world height {@code h}. Region space maps
     * the texture's top-left onto the cell's north-west corner, exactly as the top-down view
     * lays the same tile down — so a paved street is the same paving, the same way up, from
     * either camera.
     */
    private void addHorizontal(EyeProjection eye, int cx, int cy, int b, Face face, boolean fluid,
            String region, int variant, int tint, int alphaQ8, int bandDelta, float h) {
        set(clipIn, 0, cx, cy + 1, h, 0f, 1f);
        set(clipIn, 1, cx, cy, h, 0f, 0f);
        set(clipIn, 2, cx + 1, cy, h, 1f, 0f);
        set(clipIn, 3, cx + 1, cy + 1, h, 1f, 1f);
        emitClipped(eye, 4, cx, cy, b, face, fluid, region, variant, tint, alphaQ8, bandDelta,
                cx + 0.5f, cy + 0.5f, h);
    }

    private static void set(float[] poly, int i, float wx, float wy, float wh, float u, float v) {
        int o = i * 5;
        poly[o] = wx;
        poly[o + 1] = wy;
        poly[o + 2] = wh;
        poly[o + 3] = u;
        poly[o + 4] = v;
    }

    /**
     * Clips a planar polygon against the near plane, projects it, and appends the result to
     * this cell's face list as one or two quads.
     *
     * <p>The near clip is not an optimisation, it is a correctness requirement: the projection
     * divides by forward distance, and mid-stride the eye sits exactly on a cell boundary, so
     * faces beginning at distance zero are routine rather than exotic. Clipping a convex quad
     * by one plane yields three, four or five vertices; a triangle and a quad each pack into
     * one {@code SpriteBatch} quad (a repeated corner makes the triangle's second half
     * degenerate) and a pentagon into two, so the batch never learns anything unusual
     * happened.
     */
    private void emitClipped(EyeProjection eye, int count, int cx, int cy, int b, Face face,
            boolean fluid, String region, int variant, int tint, int alphaQ8, int bandDelta,
            float centroidX, float centroidY, float centroidH) {
        int n = clipNear(eye, count);
        if (n < 3) {
            return;
        }
        for (int i = 0; i < n; i++) {
            int o = i * 5;
            float d = eye.depthOf(clipOut[o], clipOut[o + 1]);
            screenX[i] = eye.screenX(eye.lateralOf(clipOut[o], clipOut[o + 1]), d);
            screenY[i] = eye.screenY(clipOut[o + 2], d);
        }
        if (offScreen(eye, n)) {
            return;
        }
        float depth = eye.depthOf(centroidX, centroidY);
        float lateral = eye.lateralOf(centroidX, centroidY);
        float dh = centroidH - eye.eyeHeight();
        float distance = (float) Math.sqrt(depth * depth + lateral * lateral + dh * dh);
        boolean whole = n == 4 && !lastClipTrimmed;
        for (int i = 1; i + 1 < n; i += 2) {
            int j = Math.min(i + 2, n - 1);
            float[] corners = {
                screenX[0], screenY[0],
                screenX[i], screenY[i],
                screenX[i + 1], screenY[i + 1],
                screenX[j], screenY[j],
            };
            float[] uvs = whole ? FULL_UVS : new float[] {
                clipOut[3], clipOut[4],
                clipOut[i * 5 + 3], clipOut[i * 5 + 4],
                clipOut[(i + 1) * 5 + 3], clipOut[(i + 1) * 5 + 4],
                clipOut[j * 5 + 3], clipOut[j * 5 + 4],
            };
            if (cellFaces.size() >= faceHeights.length) {
                return; // pathological cell; the scratch is sized for real geometry
            }
            faceHeights[cellFaces.size()] = Math.abs(centroidH - eye.eyeHeight());
            cellFaces.add(new ViewQuad.Terrain(cx, cy, b, face, fluid, region, variant, tint,
                    alphaQ8, bandDelta, distance, corners, uvs));
        }
    }

    /** Whole-polygon frustum reject: every corner off the same edge of the viewport. */
    private boolean offScreen(EyeProjection eye, int n) {
        float minX = Float.MAX_VALUE;
        float maxX = -Float.MAX_VALUE;
        float minY = Float.MAX_VALUE;
        float maxY = -Float.MAX_VALUE;
        for (int i = 0; i < n; i++) {
            minX = Math.min(minX, screenX[i]);
            maxX = Math.max(maxX, screenX[i]);
            minY = Math.min(minY, screenY[i]);
            maxY = Math.max(maxY, screenY[i]);
        }
        return maxX < 0f || minX > eye.viewportWidthPx()
                || maxY < 0f || minY > eye.viewportHeightPx();
    }

    /**
     * Sutherland-Hodgman against the single plane {@code depth >= NEAR_PLANE}, interpolating
     * world position and region-space UV. Reads {@link #clipIn}, writes {@link #clipOut}, and
     * records in {@link #lastClipTrimmed} whether anything was actually cut (so the untouched
     * common case can share one immutable UV array).
     *
     * @return the surviving vertex count: 0, or 3..5
     */
    private int clipNear(EyeProjection eye, int count) {
        int n = 0;
        lastClipTrimmed = false;
        for (int i = 0; i < count; i++) {
            int cur = i * 5;
            int prev = ((i + count - 1) % count) * 5;
            float dCur = eye.depthOf(clipIn[cur], clipIn[cur + 1]) - EyeProjection.NEAR_PLANE;
            float dPrev = eye.depthOf(clipIn[prev], clipIn[prev + 1]) - EyeProjection.NEAR_PLANE;
            if (dPrev < 0f != dCur < 0f) {
                float t = dPrev / (dPrev - dCur);
                for (int k = 0; k < 5; k++) {
                    clipOut[n * 5 + k] =
                            clipIn[prev + k] + t * (clipIn[cur + k] - clipIn[prev + k]);
                }
                n++;
                lastClipTrimmed = true;
            }
            if (dCur >= 0f) {
                System.arraycopy(clipIn, cur, clipOut, n * 5, 5);
                n++;
            } else {
                lastClipTrimmed = true;
            }
        }
        return n;
    }
}
