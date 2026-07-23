package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ReasonCode;

import java.util.function.IntSupplier;

/**
 * Narrates the played actor's EAT outcome as a toast (Sprint 4 item 3): after {@code E}
 * arms the eat intent, the sim resolves it on the next executed tick and stamps one of the
 * four eat reasons — this tracker (wired into the driver's after-tick seam like the other
 * trackers) reads the stamp off the played body and toasts the human sentence. Zero sim
 * writes; GL-free.
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

    private int pendingTicks;

    public EatFeedbackTracker(ActorRegistry registry, ToastQueue toasts,
            IntSupplier playedActorId) {
        this.registry = registry;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
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
        String line = outcomeLine(registry.get(played).lastReasonCode());
        if (line != null) {
            toasts.add(line);
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
