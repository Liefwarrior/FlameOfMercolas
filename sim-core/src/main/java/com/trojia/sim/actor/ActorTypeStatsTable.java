package com.trojia.sim.actor;

import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

/**
 * The bound, immutable table of per-type raws stats (ACTORS-SPEC.md §6),
 * sorted by {@link ActorTypeId} — mirrors {@code MaterialRegistry}'s shape.
 * Built only by {@link ActorRawsLoader}.
 */
public final class ActorTypeStatsTable {

    private final ActorTypeStats[] sorted;

    private ActorTypeStatsTable(ActorTypeStats[] sorted) {
        this.sorted = sorted;
    }

    static ActorTypeStatsTable of(List<ActorTypeStats> unsorted) {
        ActorTypeStats[] array = unsorted.toArray(new ActorTypeStats[0]);
        Arrays.sort(array, Comparator.comparing(s -> s.typeId().key()));
        return new ActorTypeStatsTable(array);
    }

    public int size() {
        return sorted.length;
    }

    public ActorTypeStats get(ActorTypeId typeId) {
        int lo = 0;
        int hi = sorted.length - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int cmp = sorted[mid].typeId().compareTo(typeId);
            if (cmp == 0) {
                return sorted[mid];
            }
            if (cmp < 0) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        throw new IllegalArgumentException("no raws bound for actor type: " + typeId);
    }

    public List<ActorTypeStats> all() {
        return List.of(sorted);
    }

    /** The sorted-table index of {@code typeId} — a stable, save-format-friendly int. */
    public int ordinalOf(ActorTypeId typeId) {
        for (int i = 0; i < sorted.length; i++) {
            if (sorted[i].typeId().equals(typeId)) {
                return i;
            }
        }
        throw new IllegalArgumentException("no raws bound for actor type: " + typeId);
    }

    /** The type id at sorted index {@code ordinal} (inverse of {@link #ordinalOf}). */
    public ActorTypeId idAt(int ordinal) {
        return sorted[ordinal].typeId();
    }
}
