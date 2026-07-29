package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorGeometry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.CullVerb;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.world.PackedPos;

/**
 * WHY THE KNIFE REFUSED (S8 playtest fix): the three reasons a {@code K} press cannot become a
 * cull attempt, told apart on screen instead of collapsed into one sentence.
 *
 * <p>The sim resolves a refused attempt as a single stamp — {@code NO_QUARRY_IN_REACH} covers
 * both "there is no carcass" and "your hand is still latched from the last one" — which read to
 * a player as the verb being broken: press {@code K} beside a plainly visible dead mouse and the
 * screen said {@code "Nothing downed within knife reach."}, immediately after telling you that
 * you were kneeling to it. The 500-tick per-culler latch is the whole loop's pacing, so it has to
 * be legible AS pacing.
 *
 * <p>This class reads that state client-side, BEFORE the intent is armed: a press that cannot
 * work is refused here with the reason that actually applies, and no kneeling is narrated for an
 * attempt that never began. Reads only — no sim writes, no draws, nothing that could move a tick
 * hash. The sim's own stamp still lands for the case only the sim can see (the carcass rose
 * between the press and the resolving tick).
 */
public final class CullAvailability {

    /** The refusal for an actor whose latch has not run out ({@link CullVerb#CULL_COOLDOWN_TICKS}). */
    public static final String COOLDOWN_PREFIX = "Your knife hand is still busy -- ready in ";

    /** The refusal for a body that could never take a scalp (a beast, or somebody's quarry). */
    public static final String CANNOT_CULL = "Yours are not the hands for that work.";

    private CullAvailability() {
    }

    /**
     * Ticks left on this actor's cull latch at {@code tick}; {@code 0} when the knife hand is
     * free. The client's window onto {@link Actor#culledUntilTick()} — the pacing a player
     * otherwise has to guess at.
     */
    public static long cooldownTicksLeft(Actor self, long tick) {
        return Math.max(0L, self.culledUntilTick() - tick);
    }

    /**
     * The toast for a latched knife hand, sized in the clock the HUD already shows: the
     * {@link DailyRhythm#DAY}-tick day reads as 24h, so the 500-tick latch is half an hour of
     * ward time. Rounds up to 1 min so a nearly-expired latch never reads as "0 min".
     */
    public static String cooldownLine(long ticksLeft) {
        long minutes = Math.max(1L, ticksLeft * 24 * 60 / DailyRhythm.DAY);
        return COOLDOWN_PREFIX + minutes + " min.";
    }

    /**
     * A downed scalpable body within {@link CullVerb#CULL_REACH} of {@code culler} — same z,
     * ascending index (the sim's own scan order). Presentation-only mirror of
     * {@link CullVerb#quarryInReach} (which needs an {@code ActorContext} the client has no
     * business holding); reads nothing and writes nothing.
     */
    public static int quarryInReach(ActorRegistry registry, Actor culler) {
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            if (other.id() == culler.id() || !CullVerb.isQuarry(other)) {
                continue;
            }
            if (PackedPos.z(other.cell()) == PackedPos.z(culler.cell())
                    && ActorGeometry.chebyshev(culler.cell(), other.cell())
                            <= CullVerb.CULL_REACH) {
                return i;
            }
        }
        return Actor.NONE;
    }

    /**
     * The sentence to toast INSTEAD of arming a cull, or {@code null} when the press may go
     * through. Checked in the sim's own order — can this body cull at all, is its latch spent,
     * is there anything to cut — so the reason on screen is the reason the sim would have hit.
     */
    public static String refusal(ActorRegistry registry, Actor culler, long tick) {
        if (!CullVerb.canCull(culler)) {
            return CANNOT_CULL;
        }
        long left = cooldownTicksLeft(culler, tick);
        if (left > 0) {
            return cooldownLine(left);
        }
        if (quarryInReach(registry, culler) == Actor.NONE) {
            return CullFeedbackTracker.NO_QUARRY;
        }
        return null;
    }
}
