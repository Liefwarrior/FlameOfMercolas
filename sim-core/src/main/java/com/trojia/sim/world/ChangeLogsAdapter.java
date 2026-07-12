package com.trojia.sim.world;

import com.trojia.sim.world.change.ChangeLogs;

/**
 * Production {@link ChangeLogSink}: straight delegation to the world's
 * {@link ChangeLogs} instance (the one returned by {@link World#changeLogs()}).
 */
final class ChangeLogsAdapter implements ChangeLogSink {

    private final ChangeLogs logs;

    ChangeLogsAdapter(ChangeLogs logs) {
        this.logs = logs;
    }

    @Override
    public boolean hasReaders(LaneId lane) {
        return logs.hasReaders(lane);
    }

    @Override
    public void append(LaneId lane, int packedPos) {
        logs.append(lane, packedPos);
    }

    @Override
    public void compact(long tick) {
        logs.compact(tick);
    }
}
