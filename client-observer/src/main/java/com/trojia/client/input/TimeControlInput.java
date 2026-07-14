package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.time.SimulationDriver;
import com.trojia.client.time.SpeedSetting;

/**
 * Polls keyboard state each frame and drives a {@link SimulationDriver} (M1 time-advancement
 * behavior). GL-dependent (reads {@link Gdx#input}); call once per {@code render()} frame,
 * alongside {@link CameraInput#poll} (bindings are disjoint from that class's WASD/arrows,
 * {@code [ }/{@code ]} and the Up/Down arrow z-level scrub).
 *
 * <p>Bindings:
 * <ul>
 *   <li>{@code SPACE} — play/pause toggle: resumes at {@link SpeedSetting#RUN} from
 *       {@link SpeedSetting#PAUSED}, or pauses from any running speed (including
 *       {@link SpeedSetting#FAST}).</li>
 *   <li>{@code F} — fast-forward toggle: switches to {@link SpeedSetting#FAST} from any
 *       other speed (including {@link SpeedSetting#PAUSED}, so it can also resume-and-speed-up
 *       in one press), or back to {@link SpeedSetting#RUN} if already {@link SpeedSetting#FAST}.</li>
 *   <li>{@code PERIOD} — manual single step: executes exactly one tick, but only while
 *       {@link SpeedSetting#PAUSED} (ignored otherwise, since stepping on top of an
 *       auto-advancing speed has no well-defined meaning).</li>
 * </ul>
 */
public final class TimeControlInput {

    private TimeControlInput() {
    }

    /** Reads the current keyboard state and applies one frame's worth of time-control input. */
    public static void poll(SimulationDriver driver) {
        if (Gdx.input.isKeyJustPressed(Input.Keys.SPACE)) {
            driver.setSpeed(driver.speed() == SpeedSetting.PAUSED
                    ? SpeedSetting.RUN
                    : SpeedSetting.PAUSED);
        }

        if (Gdx.input.isKeyJustPressed(Input.Keys.F)) {
            driver.setSpeed(driver.speed() == SpeedSetting.FAST
                    ? SpeedSetting.RUN
                    : SpeedSetting.FAST);
        }

        if (Gdx.input.isKeyJustPressed(Input.Keys.PERIOD) && driver.speed() == SpeedSetting.PAUSED) {
            driver.requestStep();
        }
    }
}
