package com.trojia.client.time;

import com.trojia.sim.engine.EngineConfig;
import com.trojia.sim.engine.SimCommand;
import com.trojia.sim.engine.SimulationEngine;
import com.trojia.sim.engine.Simulations;
import com.trojia.sim.engine.TickProfile;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.World;
import com.trojia.sim.world.WorldBuilder;
import com.trojia.sim.world.WorldConfig;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link SimulationDriver} pacing contracts (client-observer M1 time-advancement): speed
 * settings change only <em>when</em> {@code SimulationEngine#tick()} runs, this class is the
 * wall-clock accumulator that decides that timing.
 */
class SimulationDriverTest {

    @Test
    void startsPaused() {
        SimulationDriver driver = new SimulationDriver(worldlessEngine());
        assertEquals(SpeedSetting.PAUSED, driver.speed());
        assertEquals(0, driver.currentTick());
    }

    @Test
    void pausedNeverTicksEvenAsTimeElapses() {
        SimulationDriver driver = new SimulationDriver(worldlessEngine());
        driver.update(10f); // ten simulated seconds of real time
        driver.update(10f);
        assertEquals(0, driver.currentTick());
    }

    @Test
    void stepSpeedNeverAutoTicksEither() {
        SimulationDriver driver = new SimulationDriver(worldlessEngine());
        driver.setSpeed(SpeedSetting.STEP);
        driver.update(10f);
        assertEquals(0, driver.currentTick());
    }

    @Test
    void requestStepTicksExactlyOnceRegardlessOfSpeed() {
        SimulationDriver driver = new SimulationDriver(worldlessEngine());
        driver.requestStep();
        assertEquals(1, driver.currentTick());
        driver.requestStep();
        assertEquals(2, driver.currentTick());
    }

    @Test
    void runExecutesOneTickPerBaseTickPeriodCarryingRemainderForward() {
        SimulationDriver driver = new SimulationDriver(new SlowFakeEngine(0));
        driver.setSpeed(SpeedSetting.RUN);

        driver.update(0.25f); // 250 ms -> 2 ticks at 100 ms/tick, 50 ms remainder carried
        assertEquals(2, driver.currentTick());

        driver.update(0.05f); // +50 ms = 100 ms total remainder -> exactly one more tick
        assertEquals(3, driver.currentTick());
    }

    @Test
    void fastTicksFourTimesPerRunPeriodWhenTickWorkIsCheap() {
        SimulationDriver driver = new SimulationDriver(new SlowFakeEngine(0));
        driver.setSpeed(SpeedSetting.FAST);

        driver.update(0.1f); // 100 ms of backlog at 25 ms/tick = 4 ticks, well inside budget
        assertEquals(4, driver.currentTick());
    }

    @Test
    void fastSpeedDropsBacklogInsteadOfCatchingUpNextFrame() {
        // Each fake tick "costs" 8 ms of real time; the FAST budget is 12 ms/frame, so only
        // part of a 4-tick backlog fits before the budget check fires.
        SlowFakeEngine engine = new SlowFakeEngine(8);
        SimulationDriver driver = new SimulationDriver(engine);
        driver.setSpeed(SpeedSetting.FAST);

        driver.update(0.1f); // 100 ms of backlog at 25 ms/tick = 4 ticks worth
        long ticksAfterFirstFrame = driver.currentTick();
        assertTrue(ticksAfterFirstFrame >= 1 && ticksAfterFirstFrame < 4,
                () -> "expected the 12ms budget to cut off partway through, got "
                        + ticksAfterFirstFrame);

        driver.update(0f); // no new elapsed time; a carried-forward backlog would tick again
        assertEquals(ticksAfterFirstFrame, driver.currentTick(),
                "dropped backlog must not resurface as a catch-up burst on a later frame");
    }

    @Test
    void wrapsARealTickableWorldThroughThePublicConstructor() {
        // Minimum legal world: 1 interior chunk per axis plus the mandatory VOID border ring.
        TickableWorld world = WorldBuilder.create(new WorldConfig(3, 3, 3)).build();
        SimulationDriver driver = new SimulationDriver(world, 1L);

        assertEquals(SpeedSetting.PAUSED, driver.speed());
        driver.requestStep();
        assertEquals(1, driver.currentTick());
        assertEquals(1, driver.engine().currentTick());
    }

    private static SimulationEngine worldlessEngine() {
        return Simulations.create(new EngineConfig(1L), List.of());
    }

    /** A minimal {@link SimulationEngine} fake whose {@code tick()} takes a fixed real time. */
    private static final class SlowFakeEngine implements SimulationEngine {

        private final long sleepMillisPerTick;
        private long tick;

        SlowFakeEngine(long sleepMillisPerTick) {
            this.sleepMillisPerTick = sleepMillisPerTick;
        }

        @Override
        public long currentTick() {
            return tick;
        }

        @Override
        public World world() {
            return null;
        }

        @Override
        public void tick() {
            if (sleepMillisPerTick > 0) {
                try {
                    Thread.sleep(sleepMillisPerTick);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            }
            tick++;
        }

        @Override
        public void step(int ticks) {
            for (int i = 0; i < ticks; i++) {
                tick();
            }
        }

        @Override
        public void submit(SimCommand command) {
            throw new UnsupportedOperationException("not used by this test");
        }

        @Override
        public void save(Path file) throws IOException {
            throw new UnsupportedOperationException("not used by this test");
        }

        @Override
        public TickProfile inspect() {
            return TickProfile.empty();
        }
    }
}
