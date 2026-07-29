package com.trojia.client.fpv;

import com.trojia.sim.world.Dir;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * The joins between a continuous look direction and the two discrete things either side of it:
 * the sim's four-way {@code Dir}, and the eight grid steps {@code Actor.tryStep} understands.
 * A sign wrong here means pressing forward walks you backwards, so every cardinal is pinned by
 * hand rather than by symmetry.
 */
class ViewFacingTest {

    private static final float EAST = 0f;
    private static final float SOUTH = (float) (Math.PI / 2);
    private static final float WEST = (float) Math.PI;
    private static final float NORTH = (float) (3 * Math.PI / 2);

    @Test
    void everyLateralDirHasItsYaw() {
        assertEquals(EAST, ViewFacing.yawOf(Dir.EAST), 1e-5);
        assertEquals(SOUTH, ViewFacing.yawOf(Dir.SOUTH), 1e-5);
        assertEquals(WEST, ViewFacing.yawOf(Dir.WEST), 1e-5);
        assertEquals(NORTH, ViewFacing.yawOf(Dir.NORTH), 1e-5);
    }

    @Test
    void aVerticalFacingFallsBackRatherThanThrowing() {
        // An actor whose last committed step was a climb has no lateral facing to inherit.
        assertEquals(EAST, ViewFacing.yawOf(Dir.UP), 1e-5);
        assertEquals(EAST, ViewFacing.yawOf(Dir.DOWN), 1e-5);
        assertEquals(EAST, ViewFacing.yawOfFacing(-1), 1e-5);
        assertEquals(EAST, ViewFacing.yawOfFacing(99), 1e-5);
    }

    @Test
    void theCompassReadsTheWayACompassReads() {
        assertEquals(0f, ViewFacing.compassDegrees(NORTH), 1e-3);
        assertEquals(90f, ViewFacing.compassDegrees(EAST), 1e-3);
        assertEquals(180f, ViewFacing.compassDegrees(SOUTH), 1e-3);
        assertEquals(270f, ViewFacing.compassDegrees(WEST), 1e-3);
        assertEquals("N", ViewFacing.compassPoint(NORTH));
        assertEquals("E", ViewFacing.compassPoint(EAST));
        assertEquals("SW", ViewFacing.compassPoint((float) (Math.PI * 0.75)));
    }

    @Test
    void walkingForwardWalksTheWayYouAreLooking() {
        assertArrayEquals(new int[] {0, -1}, ViewFacing.stepDelta(NORTH, 1, 0));
        assertArrayEquals(new int[] {1, 0}, ViewFacing.stepDelta(EAST, 1, 0));
        assertArrayEquals(new int[] {0, 1}, ViewFacing.stepDelta(SOUTH, 1, 0));
        assertArrayEquals(new int[] {-1, 0}, ViewFacing.stepDelta(WEST, 1, 0));
    }

    @Test
    void backingUpBacksUp() {
        assertArrayEquals(new int[] {0, 1}, ViewFacing.stepDelta(NORTH, -1, 0));
        assertArrayEquals(new int[] {-1, 0}, ViewFacing.stepDelta(EAST, -1, 0));
    }

    @Test
    void strafingRightIsTheHandYouWouldPointWith() {
        // Facing north, your right hand points east.
        assertArrayEquals(new int[] {1, 0}, ViewFacing.stepDelta(NORTH, 0, 1));
        assertArrayEquals(new int[] {-1, 0}, ViewFacing.stepDelta(NORTH, 0, -1));
        // Facing east, your right hand points south.
        assertArrayEquals(new int[] {0, 1}, ViewFacing.stepDelta(EAST, 0, 1));
    }

    @Test
    void forwardAndStrafeTogetherStepDiagonally() {
        assertArrayEquals(new int[] {1, -1}, ViewFacing.stepDelta(NORTH, 1, 1));
        assertArrayEquals(new int[] {1, 1}, ViewFacing.stepDelta(EAST, 1, 1));
    }

    /**
     * Each of the eight grid directions owns a 45-degree wedge, so a yaw a little off a
     * cardinal still walks in a straight line until you have turned past halfway to the
     * diagonal. Without that, a view yaw two degrees off north would step diagonally forever
     * and the player would slide sideways down every street.
     */
    @Test
    void aSlightlyOffCardinalYawStillWalksStraight() {
        float slightlyEastOfNorth =
                FirstPersonCamera.normalize(NORTH + (float) Math.toRadians(20));
        assertArrayEquals(new int[] {0, -1}, ViewFacing.stepDelta(slightlyEastOfNorth, 1, 0));
        float pastHalfway = FirstPersonCamera.normalize(NORTH + (float) Math.toRadians(30));
        assertArrayEquals(new int[] {1, -1}, ViewFacing.stepDelta(pastHalfway, 1, 0));
    }

    @Test
    void noIntentIsNoStep() {
        assertArrayEquals(new int[] {0, 0}, ViewFacing.stepDelta(NORTH, 0, 0));
    }
}
