package com.trojia.sim.actor;

/**
 * The bank's deterministic waiting queue (Phase-2 STEP B, Pass 9): the ordered list of
 * front-to-back waiting-slot floor cells outside the counter, injected through the
 * {@link ActorsSystem} constructor like the other baked civic seams — a fixed {@code int[]},
 * no mutable state, rides no save.
 *
 * <p><b>Determinism.</b> Slot assignment is a pure function of the set of waiting actors:
 * the i-th <em>lowest actor id</em> waiting gets slot {@code i} (front-to-back), everyone
 * past the last slot stacks on the back slot. No wall clock, no insertion-order/map iteration
 * — the caller passes an ascending-id array and reads {@link #slotCellForRank(int)}. Service is
 * on absolute ticks ({@link #isServed(long, long)}: {@code now - joined >= SERVICE_TICKS}),
 * never a decrementing countdown, so a save/reload resumes the exact same service moment.
 *
 * <p>Phase 2 ships this as a callable, unit-tested primitive; the policy that walks citizens
 * into the lane and fires deposit/withdraw at the front is a Phase-3 behavior trigger.
 */
public final class BankQueue {

    /** The degraded empty queue the world-less/no-bank bake injects. */
    public static final BankQueue EMPTY = new BankQueue(new int[0]);

    /** Ticks a citizen stands at the front being served before the counter is free again. */
    public static final long SERVICE_TICKS = 200L;

    private final int[] slotCells;

    public BankQueue(int[] slotCells) {
        this.slotCells = slotCells.clone();
    }

    /** Number of physical waiting slots (front-to-back). */
    public int slotCount() {
        return slotCells.length;
    }

    /** The floor cell of slot {@code index} (0 == front). */
    public int slotCell(int index) {
        return slotCells[index];
    }

    /**
     * The cell a citizen at queue position {@code rank} stands on (0 == front of line). Ranks past
     * the last physical slot clamp to the back slot (the line bunches at the tail rather than
     * spilling to an undefined cell). {@link Actor#NONE} when no slots are wired.
     */
    public int slotCellForRank(int rank) {
        if (slotCells.length == 0 || rank < 0) {
            return Actor.NONE;
        }
        return slotCells[Math.min(rank, slotCells.length - 1)];
    }

    /**
     * The slot cell for {@code actorId} given the ascending-id array of everyone currently waiting
     * ({@code waitingAscending} must be sorted ascending by the caller — the ordering discipline
     * that makes this deterministic). {@link Actor#NONE} if the actor is not in the array.
     */
    public int assignSlotCell(int[] waitingAscending, int actorId) {
        for (int rank = 0; rank < waitingAscending.length; rank++) {
            if (waitingAscending[rank] == actorId) {
                return slotCellForRank(rank);
            }
        }
        return Actor.NONE;
    }

    /** Absolute-tick service test: {@code true} once the citizen has stood at the front long enough. */
    public static boolean isServed(long joinedTick, long nowTick) {
        return nowTick - joinedTick >= SERVICE_TICKS;
    }
}
