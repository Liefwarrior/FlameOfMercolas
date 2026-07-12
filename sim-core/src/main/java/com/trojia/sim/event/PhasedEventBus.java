package com.trojia.sim.event;

import com.trojia.sim.engine.SystemId;
import com.trojia.sim.engine.TickPhase;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.util.Arrays;
import java.util.List;
import java.util.NoSuchElementException;

/**
 * The internal event bus: per-topic append buffers stamped {@code (tick,
 * phase, regIndex, seq)}, one-lap retirement, 65,536/topic/tick hard cap
 * enforced identically in all builds. Package-private by design — systems see
 * only {@link EventSink}/{@link EventReader}, the engine sees {@link Events}.
 *
 * <p><b>Visibility</b> (ARCHITECTURE.md §4): an event emitted at pipeline
 * position {@code (P, I)} of tick T is visible to consumers positioned
 * strictly after {@code (P, I)} in T and at-or-before {@code (P, I)} in T+1,
 * then retired. Because {@link #advanceTo} is monotonic within a tick, each
 * topic buffer is naturally in stamp order — {@code seq} is the append order
 * and never stored explicitly.
 *
 * <p><b>Determinism:</b> primitive parallel arrays only, no hash containers;
 * reader iteration is buffer (= stamp) order; topic resolution is a linear
 * scan of the canonical topic table (18 entries, reference equality). All
 * state is instance-owned — any number of buses coexist in one JVM.
 */
final class PhasedEventBus implements Events {

    /** regIndex must fit the low 16 bits of a position key. */
    private static final int REG_INDEX_LIMIT = 1 << 16;

    /** Canonical topic order; index == topic id (see {@link Events#topics()}). */
    private final List<Class<? extends SimEvent>> topics = EventCodec.topics();
    /** One buffer per topic, indexed by topic id. */
    private final TopicBuffer[] buffers;

    private boolean sealed;
    /** The tick of the bus's current stamp position (0 before the first tick). */
    private long stampTick;
    /** The current position key {@code (phaseOrdinal << 16) | regIndex}; -1 before any advance. */
    private int stampPos = -1;

    PhasedEventBus() {
        this.buffers = new TopicBuffer[topics.size()];
        for (int i = 0; i < buffers.length; i++) {
            buffers[i] = new TopicBuffer();
        }
    }

    /** Packs a pipeline position into a single comparable int. */
    private static int posKey(TickPhase phase, int regIndex) {
        if (regIndex < 0 || regIndex >= REG_INDEX_LIMIT) {
            throw new IllegalArgumentException("regIndex out of range: " + regIndex);
        }
        return (phase.ordinal() << 16) | regIndex;
    }

    @Override
    public EventSink sink(SystemId system, TickPhase phase, int regIndex) {
        requireUnsealed();
        return new BoundSink(system, posKey(phase, regIndex));
    }

    @Override
    public <E extends SimEvent> EventReader<E> reader(SystemId system, TickPhase phase,
            int regIndex, Class<E> topic) {
        requireUnsealed();
        int topicId = EventCodec.topicIndex(topic);
        if (topicId < 0) {
            throw new IllegalArgumentException("not a registered event topic: "
                    + topic.getName() + " (reader of system " + system.name() + ")");
        }
        return new Cursor<>(topic, buffers[topicId], posKey(phase, regIndex));
    }

    @Override
    public void seal() {
        requireUnsealed();
        sealed = true;
    }

    @Override
    public void advanceTo(long tick, TickPhase phase, int regIndex) {
        requireSealed();
        int pos = posKey(phase, regIndex);
        if (tick < stampTick || (tick == stampTick && pos < stampPos)) {
            throw new IllegalStateException("bus position must advance monotonically: at (tick "
                    + stampTick + ", pos " + stampPos + "), asked for (tick " + tick
                    + ", pos " + pos + ")");
        }
        stampTick = tick;
        stampPos = pos;
    }

    @Override
    public void retireLap(long tick) {
        requireSealed();
        for (TopicBuffer buffer : buffers) {
            buffer.retireThrough(tick - 1);
        }
    }

    @Override
    public void serializeCarryOver(DataOutput out) throws IOException {
        out.writeLong(stampTick);
        for (TopicBuffer buffer : buffers) {
            out.writeInt(buffer.size);
            for (int i = 0; i < buffer.size; i++) {
                out.writeLong(buffer.ticks[i]);
                out.writeInt(buffer.posKeys[i]);
                EventCodec.write(out, (SimEvent) buffer.events[i]);
            }
        }
    }

    @Override
    public void loadCarryOver(DataInput in) throws IOException {
        stampTick = in.readLong();
        stampPos = Integer.MAX_VALUE;
        for (int topicId = 0; topicId < buffers.length; topicId++) {
            TopicBuffer buffer = buffers[topicId];
            buffer.clear();
            int count = in.readInt();
            if (count < 0) {
                throw new IOException("negative carry-over count for topic " + topicId);
            }
            for (int i = 0; i < count; i++) {
                long tick = in.readLong();
                int posKey = in.readInt();
                buffer.append(tick, posKey, EventCodec.read(in, topicId));
            }
        }
    }

    /** Emit path shared by every bound sink. */
    private void emit(int posKey, SimEvent event) {
        requireSealed();
        int topicId = EventCodec.topicIndex(event.getClass());
        if (topicId < 0) {
            throw new IllegalArgumentException("not a registered event topic: "
                    + event.getClass().getName());
        }
        TopicBuffer buffer = buffers[topicId];
        if (buffer.capTick != stampTick) {
            buffer.capTick = stampTick;
            buffer.emittedThisTick = 0;
        }
        if (buffer.emittedThisTick == MAX_EVENTS_PER_TOPIC_PER_TICK) {
            throw new IllegalStateException("event cap exceeded: more than "
                    + MAX_EVENTS_PER_TOPIC_PER_TICK + " events on topic "
                    + topics.get(topicId).getSimpleName() + " in tick " + stampTick);
        }
        buffer.emittedThisTick++;
        buffer.append(stampTick, posKey, event);
    }

    private void requireUnsealed() {
        if (sealed) {
            throw new IllegalStateException("event bus already sealed; registration is boot-time only");
        }
    }

    private void requireSealed() {
        if (!sealed) {
            throw new IllegalStateException("event bus not sealed yet; the engine seals before tick 1");
        }
    }

    /**
     * One topic's stamp-ordered buffer: parallel primitive arrays plus the
     * event references, compacted at retirement. {@code base} is the absolute
     * stamp-sequence index of slot 0, so reader cursors survive compaction.
     */
    private static final class TopicBuffer {

        private Object[] events = new Object[16];
        private long[] ticks = new long[16];
        private int[] posKeys = new int[16];
        private int size;
        /** Absolute index of slot 0 (grows as entries retire). */
        private long base;
        /** Tick the emission cap counter belongs to. */
        private long capTick = -1;
        private int emittedThisTick;

        void append(long tick, int posKey, SimEvent event) {
            if (size == events.length) {
                int grown = events.length * 2;
                events = Arrays.copyOf(events, grown);
                ticks = Arrays.copyOf(ticks, grown);
                posKeys = Arrays.copyOf(posKeys, grown);
            }
            events[size] = event;
            ticks[size] = tick;
            posKeys[size] = posKey;
            size++;
        }

        /** Drops every entry stamped at or before {@code uptoTick} (entries are tick-ordered). */
        void retireThrough(long uptoTick) {
            int drop = 0;
            while (drop < size && ticks[drop] <= uptoTick) {
                drop++;
            }
            if (drop == 0) {
                return;
            }
            int remaining = size - drop;
            System.arraycopy(events, drop, events, 0, remaining);
            System.arraycopy(ticks, drop, ticks, 0, remaining);
            System.arraycopy(posKeys, drop, posKeys, 0, remaining);
            Arrays.fill(events, remaining, size, null);
            size = remaining;
            base += drop;
        }

        /** Empties the buffer completely (load path). */
        void clear() {
            Arrays.fill(events, 0, size, null);
            size = 0;
            base = 0;
            capTick = -1;
            emittedThisTick = 0;
        }
    }

    /** A sink bound to one emitter's pipeline position; stamps come from the binding. */
    private final class BoundSink implements EventSink {

        private final SystemId system;
        private final int posKey;

        BoundSink(SystemId system, int posKey) {
            this.system = system;
            this.posKey = posKey;
        }

        @Override
        public void emit(SimEvent event) {
            if (event == null) {
                throw new IllegalArgumentException("null event from system " + system.name());
            }
            PhasedEventBus.this.emit(posKey, event);
        }
    }

    /**
     * A consumer's private cursor over one topic. The cursor is an absolute
     * stamp-sequence index; visibility is evaluated against the bus's current
     * tick and the consumer's bound position on every {@link #hasNext} call,
     * so an entry that is "not yet visible" this tick is found again next tick
     * without being skipped.
     */
    private final class Cursor<E extends SimEvent> implements EventReader<E> {

        private final Class<E> topic;
        private final TopicBuffer buffer;
        private final int posKey;
        /** Absolute index of the next candidate entry. */
        private long next;

        Cursor(Class<E> topic, TopicBuffer buffer, int posKey) {
            this.topic = topic;
            this.buffer = buffer;
            this.posKey = posKey;
        }

        @Override
        public Class<E> topic() {
            return topic;
        }

        @Override
        public boolean hasNext() {
            return seek() >= 0;
        }

        @Override
        public E next() {
            int local = seek();
            if (local < 0) {
                throw new NoSuchElementException("no visible " + topic.getSimpleName()
                        + " at position " + posKey + " in tick " + stampTick);
            }
            next++;
            return topic.cast(buffer.events[local]);
        }

        /**
         * Skips entries whose window this consumer has permanently missed and
         * returns the local index of the next visible entry, or -1. Within the
         * current tick, entries are position-ordered, so the first
         * not-yet-visible entry ends the scan.
         */
        private int seek() {
            if (next < buffer.base) {
                next = buffer.base; // entries before base were retired under us
            }
            while (true) {
                int local = (int) (next - buffer.base);
                if (local >= buffer.size) {
                    return -1;
                }
                long entryTick = buffer.ticks[local];
                int entryPos = buffer.posKeys[local];
                if (entryTick < stampTick - 1) {
                    next++; // stale (awaiting retirement)
                    continue;
                }
                if (entryTick == stampTick - 1) {
                    if (entryPos >= posKey) {
                        return local; // carry-over lap: at-or-before the emitter
                    }
                    next++; // window was last tick; missed
                    continue;
                }
                // entryTick == stampTick: visible only strictly after the emitter.
                return entryPos < posKey ? local : -1;
            }
        }
    }
}
