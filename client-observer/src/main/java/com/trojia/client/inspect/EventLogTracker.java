package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.GoalState;
import com.trojia.sim.actor.Home;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.ReasonCode;

import java.util.Arrays;
import java.util.Objects;

/**
 * Diffs the population once per simulated <em>tick</em> and records legibility transitions
 * into an {@link EventLog} (M-inspector Behavior 3). Three transition kinds, per actor:
 * a {@code lastReasonCode()} change (the WHY flipping), a {@code goalState()} change, and
 * an actor <em>arriving home</em> (its {@code cell()} first becoming equal to its home
 * cell). GL-free.
 *
 * <p><b>Humanized (Sprint 2, the rolled-over Sprint-1 minor).</b> Every line names its
 * PERSON off the bake-side {@link IdentityRegistry} via {@link PersonNames} — "Old Cobb
 * reason ...", never "#371 militia_watch ..." — and the justice-pipeline stamps read as
 * sentences ("Tarry Jek was arrested for theft"). The theft outcomes themselves
 * ({@code PICKPOCKETED}/{@code CAUGHT_STEALING}) are deliberately NOT narrated here:
 * {@link CrimeFeedTracker} owns those with both parties named, and one deed should land in
 * the feed once.
 *
 * <p><b>Per-tick, not per-frame.</b> Ticks and render frames are decoupled by
 * {@code SimulationDriver} (a paused frame runs zero ticks; a FAST frame can run several).
 * This tracker is wired as the driver's after-each-tick callback
 * ({@code SimulationDriver.setAfterTick(tracker::afterTick)}), so it observes the state of
 * <em>every</em> tick exactly once — never double-counting a tick that several frames
 * render, never skipping a tick a fast frame flew past. Each entry is tagged with the tick
 * whose execution produced the transition.
 *
 * <p>Baselines are captured from live (tick-0 / spawn) state at construction, so the first
 * observed tick diffs against real spawn values (e.g. the deliberately displaced movers
 * start "away", and their later home arrival is a genuine away&rarr;home edge).
 */
public final class EventLogTracker {

    private final ActorRegistry registry;
    private final HomeRegistry homes;
    private final EventLog log;
    /** The bake-side name table; {@link IdentityRegistry#EMPTY} degrades to "Serf #2" style. */
    private final IdentityRegistry identity;

    private ReasonCode[] prevReason;
    private GoalState[] prevGoal;
    private boolean[] prevAtHome;

    /** Un-forged fixtures (and pre-names tests) — the {@code "Serf #2"} fallback naming. */
    public EventLogTracker(ActorRegistry registry, HomeRegistry homes, EventLog log) {
        this(registry, homes, log, IdentityRegistry.EMPTY);
    }

    public EventLogTracker(ActorRegistry registry, HomeRegistry homes, EventLog log,
            IdentityRegistry identity) {
        this.registry = registry;
        this.homes = homes;
        this.log = log;
        this.identity = identity;
        int n = registry.size();
        this.prevReason = new ReasonCode[n];
        this.prevGoal = new GoalState[n];
        this.prevAtHome = new boolean[n];
        for (int i = 0; i < n; i++) {
            Actor actor = registry.get(i);
            prevReason[i] = actor.lastReasonCode();
            prevGoal[i] = actor.goalState();
            prevAtHome[i] = atHome(actor);
        }
    }

    /**
     * Records this tick's transitions. Call exactly once per executed tick, after the tick
     * has run (so live state reflects it) — {@code tick} is that tick's number.
     */
    public void afterTick(long tick) {
        ensureCapacity(registry.size());
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);

            ReasonCode reason = actor.lastReasonCode();
            if (!Objects.equals(reason, prevReason[i])) {
                String sentence = humanReasonLine(actor, prevReason[i], reason);
                if (sentence != null) {
                    log.add(tick, sentence);
                }
                prevReason[i] = reason;
            }

            GoalState goal = actor.goalState();
            if (goal != prevGoal[i]) {
                log.add(tick, tag(actor) + " goal " + prevGoal[i] + " -> " + goal);
                prevGoal[i] = goal;
            }

            boolean home = atHome(actor);
            if (home && !prevAtHome[i]) {
                log.add(tick, tag(actor) + " arrived home");
            }
            prevAtHome[i] = home;
        }
    }

    private boolean atHome(Actor actor) {
        int homeId = actor.homeId();
        if (homeId == Actor.NONE) {
            return false;
        }
        Home home = homes.get(homeId);
        return actor.cell() == home.homeCell();
    }

    private void ensureCapacity(int n) {
        if (n <= prevReason.length) {
            return;
        }
        int old = prevReason.length;
        prevReason = Arrays.copyOf(prevReason, n);
        prevGoal = Arrays.copyOf(prevGoal, n);
        prevAtHome = Arrays.copyOf(prevAtHome, n);
        // New-since-construction actors baseline to their current live state.
        for (int i = old; i < n; i++) {
            Actor actor = registry.get(i);
            prevReason[i] = actor.lastReasonCode();
            prevGoal[i] = actor.goalState();
            prevAtHome[i] = atHome(actor);
        }
    }

    /**
     * The named line for one reason transition: a human sentence for the justice stamps, the
     * named generic {@code "reason A -> B"} shape otherwise, {@code null} (skip) for the
     * theft outcomes {@link CrimeFeedTracker} narrates with both parties.
     */
    private String humanReasonLine(Actor actor, ReasonCode from, ReasonCode to) {
        if (to == null) {
            return tag(actor) + " reason " + name(from) + " -> -";
        }
        return switch (to) {
            case PICKPOCKETED, CAUGHT_STEALING -> null; // CrimeFeedTracker's richer line
            case TALKED, QUEST_ADVANCED -> null; // QuestFeedTracker owns quest narration
            case ARRESTED_FOR_THEFT -> tag(actor) + " was arrested for theft";
            case HOUSE_ARRESTED -> tag(actor) + " was confined under house arrest";
            case WARNED_MOVE_ALONG -> tag(actor) + " was warned to move along";
            default -> tag(actor) + " reason " + name(from) + " -> " + name(to);
        };
    }

    private String tag(Actor actor) {
        return PersonNames.fullNameOf(actor.id(), registry, identity);
    }

    private static String name(ReasonCode code) {
        return code == null ? "-" : code.name();
    }
}
