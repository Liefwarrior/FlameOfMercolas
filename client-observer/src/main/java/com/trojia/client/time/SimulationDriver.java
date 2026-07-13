package com.trojia.client.time;

import com.trojia.sim.engine.EngineConfig;
import com.trojia.sim.engine.SimulationEngine;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.engine.Simulations;
import com.trojia.sim.world.TickableWorld;

import java.util.List;

/**
 * Wall-clock pacing layer around a {@link SimulationEngine}: the observer's fixed-timestep
 * driver (ARCHITECTURE section 3). Consumes {@link SpeedSetting}, which is pure state with
 * no driver logic of its own, and decides <em>when</em> {@link SimulationEngine#tick()} runs
 * — never what it computes, so a world advanced N ticks is bit-identical at any speed.
 *
 * <p>Boots the engine at tick 0 over the caller's already-loaded {@link TickableWorld} with
 * an empty systems list: today the only {@link SimulationSystem} implementation in the tree
 * is {@code ActorsSystem} (mid-development, out of scope for the observer), so the
 * FLUIDS/THERMAL/REACTIONS/LIGHT/ACTORS/etc. phases in {@code TickPhase} are empty slots this
 * engine iterates with no registered work — each {@code tick()} costs a clock advance plus
 * {@link TickableWorld#beginTick}/{@link TickableWorld#commitTick} until real systems land and
 * are registered here.
 *
 * <p>Starts {@link SpeedSetting#PAUSED}; callers opt into ticking, it never autoplays.
 */
public final class SimulationDriver {

    private final SimulationEngine engine;
    private SpeedSetting speed = SpeedSetting.PAUSED;
    private double accumulatedMillis;

    /**
     * Wraps {@code world} in a fresh tick-0 engine (no systems registered — see class
     * javadoc) bound to {@code worldSeed}, the seed the world was originally built/baked
     * with (harmless even though nothing currently draws RNG from it, and keeps this
     * forward-compatible with whatever systems register later).
     */
    public SimulationDriver(TickableWorld world, long worldSeed) {
        this(world, worldSeed, List.<SimulationSystem>of());
    }

    /**
     * Wraps {@code world} in a fresh tick-0 engine bound to {@code worldSeed} with
     * {@code systems} registered (in list order = event-visibility order within a phase).
     * The compound-block boot path passes an {@code ActorsSystem} here so the {@code ACTORS}
     * phase actually advances its population every tick; the no-arg-systems overload above is
     * the tavern's system-less world walk-through.
     */
    public SimulationDriver(TickableWorld world, long worldSeed, List<SimulationSystem> systems) {
        this(Simulations.create(new EngineConfig(worldSeed), world, systems));
    }

    /** Package-visible seam for tests to drive a lighter (possibly world-less) engine. */
    SimulationDriver(SimulationEngine engine) {
        this.engine = engine;
    }

    /** The tick last completed; 0 before the first tick. */
    public long currentTick() {
        return engine.currentTick();
    }

    /** Read access to the ticking engine (renderers/HUD read world/tick state through it). */
    public SimulationEngine engine() {
        return engine;
    }

    /** The active speed setting. */
    public SpeedSetting speed() {
        return speed;
    }

    /** Changes the active speed setting; takes effect on the next {@link #update}. */
    public void setSpeed(SpeedSetting speed) {
        this.speed = speed;
    }

    /**
     * Executes exactly one tick, independent of {@link #speed}. The manual-step control:
     * callers gate this on their own policy (e.g. only while {@link SpeedSetting#PAUSED}).
     */
    public void requestStep() {
        engine.tick();
    }

    /**
     * Advances the wall-clock accumulator by {@code deltaSeconds} and executes whatever
     * whole ticks {@link #speed} calls for.
     *
     * <p>{@link SpeedSetting#PAUSED} and {@link SpeedSetting#STEP} never auto-tick — the
     * accumulator is not even advanced while either is active, so resuming RUN/FAST later
     * never replays a paused-time backlog. {@link SpeedSetting#RUN} accumulates real time
     * and drains it one {@link SpeedSetting#BASE_TICK_MILLIS} tick at a time, carrying any
     * remainder forward (no clamp — at 10 ticks/s a dropped frame practically never backs
     * up past one tick). {@link SpeedSetting#FAST} does the same at its faster tick period
     * but is additionally clamped to {@link SpeedSetting#FRAME_TICK_BUDGET_MILLIS} of
     * <em>measured</em> real time spent ticking inside this call; once that budget is spent
     * the remaining backlog is dropped outright (not carried to next frame), so a slow
     * frame can never spiral into an ever-growing catch-up burst.
     */
    public void update(float deltaSeconds) {
        if (!speed.autoAdvances()) {
            return;
        }
        accumulatedMillis += deltaSeconds * 1000.0;
        int periodMillis = speed.tickPeriodMillis();
        if (speed == SpeedSetting.FAST) {
            long budgetNanos = SpeedSetting.FRAME_TICK_BUDGET_MILLIS * 1_000_000L;
            long loopStartNanos = System.nanoTime();
            while (accumulatedMillis >= periodMillis) {
                if (System.nanoTime() - loopStartNanos >= budgetNanos) {
                    accumulatedMillis = 0; // drop the backlog rather than spiraling
                    return;
                }
                engine.tick();
                accumulatedMillis -= periodMillis;
            }
        } else {
            while (accumulatedMillis >= periodMillis) {
                engine.tick();
                accumulatedMillis -= periodMillis;
            }
        }
    }
}
