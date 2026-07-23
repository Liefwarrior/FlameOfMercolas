package com.trojia.sim.actor;

import com.trojia.sim.random.RandomSource;

/**
 * Named RNG streams for the actor package (ACTORS-SPEC.md §2.2, §10.7, §11.6):
 * append-only registry, each with its own {@code systemSalt} derived purely
 * from its name (same hashing shape as {@link com.trojia.sim.engine.SystemId},
 * ARCHITECTURE.md §1.1 #16). Draws are addressed {@code (worldSeed, tick,
 * salt, spatialKey=actorId, drawIndex)} via {@link NamedDraws} — never
 * {@code java.util.Random}.
 */
public enum ActorRngStream {

    /** IDLE shuffle jitter (§1.3 LOITER). */
    ACTOR_WANDER("actor.wander"),
    /** FLEE escape-heading jitter (§1.3 FLEE) when no concrete danger cell is known. */
    ACTOR_FLEE_JITTER("actor.fleeJitter"),
    /** Presentation-only bark draw (§1.3, §2.2) — no game state ever reads its result. */
    ACTOR_BARK("actor.bark"),
    /** Weighted job assignment at spawn bake (§10.4). */
    JOB_ASSIGN("job.assign"),
    /** Goal-target weighted picks (§10.1). */
    JOB_TARGET_PICK("job.targetPick"),
    /** Goal renewal jitter (§10.1). */
    JOB_RENEW("job.renew"),
    /** Household size draw at bake (§11.4 step 2). */
    HOUSEHOLD_SIZE("household.size"),
    /** Employer staff-count draw at bake (§11.4 step 3). */
    HOUSEHOLD_STAFF_COUNT("household.staffCount"),
    /** Neighbor flavor-edge count draw at bake (§11.4 step 4). */
    HOUSEHOLD_NEIGHBOR_PICK("household.neighborPick"),
    /** Friend flavor-edge count draw at bake (§11.4 step 4). */
    HOUSEHOLD_FRIEND_PICK("household.friendPick"),
    /** Per-exposure arrest-chance draw when a Villain is caught near the Watch (ARREST-SPEC). */
    WATCH_ARREST_CHECK("watch.arrestCheck"),
    /** One-time custody-length draw at arrest, uniform 1-3 days (ARREST-SPEC). */
    WATCH_SENTENCE_LENGTH("watch.sentenceLength"),
    /**
     * Bake-side identity forging (NameForge, Sprint 1 "Every Soul Has a Name"): given-name /
     * surname / epithet / kennel-name draws, all at {@code BAKE_TICK}. Appended stream —
     * its salt is derived purely from its own name, so no existing stream's draws shift; the
     * tick path never reads it (identity is scenario-bake data, never sim-core state).
     */
    IDENTITY_NAMES("identity.names"),
    /**
     * The push-contest check family (Sprint 1 skill-check core, {@link SkillChecks}): one
     * draw per contested shove, resolved pusher open_hand+AGI vs shovee grit+VIG. Appended
     * stream (name-derived salt — no existing draws shift); drawn through the shared
     * per-actor per-tick counter, spatialKey = the PUSHER's actor id.
     */
    CHECK_PUSH("check.push"),
    /**
     * The Watch's warn-vs-fine lenience draw (Sprint 1 faction standing's ONE behavior
     * read, {@code ApprehendPolicy}): at first contact the guard draws whether the offender
     * gets the customary move-along warning or an immediate fine + arrest — the threshold
     * shifts with the offender's PRESENTED Watch standing. Appended stream; spatialKey =
     * the GUARD's actor id (the guard is deciding).
     */
    WATCH_LENIENCE("watch.lenience");

    private final String streamName;
    private final long salt;

    ActorRngStream(String streamName) {
        this.streamName = streamName;
        this.salt = deriveSalt(streamName);
    }

    public String streamName() {
        return streamName;
    }

    public long salt() {
        return salt;
    }

    /** Pure hash of the stream name — same shape as {@code SystemId.of}'s name hash. */
    private static long deriveSalt(String name) {
        long h = 0xACAB_CAFE_1234_5678L;
        for (int i = 0; i < name.length(); i++) {
            h = RandomSource.mix64(h ^ name.charAt(i));
        }
        return h;
    }
}
