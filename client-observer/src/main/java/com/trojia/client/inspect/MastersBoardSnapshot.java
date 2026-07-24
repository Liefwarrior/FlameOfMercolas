package com.trojia.client.inspect;

import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.SkillTrackRegistry;

/**
 * The Masters Board's once-per-day baseline (Sprint 5 item 3, the "top climbers since
 * dawn" read): a per-actor sum-of-levels snapshot taken at construction and re-taken at
 * every day boundary, so the board can show who grew the most since dawn. Presentation-only
 * state — nothing reads it back into the sim, no determinism surface hashes it; wired into
 * the driver's after-tick seam beside the feed trackers.
 *
 * <p>Construction snapshots immediately (the bake's authored seed levels are the day-0
 * baseline — a seeded master is not a "climber" for standing still).
 */
public final class MastersBoardSnapshot {

    private final SkillTrackRegistry tracks;

    private long snappedDay;
    private int[] dawnSums;

    public MastersBoardSnapshot(SkillTrackRegistry tracks, int actorCount) {
        this.tracks = tracks;
        this.snappedDay = 0;
        this.dawnSums = snap(actorCount);
    }

    /** Call once per executed tick: re-baselines at every day boundary (dawn). */
    public void afterTick(long tick) {
        long day = Math.floorDiv(tick, DailyRhythm.DAY);
        if (day != snappedDay) {
            snappedDay = day;
            dawnSums = snap(dawnSums.length);
        }
    }

    /** An actor's total levels gained since the last dawn baseline (&ge; 0 in practice). */
    public int climbSinceDawn(int actorId) {
        int baseline = actorId < dawnSums.length ? dawnSums[actorId] : 0;
        return totalLevels(actorId) - baseline;
    }

    /** The sum of an actor's levels across every skill — the board's growth scalar. */
    public int totalLevels(int actorId) {
        if (!tracks.isWired()) {
            return 0;
        }
        int sum = 0;
        for (int raw = 0; raw < tracks.skills().size(); raw++) {
            sum += tracks.level(actorId, raw);
        }
        return sum;
    }

    private int[] snap(int actorCount) {
        int[] sums = new int[actorCount];
        for (int i = 0; i < actorCount; i++) {
            sums[i] = totalLevels(i);
        }
        return sums;
    }
}
