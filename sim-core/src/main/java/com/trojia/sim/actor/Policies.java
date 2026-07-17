package com.trojia.sim.actor;

/**
 * The shared policy library's singleton instances (ACTORS-SPEC.md §1.3): one
 * instance shared by every actor of every type — thin {@link Actor}
 * subclasses reference these constants when composing a {@link PolicyStack}
 * (§1.4), never instantiating a policy themselves.
 */
public final class Policies {

    public static final BehaviorPolicy DEFER_WIELDER = new DeferWielderPolicy();
    public static final BehaviorPolicy FLEE = new FleePolicy();
    public static final BehaviorPolicy GOAL_PURSUE = new GoalPursuePolicy();
    public static final BehaviorPolicy RETURN_HOME = new ReturnHomePolicy();
    public static final BehaviorPolicy LOITER = new LoiterPolicy();
    public static final BehaviorPolicy SEEK_FOOD = new SeekFoodPolicy();
    public static final BehaviorPolicy HELD = new HeldPolicy();
    public static final BehaviorPolicy EXECUTED = new ExecutedPolicy();
    public static final BehaviorPolicy PLAYER_CONTROL = new PlayerControlPolicy();
    public static final BehaviorPolicy APPREHEND = new ApprehendPolicy();

    private Policies() {
    }
}
