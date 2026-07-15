package com.trojia.sim.actor;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Coverage for {@link OccupancyIndex}, the primitive open-addressing cell&rarr;count map behind
 * the actor occupancy cap: point add/remove/count, multi-occupant cells, hash collisions with
 * backward-shift deletion, and growth/rehash. A fixed-seed randomized stress cross-checks the
 * whole thing against a plain {@code HashMap} reference (permitted here — the {@code HashMap} ban
 * is production-only).
 */
class OccupancyIndexTest {

    @Test
    void addCountRemoveRoundTrip() {
        OccupancyIndex index = new OccupancyIndex(8);
        int cell = 12345;
        assertEquals(0, index.count(cell), "absent cell reads 0");

        index.add(cell);
        index.add(cell);
        index.add(cell);
        assertEquals(3, index.count(cell), "three adds -> count 3");
        assertEquals(1, index.distinctCells(), "still one distinct cell");

        index.remove(cell);
        assertEquals(2, index.count(cell));
        index.remove(cell);
        index.remove(cell);
        assertEquals(0, index.count(cell), "last remove frees the cell");
        assertEquals(0, index.distinctCells(), "distinct-cell count drops to 0");
    }

    @Test
    void removeOfAbsentCellIsNoOp() {
        OccupancyIndex index = new OccupancyIndex(8);
        index.add(100);
        index.remove(999); // never added
        assertEquals(1, index.count(100), "unrelated remove leaves the present cell intact");
        assertEquals(0, index.count(999));
    }

    @Test
    void clearEmptiesEverything() {
        OccupancyIndex index = new OccupancyIndex(8);
        for (int c = 0; c < 10; c++) {
            index.add(c);
            index.add(c);
        }
        index.clear();
        assertEquals(0, index.distinctCells());
        for (int c = 0; c < 10; c++) {
            assertEquals(0, index.count(c), "cleared cell " + c + " reads 0");
        }
        // Still usable after clear.
        index.add(7);
        assertEquals(1, index.count(7));
    }

    @Test
    void collidingKeysKeepIndependentCountsAcrossDeletion() {
        // Force a small table (min capacity 16) and find three distinct cells that hash to the
        // SAME home slot, so their probe chains overlap — the case backward-shift deletion must
        // keep intact. Removing the head of the chain must not orphan the tail.
        OccupancyIndex index = new OccupancyIndex(1); // min capacity 16
        List<Integer> colliders = collidingCells(index.capacity(), 3);
        int a = colliders.get(0);
        int b = colliders.get(1);
        int c = colliders.get(2);

        index.add(a);
        index.add(b);
        index.add(b);
        index.add(c);
        assertEquals(1, index.count(a));
        assertEquals(2, index.count(b));
        assertEquals(1, index.count(c));

        index.remove(a); // deletes the chain head -> backward-shift must preserve b and c
        assertEquals(0, index.count(a));
        assertEquals(2, index.count(b), "collider b still reachable after head deletion");
        assertEquals(1, index.count(c), "collider c still reachable after head deletion");
    }

    @Test
    void growthPreservesAllCountsAndRaisesCapacity() {
        OccupancyIndex index = new OccupancyIndex(4); // capacity 16
        int startCapacity = index.capacity();
        int distinct = 40; // well past the 0.75 load factor of a 16-slot table -> forces rehash
        for (int c = 0; c < distinct; c++) {
            for (int k = 0; k <= c % 2; k++) { // some cells hold 1, some hold 2
                index.add(c * 7 + 1);
            }
        }
        assertTrue(index.capacity() > startCapacity, "table grew past its initial capacity");
        assertEquals(distinct, index.distinctCells());
        for (int c = 0; c < distinct; c++) {
            assertEquals(1 + (c % 2), index.count(c * 7 + 1),
                    "count for cell " + c + " survived the rehash");
        }
        assertEquals(0, index.count(999_999), "an untouched cell still reads 0 after growth");
    }

    @Test
    void randomizedStressMatchesReferenceMap() {
        OccupancyIndex index = new OccupancyIndex(16);
        Map<Integer, Integer> reference = new HashMap<>();
        Random rng = new Random(0xBADC0FFEEL);
        int cellSpace = 500; // small enough to guarantee heavy collisions and repeated cells

        for (int op = 0; op < 200_000; op++) {
            int cell = rng.nextInt(cellSpace);
            int refCount = reference.getOrDefault(cell, 0);
            boolean doAdd = refCount == 0 || rng.nextBoolean();
            if (doAdd) {
                index.add(cell);
                reference.put(cell, refCount + 1);
            } else {
                index.remove(cell);
                if (refCount - 1 == 0) {
                    reference.remove(cell);
                } else {
                    reference.put(cell, refCount - 1);
                }
            }
            // Spot-check the touched cell every op, and the whole space periodically.
            assertEquals(reference.getOrDefault(cell, 0), index.count(cell),
                    "mismatch on cell " + cell + " at op " + op);
            if (op % 5000 == 0) {
                for (int c = 0; c < cellSpace; c++) {
                    assertEquals(reference.getOrDefault(c, 0), index.count(c),
                            "full-scan mismatch on cell " + c + " at op " + op);
                }
            }
        }
        assertEquals(reference.size(), index.distinctCells(),
                "distinct-cell count matches the reference map size");
    }

    /** Brute-forces {@code n} distinct cells that all hash to the same home slot at {@code cap}. */
    private static List<Integer> collidingCells(int cap, int n) {
        int mask = cap - 1;
        Map<Integer, List<Integer>> buckets = new HashMap<>();
        for (int cell = 0; cell < 1_000_000; cell++) {
            int home = (cell * 0x9E3779B1) & mask;
            List<Integer> bucket = buckets.computeIfAbsent(home, k -> new ArrayList<>());
            bucket.add(cell);
            if (bucket.size() >= n) {
                return new ArrayList<>(bucket.subList(0, n));
            }
        }
        throw new IllegalStateException("could not find " + n + " colliding cells at capacity " + cap);
    }
}
