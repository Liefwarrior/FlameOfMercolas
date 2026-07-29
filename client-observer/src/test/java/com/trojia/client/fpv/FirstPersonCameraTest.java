package com.trojia.client.fpv;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * How movement <em>feels</em>, as assertions. The eye never decides where anyone is — the sim
 * does — so what is testable here is exactly the thing Eli named as the crux: that a tile step
 * arrives as a stride rather than as a teleport or a glide, at whatever speed the simulation
 * happens to be running.
 */
class FirstPersonCameraTest {

    private static final int BAND = 19;

    private static FirstPersonCamera placed() {
        FirstPersonCamera cam = new FirstPersonCamera();
        cam.snapTo(40, 60, BAND, 0f);
        return cam;
    }

    /** Runs a whole step's worth of frames at 60fps. */
    private static void runSeconds(FirstPersonCamera cam, float seconds) {
        for (float t = 0; t < seconds; t += 1 / 60f) {
            cam.advance(1 / 60f);
        }
    }

    @Test
    void snapPlacesTheEyeAtTheCellCentreAtStandingHeight() {
        FirstPersonCamera cam = placed();
        assertEquals(40.5f, cam.eyeX(), 1e-5);
        assertEquals(60.5f, cam.eyeY(), 1e-5);
        assertEquals(BandGeometry.floorHeight(BAND) + BandGeometry.EYE_HEIGHT,
                cam.eyeHeight(), 1e-4);
        assertFalse(cam.isStriding(), "a placed eye is standing still");
    }

    @Test
    void aStepIsNeitherATeleportNorAGlide() {
        FirstPersonCamera cam = placed();
        cam.followCell(41, 60, BAND);
        assertTrue(cam.isStriding());
        // Not a teleport: one frame in, the eye is still short of the destination.
        cam.advance(1 / 60f);
        assertTrue(cam.eyeX() < 41.5f, "arrived in a single frame — that is a teleport");
        assertTrue(cam.eyeX() > 40.5f, "did not move at all on the first frame");
        // Not a glide: it definitively arrives, and then stops.
        runSeconds(cam, FirstPersonCamera.MAX_STEP_EASE_SECONDS + 0.05f);
        assertEquals(41.5f, cam.eyeX(), 1e-4);
        assertFalse(cam.isStriding(), "the stride must land, not asymptote");
    }

    @Test
    void theStrideNeverOvershootsAndNeverBacksUp() {
        FirstPersonCamera cam = placed();
        cam.followCell(41, 60, BAND);
        float previous = cam.eyeX();
        for (int i = 0; i < 40; i++) {
            cam.advance(1 / 120f);
            assertTrue(cam.eyeX() >= previous - 1e-5, "the eye moved backwards mid-stride");
            assertTrue(cam.eyeX() <= 41.5f + 1e-5, "the eye overshot the destination cell");
            previous = cam.eyeX();
        }
    }

    @Test
    void theSameElapsedTimeGivesTheSamePlaceAtAnyFramerate() {
        FirstPersonCamera slow = placed();
        FirstPersonCamera fast = placed();
        slow.followCell(41, 60, BAND);
        fast.followCell(41, 60, BAND);
        for (int i = 0; i < 6; i++) {
            slow.advance(0.02f);
        }
        for (int i = 0; i < 120; i++) {
            fast.advance(0.001f);
        }
        assertEquals(slow.eyeX(), fast.eyeX(), 1e-4,
                "the stride must be a function of elapsed time, not of frame count");
    }

    /**
     * The requirement in one test: the ease has to finish before the next step commits, and
     * the step cadence is not a constant — it is {@code speedTicksPerStep} times a tick length
     * that changes the moment someone presses fast-forward. The camera measures the interval
     * instead of being told it.
     */
    @Test
    void theEaseSizesItselfToTheMeasuredStepCadence() {
        FirstPersonCamera cam = placed();
        float fastCadence = 0.05f; // the observer at FAST: 25ms ticks, two ticks per step
        cam.followCell(41, 60, BAND);
        runSeconds(cam, fastCadence);
        cam.followCell(42, 60, BAND);
        assertEquals(fastCadence, cam.measuredStepIntervalSeconds(), 0.02f);
        // The third step must find the second already landed.
        runSeconds(cam, fastCadence);
        assertFalse(cam.isStriding(),
                "at a 50ms cadence the ease was still running when the next step was due");
    }

    @Test
    void aBandChangeEasesLongerThanALateralStepAtTheSameCadence() {
        float cadence = 0.40f;
        FirstPersonCamera lateral = placed();
        lateral.followCell(41, 60, BAND);
        runSeconds(lateral, cadence);
        lateral.followCell(42, 60, BAND);

        FirstPersonCamera climb = placed();
        climb.followCell(41, 60, BAND);
        runSeconds(climb, cadence);
        climb.followCell(41, 60, BAND + 1);

        // Half a lateral ease in, the climb is still visibly on its way up while the flat step
        // has essentially landed: the climb reads as a climb.
        runSeconds(lateral, FirstPersonCamera.MAX_STEP_EASE_SECONDS + 0.02f);
        runSeconds(climb, FirstPersonCamera.MAX_STEP_EASE_SECONDS + 0.02f);
        assertFalse(lateral.isStriding(), "the lateral step should have landed by now");
        assertTrue(climb.isStriding(), "a band change must not snap like a lateral step");
        assertTrue(climb.eyeHeight() > BandGeometry.floorHeight(BAND) + BandGeometry.EYE_HEIGHT,
                "the eye must actually be rising");
        assertTrue(climb.eyeHeight()
                        < BandGeometry.floorHeight(BAND + 1) + BandGeometry.EYE_HEIGHT,
                "and must not be there yet");
    }

    @Test
    void aClimbArrivesExactlyOnTheNewBandFloor() {
        FirstPersonCamera cam = placed();
        cam.followCell(40, 60, BAND + 1);
        runSeconds(cam, FirstPersonCamera.MAX_CLIMB_EASE_SECONDS + 0.05f);
        assertEquals(BandGeometry.floorHeight(BAND + 1) + BandGeometry.EYE_HEIGHT,
                cam.eyeHeight(), 1e-4);
        assertEquals(BAND + 1, cam.band());
    }

    @Test
    void theHeadDipsMidStrideAndIsPerfectlyStillWhenStanding() {
        FirstPersonCamera cam = placed();
        float standing = cam.eyeHeight();
        cam.advance(1f);
        assertEquals(standing, cam.eyeHeight(), 1e-6, "standing still must be still");

        cam.followCell(41, 60, BAND);
        cam.advance(FirstPersonCamera.MAX_STEP_EASE_SECONDS / 2f);
        assertTrue(cam.eyeHeight() < standing, "no head dip at mid-stride");
        assertTrue(standing - cam.eyeHeight() <= FirstPersonCamera.BOB_DIP + 1e-4);
        runSeconds(cam, FirstPersonCamera.MAX_STEP_EASE_SECONDS);
        assertEquals(standing, cam.eyeHeight(), 1e-4, "the dip must be gone on arrival");
    }

    @Test
    void aClimbRisesWithoutBobbing() {
        FirstPersonCamera cam = placed();
        float standing = cam.eyeHeight();
        cam.followCell(40, 60, BAND + 1);
        float previous = standing;
        for (int i = 0; i < 12; i++) {
            cam.advance(1 / 120f);
            assertTrue(cam.eyeHeight() >= previous - 1e-5,
                    "the eye dipped while climbing — the stride bob must not apply to a climb");
            previous = cam.eyeHeight();
        }
        assertTrue(cam.eyeHeight() > standing, "a climb must actually rise");
    }

    @Test
    void turningIsClientSideAndWrapsCleanly() {
        FirstPersonCamera cam = placed();
        cam.turn((float) (-Math.PI / 2));
        assertEquals(3 * Math.PI / 2, cam.yaw(), 1e-4, "yaw must wrap into [0, 2pi)");
        cam.turn((float) (4 * Math.PI));
        assertEquals(3 * Math.PI / 2, cam.yaw(), 1e-3, "two full turns is no turn at all");
    }

    @Test
    void repeatingTheCurrentCellIsFree() {
        FirstPersonCamera cam = placed();
        cam.followCell(40, 60, BAND);
        cam.followCell(40, 60, BAND);
        assertFalse(cam.isStriding(),
                "standing still must not restart an ease every frame");
    }

    @Test
    void easeOutLeavesImmediatelyAndSettles() {
        assertEquals(0f, FirstPersonCamera.easeOut(0f), 1e-6);
        assertEquals(1f, FirstPersonCamera.easeOut(1f), 1e-6);
        assertTrue(FirstPersonCamera.easeOut(0.25f) > 0.25f,
                "ease-out must be ahead of linear early — that is the responsiveness");
        assertTrue(FirstPersonCamera.easeOut(0.75f) > 0.75f);
    }
}
