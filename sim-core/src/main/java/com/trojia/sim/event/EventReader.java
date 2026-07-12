package com.trojia.sim.event;

/**
 * The pull-only consume half of the event plumbing: a private cursor over one
 * topic for one consumer, bound at registration to the consumer's pipeline
 * position {@code (systemId, phase, regIndex)}. There are no callbacks and no
 * subscriptions-at-runtime — consumers drain their readers inside their own
 * {@code tick}.
 *
 * <p>Iteration order is the emission stamp order {@code (tick, phase,
 * regIndex, seq)} — deterministic by construction. {@link #hasNext} respects
 * the one-lap visibility window of the bound position (see {@link SimEvent});
 * events the consumer never drained are retired regardless after one lap.
 *
 * @param <E> the topic (concrete event record type)
 */
public interface EventReader<E extends SimEvent> {

    /** The topic this reader consumes. */
    Class<E> topic();

    /** Whether another event is visible at the bound position this tick. */
    boolean hasNext();

    /**
     * The next visible event, advancing the cursor.
     *
     * @throws java.util.NoSuchElementException if {@link #hasNext()} is false
     */
    E next();
}
