package com.trojia.sim.event;

/**
 * The emit half of the event plumbing. A sink is bound at registration to one
 * emitter's pipeline position {@code (systemId, phase, regIndex)}; every emit
 * is stamped {@code (tick, phase, regIndex, seq)} from that binding, so
 * emitters cannot forge positions.
 *
 * <p>Topic ids are resolved once at registration; {@link #emit} dispatches on
 * the pre-resolved int, never on class lookups. Exceeding 65,536 events per
 * topic per tick is a hard failure in ALL builds (never a silent drop).
 */
public interface EventSink {

    /**
     * Publishes {@code event} at this sink's bound pipeline position. The
     * event becomes visible per the one-lap rule documented on {@link SimEvent}.
     *
     * @throws IllegalStateException if the per-topic per-tick cap is exceeded
     */
    void emit(SimEvent event);
}
