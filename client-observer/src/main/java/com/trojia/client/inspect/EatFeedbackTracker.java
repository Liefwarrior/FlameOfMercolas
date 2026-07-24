package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillTrackRegistry;

import java.util.function.IntSupplier;

/**
 * Narrates the played actor's EAT outcome as a toast (Sprint 4 item 3): after {@code E}
 * arms the eat intent, the sim resolves it on the next executed tick and stamps one of the
 * four eat reasons — this tracker (wired into the driver's after-tick seam like the other
 * trackers) reads the stamp off the played body and toasts the human sentence. Zero sim
 * writes; GL-free.
 *
 * <p><b>The haggle made visible (Sprint 5).</b> When the outcome is a COUNTER BUY and the
 * skill/standing tables are wired, a second toast carries
 * {@link CheckLineFormatter#barterQuoteLine} — the personal quote decomposed into its
 * haggle/standing/surcharge components, read live at narration time.
 *
 * <p><b>Bounded wait.</b> The intent normally resolves on the very next tick; when
 * something outranks player control (custody, the gibbet), the pending window expires
 * silently after {@link #PENDING_TICKS} — no stale toast minutes later.
 */
public final class EatFeedbackTracker {

    /** How many executed ticks an armed intent waits for its outcome stamp. */
    public static final int PENDING_TICKS = 10;

    public static final String ATE = "You eat your fill.";
    public static final String BOUGHT = "You buy a meal at the counter and eat.";
    public static final String SCAVENGED = "You pick a meal out of a garbage bin.";
    public static final String NOTHING = "Nothing to eat within reach.";

    private final ActorRegistry registry;
    private final ToastQueue toasts;
    private final IntSupplier playedActorId;
    /** The Sprint-5 barter-decomposition reads; UNWIRED = no second toast, ever. */
    private final SkillTrackRegistry tracks;
    private final FactionStandings standings;

    private int pendingTicks;

    /** The pre-Sprint-5 wiring: outcome sentences only, no barter line. */
    public EatFeedbackTracker(ActorRegistry registry, ToastQueue toasts,
            IntSupplier playedActorId) {
        this(registry, toasts, playedActorId, SkillTrackRegistry.UNWIRED,
                FactionStandings.UNWIRED);
    }

    public EatFeedbackTracker(ActorRegistry registry, ToastQueue toasts,
            IntSupplier playedActorId, SkillTrackRegistry tracks, FactionStandings standings) {
        this.registry = registry;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
        this.tracks = tracks;
        this.standings = standings;
    }

    /** Arms the outcome watch (called by {@code EatInput.applyEat} alongside the intent). */
    public void arm() {
        pendingTicks = PENDING_TICKS;
    }

    /** Call once per executed tick, after the tick ran (the driver's after-tick seam). */
    public void afterTick(long tick) {
        if (pendingTicks <= 0) {
            return;
        }
        int played = playedActorId.getAsInt();
        if (played == Actor.NONE) {
            pendingTicks = 0; // play mode ended before the outcome landed
            return;
        }
        Actor actor = registry.get(played);
        String line = outcomeLine(actor.lastReasonCode());
        if (line != null) {
            toasts.add(line);
            if (actor.lastReasonCode() == ReasonCode.BOUGHT_FOOD) {
                // Sprint 5: the counter buy decomposes its quote — the haggle read visible.
                String quote = CheckLineFormatter.barterQuoteLine(tracks, standings,
                        played, actor.identity().presentedId());
                if (!quote.isEmpty()) {
                    toasts.add(quote);
                }
            }
            pendingTicks = 0;
            return;
        }
        pendingTicks--;
    }

    /** The toast for an eat-outcome reason stamp, or {@code null} for any other reason. */
    public static String outcomeLine(ReasonCode reason) {
        if (reason == null) {
            return null;
        }
        return switch (reason) {
            case ATE_FOOD -> ATE;
            case BOUGHT_FOOD -> BOUGHT;
            case SCAVENGED_FOOD -> SCAVENGED;
            case NO_MEAL_IN_REACH -> NOTHING;
            default -> null;
        };
    }
}
