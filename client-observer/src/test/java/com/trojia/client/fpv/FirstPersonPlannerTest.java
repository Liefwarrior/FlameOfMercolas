package com.trojia.client.fpv;

import com.trojia.client.render.DepthVision;
import com.trojia.client.render.TilePlan;
import com.trojia.sim.world.TileForm;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The first-person view's visibility rules, over a hand-authored ward with no world and no GL.
 *
 * <p>The tests fall into three groups, matching the three things that could make this view a
 * lie: it must not <b>name a cell that is not what it says it is</b> (agreement), it must not
 * <b>show what cannot be seen or hide what can</b> (occlusion and the vertical rule), and it
 * must <b>agree with the tile view about how far down a column you can see</b>, which is the
 * one place the two cameras could visibly diverge and blame each other.
 */
class FirstPersonPlannerTest {

    private static final int BAND = 19;
    private static final int W = 640;
    private static final int H = 360;

    /** Looking NORTH (-y) from the middle of the scene, standing on the quay band. */
    private static EyeProjection eyeAt(float x, float y, int band) {
        return eyeAt(x, y, band, 0f);
    }

    /** The same, craning up or down by a horizon shear (there is no pitch). */
    private static EyeProjection eyeAt(float x, float y, int band, float shearPx) {
        return EyeProjection.of(x, y,
                BandGeometry.floorHeight(band) + BandGeometry.EYE_HEIGHT,
                (float) (3 * Math.PI / 2), W, H, EyeProjection.DEFAULT_FOV_DEGREES, shearPx);
    }

    /** Craned all the way up — how you actually look at a rooftop from a street. */
    private static final float LOOK_UP = EyeProjection.MAX_SHEAR_FRACTION * H;

    /**
     * Craned all the way down — how you actually look at water below a quay you are standing
     * on. Water a band under your feet sits about five world units below the eye, which at a
     * level gaze does not clear the bottom of the frame until a dozen tiles out; leaning over
     * the edge is the whole reason the shear exists.
     */
    private static final float LOOK_DOWN = -EyeProjection.MAX_SHEAR_FRACTION * H;

    /** A flat granite street at the eye band, twenty tiles square around the eye. */
    private static FpvScene street() {
        return new FpvScene().fill(20, 44, 20, 44, BAND, TileForm.FLOOR, FpvScene.GRANITE);
    }

    // ------------------------------------------------------------------ agreement

    /**
     * Every quad names a cell, and every cell justifies the face claimed for it. A wall face
     * must come from a WALL, a floor slab from a walkable form, a water surface from pooled
     * fluid. This is the executable form of "anything you can see in first person is exactly
     * what the sim says is there".
     */
    @Test
    void everyPlannedFaceIsJustifiedByTheCellItNames() {
        FpvScene scene = street();
        scene.put(32, 26, BAND, TileForm.WALL, FpvScene.OAK);
        scene.fluid(30, 24, BAND - 1, 7);
        scene.put(30, 24, BAND, TileForm.OPEN, 0);

        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 32.5f, BAND), BAND, scene, ActorSight.empty());
        assertFalse(plan.isEmpty(), "an eye standing on a street must see the street");

        for (ViewQuad quad : plan) {
            if (!(quad instanceof ViewQuad.Terrain t)) {
                continue;
            }
            TileForm form = scene.formAt(t.tileX(), t.tileY(), t.bandZ());
            if (t.fluid()) {
                assertTrue(scene.fluidDepthAt(t.tileX(), t.tileY(), t.bandZ()) > 0,
                        "water planned on a dry cell at " + describe(t));
            } else if (t.face().isSide()) {
                assertEquals(TileForm.WALL, form,
                        "a vertical face planned on a non-wall at " + describe(t));
            } else {
                assertTrue(form != TileForm.OPEN && form != TileForm.VOID,
                        "a horizontal surface planned on empty air at " + describe(t));
            }
            assertEquals(BAND, t.bandZ() - t.bandDelta(),
                    "bandDelta must be measured from the eye's own band");
        }
    }

    /**
     * The plan resolves art through the one shared chain, not a copy of it. If this ever fails,
     * the same paving stone is about to look like two different paving stones depending on
     * which key the player last pressed.
     */
    @Test
    void artResolvesThroughTheSameChainTheTileViewUses() {
        FpvScene scene = street();
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 32.5f, BAND), BAND, scene, ActorSight.empty());
        int checked = 0;
        for (ViewQuad quad : plan) {
            if (!(quad instanceof ViewQuad.Terrain t) || t.fluid()) {
                continue;
            }
            TilePlan.BaseTilePlan expected = TilePlan.base(
                    scene.formAt(t.tileX(), t.tileY(), t.bandZ()),
                    scene.materialAt(t.tileX(), t.tileY(), t.bandZ()),
                    t.tileX(), t.tileY(), t.bandZ(),
                    FpvScene.materials(), FpvScene.resolver(), FpvScene.catalog());
            assertEquals(expected.regionName(), t.regionName(), describe(t));
            assertEquals(expected.variant(), t.variant(),
                    "cosmetic variant diverged from the tile view at " + describe(t));
            assertEquals(expected.materialTintRgb(), t.tintRgb(), describe(t));
            checked++;
        }
        assertTrue(checked > 20, "expected a street's worth of tiles, got " + checked);
    }

    // ------------------------------------------------------------------ occlusion

    @Test
    void aWallBuriedInSolidRockShowsNothingAtAll() {
        FpvScene scene = street();
        // A 3x3x3 block of wall with a wall at its centre: the middle cell has no exposed face.
        for (int z = BAND; z <= BAND + 2; z++) {
            scene.fill(30, 32, 24, 26, z, TileForm.WALL, FpvScene.GRANITE);
        }
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(31.5f, 32.5f, BAND), BAND, scene, ActorSight.empty());
        assertTrue(quadsFor(plan, 31, 25, BAND + 1).isEmpty(),
                "the cell in the middle of a solid block drew something");
        assertFalse(quadsFor(plan, 31, 26, BAND).isEmpty(),
                "the near face of the block should be visible");
    }

    @Test
    void onlyTheFacesTurnedTowardTheEyeAreDrawn() {
        FpvScene scene = street();
        scene.put(32, 26, BAND, TileForm.WALL, FpvScene.OAK);
        // Standing south of it, looking north: its SOUTH face is toward us, NORTH is not.
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 32.5f, BAND), BAND, scene, ActorSight.empty());
        List<ViewQuad.Terrain> faces = quadsFor(plan, 32, 26, BAND);
        assertTrue(faces.stream().anyMatch(q -> q.face() == Face.SOUTH),
                "the face turned toward the eye is missing");
        assertFalse(faces.stream().anyMatch(q -> q.face() == Face.NORTH),
                "a back face was drawn");
        assertFalse(faces.stream().anyMatch(q -> q.face() == Face.TOP),
                "the roof of a wall at eye level cannot be seen from eye level");
    }

    @Test
    void nearCellsAreDrawnAfterTheFarOnesTheyHide() {
        FpvScene scene = street();
        scene.put(32, 28, BAND, TileForm.WALL, FpvScene.OAK);   // near
        scene.put(32, 24, BAND, TileForm.WALL, FpvScene.OAK);   // far, directly behind it
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 32.5f, BAND), BAND, scene, ActorSight.empty());
        int far = firstIndexOf(plan, 32, 24, BAND);
        int near = firstIndexOf(plan, 32, 28, BAND);
        assertTrue(far >= 0 && near >= 0, "both walls should be planned");
        assertTrue(far < near,
                "painter's order broken: the far wall must be drawn before the near one");
    }

    /**
     * Ring order alone is not enough: a sight line can cross two cells of the SAME ring, and
     * the nearer of the two must still be drawn last. Two walls one tile apart along a
     * diagonal sight line is the case that catches a naive left-to-right ring walk.
     */
    @Test
    void twoCellsOfOneRingOnOneSightLineAreStillOrderedFarToNear() {
        FpvScene scene = street();
        scene.put(36, 33, BAND, TileForm.WALL, FpvScene.OAK);   // ring 4, nearer
        scene.put(36, 34, BAND, TileForm.WALL, FpvScene.OAK);   // ring 4, farther
        EyeProjection eye = EyeProjection.of(32.5f, 32.5f,
                BandGeometry.floorHeight(BAND) + BandGeometry.EYE_HEIGHT,
                0.36f, W, H, EyeProjection.DEFAULT_FOV_DEGREES, 0f);
        List<ViewQuad> plan = FpvScene.planner().plan(eye, BAND, scene, ActorSight.empty());
        int nearer = firstIndexOf(plan, 36, 33, BAND);
        int farther = firstIndexOf(plan, 36, 34, BAND);
        assertTrue(nearer >= 0 && farther >= 0, "both same-ring walls should be planned");
        assertTrue(farther < nearer,
                "same-ring painter order broken: the farther cell must be drawn first");
    }

    @Test
    void theWholePlanIsOrderedFarToNearByRing() {
        FpvScene scene = street();
        scene.fill(24, 40, 22, 30, BAND + 1, TileForm.WALL, FpvScene.GRANITE);
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 32.5f, BAND), BAND, scene, ActorSight.empty());
        int previousRing = Integer.MAX_VALUE;
        for (ViewQuad quad : plan) {
            int ring = Math.max(Math.abs(quad.tileX() - 32), Math.abs(quad.tileY() - 32));
            assertTrue(ring <= previousRing,
                    "ring order broken at " + quad.tileX() + "," + quad.tileY()
                            + ": " + ring + " after " + previousRing);
            previousRing = ring;
        }
    }

    @Test
    void everyQuadKnowsHowFarAwayItIs() {
        FpvScene scene = street();
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 32.5f, BAND), BAND, scene, ActorSight.empty());
        for (ViewQuad quad : plan) {
            float straightLine = (float) Math.hypot(quad.tileX() + 0.5f - 32.5f,
                    quad.tileY() + 0.5f - 32.5f);
            assertTrue(quad.distance() >= straightLine - 1.0f
                            && quad.distance() <= straightLine + 3.0f,
                    "reported distance " + quad.distance() + " does not match the cell at "
                            + quad.tileX() + "," + quad.tileY() + " (" + straightLine + ")");
        }
        // And it is monotone with the ring, which is what makes the plan order meaningful.
        float nearest = Float.MAX_VALUE;
        float farthest = 0f;
        for (ViewQuad quad : plan) {
            nearest = Math.min(nearest, quad.distance());
            farthest = Math.max(farthest, quad.distance());
        }
        assertTrue(farthest > nearest * 2f, "the plan should span a range of distances");
    }

    // ------------------------------------------------------------------ the z-axis

    /**
     * The claim the whole "two views cannot disagree" story rests on: through a column of empty
     * air, first person shows the same band the tile view's look-down shows, and stops at the
     * same place for the same reason.
     */
    @Test
    void theDownwardReachMatchesTheTileViewsLookDown() {
        FpvScene scene = street();
        // Cut a hole in the quay and put water two bands under it, over a bed two more down.
        for (int y = 22; y <= 28; y++) {
            for (int x = 28; x <= 36; x++) {
                scene.put(x, y, BAND, TileForm.OPEN, 0);
            }
        }
        scene.fill(28, 36, 22, 28, BAND - 4, TileForm.FLOOR, FpvScene.GRANITE);
        for (int y = 22; y <= 28; y++) {
            for (int x = 28; x <= 36; x++) {
                scene.fluid(x, y, BAND - 2, 7);
            }
        }
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 32.5f, BAND, LOOK_DOWN), BAND, scene, ActorSight.empty());

        int belowBandQuads = 0;
        for (ViewQuad quad : plan) {
            if (quad.bandDelta() >= 0) {
                continue;
            }
            belowBandQuads++;
            assertEquals(BAND - 2, quad.bandZ(),
                    "column " + quad.tileX() + "," + quad.tileY() + " showed band "
                            + quad.bandZ() + "; the tile view's look-down stops at the water");
        }
        assertTrue(belowBandQuads > 4,
                "expected to see the harbour through the hole, saw " + belowBandQuads);
        // And the bed under the water is not shown, exactly as the tile view would not show it.
        assertTrue(quadsFor(plan, 32, 24, BAND - 4).isEmpty(),
                "saw the harbour bed through opaque water");
    }

    @Test
    void aFloorUnderfootHidesEverythingBelowIt() {
        FpvScene scene = street();
        scene.fill(20, 44, 20, 44, BAND - 3, TileForm.FLOOR, FpvScene.OAK);
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 32.5f, BAND), BAND, scene, ActorSight.empty());
        assertTrue(quadsFor(plan, 32, 28, BAND - 3).isEmpty(),
                "a cellar was visible through a solid street");
    }

    @Test
    void theColumnNeverReachesPastTheSharedLookDownBudget() {
        FpvScene scene = new FpvScene(); // all air except one very deep floor
        scene.fill(20, 44, 20, 44, BAND - DepthVision.MAX_LOOKDOWN - 1, TileForm.FLOOR,
                FpvScene.GRANITE);
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 32.5f, BAND), BAND, scene, ActorSight.empty());
        assertTrue(plan.isEmpty(),
                "a floor one band past the shared look-down reach must be invisible, exactly "
                        + "as it is from above");
    }

    @Test
    void roofsAboveAreVisibleAndReadAsAbove() {
        FpvScene scene = street();
        scene.fill(26, 38, 14, 20, BAND + 3, TileForm.FLOOR, FpvScene.OAK);
        EyeProjection eye = eyeAt(32.5f, 32.5f, BAND, LOOK_UP);
        List<ViewQuad> plan = FpvScene.planner().plan(eye, BAND, scene, ActorSight.empty());
        List<ViewQuad.Terrain> roof = quadsFor(plan, 32, 18, BAND + 3);
        assertFalse(roof.isEmpty(), "a rooftop three bands up should be in view from a street");
        ViewQuad.Terrain slab = roof.get(0);
        assertEquals(3, slab.bandDelta());
        assertEquals(Face.BOTTOM, slab.face(),
                "seen from below, a floor slab is a ceiling — there is no CEILING form");
        assertTrue(highestCornerY(slab) > eye.horizonY(),
                "a roof three bands up must project above the horizon");
    }

    @Test
    void aStackedBuildingShowsEveryStoreyNotJustTheFirst() {
        FpvScene scene = street();
        for (int z = BAND; z <= BAND + 2; z++) {
            scene.fill(30, 34, 24, 26, z, TileForm.WALL, FpvScene.OAK);
        }
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 34.5f, BAND, LOOK_UP), BAND, scene, ActorSight.empty());
        for (int z = BAND; z <= BAND + 2; z++) {
            assertFalse(quadsFor(plan, 32, 26, z).isEmpty(),
                    "storey at band " + z + " vanished — rooms over rooms is the whole point");
        }
    }

    /**
     * The harbour surface has to land somewhere, and the somewhere has to be defensible: full
     * depth-7 water sits just under the lip of the walk plane above it, not level with it and
     * not a whole band below.
     */
    @Test
    void deepWaterSitsJustUnderTheQuayItLapsAgainst() {
        float surface = BandGeometry.fluidSurfaceHeight(BAND - 1, 7);
        float quay = BandGeometry.floorHeight(BAND);
        assertTrue(surface < quay, "water must not stand above the quay it laps");
        assertTrue(quay - surface < BandGeometry.BAND_HEIGHT / 4f,
                "deep water a band down should be close under the lip, not a storey below it");
        assertTrue(BandGeometry.fluidSurfaceHeight(BAND, 2)
                        < BandGeometry.floorHeight(BAND) + BandGeometry.EYE_HEIGHT / 2f,
                "a shallow pool should be ankle-deep, not chest-deep");
    }

    // ------------------------------------------------------------------ actors

    @Test
    void actorsPlanAsBillboardsStandingOnTheirOwnBand() {
        FpvScene scene = street();
        com.trojia.client.sprite.SpriteRef sprite = new com.trojia.client.sprite.SpriteRef(
                "dockhand", 0, 0, 1, 1, 1, java.util.Set.of("actor"));
        ActorSight actors = (x, y, z) -> x == 32 && y == 27 && z == BAND
                ? List.of(new ActorSight.Mark(77, sprite, false))
                : ActorSight.NONE_HERE;
        List<ViewQuad> plan = FpvScene.planner()
                .plan(eyeAt(32.5f, 32.5f, BAND), BAND, scene, actors);

        ViewQuad.Billboard billboard = null;
        int floorIndex = -1;
        for (int i = 0; i < plan.size(); i++) {
            ViewQuad q = plan.get(i);
            if (q instanceof ViewQuad.Billboard b) {
                billboard = b;
            } else if (q.tileX() == 32 && q.tileY() == 27 && q.bandZ() == BAND) {
                floorIndex = i;
            }
        }
        assertNotNull(billboard, "the actor five tiles ahead was not planned");
        assertEquals(77, billboard.actorId());
        assertEquals(BAND, billboard.bandZ());
        assertEquals(0, billboard.bandDelta());
        assertTrue(plan.indexOf(billboard) > floorIndex,
                "an actor must be drawn after the floor it is standing on, not under it");
        float[] c = billboard.corners();
        assertEquals(c[0], c[2], 1e-4, "a billboard's left edge must be vertical on screen");
        assertEquals(c[4], c[6], 1e-4, "a billboard's right edge must be vertical on screen");
        assertTrue(c[3] > c[1], "the billboard must stand up, not hang down");
    }

    @Test
    void aDeadBodyPlansSquashedLikeItDoesFromAbove() {
        FpvScene scene = street();
        com.trojia.client.sprite.SpriteRef sprite = new com.trojia.client.sprite.SpriteRef(
                "body", 0, 0, 1, 1, 1, java.util.Set.of("actor"));
        ActorSight standing = (x, y, z) -> x == 32 && y == 27 && z == BAND
                ? List.of(new ActorSight.Mark(5, sprite, false)) : ActorSight.NONE_HERE;
        ActorSight dead = (x, y, z) -> x == 32 && y == 27 && z == BAND
                ? List.of(new ActorSight.Mark(5, sprite, true)) : ActorSight.NONE_HERE;
        EyeProjection eye = eyeAt(32.5f, 32.5f, BAND);
        float tall = billboardHeight(FpvScene.planner().plan(eye, BAND, scene, standing));
        float flat = billboardHeight(FpvScene.planner().plan(eye, BAND, scene, dead));
        assertEquals(tall * FirstPersonPlanner.CORPSE_SQUASH, flat, 0.5f,
                "the corpse squash must carry over from the tile view");
    }

    // ------------------------------------------------------------------ helpers

    private static String describe(ViewQuad.Terrain t) {
        return "(" + t.tileX() + "," + t.tileY() + ",z" + t.bandZ() + ") " + t.face();
    }

    private static List<ViewQuad.Terrain> quadsFor(List<ViewQuad> plan, int x, int y, int z) {
        return plan.stream()
                .filter(q -> q instanceof ViewQuad.Terrain)
                .map(q -> (ViewQuad.Terrain) q)
                .filter(q -> q.tileX() == x && q.tileY() == y && q.bandZ() == z)
                .toList();
    }

    private static int firstIndexOf(List<ViewQuad> plan, int x, int y, int z) {
        for (int i = 0; i < plan.size(); i++) {
            ViewQuad q = plan.get(i);
            if (q.tileX() == x && q.tileY() == y && q.bandZ() == z) {
                return i;
            }
        }
        return -1;
    }

    private static float highestCornerY(ViewQuad quad) {
        float[] c = quad.corners();
        return Math.max(Math.max(c[1], c[3]), Math.max(c[5], c[7]));
    }

    private static float billboardHeight(List<ViewQuad> plan) {
        for (ViewQuad q : plan) {
            if (q instanceof ViewQuad.Billboard b) {
                float[] c = b.corners();
                return Math.abs(c[3] - c[1]);
            }
        }
        throw new AssertionError("no billboard planned");
    }
}
