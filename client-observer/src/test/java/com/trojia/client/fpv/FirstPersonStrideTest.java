package com.trojia.client.fpv;

import com.trojia.client.boot.RepoPaths;
import com.trojia.client.time.SpeedSetting;
import com.trojia.sim.actor.ActorRawsLoader;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.ActorTypeStatsTable;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * <b>The stride, timed against the cadence it actually has to fill.</b>
 *
 * <p>Eli asked for smooth and got a lurch, and the reason was that the ease was sized as a
 * fraction of a cadence nobody had measured. So this test measures it, from the two shipped
 * sources that between them decide it and nothing else:
 *
 * <ul>
 *   <li>{@code content/raws/actors/*.json} — every actor type's {@code speedTicksPerStep},
 *       read through {@link ActorRawsLoader}, the same loader the observer boots with.</li>
 *   <li>{@link SpeedSetting} — the tick period at each speed the player can select.</li>
 * </ul>
 *
 * <p>Their product is the complete set of real step cadences, and every one of them is then
 * driven through {@link FirstPersonCamera} frame by frame at 60 Hz with the same two questions
 * asked of each: does the eye keep moving for the whole cadence, and does it land before the
 * next step commits. Those are the two failure modes — the lurch and the glide — and there is
 * no third.
 *
 * <p>No world, no GL, no registry: the raws are JSON and the camera is arithmetic.
 */
class FirstPersonStrideTest {

    private static final int BAND = 19;
    private static final float FRAME = 1 / 60f;

    private static ActorTypeStatsTable typeStats;

    @BeforeAll
    static void loadTheShippedRaws() {
        typeStats = ActorRawsLoader.load(RepoPaths.locate("content", "raws", "actors"));
    }

    /** Every real (speed setting, actor type) step cadence in seconds, deduplicated. */
    private static List<Float> realCadencesSeconds() {
        TreeSet<Integer> ticksPerStep = new TreeSet<>();
        for (ActorTypeStats stats : typeStats.all()) {
            ticksPerStep.add(stats.speedTicksPerStep());
        }
        List<Float> cadences = new ArrayList<>();
        for (SpeedSetting speed : SpeedSetting.values()) {
            if (!speed.autoAdvances()) {
                continue; // PAUSED and STEP have no cadence to fill
            }
            for (int ticks : ticksPerStep) {
                cadences.add(ticks * speed.tickPeriodMillis() / 1000f);
            }
        }
        System.out.println("fpv stride: ticksPerStep in the shipped raws " + ticksPerStep
                + "; real cadences (s) " + cadences);
        return cadences;
    }

    /**
     * The measurement itself, on the record. If a raws edit ever adds a slower actor type this
     * fails and the ease constants get re-argued, which is the point of pinning it.
     */
    @Test
    void theRealStepCadencesAreTheOnesTheEaseIsSizedFor() {
        List<Float> cadences = realCadencesSeconds();
        assertFalse(cadences.isEmpty());
        float slowest = 0f;
        for (float cadence : cadences) {
            slowest = Math.max(slowest, cadence);
        }
        assertEquals(0.200f, slowest, 1e-4,
                "the slowest real cadence is a speedTicksPerStep=2 actor at RUN: 2 x 100 ms");
        assertTrue(FirstPersonCamera.MAX_STEP_EASE_SECONDS
                        > slowest * FirstPersonCamera.STEP_ARRIVAL_FRACTION,
                "the ease clamp bites before the arrival fraction does, so the stride's shape "
                        + "at the slowest real cadence is set by a clamp rather than by the "
                        + "curve — clamp " + FirstPersonCamera.MAX_STEP_EASE_SECONDS + "s vs "
                        + "wanted " + (slowest * FirstPersonCamera.STEP_ARRIVAL_FRACTION) + "s");
    }

    /**
     * <b>At every real cadence: the eye is still moving on the second-to-last frame, and it has
     * landed by the time the next step commits.</b>
     *
     * <p>The old numbers fail the first half of this at RUN by a mile — 0.62 of a 200 ms
     * cadence is 124 ms, so the eye was parked for the last 76 ms, four and a half frames out
     * of twelve.
     */
    @Test
    void everyRealCadenceIsWalkedRatherThanLurched() {
        for (float cadence : realCadencesSeconds()) {
            // A cadence the display cannot resolve is not a stride question: at FAST a
            // speed-1 actor steps every 25 ms, which is a step and a half per rendered frame,
            // and no camera can show what the screen has no frames for. The cadence the eye
            // actually has to fill is the whole frames that fit it.
            int frames = Math.max(1, Math.round(cadence / FRAME));
            float renderedCadence = frames * FRAME;

            FirstPersonCamera cam = new FirstPersonCamera();
            cam.snapTo(40, 60, BAND, 0f);
            // Three steps to teach it the cadence and let it settle, then measure the fourth.
            for (int step = 1; step <= 3; step++) {
                cam.followCell(40 + step, 60, BAND);
                advanceFrames(cam, frames);
            }
            assertEquals(renderedCadence, cam.measuredStepIntervalSeconds(), 1e-4,
                    "the camera mis-measured a " + cadence + "s cadence");
            float from = cam.eyeX();
            cam.followCell(44, 60, BAND);

            float previous = cam.eyeX();
            int stalled = 0;
            for (int i = 0; i < frames; i++) {
                cam.advance(FRAME);
                float moved = cam.eyeX() - previous;
                previous = cam.eyeX();
                if (moved < 1e-4f) {
                    stalled++;
                }
            }
            // One settle frame is the deliberate 8% of slack the arrival fraction leaves for
            // frame jitter to arrive late into. More than one is the lurch.
            assertTrue(stalled <= 1,
                    "at a " + cadence + "s cadence (" + frames + " frames) the eye stood "
                            + "perfectly still for " + stalled + " of them — that is the lurch");
            assertTrue(cam.eyeX() > from,
                    "the eye did not move at all over a whole " + cadence + "s cadence");
            assertEquals(44.5f, cam.eyeX(), 1e-4,
                    "the eye must be standing where the sim put the body by the time the next "
                            + "step is due (" + cadence + "s cadence)");
            assertFalse(cam.isStriding(),
                    "at a " + cadence + "s cadence the ease was still running when the next "
                            + "step was due — that is a glide");
        }
    }

    /**
     * Chained walking is near-constant velocity, which is what a stride is. Each committed step
     * re-bases the ease from where the eye actually is, so the only speed variation left is the
     * curve's own push-off and settle. Sampled over five steps at the real-time cadence: apart
     * from the one settle frame each step is allowed, the fastest frame must be under twice the
     * slowest.
     *
     * <p>The previous curve was between 3.5x average and a dead stop for the last five frames
     * of every twelve, which is not a ratio at all — it is a lunge and a freeze, five times a
     * second.
     */
    @Test
    void chainedStepsHoldARoughlyConstantWalkingSpeed() {
        int frames = 12;               // a 200 ms cadence at 60 Hz
        FirstPersonCamera cam = new FirstPersonCamera();
        cam.snapTo(40, 60, BAND, 0f);
        cam.followCell(41, 60, BAND);
        advanceFrames(cam, frames);

        float fastest = 0f;
        float slowest = Float.MAX_VALUE;
        int settleFrames = 0;
        float previous = cam.eyeX();
        for (int step = 2; step <= 6; step++) {
            cam.followCell(40 + step, 60, BAND);
            for (int i = 0; i < frames; i++) {
                cam.advance(FRAME);
                float moved = cam.eyeX() - previous;
                previous = cam.eyeX();
                if (moved < 0.01f) {
                    settleFrames++;   // the deliberate 8% of slack at the end of each ease
                    continue;
                }
                fastest = Math.max(fastest, moved);
                slowest = Math.min(slowest, moved);
            }
        }
        System.out.println("fpv stride: chained walk at a 200 ms cadence moves between "
                + String.format("%.4f", slowest) + " and " + String.format("%.4f", fastest)
                + " tiles per frame, with " + settleFrames + " settle frames in 5 steps");
        assertTrue(settleFrames <= 5, "more than one settle frame per step: " + settleFrames);
        assertTrue(fastest < slowest * 2f,
                "the walk pulses between " + slowest + " and " + fastest + " tiles/frame — a "
                        + "stride should not double and halve its speed inside a step");
    }

    /**
     * <b>A climb takes exactly as long as a step at every real cadence, and that is fine.</b>
     * Raising the lateral arrival fraction to fill the cadence brought it level with the
     * climb's, so the old "a climb eases longer" is no longer true and is not claimed. What is
     * asserted instead is what actually distinguishes the two, and it is a bigger difference
     * than a duration ever was: in the same time, the climb covers a whole band of rise rather
     * than one tile of ground — 2.75x the speed, straight up — and it never dips.
     *
     * <p>The head bob is the tell. A stride dips and recovers; a climb that dipped would read
     * as tripping on the stair.
     */
    @Test
    void aClimbCoversABandInTheTimeAStepCoversATileAndNeverDips() {
        float cadence = 0.200f;
        FirstPersonCamera lateral = walkedTo(cadence, 41, 60, BAND);
        FirstPersonCamera climb = walkedTo(cadence, 40, 60, BAND + 1);

        float standing = BandGeometry.floorHeight(BAND) + BandGeometry.EYE_HEIGHT;
        float previous = climb.eyeHeight();
        int frames = Math.round(cadence / FRAME);
        for (int i = 0; i < frames; i++) {
            climb.advance(FRAME);
            assertTrue(climb.eyeHeight() >= previous - 1e-5f,
                    "the eye dipped while climbing — the stride bob must not apply to a climb");
            previous = climb.eyeHeight();
        }
        advanceFrames(lateral, frames);

        assertFalse(lateral.isStriding(), "a lateral step must land inside its own cadence");
        assertFalse(climb.isStriding(), "and so must a climb, or a staircase becomes a glide");
        assertEquals(41.5f, lateral.eyeX(), 1e-4);
        assertEquals(BandGeometry.floorHeight(BAND + 1) + BandGeometry.EYE_HEIGHT,
                climb.eyeHeight(), 1e-4, "a climb must land exactly on the new band's floor");
        assertEquals(BandGeometry.BAND_HEIGHT, climb.eyeHeight() - standing, 1e-4,
                "in one cadence a climb covers a whole band while a step covers one tile");
    }

    private static FirstPersonCamera walkedTo(float cadence, int x, int y, int z) {
        FirstPersonCamera cam = new FirstPersonCamera();
        cam.snapTo(40, 60, BAND, 0f);
        cam.followCell(40, 61, BAND);
        runSeconds(cam, cadence);
        cam.followCell(x, y, z);
        return cam;
    }

    private static void runSeconds(FirstPersonCamera cam, float seconds) {
        advanceFrames(cam, Math.max(1, Math.round(seconds / FRAME)));
    }

    private static void advanceFrames(FirstPersonCamera cam, int frames) {
        for (int i = 0; i < frames; i++) {
            cam.advance(FRAME);
        }
    }
}
