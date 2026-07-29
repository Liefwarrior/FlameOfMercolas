package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ReasonCode;

import java.util.function.IntSupplier;

/**
 * Narrates the played soul's counter sale as a toast (S8 — the {@link CullFeedbackTracker}
 * pattern applied to the counter): after {@code B} arms the sell intent, the sim resolves it
 * next tick through {@code SellVerb.sellMaterialsInReach} and stamps SOLD_GOODS or
 * NO_BUYER_IN_REACH; this tracker reads the stamp and toasts the human sentence. No check line
 * here — a counter sale is an exchange at a published price, not a contest, so there are no
 * dice to show (the formatter's no-formula-duplication rule). Zero sim writes; GL-free.
 */
public final class SellFeedbackTracker {

    /** How many executed ticks an armed intent waits for its outcome stamp. */
    public static final int PENDING_TICKS = 10;

    public static final String SOLD = "Coins on the counter -- your goods are sold.";
    public static final String NO_BUYER = "No counter within reach is buying.";

    private final ActorRegistry registry;
    private final ToastQueue toasts;
    private final IntSupplier playedActorId;

    private int pendingTicks;

    public SellFeedbackTracker(ActorRegistry registry, ToastQueue toasts,
            IntSupplier playedActorId) {
        this.registry = registry;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
    }

    /** Arms the outcome watch (called by {@code SellInput.applySell} beside the intent). */
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
            pendingTicks = 0;
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

    /** The toast for a sell-outcome reason stamp, or {@code null} for any other reason. */
    public static String outcomeLine(ReasonCode reason) {
        if (reason == null) {
            return null;
        }
        return switch (reason) {
            case SOLD_GOODS -> SOLD;
            case NO_BUYER_IN_REACH -> NO_BUYER;
            default -> null;
        };
    }
}
