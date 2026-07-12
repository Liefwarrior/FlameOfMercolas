package com.trojia.sim.world.change;

import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.WorldConfig;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Contract tests for {@link ActiveSet}: FIFO order, O(1) dedupe via the
 * per-chunk bitsets, membership across poll/re-add, ring growth and
 * wraparound, chunk drops preserving survivor order, and cross-instance
 * determinism of identical operation sequences.
 */
final class ActiveSetTest {

    /** 4x4x3 chunks: a 2x2 interior of chunk columns inside the VOID border. */
    private static final Coords COORDS = Coords.of(new WorldConfig(4, 4, 3));

    @Test
    void addDedupesAndPollsInFirstInsertionOrder() {
        ActiveSet set = new ActiveSet(COORDS);
        int a = PackedPos.pack(33, 40, 8);
        int b = PackedPos.pack(34, 40, 8);
        int c = PackedPos.pack(35, 40, 9);

        assertTrue(set.add(a));
        assertTrue(set.add(b));
        assertFalse(set.add(a)); // duplicate intake: rejected, order kept
        assertTrue(set.add(c));

        assertEquals(3, set.size());
        assertEquals(a, set.poll());
        assertEquals(b, set.poll());
        assertEquals(c, set.poll());
        assertTrue(set.isEmpty());
    }

    @Test
    void pollRemovesMembershipSoCellsCanRequeue() {
        ActiveSet set = new ActiveSet(COORDS);
        int cell = PackedPos.pack(40, 40, 10);

        assertTrue(set.add(cell));
        assertTrue(set.contains(cell));
        assertEquals(cell, set.poll());
        assertFalse(set.contains(cell));
        assertTrue(set.add(cell)); // re-add after poll is a fresh insertion
        assertTrue(set.contains(cell));
    }

    @Test
    void fifoOrderSurvivesRingGrowthAndWraparound() {
        ActiveSet set = new ActiveSet(COORDS);
        // Churn head away from 0 so growth happens on a wrapped ring.
        for (int i = 0; i < 700; i++) {
            set.add(PackedPos.pack(32 + (i & 31), 32, 8));
            set.poll();
        }
        // Now enqueue more cells than the initial capacity (1024).
        List<Integer> expected = new ArrayList<>();
        for (int i = 0; i < 3000; i++) {
            int cell = PackedPos.pack(32 + (i & 63), 32 + ((i >> 6) & 63), 8 + (i >> 12));
            if (set.add(cell)) {
                expected.add(cell);
            }
        }
        List<Integer> polled = new ArrayList<>();
        while (!set.isEmpty()) {
            polled.add(set.poll());
        }
        assertEquals(expected, polled);
    }

    @Test
    void dropChunkRemovesOnlyThatChunkPreservingSurvivorOrder() {
        ActiveSet set = new ActiveSet(COORDS);
        int dropped = COORDS.chunkIndexOf(1, 1, 1);
        int inDropped1 = PackedPos.pack(33, 33, 9);
        int inDropped2 = PackedPos.pack(50, 50, 10);
        int survivor1 = PackedPos.pack(70, 33, 9);   // chunk (2,1,1)
        int survivor2 = PackedPos.pack(33, 70, 9);   // chunk (1,2,1)
        int survivor3 = PackedPos.pack(33, 33, 17);  // chunk (1,1,2)

        set.add(inDropped1);
        set.add(survivor1);
        set.add(inDropped2);
        set.add(survivor2);
        set.add(survivor3);
        set.dropChunk(dropped);

        assertEquals(3, set.size());
        assertFalse(set.contains(inDropped1));
        assertFalse(set.contains(inDropped2));
        assertTrue(set.contains(survivor1));
        assertEquals(survivor1, set.poll());
        assertEquals(survivor2, set.poll());
        assertEquals(survivor3, set.poll());
        // The dropped cells may re-enter later (e.g. after a thaw).
        assertTrue(set.add(inDropped1));
    }

    @Test
    void dropChunkOnAnUntouchedChunkIsANoOp() {
        ActiveSet set = new ActiveSet(COORDS);
        int cell = PackedPos.pack(33, 33, 9);
        set.add(cell);

        set.dropChunk(COORDS.chunkIndexOf(2, 2, 1));

        assertEquals(1, set.size());
        assertTrue(set.contains(cell));
    }

    @Test
    void clearEmptiesEverythingAndAllowsReuse() {
        ActiveSet set = new ActiveSet(COORDS);
        int cell = PackedPos.pack(44, 44, 12);
        set.add(cell);
        set.add(PackedPos.pack(45, 44, 12));

        set.clear();

        assertTrue(set.isEmpty());
        assertEquals(0, set.size());
        assertFalse(set.contains(cell));
        assertTrue(set.add(cell));
        assertEquals(cell, set.poll());
    }

    @Test
    void identicalOperationSequencesYieldIdenticalPollOrder() {
        List<Integer> first = runScriptedSequence();
        List<Integer> second = runScriptedSequence();
        assertEquals(first, second);
    }

    /**
     * A fixed add/poll/drop script with collisions and interleaved drains;
     * returns the full observed poll sequence.
     */
    private static List<Integer> runScriptedSequence() {
        ActiveSet set = new ActiveSet(COORDS);
        List<Integer> polled = new ArrayList<>();
        for (int round = 0; round < 40; round++) {
            for (int i = 0; i < 25; i++) {
                int n = round * 25 + i;
                // Deliberately collides across rounds to exercise dedupe.
                set.add(PackedPos.pack(32 + (n % 60), 32 + ((n * 7) % 60), 8 + (n % 12)));
            }
            if (round % 5 == 4) {
                set.dropChunk(COORDS.chunkIndexOf(1 + (round / 5) % 2, 1, 1));
            }
            for (int i = 0; i < 20 && !set.isEmpty(); i++) {
                polled.add(set.poll());
            }
        }
        while (!set.isEmpty()) {
            polled.add(set.poll());
        }
        return polled;
    }
}
