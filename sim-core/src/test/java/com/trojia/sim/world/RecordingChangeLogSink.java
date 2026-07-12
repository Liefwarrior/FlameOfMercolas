package com.trojia.sim.world;

import java.util.ArrayList;
import java.util.List;

/**
 * Recording {@link ChangeLogSink} fake: lanes named at construction "have
 * readers"; every append is journaled as {@code laneName@packedPos}. Lets the
 * writer tests verify change-log traffic without the real
 * {@code world.change.ChangeLogs} (built in parallel).
 */
final class RecordingChangeLogSink implements ChangeLogSink {

    final List<String> appends = new ArrayList<>();
    final List<Long> compacts = new ArrayList<>();
    /** Optional shared journal for cross-sink ordering assertions. */
    List<String> journal;
    private final List<String> readerLanes;

    RecordingChangeLogSink(String... readerLanes) {
        this.readerLanes = List.of(readerLanes);
    }

    @Override
    public boolean hasReaders(LaneId lane) {
        return readerLanes.contains(lane.name());
    }

    @Override
    public void append(LaneId lane, int packedPos) {
        if (!hasReaders(lane)) {
            throw new AssertionError("append on reader-less lane " + lane.name()
                    + " — the writer must check hasReaders first");
        }
        appends.add(lane.name() + "@" + packedPos);
    }

    @Override
    public void compact(long tick) {
        compacts.add(tick);
        if (journal != null) {
            journal.add("logs.compact@" + tick);
        }
    }
}
