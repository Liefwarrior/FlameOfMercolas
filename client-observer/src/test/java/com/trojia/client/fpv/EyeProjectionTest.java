package com.trojia.client.fpv;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The pinhole arithmetic, on its own: no world, no plan, no GL. Everything the first-person
 * view draws is downstream of these few lines of maths — where forward is, which way yaw
 * turns, where the horizon sits, and how far up you can crane without a real pitch — so they
 * get pinned first.
 */
class EyeProjectionTest {

    private static final int W = 1280;
    private static final int H = 720;

    private static EyeProjection facingEast(float eyeHeight) {
        return EyeProjection.of(10.5f, 10.5f, eyeHeight, 0f, W, H,
                EyeProjection.DEFAULT_FOV_DEGREES, 0f);
    }

    @Test
    void forwardIsEastAndRightIsSouth() {
        EyeProjection eye = facingEast(50f);
        assertEquals(4f, eye.depthOf(14.5f, 10.5f), 1e-4, "east of the eye is forward");
        assertEquals(0f, eye.lateralOf(14.5f, 10.5f), 1e-4, "dead ahead is not to a side");
        assertTrue(eye.lateralOf(10.5f, 13.5f) > 0f,
                "facing east, SOUTH (+y) must be on the right");
        assertTrue(eye.depthOf(6.5f, 10.5f) < 0f, "behind the eye is negative depth");
    }

    @Test
    void yawTurnsClockwiseThroughSouth() {
        EyeProjection south = EyeProjection.of(10.5f, 10.5f, 50f, (float) (Math.PI / 2), W, H,
                EyeProjection.DEFAULT_FOV_DEGREES, 0f);
        assertEquals(3f, south.depthOf(10.5f, 13.5f), 1e-4, "yaw pi/2 looks SOUTH");
        EyeProjection north = EyeProjection.of(10.5f, 10.5f, 50f, (float) (3 * Math.PI / 2), W, H,
                EyeProjection.DEFAULT_FOV_DEGREES, 0f);
        assertEquals(3f, north.depthOf(10.5f, 7.5f), 1e-4, "yaw 3pi/2 looks NORTH");
    }

    @Test
    void deadAheadLandsAtScreenCentreAndEyeHeightLandsOnTheHorizon() {
        EyeProjection eye = facingEast(50f);
        assertEquals(W / 2f, eye.screenX(0f, 6f), 1e-3);
        assertEquals(eye.horizonY(), eye.screenY(50f, 6f), 1e-3);
        assertEquals(H / 2f, eye.horizonY(), 1e-3, "no shear: the horizon is centred");
    }

    @Test
    void thingsHalveAsTheyDoubleInDistance() {
        EyeProjection eye = facingEast(50f);
        float near = eye.halfWidthPx(1f, 4f);
        float far = eye.halfWidthPx(1f, 8f);
        assertEquals(near / 2f, far, 1e-3);
        // And the same for vertical offsets: a wall top twice as far is half as high.
        float nearTop = eye.screenY(52f, 4f) - eye.horizonY();
        float farTop = eye.screenY(52f, 8f) - eye.horizonY();
        assertEquals(nearTop / 2f, farTop, 1e-3);
    }

    @Test
    void lookingUpMovesTheHorizonDownTheScreen() {
        EyeProjection level = facingEast(50f);
        EyeProjection up = EyeProjection.of(10.5f, 10.5f, 50f, 0f, W, H,
                EyeProjection.DEFAULT_FOV_DEGREES, 120f);
        assertTrue(up.horizonY() < level.horizonY(),
                "looking up must lower the horizon line, not raise it");
        assertTrue(up.highestVisibleHeight(10f) > level.highestVisibleHeight(10f),
                "looking up must show more of what is overhead");
    }

    @Test
    void shearIsClampedSoTheFrameNeverInverts() {
        EyeProjection wild = EyeProjection.of(10.5f, 10.5f, 50f, 0f, W, H,
                EyeProjection.DEFAULT_FOV_DEGREES, 100_000f);
        float minHorizon = H / 2f - EyeProjection.MAX_SHEAR_FRACTION * H;
        assertEquals(minHorizon, wild.horizonY(), 1e-3);
        assertTrue(wild.horizonY() > 0f, "the horizon must stay on screen");
    }

    /**
     * The frame starts a few tiles out. With a level horizon the ground under your own feet is
     * off the bottom of the screen and the visible street begins around four tiles ahead —
     * which is why looking down (a shear, since there is no pitch) has to bring it closer
     * rather than being cosmetic.
     */
    @Test
    void theVisibleGroundStartsAheadOfYouAndLookingDownBringsItCloser() {
        EyeProjection level = EyeProjection.of(10.5f, 10.5f, BandGeometry.EYE_HEIGHT, 0f,
                W, H, EyeProjection.DEFAULT_FOV_DEGREES, 0f);
        EyeProjection down = EyeProjection.of(10.5f, 10.5f, BandGeometry.EYE_HEIGHT, 0f,
                W, H, EyeProjection.DEFAULT_FOV_DEGREES,
                -EyeProjection.MAX_SHEAR_FRACTION * H);
        float levelStart = level.nearestVisibleFloorDistance(0f);
        float downStart = down.nearestVisibleFloorDistance(0f);
        assertTrue(levelStart > 2f && levelStart < 8f,
                "the visible street should start a few tiles out, not underfoot or over the "
                        + "horizon: " + levelStart);
        assertTrue(downStart < levelStart, "looking down must show more of the ground");
    }

    /**
     * How much vertical reach the shear buys, stated as the thing it is for: craning up at a
     * three-storey roofline from across a street. At a level gaze the vertical field is about
     * 44 degrees at 16:9, so a roof three bands up only clears the frame from a long way off;
     * the shear roughly doubles the upward half of that, bringing the same roofline in from
     * across a nine-tile street. Standing with your nose against the wall you still cannot see
     * its top, which is also true of walls.
     */
    @Test
    void lookingUpReachesAThreeStoreyRooflineFromAcrossTheStreet() {
        float eyeHeight = BandGeometry.floorHeight(19) + BandGeometry.EYE_HEIGHT;
        float roofline = BandGeometry.floorHeight(19 + 3);
        EyeProjection level = EyeProjection.of(10.5f, 10.5f, eyeHeight, 0f, W, H,
                EyeProjection.DEFAULT_FOV_DEGREES, 0f);
        EyeProjection craned = EyeProjection.of(10.5f, 10.5f, eyeHeight, 0f, W, H,
                EyeProjection.DEFAULT_FOV_DEGREES, EyeProjection.MAX_SHEAR_FRACTION * H);
        assertTrue(craned.highestVisibleHeight(9f) > roofline,
                "cannot see a three-storey roofline from across a nine-tile street even "
                        + "looking up");
        assertTrue(level.highestVisibleHeight(9f) < roofline,
                "if a level gaze already reached it, the shear would not be doing anything");
    }
}
