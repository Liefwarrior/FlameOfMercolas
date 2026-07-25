package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.FishingSpots;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillTrackRegistry;

import java.util.function.IntSupplier;

/**
 * Narrates the played soul's FISH outcome as a toast (Sprint 6 fishing — the
 * {@link EatFeedbackTracker} pattern applied to the water): after {@code R} arms the cast
 * intent, the sim resolves it next tick through the shared cast attempt
 * ({@code PlayerControlPolicy} &rarr; {@code JobBehaviors.resolveCastAttempt}) and stamps
 * one of the three fish reasons — this tracker reads the stamp off the played body and
 * toasts the human sentence. On a resolved CAST (caught or got away) a second toast
 * carries {@link CheckLineFormatter#fishingLine} — the catch check's visible dice, sized
 * off the live spot still beside the fisher at narration time (live reads, the
 * honesty-note convention; a spot that sank in the same breath just skips the line).
 * Zero sim writes; GL-free.
 *
 * <p><b>Bounded wait</b> ({@link #PENDING_TICKS}): custody/the gibbet outrank player
 * control, so a never-resolving intent expires silently — no stale toast minutes later.
 *
 * <p>The FISHING skill-up itself needs no code here: {@code SkillUpTracker} already
 * toasts the played soul's every level in every skill ("Fishing increased to N").
 */
public final class FishFeedbackTracker {

    /** How many executed ticks an armed intent waits for its outcome stamp. */
    public static final int PENDING_TICKS = 10;

    /** Chebyshev reach of the played cast — mirrors {@code PlayerControlPolicy.PLAYER_CAST_REACH}
     * (the {@link AdjacentTargets#REACH} mirroring convention). */
    public static final int CAST_REACH = 2;

    public static final String CAUGHT = "A pull on the line -- you land a fish!";
    public static final String GOT_AWAY = "A ripple, a tug... the fish slips the hook.";
    public static final String NO_SPOT = "No worked water within casting reach.";

    private final ActorRegistry registry;
    private final ToastQueue toasts;
    private final IntSupplier playedActorId;
    /** The check-line reads; {@code UNWIRED}/{@code EMPTY} = outcome sentence alone. */
    private final SkillTrackRegistry tracks;
    private final FishingSpots spots;

    private int pendingTicks;

    public FishFeedbackTracker(ActorRegistry registry, ToastQueue toasts,
            IntSupplier playedActorId, SkillTrackRegistry tracks, FishingSpots spots) {
        this.registry = registry;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
        this.tracks = tracks;
        this.spots = spots;
    }

    /** Arms the outcome watch (called by {@code FishInput.applyFish} beside the intent). */
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
        ReasonCode reason = actor.lastReasonCode();
        String line = outcomeLine(reason);
        if (line != null) {
            toasts.add(line);
            if (reason == ReasonCode.CAUGHT_FISH || reason == ReasonCode.FISH_GOT_AWAY) {
                String check = checkLine(played, reason == ReasonCode.CAUGHT_FISH);
                if (!check.isEmpty()) {
                    toasts.add(check);
                }
            }
            pendingTicks = 0;
            return;
        }
        pendingTicks--;
    }

    /**
     * The catch check's visible dice, sized off the live spot still within the played
     * cast's reach at narration time; {@code ""} (skip) when it already sank or the
     * skill table is unwired.
     */
    private String checkLine(int played, boolean success) {
        if (!tracks.isWired()) {
            return "";
        }
        int slot = spots.slotNear(registry.get(played).cell(), CAST_REACH);
        if (slot == Actor.NONE) {
            return ""; // the water went quiet between the cast and the telling
        }
        return CheckLineFormatter.fishingLine(tracks, played, spots.sizeClassAt(slot),
                spots.catchResistAt(slot), success);
    }

    /** The toast for a fish-outcome reason stamp, or {@code null} for any other reason. */
    public static String outcomeLine(ReasonCode reason) {
        if (reason == null) {
            return null;
        }
        return switch (reason) {
            case CAUGHT_FISH -> CAUGHT;
            case FISH_GOT_AWAY -> GOT_AWAY;
            case NO_SPOT_IN_REACH -> NO_SPOT;
            default -> null;
        };
    }
}
