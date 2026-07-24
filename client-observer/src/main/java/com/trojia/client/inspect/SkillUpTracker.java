package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.SkillLevelLog;
import com.trojia.sim.actor.SkillTrackRegistry;

import java.util.function.IntSupplier;

/**
 * Consumes the Sim team's {@link SkillLevelLog} seam once per executed tick (Sprint 1
 * item 3, grown Sprint 5 into the torrent's flood control): the PLAYED actor's level-ups
 * become bottom-center toasts ("Skyrunning increased to 32") — always verbatim, every
 * level; MILESTONE levels (multiples of {@link #MILESTONE_STEP}) land in the feed as
 * immediate named lines ("Withy reached Kit-Keeping 75"); every other population level-up
 * banks into the {@link GrowthDigest}, which emits one digest line per day-phase turn.
 * All feed lines ride the {@link EventLog.Channel#GROWTH} channel so the feed filter can
 * pull the lane in or out. GL-free; wired as an after-tick callback beside
 * {@link EventLogTracker} on the same {@code SimulationDriver} seam, so no tick's
 * level-ups are missed on a FAST frame nor double-seen on a re-rendered one.
 *
 * <p><b>Read-only cursor.</b> The tracker keeps one monotonic cursor over
 * {@link SkillLevelLog#totalRecorded()} and reads only the rows recorded since its last
 * call — it never mutates sim state. Rows already in the ring at construction are treated
 * as history, not news (the {@code EventLogTracker} spawn-baseline convention). If a burst
 * ever outruns the ring capacity between reads, the overwritten rows are simply lost to
 * narration (the ring keeps the newest — presentation-only, so nothing else can care).
 *
 * <p><b>Multi-level carry.</b> One award can cross several thresholds in a single tick
 * ({@code SkillTrack#awardXp} loops); each emits its own log row — the played actor
 * toasts each, the population's cross into the digest (milestones excepted, which each
 * get their named line).
 */
public final class SkillUpTracker {

    /** Levels at multiples of this always pass the digest as immediate named lines. */
    public static final int MILESTONE_STEP = 25;

    private final SkillTrackRegistry tracks;
    private final ActorRegistry registry;
    private final IdentityRegistry identity;
    private final EventLog eventLog;
    private final ToastQueue toasts;
    /** Live "who is played this tick" read — {@code Actor.NONE} when nobody is. */
    private final IntSupplier playedActorId;
    /** The per-phase aggregation of ordinary population growth (Sprint 5 flood control). */
    private final GrowthDigest digest;

    private long consumedRows;

    public SkillUpTracker(SkillTrackRegistry tracks, ActorRegistry registry,
            IdentityRegistry identity, EventLog eventLog, ToastQueue toasts,
            IntSupplier playedActorId) {
        this.tracks = tracks;
        this.registry = registry;
        this.identity = identity;
        this.eventLog = eventLog;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
        this.digest = new GrowthDigest(tracks);
        // Baseline: whatever the log already holds is history, not this session's news.
        this.consumedRows = tracks.levelLog().totalRecorded();
    }

    /**
     * Narrates every level-up recorded since the last call, and turns the growth digest's
     * bucket over on a day-phase boundary. Call exactly once per executed tick (the
     * {@code SimulationDriver.setAfterTick} seam); cheap when nothing levelled.
     */
    public void afterTick(long tick) {
        // Phase turn first, so a boundary tick's own rows land in the NEW bucket.
        String digestLine = digest.maybeFlush(tick);
        if (digestLine != null) {
            eventLog.add(tick, EventLog.Channel.GROWTH, digestLine);
        }
        SkillLevelLog log = tracks.levelLog();
        long total = log.totalRecorded();
        if (total == consumedRows) {
            return;
        }
        int size = log.size();
        // Oldest-first ring index of the first unconsumed row; rows the ring already
        // overwrote (absolute row < total - size) are unrecoverable and skipped.
        int start = (int) Math.max(0, consumedRows - (total - size));
        for (int i = start; i < size; i++) {
            narrate(log.tickAt(i), log.actorIdAt(i), log.skillRawAt(i), log.newLevelAt(i));
        }
        consumedRows = total;
    }

    private void narrate(long tick, int actorId, int skillRaw, int newLevel) {
        String skill = tracks.skills().get(skillRaw).displayName();
        if (actorId == playedActorId.getAsInt()) {
            // The played soul's growth is always verbatim — the Morrowind moment.
            toasts.add(skill + " increased to " + newLevel);
        } else if (newLevel % MILESTONE_STEP == 0) {
            // A milestone is a headline: it passes the digest as an immediate named line
            // (and still counts in the phase census below — the census is a census).
            eventLog.add(tick, EventLog.Channel.GROWTH,
                    PersonNames.fullNameOf(actorId, registry, identity)
                            + " reached " + skill + " " + newLevel);
            digest.observe(actorId, skillRaw);
        } else {
            digest.observe(actorId, skillRaw);
        }
    }
}
