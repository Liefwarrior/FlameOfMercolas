package com.trojia.sim.actor;

import com.trojia.sim.progression.AttributeId;

/**
 * The skill-check core (Sprint 1, rank 3): ONE pure check function — a named draw against
 * {@code skill level + governing attribute} — that every check family shares, so a
 * d20-style outcome is always reconstructable from {@code (worldSeed, tick, stream,
 * actorId, drawIndex)} and arguable-about after the fact (the Disco Elysium skeleton on our
 * determinism substrate).
 *
 * <p><b>Model.</b> A check succeeds when {@code draw mod 1000} lands under a permille
 * threshold: {@code clamp(base + POINTS_TO_PERMILLE * (score - resistScore), floor, ceil)},
 * where a score is {@code skillLevel + attribute}. Integer-only, division-free on the state
 * path, pure — the caller supplies the draw (from its own named {@link ActorRngStream} with
 * pinned per-actor drawIndex attribution) and the scores (from
 * {@link SkillTrackRegistry#level} / {@link SkillTrackRegistry#attribute}).
 *
 * <p><b>First consumer</b> (this sprint): push-contest resolution in {@link PushMechanics} —
 * the pusher's open_hand+AGI against the shovee's grit+VIG on the {@code check.push} stream.
 * The floor/ceiling clamp keeps the contest a LIVENESS-SAFE perturbation: even a hopeless
 * pusher succeeds most attempts (the squeeze-past deadlock dissolver must keep dissolving),
 * and even a master can fumble (no certainty, per the genre contract). Failure burns no
 * cooldown — the blocked step simply retries next tick, so the density equilibrium the
 * riot/starvation bars are tuned against shifts by at most a tick or two of delay.
 */
public final class SkillChecks {

    /** Baseline success permille when the contest scores are equal. */
    public static final int PUSH_BASE_PERMILLE = 950;
    /** Success-floor permille: a contest never becomes a wall (liveness guard). */
    public static final int PUSH_FLOOR_PERMILLE = 600;
    /** Success-ceiling permille: mastery never buys certainty. */
    public static final int PUSH_CEIL_PERMILLE = 990;
    /** Permille shift per point of score difference (level + attribute points). */
    public static final int POINTS_TO_PERMILLE = 10;

    /** The permille modulus every check draws against. */
    public static final int PERMILLE = 1000;

    private SkillChecks() {
    }

    /**
     * The one pure threshold function: success permille for {@code score} vs
     * {@code resistScore} around {@code basePermille}, clamped to
     * {@code [floorPermille, ceilPermille]}.
     */
    public static int successPermille(int score, int resistScore, int basePermille,
            int floorPermille, int ceilPermille) {
        int raw = basePermille + POINTS_TO_PERMILLE * (score - resistScore);
        return Math.max(floorPermille, Math.min(ceilPermille, raw));
    }

    /** Whether a named draw passes a permille threshold (unsigned modulus — no sign bias). */
    public static boolean passes(long draw, int permille) {
        return Long.remainderUnsigned(draw, PERMILLE) < permille;
    }

    /**
     * The push-contest family threshold: pusher {@code open_hand + AGI} vs shovee
     * {@code grit + VIG}, on the push family's base/floor/ceiling. Reads TRUE actor ids —
     * a shove is a physical contest of the bodies actually colliding, not a social read
     * (presentedId governs social systems only, per the Persona contract).
     */
    public static int pushContestPermille(SkillTrackRegistry tracks, int pusherId, int shoveeId) {
        int pusherScore = tracks.level(pusherId, tracks.openHandRaw())
                + tracks.attribute(pusherId, AttributeId.AGI);
        int shoveeScore = tracks.level(shoveeId, tracks.gritRaw())
                + tracks.attribute(shoveeId, AttributeId.VIG);
        return successPermille(pusherScore, shoveeScore, PUSH_BASE_PERMILLE,
                PUSH_FLOOR_PERMILLE, PUSH_CEIL_PERMILLE);
    }
}
