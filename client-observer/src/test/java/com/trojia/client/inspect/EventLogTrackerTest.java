package com.trojia.client.inspect;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.engine.SimulationSystem;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.concurrent.atomic.AtomicLong;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link EventLogTracker} wired to a real {@link SimulationDriver} over the compound
 * population — proves the per-<em>tick</em> (not per-frame) event feed: the driver's
 * after-each-tick seam fires exactly once per executed tick, and the tracker turns real
 * transitions (reason/goal changes, home arrivals) into tick-tagged entries. Headless:
 * reads committed raws + baked world, no GL.
 */
class EventLogTrackerTest {

    private static final int TICKS = 600;

    @Test
    void logsRealTransitionsOncePerTick() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadCompoundBlock();
        CompoundBlockPopulation population =
                CompoundBlockPopulation.build(loaded.worldSeed(), loaded.world());

        // Big enough to retain the whole run's history for assertions (the live app caps at 30).
        EventLog log = new EventLog(5000);
        EventLogTracker tracker = new EventLogTracker(population.registry(), population.homes(), log);

        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));

        // Count every after-tick callback to prove it fires exactly once per executed tick.
        AtomicLong ticksSeen = new AtomicLong();
        driver.setAfterTick(tick -> {
            ticksSeen.incrementAndGet();
            tracker.afterTick(tick);
        });

        for (int i = 0; i < TICKS; i++) {
            driver.requestStep();
        }

        assertEquals(TICKS, ticksSeen.get(), "after-tick must fire exactly once per tick");
        assertEquals(TICKS, driver.currentTick());

        List<EventLog.Entry> all = log.recentNewestFirst(Integer.MAX_VALUE);
        assertTrue(all.size() > 0, "expected transitions to be logged over 600 ticks");

        // Every entry is tagged with a plausible tick within the run.
        for (EventLog.Entry e : all) {
            assertTrue(e.tick() >= 1 && e.tick() <= TICKS,
                    () -> "entry tick out of range: " + e);
        }

        // The deliberately displaced movers walk back and arrive home within the run.
        EventLog.Entry arrival = all.stream()
                .filter(e -> e.text().contains("arrived home"))
                .findFirst()
                .orElseThrow(() -> new AssertionError("no home-arrival transition logged"));
        assertTrue(arrival.tick() > 0 && arrival.tick() < TICKS,
                () -> "home arrival should occur mid-run, was at tick " + arrival.tick());

        // Reason-code and goal-state transitions are both captured.
        assertTrue(all.stream().anyMatch(e -> e.text().contains("reason")),
                "expected at least one reason-code transition");
        assertTrue(all.stream().anyMatch(e -> e.text().contains("goal")),
                "expected at least one goal-state transition");
    }
}
