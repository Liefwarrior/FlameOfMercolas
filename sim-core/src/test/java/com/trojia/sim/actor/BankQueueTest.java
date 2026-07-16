package com.trojia.sim.actor;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Pass 9 (Feature 4) — the deterministic bank {@link BankQueue} primitive: waiting-slot assignment
 * is by ascending actor id (the i-th lowest waiting id gets slot i, front-to-back), ranks past the
 * last slot clamp to the back slot, and service is on absolute ticks — no wall clock, no map or
 * insertion-order iteration.
 */
final class BankQueueTest {

    private static final int FRONT = 500;
    private static final int MID = 501;
    private static final int BACK = 502;

    @Test
    void slotAssignmentIsByAscendingActorIdRegardlessOfArrayOrder() {
        BankQueue queue = new BankQueue(new int[] {FRONT, MID, BACK});
        // The caller passes waiting ids ascending; the lowest id is served first (front slot).
        int[] waiting = {12, 27, 34};
        assertEquals(FRONT, queue.assignSlotCell(waiting, 12), "lowest id -> front of the line");
        assertEquals(MID, queue.assignSlotCell(waiting, 27));
        assertEquals(BACK, queue.assignSlotCell(waiting, 34));
        assertEquals(Actor.NONE, queue.assignSlotCell(waiting, 99), "not waiting -> no slot");
    }

    @Test
    void ranksPastTheLastSlotClampToTheBackSlot() {
        BankQueue queue = new BankQueue(new int[] {FRONT, MID, BACK});
        assertEquals(FRONT, queue.slotCellForRank(0));
        assertEquals(BACK, queue.slotCellForRank(2));
        assertEquals(BACK, queue.slotCellForRank(3), "the 4th waiter bunches onto the back slot");
        assertEquals(BACK, queue.slotCellForRank(50), "the line tail, not an undefined cell");
        assertEquals(Actor.NONE, queue.slotCellForRank(-1));
    }

    @Test
    void serviceIsOnAbsoluteTicksNotACountdown() {
        long joined = 10_000L;
        assertFalse(BankQueue.isServed(joined, joined), "just arrived at the counter");
        assertFalse(BankQueue.isServed(joined, joined + BankQueue.SERVICE_TICKS - 1));
        assertTrue(BankQueue.isServed(joined, joined + BankQueue.SERVICE_TICKS),
                "served once the absolute-tick service window elapses");
        assertTrue(BankQueue.isServed(joined, joined + BankQueue.SERVICE_TICKS + 5_000));
    }

    @Test
    void emptyQueueYieldsNoSlots() {
        assertEquals(0, BankQueue.EMPTY.slotCount());
        assertEquals(Actor.NONE, BankQueue.EMPTY.slotCellForRank(0));
        assertEquals(Actor.NONE, BankQueue.EMPTY.assignSlotCell(new int[] {1, 2}, 1));
    }
}
