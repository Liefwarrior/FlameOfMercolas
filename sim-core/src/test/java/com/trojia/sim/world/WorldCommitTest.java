package com.trojia.sim.world;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/**
 * Scheduler-face contract of {@link TickableWorld}: begin/commit alternation,
 * advancing ticks only, and the commit fan-out order — revisions bump before
 * change-log compaction.
 */
final class WorldCommitTest {

    @Test
    void commitBumpsRevisionsThenCompactsLogs() {
        WorldFixture fixture = WorldFixture.minimal(Lanes.MATERIAL);
        List<String> journal = new ArrayList<>();
        fixture.logs.journal = journal;
        fixture.revisions.journal = journal;
        fixture.world.beginTick(1);
        fixture.writer.setMaterial(WorldFixture.interiorPos(), (short) 4);
        fixture.world.commitTick(1);
        assertEquals(List.of("revisions.commit@1", "logs.compact@1"), journal);
        assertEquals(List.of(1L), fixture.revisions.commits);
        assertEquals(List.of(1L), fixture.logs.compacts);
    }

    @Test
    void consecutiveTicksAlternateBeginAndCommit() {
        WorldFixture fixture = WorldFixture.minimal();
        fixture.world.beginTick(1);
        fixture.world.commitTick(1);
        fixture.world.beginTick(2);
        fixture.world.commitTick(2);
        assertEquals(List.of(1L, 2L), fixture.revisions.commits);
    }

    @Test
    void misuseIsRejectedLoudly() {
        WorldFixture fixture = WorldFixture.minimal();
        assertThrows(IllegalStateException.class, () -> fixture.world.commitTick(1));
        fixture.world.beginTick(1);
        assertThrows(IllegalStateException.class, () -> fixture.world.beginTick(2));
        assertThrows(IllegalStateException.class, () -> fixture.world.commitTick(2));
        fixture.world.commitTick(1);
        // Ticks may only advance.
        assertThrows(IllegalStateException.class, () -> fixture.world.beginTick(1));
        assertThrows(IllegalStateException.class, () -> fixture.world.beginTick(0));
        fixture.world.beginTick(5); // gaps are legal (load-at-K resume)
        fixture.world.commitTick(5);
    }
}
