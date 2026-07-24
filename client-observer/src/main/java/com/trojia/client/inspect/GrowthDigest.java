package com.trojia.client.inspect;

import com.trojia.client.hud.DayPhase;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.SkillTrackRegistry;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * The growth-feed flood control (Sprint 5 "the torrent"): once every job trains its trade,
 * 692 souls level daily — narrated one-line-per-level-up that torrent evicts every
 * crime/quest/eat line from the 16-line feed window within a day. This aggregator banks
 * the population's ordinary level-ups per {@link DayPhase} bucket and emits ONE digest
 * line at each phase turn ({@code "Growth, Day 2 Dusk: 34 souls trained Fieldcraft, 12
 * Streetwise"}), so the feed keeps breathing while the growth stays visible.
 *
 * <p>{@link SkillUpTracker} owns the routing policy: the PLAYED actor's level-ups keep
 * their verbatim toasts, milestone levels pass through as immediate named lines, and only
 * the ordinary population rows land here. GL-free, presentation-only — content derives
 * purely from the deterministic {@code SkillLevelLog} rows plus the tick, so twin runs
 * digest identically; nothing reads back into the sim.
 *
 * <p><b>Deterministic line shape.</b> Skills order by distinct-soul count descending,
 * ties by raw id ascending; at most {@link #MAX_NAMED_SKILLS} named, the rest folded into
 * {@code "+N more trades"}. A bucket with no level-ups turns over silently.
 */
public final class GrowthDigest {

    /** Digest lines name at most this many skills; the tail folds into "+N more trades". */
    public static final int MAX_NAMED_SKILLS = 6;

    private final SkillTrackRegistry tracks;

    /** The open bucket's day index ({@code tick / DAY}), or -1 before the first tick. */
    private long bucketDay = -1;
    private DayPhase bucketPhase;
    /** skillRaw -> the distinct souls that levelled it this bucket. */
    private final Map<Integer, Set<Integer>> soulsBySkill = new HashMap<>();

    public GrowthDigest(SkillTrackRegistry tracks) {
        this.tracks = tracks;
    }

    /**
     * Turns the bucket over when {@code tick} has entered a new day-phase: returns the
     * closed bucket's digest line (or {@code null} when the bucket was empty or this is
     * the first tick seen). Call once per executed tick BEFORE {@link #observe}-ing that
     * tick's rows, so a boundary tick's level-ups land in the new bucket.
     */
    public String maybeFlush(long tick) {
        long day = Math.floorDiv(tick, DailyRhythm.DAY);
        DayPhase phase = DayPhase.of(tick);
        if (day == bucketDay && phase == bucketPhase) {
            return null;
        }
        String line = bucketDay < 0 ? null : lineFor(bucketDay, bucketPhase);
        bucketDay = day;
        bucketPhase = phase;
        soulsBySkill.clear();
        return line;
    }

    /** Banks one ordinary population level-up into the open bucket. */
    public void observe(int actorId, int skillRaw) {
        soulsBySkill.computeIfAbsent(skillRaw, k -> new HashSet<>()).add(actorId);
    }

    /** The closed bucket's one digest line, or {@code null} when nothing levelled. */
    private String lineFor(long day, DayPhase phase) {
        if (soulsBySkill.isEmpty()) {
            return null;
        }
        // Deterministic ordering: distinct-soul count desc, then skill raw asc.
        List<int[]> rows = new ArrayList<>(); // {skillRaw, soulCount}
        for (Map.Entry<Integer, Set<Integer>> e : soulsBySkill.entrySet()) {
            rows.add(new int[] {e.getKey(), e.getValue().size()});
        }
        rows.sort((a, b) -> a[1] != b[1]
                ? Integer.compare(b[1], a[1]) : Integer.compare(a[0], b[0]));

        StringBuilder out = new StringBuilder("Growth, Day ").append(day + 1).append(' ')
                .append(phase.label()).append(": ");
        int named = Math.min(rows.size(), MAX_NAMED_SKILLS);
        for (int i = 0; i < named; i++) {
            int[] row = rows.get(i);
            if (i == 0) {
                out.append(row[1]).append(row[1] == 1 ? " soul" : " souls")
                        .append(" trained ");
            } else {
                out.append(", ").append(row[1]).append(' ');
            }
            out.append(tracks.skills().get(row[0]).displayName());
        }
        if (rows.size() > named) {
            out.append(", +").append(rows.size() - named).append(" more trades");
        }
        return out.toString();
    }
}
