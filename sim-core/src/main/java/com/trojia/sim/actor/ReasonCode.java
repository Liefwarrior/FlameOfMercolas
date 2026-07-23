package com.trojia.sim.actor;

/**
 * The legibility reason code carried by a policy change (ACTORS-SPEC.md
 * §1.2/§11.6): "if the observer can't reconstruct WHY, the emergence is
 * wasted." Append-only.
 */
public enum ReasonCode {
    NEED_HUNGER_LOW,
    NEED_REST_LOW,
    RHYTHM_NIGHT_HOME,
    STIM_FIRE_SEEN,
    STIM_CRIME_SEEN,
    STIM_ALARM_HEARD,
    TARGET_LOST,
    TIMER_EXPIRED,
    DEFERENCE,
    JOB_GOAL,
    IDLE_DEFAULT,
    SAFETY_CRITICAL,
    ARRESTED,
    HELD_IN_CUSTODY,
    RELEASED_FROM_CUSTODY,
    MAIMED_FIRST_OFFENSE,
    EXECUTED_SECOND_OFFENSE,
    /** Play mode: this tick's movement/idle came from direct human input (PLAY-MODE-SPEC.md §5.2). */
    PLAYER_CONTROLLED,
    /** Ate a FOOD from a home larder or a free commons cell (economy-loop pass). */
    ATE_FOOD,
    /** Bought a FOOD at a shop counter (ID-authorized Royal transfer) and ate it (economy-loop pass). */
    BOUGHT_FOOD,
    /** A guard is actively intercepting/holding a locked offender (law &amp; order pass, APPREHEND). */
    APPREHENDING,
    /** An offender was told to move along (first-contact warning; leave the zone and it clears). */
    WARNED_MOVE_ALONG,
    /** Ate a FOOD scrap off a garbage-bin cell — the broke's last resort (law &amp; order pass). */
    SCAVENGED_FOOD,
    /** A hungry beast is chasing a locked live mouse (beast food channel, BEAST_HUNT). */
    HUNTING,
    /** A predator caught its mouse at adjacency and restored HUNGER (no FOOD item involved). */
    ATE_PREY,
    /** This mouse was just caught: DOWNED with a revive countdown on {@code downedTimer}. */
    PREY_CAUGHT,
    /** A mouse nibbled crumbs/spilled grain around its den at a wander-dwell boundary. */
    NIBBLED_DEN,
    /** The Watch sensed a shove riot and sent this shover home under a 1-day house arrest. */
    HOUSE_ARRESTED,
    /** Serving house arrest: walking home / held at home sleeping until the deadline. */
    UNDER_HOUSE_ARREST,
    /** The house-arrest deadline passed — released back to ordinary life. */
    RELEASED_FROM_HOUSE_ARREST,
    /** Lifted a mark's pocket coin clean (Sprint 2 theft: a won pickpocket contest). */
    PICKPOCKETED,
    /** A pickpocket attempt failed — the mark caught the hand (a witnessed crime row). */
    CAUGHT_STEALING,
    /** A guard corrected a witnessed theft: fine seized + custody (Sprint 2 justice). */
    ARRESTED_FOR_THEFT
}
