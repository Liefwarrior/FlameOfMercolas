package com.trojia.client.inspect;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;

/**
 * A rolling, capped feed of legibility-relevant transitions across the whole population
 * (ACTORS-SPEC.md §1.2/§11.6: "if the observer can't reconstruct WHY, the emergence is
 * wasted"). Each entry is tagged with the tick it happened on. GL-free — the renderer
 * reads {@link #recentNewestFirst(int)} and draws it; {@link EventLogTracker} feeds it.
 *
 * <p><b>History is the one thing that IS retained.</b> Unlike the panel (recomputed live
 * every frame), this buffer intentionally remembers past transitions — that is its whole
 * purpose. It keeps only the last {@link #capacity()} entries; older ones fall off the
 * front so memory is bounded regardless of run length.
 */
public final class EventLog {

    /** One logged transition. */
    public record Entry(long tick, String text) {
    }

    private final int capacity;
    private final Deque<Entry> entries = new ArrayDeque<>();

    /** @param capacity the maximum number of retained entries (older ones are evicted) */
    public EventLog(int capacity) {
        if (capacity < 1) {
            throw new IllegalArgumentException("capacity must be >= 1: " + capacity);
        }
        this.capacity = capacity;
    }

    public int capacity() {
        return capacity;
    }

    public int size() {
        return entries.size();
    }

    /** Appends a transition tagged with {@code tick}, evicting the oldest if at capacity. */
    public void add(long tick, String text) {
        entries.addLast(new Entry(tick, text));
        while (entries.size() > capacity) {
            entries.removeFirst();
        }
    }

    /**
     * Up to {@code limit} most-recent entries, newest first — the render order for a
     * bottom-anchored feed. Never more than are held.
     */
    public List<Entry> recentNewestFirst(int limit) {
        List<Entry> out = new ArrayList<>(Math.min(limit, entries.size()));
        var it = entries.descendingIterator();
        while (it.hasNext() && out.size() < limit) {
            out.add(it.next());
        }
        return out;
    }
}
