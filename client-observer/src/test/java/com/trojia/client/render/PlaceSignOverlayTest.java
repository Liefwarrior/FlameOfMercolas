package com.trojia.client.render;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link PlaceSignOverlay}: the GL-free place-label planner — marks culled to the camera box,
 * the depth-vision rule inherited verbatim from the fishing overlay, the single-box clutter
 * law, and the ordered tie-break that decides WHICH place a reader is told about. Headless:
 * the places are hand-built records, the depth column is a synthetic lambda.
 */
class PlaceSignOverlayTest {

    private static final int VIEW_Z = 11;
    private static final boolean OBSERVING = false;
    private static final boolean PLAYING = true;

    /** A tight shop: door at (20,29), footprint (18,30)-(24,36). */
    private static PlaceSign shop() {
        return new PlaceSign("Brann's Chandlery", "ship stores", PlaceSign.Kind.DOOR,
                20, 29, VIEW_Z, 18, 30, 24, 36);
    }

    /** A long shed whose door is way off to the west: door (4,85), footprint (5,82)-(67,90). */
    private static PlaceSign ropewalk() {
        return new PlaceSign("The Ropewalk", "the shed where cable is laid", PlaceSign.Kind.DOOR,
                4, 85, VIEW_Z, 5, 82, 67, 90);
    }

    /** A place one z-band below the view plane: door (40,50), footprint (40,50)-(44,54). */
    private static PlaceSign below() {
        return new PlaceSign("The Quayward Compound", "mansion and condos", PlaceSign.Kind.DOOR,
                40, 50, VIEW_Z - 1, 40, 50, 44, 54);
    }

    /** The street the shop's door opens onto: post at (30,29), run (0,28)-(60,29). */
    private static PlaceSign street() {
        return new PlaceSign("Tarwalk", "the working spine", PlaceSign.Kind.WAY,
                30, 29, VIEW_Z, 0, 28, 60, 29);
    }

    /** The whole map is in view. */
    private static PlaceSignOverlay.Plan planAt(List<PlaceSign> signs, DepthSight depth,
            int attentionX, int attentionY, boolean doorwayReach) {
        return PlaceSignOverlay.plan(signs, VIEW_Z, 0, 200, 0, 200, depth,
                attentionX, attentionY, true, doorwayReach);
    }

    /** Every column is solid at the view z: nothing below the plane is ever seen through it. */
    private static final DepthSight OCCLUDED = (viewZ, x, y) -> DepthSight.NONE;

    // ------------------------------------------------------------------ the marks

    @Test
    void everySameZPlaceStandsAMarkAtItsOwnCell() {
        PlaceSignOverlay.Plan plan = planAt(List.of(shop(), ropewalk()), OCCLUDED, 0, 0,
                OBSERVING);
        assertEquals(2, plan.markers().size());
        assertEquals(20, plan.markers().get(0).tileX());
        assertEquals(29, plan.markers().get(0).tileY());
        assertEquals(0, plan.markers().get(0).depth());
        assertFalse(plan.markers().get(0).way(), "a shop's mark is the hanging plaque");
        assertEquals(4, plan.markers().get(1).tileX());
    }

    @Test
    void aWayIsPlannedAsAFingerpostNotAPlaque() {
        PlaceSignOverlay.Plan plan = planAt(List.of(street()), OCCLUDED, 0, 0, OBSERVING);
        assertEquals(1, plan.markers().size());
        assertTrue(plan.markers().get(0).way(), "a street's mark is the kerb post");
    }

    @Test
    void signsOutsideTheCameraBoxAreCulled() {
        PlaceSignOverlay.Plan plan = PlaceSignOverlay.plan(List.of(shop(), ropewalk()), VIEW_Z,
                10, 60, 10, 60, OCCLUDED, 0, 0, false, OBSERVING);
        assertEquals(1, plan.markers().size(), "only the shop's door is on screen");
        assertEquals(20, plan.markers().get(0).tileX());
    }

    @Test
    void anEmptyWardPlansNothing() {
        assertEquals(PlaceSignOverlay.Plan.NOTHING,
                planAt(List.of(), OCCLUDED, 20, 32, OBSERVING));
    }

    // --------------------------------------------------------------- the depth rule

    @Test
    void aPlaceBelowThePlaneDrawsOnlyWhereTheLookDownResolvesToIt() {
        // The column over the compound's door genuinely shows z10 through empty air.
        DepthSight open = (viewZ, x, y) -> x == 40 && y == 50 ? VIEW_Z - 1 : DepthSight.NONE;
        PlaceSignOverlay.Plan plan = planAt(List.of(below()), open, 42, 52, OBSERVING);
        assertEquals(1, plan.markers().size());
        assertEquals(1, plan.markers().get(0).depth(), "one band down");
        assertNotNull(plan.box());
        assertEquals(1, plan.box().depth());
    }

    @Test
    void aPlaceUnderTheRoofYouAreLookingAtVanishesSignAndBoxTogether() {
        // Eli's own hazard: a sign two bands below must not float over the roof in view.
        PlaceSignOverlay.Plan plan = planAt(List.of(below()), OCCLUDED, 42, 52, OBSERVING);
        assertTrue(plan.markers().isEmpty(), "occluded column: no mark");
        assertNull(plan.box(), "and no words either");
    }

    @Test
    void aPlaceAboveTheViewPlaneNeverDraws() {
        PlaceSign attic = new PlaceSign("Saltgate Watch-Post", "the ward's one watch station",
                PlaceSign.Kind.DOOR, 20, 29, VIEW_Z + 2, 18, 30, 24, 36);
        PlaceSignOverlay.Plan plan = planAt(List.of(attic), OCCLUDED, 20, 32, OBSERVING);
        assertTrue(plan.markers().isEmpty());
        assertNull(plan.box());
    }

    // ---------------------------------------------- a mark cell names its own place

    /**
     * The round-1 defect, in miniature: the shop's own door cell (20,29) is a Tarwalk cell, so
     * a plain nearest-wins rule handed the box to the street the player was standing on — six
     * of the ward's thirty-nine plaques named a NEIGHBOUR when you pointed straight at them.
     */
    @Test
    void pointingAtAPlacesOwnMarkNamesThatPlace() {
        List<PlaceSign> both = List.of(street(), shop());
        assertEquals("Brann's Chandlery", planAt(both, OCCLUDED, 20, 29, OBSERVING).box().place());
        assertEquals("Brann's Chandlery", planAt(both, OCCLUDED, 20, 29, PLAYING).box().place());
        // And it is order-independent: the loser cannot win by being authored first.
        assertEquals("Brann's Chandlery",
                planAt(List.of(shop(), street()), OCCLUDED, 20, 29, OBSERVING).box().place());
    }

    @Test
    void aMarkCellAnswersEvenBeyondTheReach() {
        // A compound gate legally hangs two tiles off its own wall; pointing at that gate is
        // outside the play-mode reach and must still name the compound.
        PlaceSign gate = new PlaceSign("The Gullet Compound", "decayed", PlaceSign.Kind.DOOR,
                50, 60, VIEW_Z, 52, 62, 60, 70);
        assertEquals("The Gullet Compound",
                planAt(List.of(gate), OCCLUDED, 50, 60, PLAYING).box().place());
    }

    @Test
    void theMoreSpecificPlaceWinsATie() {
        // Wormwood Pier is part of Pier Row: standing on its deck, both are distance 0, and
        // the answer a player wants is the smaller, more specific one.
        PlaceSign row = new PlaceSign("Pier Row", "four timber finger-piers", PlaceSign.Kind.WAY,
                99, 24, VIEW_Z, 98, 4, 124, 25);
        PlaceSign wormwood = new PlaceSign("Wormwood Pier", "condemned and rotten",
                PlaceSign.Kind.WAY, 123, 24, VIEW_Z, 122, 4, 124, 25);
        assertEquals("Wormwood Pier",
                planAt(List.of(row, wormwood), OCCLUDED, 123, 10, OBSERVING).box().place());
        assertEquals("Pier Row",
                planAt(List.of(row, wormwood), OCCLUDED, 99, 10, OBSERVING).box().place());
    }

    @Test
    void aStreetNamesItselfWhereNoBuildingIsNearer() {
        List<PlaceSign> both = List.of(street(), shop());
        // Standing in the middle of the street, clear of the shop's own door cell.
        assertEquals("Tarwalk", planAt(both, OCCLUDED, 40, 29, OBSERVING).box().place());
        assertEquals("Tarwalk", planAt(both, OCCLUDED, 40, 29, PLAYING).box().place());
        // Inside the shop the shop wins: the street's run does not reach in there.
        assertEquals("Brann's Chandlery", planAt(both, OCCLUDED, 21, 33, OBSERVING).box().place());
    }

    // ------------------------------------------------------------------- the one box

    @Test
    void theBoxSpeaksTheGazetteersOwnTwoLines() {
        PlaceSignOverlay.Box box = planAt(List.of(shop()), OCCLUDED, 21, 33, OBSERVING).box();
        assertNotNull(box);
        assertEquals("Brann's Chandlery", box.place());
        assertEquals("ship stores", box.what());
        assertEquals(21, box.anchorTileX(), "anchored on the attention tile, not the door");
        assertEquals(33, box.anchorTileY());
    }

    @Test
    void standingDeepInsideALongShedStillNamesItToAnObserver() {
        // Attention at (40,86) is 36 tiles from the Ropewalk's door and INSIDE its footprint:
        // distance-to-rect is what makes this work, distance-to-door never would.
        PlaceSignOverlay.Box box = planAt(List.of(ropewalk()), OCCLUDED, 40, 86, OBSERVING).box();
        assertNotNull(box);
        assertEquals("The Ropewalk", box.place());
    }

    /**
     * ...and in PLAY mode the same spot is silent, which is the point: a building is measured
     * from its DOOR while an actor is being walked, so the box snaps ON at the threshold and
     * OFF two steps in. Round 1 measured the floor in both modes, so the box was permanently
     * lit for as long as you stood anywhere inside a large site.
     */
    @Test
    void walkingIntoALongShedTheBoxSnapsOnAtTheDoorAndOffInside() {
        assertNotNull(planAt(List.of(ropewalk()), OCCLUDED, 5, 85, PLAYING).box(),
                "one step in from the door still speaks");
        assertNull(planAt(List.of(ropewalk()), OCCLUDED, 40, 86, PLAYING).box(),
                "deep inside the shed the box is gone");
    }

    @Test
    void aLongShedNamesItselfEvenWhenItsDoorIsOffScreen() {
        PlaceSignOverlay.Plan plan = PlaceSignOverlay.plan(List.of(ropewalk()), VIEW_Z,
                30, 60, 70, 95, OCCLUDED, 40, 86, true, OBSERVING);
        assertTrue(plan.markers().isEmpty(), "the west door is not on screen");
        assertNotNull(plan.box(), "but the shed you are pointing into still speaks");
        assertEquals("The Ropewalk", plan.box().place());
    }

    @Test
    void onlyEverOneBoxNoMatterHowManyPlacesCrowdTheAttentionPoint() {
        // Five overlapping shops all within reach of one point: the ward may not drown.
        List<PlaceSign> crowd = List.of(
                new PlaceSign("A", "a", PlaceSign.Kind.DOOR, 10, 9, VIEW_Z, 10, 10, 12, 12),
                new PlaceSign("B", "b", PlaceSign.Kind.DOOR, 13, 9, VIEW_Z, 13, 10, 15, 12),
                new PlaceSign("C", "c", PlaceSign.Kind.DOOR, 16, 9, VIEW_Z, 16, 10, 18, 12),
                new PlaceSign("D", "d", PlaceSign.Kind.DOOR, 10, 13, VIEW_Z, 10, 13, 12, 15),
                new PlaceSign("E", "e", PlaceSign.Kind.DOOR, 13, 13, VIEW_Z, 13, 13, 15, 15));
        PlaceSignOverlay.Plan plan = planAt(crowd, OCCLUDED, 13, 11, OBSERVING);
        assertEquals(5, plan.markers().size(), "all five doors still show their mark");
        assertNotNull(plan.box());
        assertEquals("B", plan.box().place(), "the one the cursor is actually inside");
        assertEquals(1, plan.markers().stream().filter(PlaceSignOverlay.Marker::named).count(),
                "exactly one mark is lit as the named one");
    }

    @Test
    void theNearestPlaceWinsAndEqualTiesBreakOnAscendingOrder() {
        PlaceSign west = new PlaceSign("West", "w", PlaceSign.Kind.DOOR,
                10, 9, VIEW_Z, 8, 10, 10, 12);
        PlaceSign east = new PlaceSign("East", "e", PlaceSign.Kind.DOOR,
                16, 9, VIEW_Z, 14, 10, 16, 12);
        // (12,11) is 2 tiles from both rects and they are the same size — order takes it.
        assertEquals("West", planAt(List.of(west, east), OCCLUDED, 12, 11, OBSERVING).box().place());
        assertEquals("East", planAt(List.of(east, west), OCCLUDED, 12, 11, OBSERVING).box().place());
        // One tile east and East is genuinely nearer, order notwithstanding.
        assertEquals("East", planAt(List.of(west, east), OCCLUDED, 13, 11, OBSERVING).box().place());
    }

    @Test
    void openWaterSaysNothing() {
        PlaceSignOverlay.Plan plan = planAt(List.of(shop()), OCCLUDED, 100, 100, OBSERVING);
        assertEquals(1, plan.markers().size(), "the shop's own mark still stands");
        assertNull(plan.box(), "but nothing is being asked about");
        assertFalse(plan.markers().get(0).named());
    }

    @Test
    void theModesDifferByReachAndByWhatABuildingSpeaksFrom() {
        // Three tiles clear of the shop's west wall: an observer pointing there gets the name.
        assertNotNull(planAt(List.of(shop()), OCCLUDED, 15, 33, OBSERVING).box());
        // A played actor standing there does not — it is four tiles from the door.
        assertNull(planAt(List.of(shop()), OCCLUDED, 15, 33, PLAYING).box());
        // One step off the door cell: both speak.
        assertNotNull(planAt(List.of(shop()), OCCLUDED, 21, 30, PLAYING).box());
    }

    @Test
    void noAttentionPointMeansMarksWithoutWords() {
        PlaceSignOverlay.Plan plan = PlaceSignOverlay.plan(List.of(shop()), VIEW_Z,
                0, 200, 0, 200, OCCLUDED, 21, 33, false, OBSERVING);
        assertEquals(1, plan.markers().size());
        assertNull(plan.box());
    }

    @Test
    void planningIsPureAndRepeatable() {
        List<PlaceSign> signs = List.of(shop(), ropewalk(), below(), street());
        DepthSight open = (viewZ, x, y) -> x == 40 && y == 50 ? VIEW_Z - 1 : DepthSight.NONE;
        assertEquals(planAt(signs, open, 21, 33, OBSERVING),
                planAt(signs, open, 21, 33, OBSERVING));
    }

    /**
     * Chebyshev, not the rectilinear sum the round-1 javadoc claimed while the code summed
     * axes: at a reach of 1 the sum silently excluded a doorway's diagonal, and the sim's own
     * work reach is chebyshev.
     */
    @Test
    void footprintDistanceIsZeroInsideAndChebyshevOutside() {
        PlaceSign shop = shop();
        assertEquals(0, shop.distanceTo(21, 33));
        assertEquals(0, shop.distanceTo(18, 30));
        assertEquals(1, shop.distanceTo(17, 30));
        assertEquals(1, shop.distanceTo(17, 29), "the diagonal off a corner is ONE step");
        assertEquals(4, shop.distanceTo(28, 33));
        assertEquals(0, shop.distanceToMark(20, 29));
        assertEquals(1, shop.distanceToMark(21, 28), "and so is the diagonal off a door");
        assertTrue(shop.isMarkCell(20, 29));
        assertFalse(shop.isMarkCell(20, 30));
        assertEquals(7 * 7, shop.area());
    }
}
