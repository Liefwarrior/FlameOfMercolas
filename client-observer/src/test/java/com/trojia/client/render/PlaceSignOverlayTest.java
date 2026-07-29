package com.trojia.client.render;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link PlaceSignOverlay}: the GL-free building-label planner — hanging signs culled to the
 * camera box, the depth-vision rule inherited verbatim from the fishing overlay, and the
 * single-box clutter law. Headless: the places are hand-built records, the depth column is a
 * synthetic lambda.
 */
class PlaceSignOverlayTest {

    private static final int VIEW_Z = 11;

    /** A tight shop: door at (20,29), footprint (18,30)-(24,36). */
    private static PlaceSign shop() {
        return new PlaceSign("Brann's Chandlery", "ship stores", 20, 29, VIEW_Z, 18, 30, 24, 36);
    }

    /** A long shed whose door is way off to the west: door (4,85), footprint (5,82)-(67,90). */
    private static PlaceSign ropewalk() {
        return new PlaceSign("The Ropewalk", "the shed where cable is laid",
                4, 85, VIEW_Z, 5, 82, 67, 90);
    }

    /** A place one z-band below the view plane: door (40,50), footprint (40,50)-(44,54). */
    private static PlaceSign below() {
        return new PlaceSign("The Quayward Compound", "mansion and condos",
                40, 50, VIEW_Z - 1, 40, 50, 44, 54);
    }

    /** The whole map is in view. */
    private static PlaceSignOverlay.Plan planAt(List<PlaceSign> signs, DepthSight depth,
            int attentionX, int attentionY, int approach) {
        return PlaceSignOverlay.plan(signs, VIEW_Z, 0, 200, 0, 200, depth,
                attentionX, attentionY, true, approach);
    }

    /** Every column is solid at the view z: nothing below the plane is ever seen through it. */
    private static final DepthSight OCCLUDED = (viewZ, x, y) -> DepthSight.NONE;

    // ------------------------------------------------------------------ hanging signs

    @Test
    void everySameZPlaceHangsASignAtItsDoor() {
        PlaceSignOverlay.Plan plan = planAt(List.of(shop(), ropewalk()), OCCLUDED, 0, 0,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES);
        assertEquals(2, plan.markers().size());
        assertEquals(20, plan.markers().get(0).tileX());
        assertEquals(29, plan.markers().get(0).tileY());
        assertEquals(0, plan.markers().get(0).depth());
        assertEquals(4, plan.markers().get(1).tileX());
    }

    @Test
    void signsOutsideTheCameraBoxAreCulled() {
        PlaceSignOverlay.Plan plan = PlaceSignOverlay.plan(List.of(shop(), ropewalk()), VIEW_Z,
                10, 60, 10, 60, OCCLUDED, 0, 0, false,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES);
        assertEquals(1, plan.markers().size(), "only the shop's door is on screen");
        assertEquals(20, plan.markers().get(0).tileX());
    }

    @Test
    void anEmptyWardPlansNothing() {
        assertEquals(PlaceSignOverlay.Plan.NOTHING,
                planAt(List.of(), OCCLUDED, 20, 32, PlaceSignOverlay.OBSERVER_APPROACH_TILES));
    }

    // --------------------------------------------------------------- the depth rule

    @Test
    void aPlaceBelowThePlaneDrawsOnlyWhereTheLookDownResolvesToIt() {
        // The column over the compound's door genuinely shows z10 through empty air.
        DepthSight open = (viewZ, x, y) -> x == 40 && y == 50 ? VIEW_Z - 1 : DepthSight.NONE;
        PlaceSignOverlay.Plan plan = planAt(List.of(below()), open, 42, 52,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES);
        assertEquals(1, plan.markers().size());
        assertEquals(1, plan.markers().get(0).depth(), "one band down");
        assertNotNull(plan.box());
        assertEquals(1, plan.box().depth());
    }

    @Test
    void aPlaceUnderTheRoofYouAreLookingAtVanishesSignAndBoxTogether() {
        // Eli's own hazard: a sign two bands below must not float over the roof in view.
        PlaceSignOverlay.Plan plan = planAt(List.of(below()), OCCLUDED, 42, 52,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES);
        assertTrue(plan.markers().isEmpty(), "occluded column: no hanging sign");
        assertNull(plan.box(), "and no words either");
    }

    @Test
    void aPlaceAboveTheViewPlaneNeverDraws() {
        PlaceSign attic = new PlaceSign("Saltgate Watch-Post", "the ward's one watch station",
                20, 29, VIEW_Z + 2, 18, 30, 24, 36);
        PlaceSignOverlay.Plan plan = planAt(List.of(attic), OCCLUDED, 20, 32,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES);
        assertTrue(plan.markers().isEmpty());
        assertNull(plan.box());
    }

    // ------------------------------------------------------------------- the one box

    @Test
    void theBoxSpeaksTheGazetteersOwnTwoLines() {
        PlaceSignOverlay.Box box = planAt(List.of(shop()), OCCLUDED, 21, 33,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES).box();
        assertNotNull(box);
        assertEquals("Brann's Chandlery", box.place());
        assertEquals("ship stores", box.what());
        assertEquals(21, box.anchorTileX(), "anchored on the attention tile, not the door");
        assertEquals(33, box.anchorTileY());
    }

    @Test
    void standingDeepInsideALongShedStillNamesIt() {
        // Attention at (40,86) is 36 tiles from the Ropewalk's door and INSIDE its footprint:
        // distance-to-rect is what makes this work, distance-to-door never would.
        PlaceSignOverlay.Box box = planAt(List.of(ropewalk()), OCCLUDED, 40, 86,
                PlaceSignOverlay.PLAY_APPROACH_TILES).box();
        assertNotNull(box);
        assertEquals("The Ropewalk", box.place());
    }

    @Test
    void aLongShedNamesItselfEvenWhenItsDoorIsOffScreen() {
        PlaceSignOverlay.Plan plan = PlaceSignOverlay.plan(List.of(ropewalk()), VIEW_Z,
                30, 60, 70, 95, OCCLUDED, 40, 86, true, PlaceSignOverlay.PLAY_APPROACH_TILES);
        assertTrue(plan.markers().isEmpty(), "the west door is not on screen");
        assertNotNull(plan.box(), "but the shed you are standing in still speaks");
        assertEquals("The Ropewalk", plan.box().place());
    }

    @Test
    void onlyEverOneBoxNoMatterHowManyPlacesCrowdTheAttentionPoint() {
        // Five overlapping shops all within reach of one point: the ward may not drown.
        List<PlaceSign> crowd = List.of(
                new PlaceSign("A", "a", 10, 9, VIEW_Z, 10, 10, 12, 12),
                new PlaceSign("B", "b", 13, 9, VIEW_Z, 13, 10, 15, 12),
                new PlaceSign("C", "c", 16, 9, VIEW_Z, 16, 10, 18, 12),
                new PlaceSign("D", "d", 10, 13, VIEW_Z, 10, 13, 12, 15),
                new PlaceSign("E", "e", 13, 13, VIEW_Z, 13, 13, 15, 15));
        PlaceSignOverlay.Plan plan = planAt(crowd, OCCLUDED, 13, 11,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES);
        assertEquals(5, plan.markers().size(), "all five doors still show their mark");
        assertNotNull(plan.box());
        assertEquals("B", plan.box().place(), "the one the cursor is actually inside");
        assertEquals(1, plan.markers().stream().filter(PlaceSignOverlay.Marker::named).count(),
                "exactly one sign is lit as the named one");
    }

    @Test
    void theNearestPlaceWinsAndTiesBreakOnAscendingOrder() {
        PlaceSign west = new PlaceSign("West", "w", 10, 9, VIEW_Z, 8, 10, 10, 12);
        PlaceSign east = new PlaceSign("East", "e", 16, 9, VIEW_Z, 14, 10, 16, 12);
        // (12,11) is 2 tiles from both rects — the first in list order takes it.
        assertEquals("West", planAt(List.of(west, east), OCCLUDED, 12, 11,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES).box().place());
        assertEquals("East", planAt(List.of(east, west), OCCLUDED, 12, 11,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES).box().place());
        // One tile east and East is genuinely nearer, order notwithstanding.
        assertEquals("East", planAt(List.of(west, east), OCCLUDED, 13, 11,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES).box().place());
    }

    @Test
    void openWaterSaysNothing() {
        PlaceSignOverlay.Plan plan = planAt(List.of(shop()), OCCLUDED, 100, 100,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES);
        assertEquals(1, plan.markers().size(), "the shop's own sign still hangs");
        assertNull(plan.box(), "but nothing is being asked about");
        assertFalse(plan.markers().get(0).named());
    }

    @Test
    void theModesDifferExactlyByReach() {
        // Three tiles clear of the shop's west wall: an observer pointing there gets the name,
        // a played actor standing there does not (the doorway rule).
        assertNotNull(planAt(List.of(shop()), OCCLUDED, 15, 33,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES).box());
        assertNull(planAt(List.of(shop()), OCCLUDED, 15, 33,
                PlaceSignOverlay.PLAY_APPROACH_TILES).box());
        // One step from the wall: both speak.
        assertNotNull(planAt(List.of(shop()), OCCLUDED, 17, 33,
                PlaceSignOverlay.PLAY_APPROACH_TILES).box());
    }

    @Test
    void noAttentionPointMeansSignsWithoutWords() {
        PlaceSignOverlay.Plan plan = PlaceSignOverlay.plan(List.of(shop()), VIEW_Z,
                0, 200, 0, 200, OCCLUDED, 21, 33, false,
                PlaceSignOverlay.OBSERVER_APPROACH_TILES);
        assertEquals(1, plan.markers().size());
        assertNull(plan.box());
    }

    @Test
    void planningIsPureAndRepeatable() {
        List<PlaceSign> signs = List.of(shop(), ropewalk(), below());
        DepthSight open = (viewZ, x, y) -> x == 40 && y == 50 ? VIEW_Z - 1 : DepthSight.NONE;
        assertEquals(planAt(signs, open, 21, 33, PlaceSignOverlay.OBSERVER_APPROACH_TILES),
                planAt(signs, open, 21, 33, PlaceSignOverlay.OBSERVER_APPROACH_TILES));
    }

    @Test
    void footprintDistanceIsZeroInsideAndRectilinearOutside() {
        PlaceSign shop = shop();
        assertEquals(0, shop.distanceTo(21, 33));
        assertEquals(0, shop.distanceTo(18, 30));
        assertEquals(1, shop.distanceTo(17, 30));
        assertEquals(2, shop.distanceTo(17, 29), "one west + one north");
        assertEquals(4, shop.distanceTo(28, 33));
    }
}
