package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.CullVerb;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillTrackRegistry;

import java.util.function.IntSupplier;

/**
 * Narrates the played soul's CULL outcome as a toast (S8 scalps — the {@link
 * FishFeedbackTracker} pattern applied to the vermin bounty): after {@code K} arms the cull
 * intent, the sim resolves it next tick through the shared verb ({@code PlayerControlPolicy}
 * &rarr; {@code CullVerb.resolveCull}) and stamps one of the three cull reasons — this tracker
 * reads the stamp off the played body and toasts the human sentence. On a resolved attempt
 * (taken or ruined) a second toast carries {@link CheckLineFormatter#cullLine} — the cull
 * check's visible dice, named off the carcass still beside the culler at narration time (live
 * reads, the honesty-note convention; a body that got up in the same breath just skips the
 * line). Zero sim writes; GL-free.
 *
 * <p><b>Bounded wait</b> ({@link #PENDING_TICKS}): custody/the gibbet outrank player control,
 * so a never-resolving intent expires silently — no stale toast minutes later.
 *
 * <p>The FIELDCRAFT skill-up itself needs no code here: {@code SkillUpTracker} already toasts
 * the played soul's every level in every skill.
 */
public final class CullFeedbackTracker {

    /** How many executed ticks an armed intent waits for its outcome stamp. */
    public static final int PENDING_TICKS = 10;

    public static final String TOOK = "The pelt comes away clean -- a scalp for the bounty.";
    public static final String RUINED = "The knife slips; the pelt is spoiled.";
    public static final String NO_QUARRY = "Nothing downed within knife reach.";

    private final ActorRegistry registry;
    private final ToastQueue toasts;
    private final IntSupplier playedActorId;
    /** The check-line reads; {@code UNWIRED} = outcome sentence alone. */
    private final SkillTrackRegistry tracks;

    private int pendingTicks;

    public CullFeedbackTracker(ActorRegistry registry, ToastQueue toasts,
            IntSupplier playedActorId, SkillTrackRegistry tracks) {
        this.registry = registry;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
        this.tracks = tracks;
    }

    /** Arms the outcome watch (called by {@code CullInput.applyCull} beside the intent). */
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
        ReasonCode reason = registry.get(played).lastReasonCode();
        String line = outcomeLine(reason);
        if (line != null) {
            toasts.add(line);
            if (reason == ReasonCode.TOOK_SCALP || reason == ReasonCode.SCALP_RUINED) {
                String check = checkLine(played, reason == ReasonCode.TOOK_SCALP);
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
     * The cull check's visible dice, named off a downed scalpable body still within knife
     * reach at narration time; {@code ""} (skip) when it already rose or the table is unwired.
     */
    private String checkLine(int played, boolean success) {
        if (!tracks.isWired()) {
            return "";
        }
        Actor culler = registry.get(played);
        int bodyId = nearestQuarry(culler);
        if (bodyId == Actor.NONE) {
            return ""; // the carcass got up between the cut and the telling
        }
        var stats = registry.get(bodyId).stats();
        return CheckLineFormatter.cullLine(tracks, played, stats.displayName(),
                stats.scalpResist(), success);
    }

    /**
     * A downed scalpable body within {@link CullVerb#CULL_REACH} — same-z, ascending index.
     * Presentation-only mirror of the sim's own reach (the {@code AdjacentTargets#REACH}
     * mirroring convention); reads nothing and writes nothing.
     */
    private int nearestQuarry(Actor culler) {
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            if (other.id() == culler.id() || !CullVerb.isQuarry(other)) {
                continue;
            }
            if (com.trojia.sim.world.PackedPos.z(other.cell())
                    == com.trojia.sim.world.PackedPos.z(culler.cell())
                    && com.trojia.sim.actor.ActorGeometry.chebyshev(culler.cell(), other.cell())
                            <= CullVerb.CULL_REACH) {
                return i;
            }
        }
        return Actor.NONE;
    }

    /** The toast for a cull-outcome reason stamp, or {@code null} for any other reason. */
    public static String outcomeLine(ReasonCode reason) {
        if (reason == null) {
            return null;
        }
        return switch (reason) {
            case TOOK_SCALP -> TOOK;
            case SCALP_RUINED -> RUINED;
            case NO_QUARRY_IN_REACH -> NO_QUARRY;
            default -> null;
        };
    }
}
