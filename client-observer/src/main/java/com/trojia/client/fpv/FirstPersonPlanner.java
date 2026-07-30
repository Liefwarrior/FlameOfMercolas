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
 * first, so a shallow pool draws over the floor slab it is standing on and a floor draws under
 * the roof that hides it. (That is a <em>within-cell</em> guarantee only: the bed of a deep
 * water column is a different cell, and the shared descent rule stops at the water, so it is
 * never planned at all — see {@link #planFluidSurface}.) Occupants come last in their cell, so
 * an actor stands on the floor rather than in it.
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

    /** {@code excludeActorId} value meaning "hide nobody" — a plan for a disembodied eye. */
    public static final int SHOW_EVERYONE = com.trojia.sim.actor.Actor.NONE;

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
    /** The actor whose eyes this frame is being planned through — never drawn. */
    private int hiddenActorId = SHOW_EVERYONE;

    public FirstPersonPlanner(MaterialRegistry materials, FluidRegistry fluids,
            TileArtResolver artResolver, RegionCatalog catalog) {
        this.materials = materials;
        this.fluids = fluids;
        this.artResolver = artResolver;
        this.catalog = catalog;
    }

    /**
     * Plans one frame for a disembodied eye — every actor in the ward is drawn, including one
     * standing where the eye is. Only the terrain tests want this; the live view always names
     * the actor it is looking out of.
     */
    public List<ViewQuad> plan(EyeProjection eye, int eyeBand, CellSight sight,
            ActorSight actors) {
        return plan(eye, eyeBand, sight, actors, SHOW_EVERYONE);
    }

    /**
     * Plans one frame.
     *
     * <p><b>You cannot see yourself.</b> {@code excludeActorId} is the driven actor — the one
     * whose eyes this is — and it contributes no billboard at all. That is not a nicety: the
     * eye slides continuously while the actor steps on the grid ({@link FirstPersonCamera}),
     * so for most of every stride the sim has already committed the body to the cell ahead
     * while the eye is still crossing into it, and the body's own sprite stands square in the
     * middle of the frame. The exclusion is by id rather than by cell because that is the only
     * form that stays correct mid-stride, when "the eye's cell" and "the actor's cell" are two
     * different cells.
     *
     * @param eye             the frame's projection (eye position, yaw, viewport)
     * @param eyeBand         the absolute world z the eye's actor is standing on
     * @param sight           the tile lanes
     * @param actors          who is standing where
     * @param excludeActorId  the actor being looked out of, or {@link #SHOW_EVERYONE}
     * @return the frame's quads, farthest first — draw them in this order
     */
    public List<ViewQuad> plan(EyeProjection eye, int eyeBand, CellSight sight,
            ActorSight actors, int excludeActorId) {
        this.hiddenActorId = excludeActorId;
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
        planColumn(out, eye, eyeBand, sight, actors, eyeCellX, eyeCellY,
                eye.cellFarDepth(eyeCellX + 0.5f, eyeCellY + 0.5f));
        return out;
    }

    private void maybeColumn(List<ViewQuad> out, EyeProjection eye, int eyeBand, CellSight sight,
            ActorSight actors, int cx, int cy, float maxDepth) {
        if (!eye.mayBeVisible(cx + 0.5f, cy + 0.5f, maxDepth)) {
            return;
        }
        planColumn(out, eye, eyeBand, sight, actors, cx, cy,
                eye.cellFarDepth(cx + 0.5f, cy + 0.5f));
    }

    /**
     * Plans one {@code (x, y)} column: the eye's own band and downward to the first cell that
     * draws, then upward until the frame runs out of sky (see the class javadoc's vertical
     * rule).
     *
     * <p>The upward cutoff is taken at the column's <b>far</b> depth, which is what makes it an
     * exact cutoff rather than an over-eager one. The lowest thing anything in band {@code b}
     * can occupy is that band's floor slab — an actor stands on it, a wall starts at it, the
     * slab itself is it — so if the slab is off the top of the frame at the cell's farthest
     * point, nothing in that band or any band above it has a pixel on screen, and the break is
     * sound.
     *
     * <p><b>The descent stops where the EYE is, not where the body has been committed.</b> A
     * cell only blocks the view below it if you are above it, and mid-climb you are not: the
     * sim commits {@code eyeBand} to the new band the instant the climb resolves, while the
     * eye takes {@link FirstPersonCamera#CLIMB_ARRIVAL_FRACTION} of a step cadence to rise
     * through it. The naive scan started at the committed band, hit its floor slab (a slab
     * draws, so the descent stops), and never planned the band the eye was physically still
     * standing in — for the first ~70 ms of every climb the frame was missing the storey around
     * it, which at a real cadence is four frames of a staircase with no staircase in them. The
     * guard is exact rather than a fudge: {@code eyeHeight >= floorHeight(b)} is precisely
     * "this slab is at or below the eye", which is the condition that makes the stop sound in
     * the first place. The reach is left anchored on {@code eyeBand} so this can only ever
     * <em>add</em> the band under your feet, never see deeper than the tile view does.
     *
     * @param farDepth forward distance to the column's far side ({@link
     *                 EyeProjection#cellFarDepth})
     */
    private void planColumn(List<ViewQuad> out, EyeProjection eye, int eyeBand, CellSight sight,
            ActorSight actors, int cx, int cy, float farDepth) {
        for (int b = eyeBand; b >= eyeBand - BANDS_BELOW; b--) {
            boolean seeThrough = planCell(out, eye, eyeBand, sight, actors, cx, cy, b);
            if (!seeThrough && eye.eyeHeight() >= BandGeometry.floorHeight(b)) {
                break;
            }
        }
        float ceiling = eye.highestVisibleHeight(Math.max(EyeProjection.NEAR_PLANE, farDepth));
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
     * @return {@code true} if a downward scan may continue past this cell — i.e. nothing here
     *         draws. Only a cell that draws (a slab, a wall, pooled water) stops the descent,
     *         which is the tile view's {@code cellDrawsSomething} verbatim; the world's outer
     *         edge stops it because there is no cell to read.
     */
    private boolean planCell(List<ViewQuad> out, EyeProjection eye, int eyeBand, CellSight sight,
            ActorSight actors, int cx, int cy, int b) {
        if (!sight.moveTo(cx, cy, b)) {
            return false;
        }
        int form = sight.form();
        if (form == VOID) {
            // Unauthored void draws nothing and — this is the fix — does not stop the descent
            // either. WorldRenderer.cellDrawsSomething says false for VOID, so the tile view's
            // look-down walks straight through it; stopping here made the two views disagree
            // about how deep a column shows the moment one had a void cell in it.
            return true;
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
     * is pooled in the cell above it — so the harbour's two-band water column is one surface,
     * not two stacked sheets. <b>What that surface composites against is the frame's backdrop,
     * not its own bed</b>, and that is worth stating plainly because the obvious reading is
     * wrong: harbour water is alphaQ8 240 of 256 and you can see through it, but the shared
     * descent rule stops at the first cell that draws and pooled fluid draws, so the bed under
     * it is never planned — exactly as the tile view's own look-down never plans it either.
     * The two views agree; what they agree on is that the bed is not shown. Fixing it means
     * changing the descent rule in <em>both</em> views, which is a bigger decision than a
     * renderer gets to make on its own. It is why the backdrop had to stop being void black
     * ({@link SkyBand#groundHazeAt}). The surface sits {@code depth/8} of the
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
     *
     * <p><b>The billboard is square because the sprite cell is square.</b> Every actor sprite
     * in the shipped index is one 16x16 cell, and the ink inside it measures 11 px across by
     * 15 px tall (median over all 25 actor sprites) — a figure with its own proportions.
     * Drawing that cell one tile wide and {@link BandGeometry#ACTOR_HEIGHT} tall stretched
     * every one of them by 1.9x vertically, so the same dockhand was a square from above and a
     * lamppost from the street. The quad now carries the cell's own aspect
     * ({@code cellsW : cellsH}), sized so the ink still stands at about
     * {@code ACTOR_HEIGHT} — the corpse squash is applied after, because that flattening is
     * deliberate and carried over from the tile view.
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
            if (mark.actorId() == hiddenActorId) {
                continue; // this is you
            }
            float height = BandGeometry.ACTOR_HEIGHT * mark.sprite().cellsH()
                    * (mark.corpse() ? CORPSE_SQUASH : 1f);
            float halfW = eye.halfWidthPx(
                    BandGeometry.ACTOR_HEIGHT * mark.sprite().cellsW(), depth);
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
