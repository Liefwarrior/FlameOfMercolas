package com.trojia.client.fpv;

import com.trojia.client.camera.MapCamera;
import com.trojia.client.render.AmbientLight;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * <b>Eli said the switch must not be jarring, so the three things that make it survivable are
 * tested as one unit.</b> The mode change itself is easy; not losing the player is the work.
 *
 * <p>The claim being defended is that orientation is preserved by <em>anchors</em>, not by
 * animation: the tile under the middle of the screen does not move, the direction you will be
 * looking was already drawn on the map before you pressed the key, and the compass never
 * blinks. Each of those is checkable, and each is checked here.
 */
class SwitchAnchorsTest {

    private static final float FRAME = 1 / 60f;

    // ------------------------------------------------------------------ anchor 1: the tile

    /**
     * The eye opens at the centre of the driven actor's own cell, which is the tile the follow
     * camera already had under the middle of the screen. Nothing about the switch moves the
     * point of the world you were looking at.
     */
    @Test
    void theEyeOpensOnTheTileTheMapWasAlreadyCentredOn() {
        FirstPersonCamera eye = new FirstPersonCamera();
        eye.snapTo(91, 60, 19, ViewFacing.yawOf(com.trojia.sim.world.Dir.NORTH));
        assertEquals(91.5f, eye.eyeX(), 1e-5);
        assertEquals(60.5f, eye.eyeY(), 1e-5);
        assertEquals(19, eye.band());
        assertFalse(eye.isStriding(), "entering the view must not start a stride");
    }

    // ------------------------------------------------------------------ anchor 2: the wedge

    /**
     * The wedge on the map points where the first-person camera is about to look. Screen y runs
     * up while world y runs south, and getting that flip wrong points the marker at the
     * player's back — which is the exact failure the marker exists to prevent, so it is pinned
     * for all four cardinals.
     */
    @Test
    void theMapMarkerPointsWhereTheEyeWillLook() {
        float cx = 100f;
        float cy = 200f;
        float span = 32f;
        assertTrue(tipX(cx, cy, span, ViewFacing.yawOf(com.trojia.sim.world.Dir.EAST)) > cx,
                "facing EAST the wedge must point right");
        assertTrue(tipX(cx, cy, span, ViewFacing.yawOf(com.trojia.sim.world.Dir.WEST)) < cx,
                "facing WEST the wedge must point left");
        assertTrue(tipY(cx, cy, span, ViewFacing.yawOf(com.trojia.sim.world.Dir.NORTH)) > cy,
                "facing NORTH the wedge must point UP the screen (world y runs south)");
        assertTrue(tipY(cx, cy, span, ViewFacing.yawOf(com.trojia.sim.world.Dir.SOUTH)) < cy,
                "facing SOUTH the wedge must point DOWN the screen");
    }

    @Test
    void theWedgeIsATriangleThatFitsInsideItsTile() {
        float[] corners = FacingWedge.corners(100f, 200f, 32f, 0.7f);
        assertEquals(8, corners.length);
        assertEquals(corners[4], corners[6], 1e-5, "the fourth corner repeats the third");
        assertEquals(corners[5], corners[7], 1e-5, "so the quad's second triangle collapses");
        for (int i = 0; i < 8; i += 2) {
            assertTrue(Math.abs(corners[i] - 100f) <= 32f, "wedge escaped its tile in x");
            assertTrue(Math.abs(corners[i + 1] - 200f) <= 32f, "wedge escaped its tile in y");
        }
    }

    /**
     * <b>The wedge is live.</b> It was not: the yaw was seeded once from {@code Actor.facing()}
     * when the actor was taken over and then never moved again, so the marker on the map went
     * on pointing wherever the body happened to be facing at that moment however far you walked
     * it. An anchor that is wrong is worse than an anchor that is missing — this one promises
     * the first-person frame will open one way and opens it another.
     */
    @Test
    void theWedgeTurnsWithTheActorAsItWalks() {
        FirstPersonCamera eye = new FirstPersonCamera();
        eye.snapTo(50, 50, 19, ViewFacing.yawOf(com.trojia.sim.world.Dir.EAST));
        float cx = 100f;
        float cy = 200f;
        float span = 32f;
        assertTrue(FacingWedge.corners(cx, cy, span, eye.yaw())[0] > cx,
                "seeded facing EAST, the wedge should start pointing right");

        // The tile view is what is on screen, so a committed step aims the marker.
        eye.followCell(50, 49, 19, true); // stepped NORTH
        for (int i = 0; i < 60; i++) {
            eye.advance(FRAME);
        }
        assertTrue(FacingWedge.corners(cx, cy, span, eye.yaw())[1] > cy,
                "after walking north the wedge must point UP the screen, not east");

        eye.followCell(49, 49, 19, true); // then WEST
        for (int i = 0; i < 60; i++) {
            eye.advance(FRAME);
        }
        assertTrue(FacingWedge.corners(cx, cy, span, eye.yaw())[0] < cx,
                "after walking west the wedge must point left");
    }

    /**
     * <b>And it is aimable.</b> Turning is live from the tile view while an actor is driven —
     * the arrow keys are free there, because {@code CameraInput} suppresses panning whenever
     * something is being driven — so a player can stand still, point the wedge down the street
     * they mean to walk, and press V already looking that way. The aim survives until the next
     * committed step, which is the only thing with a better claim on where you are looking.
     */
    @Test
    void anAimedWedgeHoldsItsDirectionUntilTheNextStep() {
        FirstPersonCamera eye = new FirstPersonCamera();
        eye.snapTo(50, 50, 19, ViewFacing.yawOf(com.trojia.sim.world.Dir.EAST));
        eye.turn((float) Math.toRadians(90)); // aim it south, standing still
        float aimed = eye.yaw();
        for (int i = 0; i < 120; i++) {
            eye.advance(FRAME);
        }
        assertEquals(aimed, eye.yaw(), 1e-5, "standing still must not drift the aim");
        assertTrue(tipY(100f, 200f, 32f, eye.yaw()) < 200f, "aimed south, pointing down-screen");
    }

    /**
     * The wedge and the eye read the same number. If the wedge ever tracked
     * {@code Actor.facing()} instead, a diagonal stride would promise east and deliver
     * north-east, because the sim's facing takes the x component of a diagonal.
     */
    @Test
    void theWedgeAndTheEyeShareOneDirection() {
        FirstPersonCamera eye = new FirstPersonCamera();
        eye.snapTo(10, 10, 19, 0f);
        eye.turn((float) Math.toRadians(37));
        float[] fromEye = FacingWedge.corners(0f, 0f, 10f, eye.yaw());
        float[] independent = FacingWedge.corners(0f, 0f, 10f, (float) Math.toRadians(37));
        assertEquals(independent[0], fromEye[0], 1e-4);
        assertEquals(independent[1], fromEye[1], 1e-4);
    }

    // ------------------------------------------------------------------ anchor 3: the compass

    @Test
    void theCompassReadsTheSameNumberInBothModes() {
        FirstPersonCamera eye = new FirstPersonCamera();
        eye.snapTo(10, 10, 19, ViewFacing.yawOf(com.trojia.sim.world.Dir.NORTH));
        ViewModeState mode = new ViewModeState();
        List<CompassStrip.Mark> beforeSwitch = CompassStrip.plan(eye.yaw(), 300f);
        mode.toggle(4);
        mode.advance(ViewModeState.TRANSITION_SECONDS / 2f);
        List<CompassStrip.Mark> midFade = CompassStrip.plan(eye.yaw(), 300f);
        assertEquals(beforeSwitch, midFade,
                "the compass must be untouched by the mode change — it is the anchor");
    }

    // ------------------------------------------------------------------ the fade itself

    @Test
    void theFadeRunsBothWaysAndSettles() {
        ViewModeState mode = new ViewModeState();
        assertEquals(ViewModeState.Mode.TOP_DOWN, mode.mode());
        assertFalse(mode.firstPersonVisible());

        mode.toggle(3);
        assertTrue(mode.transitioning());
        for (int i = 0; i < 60; i++) {
            mode.advance(FRAME);
        }
        assertEquals(1f, mode.blend(), 1e-5);
        assertTrue(mode.settledFirstPerson());
        assertFalse(mode.topDownVisible(), "a settled first-person frame draws alone");

        mode.toggle(MapCamera.MAX_ZOOM);
        for (int i = 0; i < 60; i++) {
            mode.advance(FRAME);
        }
        assertEquals(0f, mode.blend(), 1e-5);
        assertEquals(ViewModeState.Mode.TOP_DOWN, mode.mode());
        assertFalse(mode.transitioning());
    }

    @Test
    void bothFramesAreOnScreenTogetherDuringTheFade() {
        ViewModeState mode = new ViewModeState();
        mode.toggle(3);
        mode.advance(ViewModeState.TRANSITION_SECONDS / 2f);
        assertTrue(mode.topDownVisible() && mode.firstPersonVisible(),
                "the cross-fade must actually cross — a hard cut is the jarring version");
        assertTrue(mode.blend() > 0.2f && mode.blend() < 0.8f);
    }

    @Test
    void theDollyRunsInToMaxZoomAndBackToWhereThePlayerHadIt() {
        ViewModeState mode = new ViewModeState();
        int zoom = 2;
        mode.toggle(zoom);
        for (int i = 0; i < 40 && mode.transitioning(); i++) {
            zoom = mode.dollyZoom(zoom);
            mode.advance(FRAME);
        }
        assertEquals(MapCamera.MAX_ZOOM, zoom, "the transition should dolly all the way in");

        mode.toggle(zoom);
        for (int i = 0; i < 40 && mode.transitioning(); i++) {
            zoom = mode.dollyZoom(zoom);
            mode.advance(FRAME);
        }
        assertEquals(2, zoom, "coming back must restore the zoom the player was surveying at");
    }

    @Test
    void theDollyOnlyEverReturnsALegalIntegerZoom() {
        ViewModeState mode = new ViewModeState();
        mode.toggle(1);
        int zoom = 1;
        for (int i = 0; i < 40; i++) {
            zoom = mode.dollyZoom(zoom);
            mode.advance(FRAME);
            assertTrue(zoom >= MapCamera.MIN_ZOOM && zoom <= MapCamera.MAX_ZOOM,
                    "the tile view's integer-zoom contract is not negotiable: " + zoom);
        }
    }

    /**
     * <b>Letting the body go is a fade, not a cut.</b> It used to be the one un-transitioned
     * mode change in the client: release the driven actor while in first person and the frame
     * vanished mid-stride. Everything the cross-fade exists to prevent applies at least as
     * strongly on the way out.
     */
    @Test
    void losingTheDrivenActorFadesBackToTheMap() {
        ViewModeState mode = new ViewModeState();
        mode.toggle(4);
        mode.advance(ViewModeState.TRANSITION_SECONDS);
        assertEquals(1f, mode.blend(), 1e-6);

        mode.releaseToTopDown();
        assertEquals(ViewModeState.Mode.TOP_DOWN, mode.mode());
        assertTrue(mode.transitioning(), "letting go of the body must not be a hard cut");
        mode.advance(ViewModeState.TRANSITION_SECONDS / 2f);
        assertTrue(mode.topDownVisible() && mode.firstPersonVisible(),
                "both frames must be on screen while the release fades");

        mode.advance(ViewModeState.TRANSITION_SECONDS);
        assertEquals(0f, mode.blend(), 1e-6);
        assertFalse(mode.firstPersonVisible());
        // And it is idempotent: the caller runs it every frame while nothing is driven.
        mode.releaseToTopDown();
        assertFalse(mode.transitioning());
        assertEquals(0f, mode.blend(), 1e-6);
    }

    /** Releasing from the tile view is a no-op, not a spurious fade. */
    @Test
    void releasingWhileAlreadyOnTheMapChangesNothing() {
        ViewModeState mode = new ViewModeState();
        mode.releaseToTopDown();
        assertFalse(mode.transitioning());
        assertEquals(0f, mode.blend(), 1e-6);
    }

    // ------------------------------------------------------------------ the sky

    @Test
    void theSkyIsNotTheVoidAndFollowsTheDaylight() {
        SkyBand.Rgb day = SkyBand.colorAt(AmbientLight.NEUTRAL);
        assertEquals(((SkyBand.DAY_SKY_RGB >> 16) & 0xFF) / 255f, day.r(), 1e-5);
        assertTrue(day.r() > 0.05f && day.g() > 0.05f && day.b() > 0.05f,
                "an unroofed column terminating on void black reads as a bug, not as sky");

        SkyBand.Rgb night = SkyBand.colorAt(new AmbientLight(0.25f, 0.25f, 0.4f, 1f));
        assertTrue(night.r() < day.r() && night.g() < day.g(),
                "the sky must darken with the ward");
        assertTrue(night.b() / day.b() > night.r() / day.r(),
                "and cool as it darkens, like every other scene draw");
    }

    /**
     * <b>The other half of the frame gets painted too.</b> The plan reaches twenty tiles and no
     * farther; a ground plane below the eye creeps toward the horizon line as it recedes and
     * never arrives, so there is always a band of frame between the last drawn tile and the
     * horizon. Left to the pack's void black that band was a stripe of pure black sitting at
     * eye level — worst of all standing on the quay looking out over the harbour, which is the
     * view this whole feature exists for.
     */
    @Test
    void thereIsGroundUnderTheHorizonAsWellAsSkyOverIt() {
        SkyBand.Rgb sky = SkyBand.colorAt(AmbientLight.NEUTRAL);
        SkyBand.Rgb haze = SkyBand.groundHazeAt(AmbientLight.NEUTRAL);
        assertTrue(haze.r() > 0.05f && haze.g() > 0.05f && haze.b() > 0.05f,
                "a black band at eye level reads as a rendering fault, not as distance");
        assertTrue(haze.g() < sky.g() && haze.b() < sky.b(),
                "the ground must sit darker than the sky, or the horizon stops being one");
        assertTrue(haze.r() / haze.b() > sky.r() / sky.b(),
                "and warmer, so the far ward is earth under slate rather than more slate");

        SkyBand.Rgb night = SkyBand.groundHazeAt(new AmbientLight(0.25f, 0.25f, 0.4f, 1f));
        assertTrue(night.g() < haze.g(), "and it takes the day/night cycle like everything else");
        assertTrue(night.g() > 0f, "but never all the way to black");
    }

    private static float tipX(float cx, float cy, float span, float yaw) {
        return FacingWedge.corners(cx, cy, span, yaw)[0];
    }

    private static float tipY(float cx, float cy, float span, float yaw) {
        return FacingWedge.corners(cx, cy, span, yaw)[1];
    }
}
