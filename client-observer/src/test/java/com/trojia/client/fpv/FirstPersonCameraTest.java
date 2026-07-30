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

    /**
     * The climb clamp is the longer of the two, so once the sim is running slowly enough for
     * the clamps to bite at all, a band change does still ease longer than a lateral step. At
     * the real cadences neither clamp bites and the two are the same length — see
     * {@code FirstPersonStrideTest} for what separates them there.
     */
    @Test
    void aBandChangeEasesLongerThanALateralStepOnceTheClampsBite() {
        float cadence = 0.40f; // slower than any real cadence, so both clamps are in play
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

    // ------------------------------------------------------------------ the stride curve

    /**
     * The curve is exact at both ends and monotone in between — the minimum for "the eye ends
     * up in the cell the sim put the body in, having only ever gone forwards".
     */
    @Test
    void theStrideCurveIsExactAtBothEndsAndNeverBacksUp() {
        assertEquals(0f, FirstPersonCamera.strideEase(0f), 1e-6);
        assertEquals(1f, FirstPersonCamera.strideEase(1f), 1e-6);
        float previous = -1f;
        for (int i = 0; i <= 100; i++) {
            float e = FirstPersonCamera.strideEase(i / 100f);
            assertTrue(e >= previous, "the stride curve went backwards at t=" + (i / 100f));
            previous = e;
        }
    }

    /**
     * <b>The lurch, as an assertion.</b> The previous curve — ease-out cubic — put 58% of the
     * cell behind you in the first quarter of the ease and 88% in the first half, then coasted.
     * A stride does not do that. Nothing may be more than a fifth ahead of a straight line at
     * any point, which is loose enough to allow a real push-off and tight enough that
     * ease-out cubic fails it three times over.
     */
    @Test
    void theStrideIsNotFrontLoadedTheWayALurchIs() {
        for (int i = 1; i < 100; i++) {
            float t = i / 100f;
            float e = FirstPersonCamera.strideEase(t);
            assertTrue(e - t <= 0.20f,
                    "the stride is front-loaded at t=" + t + ": " + e + " vs linear " + t
                            + " — that is a lunge, not a step");
            assertTrue(t - e <= 0.20f, "the stride stalls at t=" + t);
        }
        // For the record, the curve this replaced, at the same two points.
        assertTrue(1f - (float) Math.pow(0.75, 3) > 0.55f);
    }

    /**
     * <b>The eye never stops moving mid-stride.</b> This is the actual complaint: the old ease
     * finished in 62% of the cadence and then held the eye perfectly still for the rest of it,
     * which reads as a lurch and a freeze rather than as walking. Sampled per frame at 60 Hz
     * over a real 200 ms cadence, every frame must carry a real fraction of the cell.
     */
    @Test
    void everyFrameOfAStrideMovesTheEye() {
        FirstPersonCamera cam = placed();
        float cadence = 0.200f; // a speedTicksPerStep=2 actor at real time
        cam.followCell(41, 60, BAND);
        runSeconds(cam, cadence);
        cam.followCell(42, 60, BAND);   // now the cadence is measured

        float frame = 1 / 60f;
        float average = 1f / (cadence / frame); // cell fraction per frame at constant speed
        float floor = average * FirstPersonCamera.MIN_STRIDE_SPEED_FRACTION * 0.9f;
        float previous = cam.eyeX();
        int frames = (int) (cadence / frame);
        int stalled = 0;
        for (int i = 0; i < frames; i++) {
            cam.advance(frame);
            float moved = cam.eyeX() - previous;
            previous = cam.eyeX();
            if (moved < floor) {
                stalled++;
            }
        }
        assertTrue(stalled <= 1,
                stalled + " of " + frames + " frames of a 200 ms stride did not move the eye "
                        + "(only the one settle frame is allowed)");
    }

    @Test
    void theStrideFillsTheCadenceItMeasured() {
        FirstPersonCamera cam = placed();
        float cadence = 0.200f;
        cam.followCell(41, 60, BAND);
        runSeconds(cam, cadence);
        cam.followCell(42, 60, BAND);
        // Still striding after five sixths of the cadence: the eye is walking, not parked.
        runSeconds(cam, cadence * 5f / 6f);
        assertTrue(cam.isStriding(),
                "the stride ended with a third of the cadence still to run — that is the lurch");
        runSeconds(cam, cadence);
        assertFalse(cam.isStriding(), "and it still has to land before the next step is due");
    }

    // ------------------------------------------------------------------ the yaw follows a walk

    /**
     * The facing wedge on the map draws this yaw, so a yaw that never moves is a marker that
     * lies about where the first-person frame will open. While the tile view is the one on
     * screen, a committed step aims it.
     */
    @Test
    void theYawSwingsToTheDirectionActuallyWalked() {
        FirstPersonCamera cam = placed();
        cam.snapTo(40, 60, BAND, 0f); // looking EAST
        cam.followCell(40, 59, BAND, true); // stepped NORTH (world y runs south)
        assertTrue(cam.isTurningToTravel(), "a step on the map must aim the wedge");
        runSeconds(cam, 0.5f);
        assertEquals(3 * Math.PI / 2, cam.yaw(), 1e-4, "the yaw must end up pointing NORTH");
        assertFalse(cam.isTurningToTravel(), "and land on it, not asymptote toward it");
    }

    /** A diagonal stride points diagonally. {@code Actor.facing()} takes the x component of a
     * diagonal, so a wedge reading that would promise east and deliver north-east. */
    @Test
    void aDiagonalStrideAimsTheWedgeDiagonally() {
        FirstPersonCamera cam = placed();
        cam.snapTo(40, 60, BAND, 0f);
        cam.followCell(41, 61, BAND, true); // south-east
        runSeconds(cam, 0.5f);
        assertEquals(Math.PI / 4, cam.yaw(), 1e-4);
    }

    /** It takes the SHORT way round, so walking west from a yaw of nearly-north swings a
     * quarter turn rather than three quarters. */
    @Test
    void theSwingTakesTheShortWayRound() {
        FirstPersonCamera cam = placed();
        cam.snapTo(40, 60, BAND, (float) (3 * Math.PI / 2)); // NORTH
        cam.followCell(39, 60, BAND, true);                  // WEST is a quarter turn back
        cam.advance(1 / 60f);
        float delta = FirstPersonCamera.shortestTurn((float) (3 * Math.PI / 2), cam.yaw());
        assertTrue(delta < 0f, "the swing went the long way round the compass");
        runSeconds(cam, 0.5f);
        assertEquals(Math.PI, cam.yaw(), 1e-4);
    }

    /** In first person the yaw is the player's: the movement keys are relative to it, so
     * adopting the travelled direction would quantise the aim to one of eight every step. */
    @Test
    void aFirstPersonStepNeverAimsTheView() {
        FirstPersonCamera cam = placed();
        cam.snapTo(40, 60, BAND, 0.7f);
        cam.followCell(40, 59, BAND); // the no-adopt overload — what first person calls
        runSeconds(cam, 0.5f);
        assertEquals(0.7f, cam.yaw(), 1e-5, "first person must never re-aim the player's look");
        assertFalse(cam.isTurningToTravel());
    }

    /** An explicit turn beats a swing already in flight — you aimed, so you meant it. */
    @Test
    void aimingCancelsASwingInFlight() {
        FirstPersonCamera cam = placed();
        cam.snapTo(40, 60, BAND, 0f);
        cam.followCell(40, 59, BAND, true);
        cam.advance(1 / 60f);
        cam.turn(0.25f);
        assertFalse(cam.isTurningToTravel(), "aiming must cancel the follow, not queue behind it");
        float aimed = cam.yaw();
        runSeconds(cam, 0.5f);
        assertEquals(aimed, cam.yaw(), 1e-5);
    }

    @Test
    void shortestTurnIsSignedAndWrapsTheShortWay() {
        assertEquals(0.2f, FirstPersonCamera.shortestTurn(0.1f, 0.3f), 1e-5);
        assertEquals(-0.2f, FirstPersonCamera.shortestTurn(0.3f, 0.1f), 1e-5);
        assertEquals(-0.2f, FirstPersonCamera.shortestTurn(0.1f, 0.1f - 0.2f), 1e-5);
        assertEquals(0.2f,
                FirstPersonCamera.shortestTurn((float) (2 * Math.PI - 0.1), 0.1f), 1e-5);
    }
}
