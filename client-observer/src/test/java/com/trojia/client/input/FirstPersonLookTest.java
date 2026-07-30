package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.fpv.EyeProjection;
import com.trojia.client.fpv.FirstPersonCamera;
import com.trojia.client.inspect.PlayModeState;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Proxy;
import java.util.HashSet;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * <b>The look key, held hard, in both directions.</b>
 *
 * <p>The defect this pins: {@code poll} returned {@code shearPx + this frame's delta} with no
 * clamp anywhere on the accumulator, and the clamp lived only at the far end of the pipe, in
 * {@link EyeProjection}'s constructor. Holding {@code PageUp} for five seconds therefore wound
 * the accumulator to {@code 5 x 420 = 2100} px against a clamp of {@code 0.45 x 720 = 324}: the
 * frame stopped moving after 0.77 s, and {@code PageDown} then had 1776 px — <b>four and a
 * quarter seconds</b> — of invisible slack to unwind before the horizon budged. The control
 * locked up, worst standing on the quay looking out over the harbour, which is the view the
 * whole feature exists for.
 *
 * <p>Both halves are exercised: the {@code applyLook} seam directly (the deterministic path the
 * scripted tape drives), and {@code poll} itself against a stand-in {@code Gdx.input}, so the
 * live key read is proven to go through the clamped accumulator rather than round it.
 */
class FirstPersonLookTest {

    private static final int VIEWPORT_H = 720;
    private static final float FRAME = 1 / 60f;
    private static final float MAX = EyeProjection.MAX_SHEAR_FRACTION * VIEWPORT_H;
    /** One frame's worth of look at the shipped rate — what "responds" has to mean. */
    private static final float PER_FRAME = FirstPersonInput.SHEAR_PX_PER_SECOND * FRAME;

    private Input realInput;

    @AfterEach
    void restoreTheKeyboard() {
        Gdx.input = realInput;
    }

    // ------------------------------------------------------------------ the seam

    @Test
    void theAccumulatorIsClampedSoTheOppositeLookAnswersOnTheVeryNextFrame() {
        FirstPersonCamera eye = placedEye();

        // Five seconds of PageUp: 2100 px wanted, 324 px allowed.
        for (int frame = 0; frame < 300; frame++) {
            FirstPersonInput.applyLook(eye, +1, FRAME, VIEWPORT_H);
            assertTrue(eye.lookShearPx() <= MAX + 1e-3f,
                    "the accumulator wound to " + eye.lookShearPx() + " px past a clamp of "
                            + MAX + " — that is the lock-up");
        }
        assertEquals(MAX, eye.lookShearPx(), 1e-3, "a held look key should sit ON the clamp");

        // The very next frame in the other direction has to move the horizon.
        FirstPersonInput.applyLook(eye, -1, FRAME, VIEWPORT_H);
        assertEquals(MAX - PER_FRAME, eye.lookShearPx(), 1e-3,
                "PageDown after a long PageUp must move the horizon a full frame's worth "
                        + "immediately, not unwind invisible slack");

        // And the same again the other way round.
        for (int frame = 0; frame < 300; frame++) {
            FirstPersonInput.applyLook(eye, -1, FRAME, VIEWPORT_H);
        }
        assertEquals(-MAX, eye.lookShearPx(), 1e-3);
        FirstPersonInput.applyLook(eye, +1, FRAME, VIEWPORT_H);
        assertEquals(-MAX + PER_FRAME, eye.lookShearPx(), 1e-3,
                "PageUp after a long PageDown must answer on the next frame too");
    }

    @Test
    void theScriptedLookActionCannotWindPastTheClampEither() {
        FirstPersonCamera eye = placedEye();
        eye.setLookShear(9000f, VIEWPORT_H);
        assertEquals(MAX, eye.lookShearPx(), 1e-3, "a tape must not be able to arm the lock-up");
        eye.setLookShear(-9000f, VIEWPORT_H);
        assertEquals(-MAX, eye.lookShearPx(), 1e-3);
    }

    @Test
    void theHorizonComesBackLevelWithTheEye() {
        FirstPersonCamera eye = placedEye();
        FirstPersonInput.applyLook(eye, -1, 1f, VIEWPORT_H);
        assertTrue(eye.lookShearPx() < 0f, "we are looking at the floor");

        // Taking over a different body reseats the eye — and the horizon with it, or the frame
        // re-opens craned at the ground while the wedge says otherwise.
        eye.snapTo(12, 9, 19, 0f);
        assertEquals(0f, eye.lookShearPx(), 1e-4, "a reseated eye looks straight ahead");

        FirstPersonInput.applyLook(eye, +1, 1f, VIEWPORT_H);
        eye.levelTheHorizon();
        assertEquals(0f, eye.lookShearPx(), 1e-4, "the V key levels the horizon on the way in");
    }

    // ------------------------------------------------------------------ the live key read

    @Test
    void aHeldLookKeyThroughTheRealPollBehavesTheSame() {
        FirstPersonCamera eye = placedEye();
        PlayModeState playMode = new PlayModeState();
        playMode.enable(7);
        Set<Integer> pressed = fakeKeyboard();

        pressed.add(Input.Keys.PAGE_UP);
        for (int frame = 0; frame < 300; frame++) {
            FirstPersonInput.poll(eye, playMode, null, FRAME, VIEWPORT_H);
        }
        assertEquals(MAX, eye.lookShearPx(), 1e-3,
                "five seconds of held PageUp must leave the accumulator on the clamp");

        pressed.clear();
        pressed.add(Input.Keys.PAGE_DOWN);
        FirstPersonInput.poll(eye, playMode, null, FRAME, VIEWPORT_H);
        assertEquals(MAX - PER_FRAME, eye.lookShearPx(), 1e-3,
                "the opposite key must answer on the very next polled frame");
    }

    // ------------------------------------------------------------------ helpers

    private static FirstPersonCamera placedEye() {
        FirstPersonCamera eye = new FirstPersonCamera();
        eye.snapTo(40, 60, 19, 0f);
        return eye;
    }

    /**
     * A stand-in {@code Gdx.input} that answers {@code isKeyPressed} from a mutable set and
     * nothing else — enough to drive {@code poll} with no window, no GL and no keyboard.
     */
    private Set<Integer> fakeKeyboard() {
        realInput = Gdx.input;
        Set<Integer> pressed = new HashSet<>();
        Gdx.input = (Input) Proxy.newProxyInstance(Input.class.getClassLoader(),
                new Class<?>[] {Input.class}, (proxy, method, args) -> {
                    if ("isKeyPressed".equals(method.getName())) {
                        return pressed.contains((Integer) args[0]);
                    }
                    Class<?> returns = method.getReturnType();
                    if (returns == boolean.class) {
                        return false;
                    }
                    if (returns == int.class) {
                        return 0;
                    }
                    if (returns == long.class) {
                        return 0L;
                    }
                    if (returns == float.class) {
                        return 0f;
                    }
                    return null;
                });
        return pressed;
    }
}
