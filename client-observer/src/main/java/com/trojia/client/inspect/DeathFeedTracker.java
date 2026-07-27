package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.DeathLog;
import com.trojia.sim.actor.ReasonCode;

import java.util.function.IntSupplier;

/**
 * Consumes the Sim team's {@link DeathLog} seam once per executed tick (Sprint 6 death,
 * the brief's "feed announces by name"): every death row becomes a named feed line —
 * {@code "Tatter Deepnet has died -- starvation"}, {@code "Weasel Rin has died -- hanged"}
 * — and the PLAYED soul's own death additionally toasts. Executions ride the
 * {@link EventLog.Channel#CRIME} lane (they are the justice pipeline's last word);
 * starvation and any future cause ride {@link EventLog.Channel#GENERAL}, so the ALL feed
 * always carries the ward's dead. GL-free; wired as an after-tick callback beside
 * {@link EventLogTracker} on the same {@code SimulationDriver} seam. Read-only cursor
 * over {@link DeathLog#totalRecorded()} — the {@link SkillUpTracker} convention,
 * including its treat-existing-rows-as-history baseline and its
 * overwritten-rows-are-lost shrug.
 *
 * <p>The dead are named by their TRUE identity (never the presented face): death is a
 * body fact, and the ward buries the body.
 */
public final class DeathFeedTracker {

    /** The played soul's own death toast (the sim keeps the body; the driver loses it). */
    public static final String PLAYED_DEATH_TOAST = "You have died.";

    private final DeathLog log;
    private final ActorRegistry registry;
    private final IdentityRegistry identity;
    private final EventLog eventLog;
    private final ToastQueue toasts;
    /** Live "who is played this tick" read — {@code Actor.NONE} when nobody is. */
    private final IntSupplier playedActorId;

    private long consumedRows;

    public DeathFeedTracker(DeathLog log, ActorRegistry registry, IdentityRegistry identity,
            EventLog eventLog, ToastQueue toasts, IntSupplier playedActorId) {
        this.log = log;
        this.registry = registry;
        this.identity = identity;
        this.eventLog = eventLog;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
        // Baseline: whatever the log already holds is history, not this session's news.
        this.consumedRows = log.totalRecorded();
    }

    /**
     * Narrates every death recorded since the last call. Call exactly once per executed
     * tick (the {@code SimulationDriver.setAfterTick} seam).
     */
    public void afterTick(long tick) {
        long total = log.totalRecorded();
        if (total == consumedRows) {
            return;
        }
        int size = log.size();
        int start = (int) Math.max(0, consumedRows - (total - size));
        for (int i = start; i < size; i++) {
            narrate(log.tickAt(i), log.actorIdAt(i), log.causeAt(i));
        }
        consumedRows = total;
    }

    private void narrate(long tick, int actorId, ReasonCode cause) {
        String name = PersonNames.fullNameOf(actorId, registry, identity);
        eventLog.add(tick, channelOf(cause), name + " has died -- " + causeWord(cause));
        if (actorId == playedActorId.getAsInt()) {
            toasts.add(PLAYED_DEATH_TOAST);
        }
    }

    /** Executions land on the CRIME lane (justice's last word); everything else GENERAL. */
    static EventLog.Channel channelOf(ReasonCode cause) {
        return cause == ReasonCode.EXECUTED_SECOND_OFFENSE
                ? EventLog.Channel.CRIME : EventLog.Channel.GENERAL;
    }

    /** The human cause word for a death row (future causes degrade to the enum name). */
    static String causeWord(ReasonCode cause) {
        return switch (cause) {
            case EXECUTED_SECOND_OFFENSE -> "hanged";
            case STARVED_TO_DEATH -> "starvation";
            default -> cause.name().toLowerCase(java.util.Locale.ROOT);
        };
    }
}
