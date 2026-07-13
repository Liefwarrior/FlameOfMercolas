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
    SAFETY_CRITICAL
}
