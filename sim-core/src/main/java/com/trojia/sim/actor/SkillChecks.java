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

    // ---- the pickpocket family (Sprint 2 theft): thief skyrunning+AGI vs mark
    // streetwise+WIT. The base sits WELL below the push family's: a lift is a real gamble
    // (~30% base failure feeds the justice pipeline — failure IS the feature: without
    // witnessed crimes there are no consequences to react to). The wide floor/ceiling
    // spread is the level-gap payoff: a trained skyrunner robs novices nearly at will,
    // a novice robbing a streetwise mark is mostly caught — and NOBODY lifts with
    // certainty (the genre contract). ----
    /** Baseline success permille when thief and mark scores are equal. */
    public static final int PICKPOCKET_BASE_PERMILLE = 700;
    /** Success floor: even a hopeless thief occasionally gets lucky. */
    public static final int PICKPOCKET_FLOOR_PERMILLE = 50;
    /** Success ceiling: mastery never buys certainty. */
    public static final int PICKPOCKET_CEIL_PERMILLE = 950;

    // ---- the search family (Sprint 3 quests): searcher skill+WIT vs an authored lock
    // resist — the quest engine's cooldown-gated pry at a declared search cell (the
    // clerk's locked drawer). The base sits low: a locked drawer is a real obstacle
    // (a streetwise ~15-20 body pries near the 550-650 permille band; a novice sweats
    // near the floor) — the honest routes around it are the quest's OTHER advance
    // triggers (carry the key: draw-free success), so the clamp is a pacing knob,
    // not a liveness guard. ----
    /** Baseline success permille when searcher score equals the lock's resist. */
    public static final int SEARCH_BASE_PERMILLE = 350;
    /** Success floor: even a hopeless pry occasionally gives. */
    public static final int SEARCH_FLOOR_PERMILLE = 50;
    /** Success ceiling: mastery never buys certainty. */
    public static final int SEARCH_CEIL_PERMILLE = 950;

    // ---- the fishing families (Sprint 6): PERCEPTION — the stable per-(spot, soul)
    // sight check that gates whether a spot is targetable at all — and SUCCESS — the
    // per-cast catch check against the spot size's resist. An untrained eye misses most
    // water (base sight well under half); a trained fisher reads the harbor almost at
    // will but never with certainty, and even a master's line comes up empty sometimes
    // (the genre contract). ----
    /** Baseline permille that an untrained soul SEES a given live spot. */
    public static final int FISH_SIGHT_BASE_PERMILLE = 300;
    /** Sight permille gained per FISHING level (perception deepens with the trade). */
    public static final int FISH_SIGHT_PER_LEVEL_PERMILLE = 13;
    /** Sight ceiling: even a master's eye misses the odd ripple. */
    public static final int FISH_SIGHT_CEIL_PERMILLE = 950;
    /** Baseline catch permille at score parity with the spot's resist. */
    public static final int FISH_CATCH_BASE_PERMILLE = 350;
    /** Catch floor: even a hopeless cast occasionally lands one. */
    public static final int FISH_CATCH_FLOOR_PERMILLE = 100;
    /** Catch ceiling: mastery never buys certainty. */
    public static final int FISH_CATCH_CEIL_PERMILLE = 900;

    // ---- the cull family (S8 scalps): culler fieldcraft + AGI against the quarry type's
    // authored scalpResist. A downed body does not fight back, so the base sits comfortably
    // above the fishing families' — taking a scalp is a matter of a steady hand, not luck —
    // but the ceiling stays under 1000 like every other family in this file: mastery never
    // buys certainty (the genre contract), and a ceiling AT 1000 would turn a standing body
    // beside a carcass into a guaranteed materials faucet with no story in it. The floor is
    // generous enough that a novice cutting a mouse still learns the trade. ----
    /** Baseline cull permille when culler score equals the quarry's resist. */
    public static final int CULL_BASE_PERMILLE = 600;
    /** Cull floor: even a clumsy hand gets the pelt off sometimes. */
    public static final int CULL_FLOOR_PERMILLE = 150;
    /** Cull ceiling: mastery never buys certainty (and never a guaranteed faucet). */
    public static final int CULL_CEIL_PERMILLE = 940;

    // ---- the linkcraft family (Simple Magic): caster LINKCRAFT + WIT against the
    // difficulty SpellCost computes from what the crafting moves and how far it moves it.
    // The base sits below the cull's and near the search family's: the public shelf is
    // "overly general and skimmed over most of the details" (L2472), so an untrained reader
    // works a crafting about as reliably as a novice pries a locked drawer. The floor is low
    // on purpose — a hopeless attempt SHOULD mostly fizzle, and the fizzle still pays its
    // use-XP, which is how a laborer becomes a crafter. The ceiling stays under 1000 like
    // every other family here: mastery never buys certainty (the genre contract), and a
    // certain crafting would make a warm body a switch rather than a skill. ----
    /** Baseline crafting permille when the caster's score equals the crafting's difficulty. */
    public static final int LINKCRAFT_BASE_PERMILLE = 500;
    /** Crafting floor: even a hopeless reader gets a link open now and then. */
    public static final int LINKCRAFT_FLOOR_PERMILLE = 50;
    /** Crafting ceiling: mastery never buys certainty. */
    public static final int LINKCRAFT_CEIL_PERMILLE = 900;

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

    /**
     * The pickpocket family threshold (Sprint 2 theft): thief {@code skyrunning + AGI}
     * (the raws list "pickpocket" among skyrunning's own covers) vs mark
     * {@code streetwise + WIT} (a street-wise mark knows the dip's tricks), on the
     * pickpocket base/floor/ceiling. Reads TRUE actor ids — the lift is a physical
     * contest of the bodies involved, not a social read (the push-contest precedent).
     */
    public static int pickpocketContestPermille(SkillTrackRegistry tracks, int thiefId,
            int victimId) {
        int thiefScore = tracks.level(thiefId, tracks.skyrunningRaw())
                + tracks.attribute(thiefId, AttributeId.AGI);
        int victimScore = tracks.level(victimId, tracks.streetwiseRaw())
                + tracks.attribute(victimId, AttributeId.WIT);
        return successPermille(thiefScore, victimScore, PICKPOCKET_BASE_PERMILLE,
                PICKPOCKET_FLOOR_PERMILLE, PICKPOCKET_CEIL_PERMILLE);
    }

    /**
     * The search family threshold (Sprint 3 quests): searcher {@code skill + WIT} (a pry
     * is a perception-and-cunning read of a lock, whatever skill the raws name) against
     * the authored lock {@code resist}, on the search base/floor/ceiling. Reads the TRUE
     * actor id — a pry is a physical act of the body at the drawer, not a social read
     * (the push/pickpocket precedent).
     */
    public static int searchPermille(SkillTrackRegistry tracks, int searcherId, int skillRaw,
            int resist) {
        int score = tracks.level(searcherId, skillRaw)
                + tracks.attribute(searcherId, AttributeId.WIT);
        return successPermille(score, resist, SEARCH_BASE_PERMILLE,
                SEARCH_FLOOR_PERMILLE, SEARCH_CEIL_PERMILLE);
    }

    /**
     * The fishing PERCEPTION threshold (Sprint 6): the permille chance {@code actorId}'s
     * eye picks a given live spot out of the water at all — {@code base + perLevel *
     * fishing}, ceiling-clamped. Pure level read (no attribute: reading water is learned
     * craft, not raw stats); reads the TRUE id. Degrades to the base for the untrained and
     * wherever tracks are unwired.
     */
    public static int fishSightPermille(SkillTrackRegistry tracks, int actorId) {
        int raw = FISH_SIGHT_BASE_PERMILLE
                + FISH_SIGHT_PER_LEVEL_PERMILLE * tracks.level(actorId, tracks.fishingRaw());
        return Math.min(FISH_SIGHT_CEIL_PERMILLE, raw);
    }

    /**
     * The fishing SUCCESS threshold (Sprint 6): fisher {@code fishing + AGI} against the
     * spot size's authored {@code resist}, on the fishing family's base/floor/ceiling.
     * Reads the TRUE id — a cast is the body's own work (the push/pickpocket precedent).
     */
    public static int fishCatchPermille(SkillTrackRegistry tracks, int fisherId, int resist) {
        int score = tracks.level(fisherId, tracks.fishingRaw())
                + tracks.attribute(fisherId, AttributeId.AGI);
        return successPermille(score, resist, FISH_CATCH_BASE_PERMILLE,
                FISH_CATCH_FLOOR_PERMILLE, FISH_CATCH_CEIL_PERMILLE);
    }

    /**
     * The CULL threshold (S8 scalps): culler {@code fieldcraft + AGI} against the quarry
     * type's authored {@code scalpResist}, on the cull family's base/floor/ceiling — through
     * this file's ONE {@link #successPermille} function, like every other family. Reads the
     * TRUE id: skinning is the body's own work, not a social read (the push/pickpocket/cast
     * precedent). Degrades to the base where the skill table is unwired.
     */
    public static int cullPermille(SkillTrackRegistry tracks, int cullerId, int resist) {
        int score = tracks.level(cullerId, tracks.fieldcraftRaw())
                + tracks.attribute(cullerId, AttributeId.AGI);
        return successPermille(score, resist, CULL_BASE_PERMILLE,
                CULL_FLOOR_PERMILLE, CULL_CEIL_PERMILLE);
    }

    /**
     * The CRAFTING threshold (Simple Magic): caster {@code linkcraft + WIT} against the
     * {@code resist} {@code SpellCost} computed for this crafting at this distance, on the
     * linkcraft family's base/floor/ceiling — through this file's ONE
     * {@link #successPermille} function, like every other family. WIT governs because the
     * libraries gate on READING, not on the arm.
     *
     * <p>Reads the TRUE id: a link is the body's own work, not a social read (the
     * push/pickpocket/cast/cull precedent). Note that the WIT it reads is the LIVE attribute,
     * so a crafting that steadies the head makes the next crafting likelier — the one loop
     * where magic feeds itself, and it is bounded by the same cost model as everything else.
     * Degrades to the base where the skill table is unwired.
     */
    public static int linkcraftPermille(SkillTrackRegistry tracks, int casterId, int resist) {
        int score = tracks.level(casterId, tracks.linkcraftRaw())
                + tracks.attribute(casterId, AttributeId.WIT);
        return successPermille(score, resist, LINKCRAFT_BASE_PERMILLE,
                LINKCRAFT_FLOOR_PERMILLE, LINKCRAFT_CEIL_PERMILLE);
    }
}
