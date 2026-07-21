package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * {@code BEAST_HUNT} (living-docks beast pass) — the BEAST food channel. The citizen
 * {@code SEEK_FOOD} machine is structurally unusable by a beast (no ID card so no purchase, no
 * stocked larder, commons are farm-fed citizen ground), yet its score fired unconditionally on
 * the HUNGER band — so a hungry gull was permanently pinned by a policy that could never feed
 * it (the observed two-cell oscillation's second defect). This policy replaces it in beast
 * stacks with a channel a beast can actually use: sense the nearest same-z live mouse, chase
 * it, catch at adjacency, restore HUNGER.
 *
 * <p><b>Acquire-gated scoring (the {@link ApprehendPolicy} shape):</b> the score is the exact
 * SEEK_FOOD pricing ({@code seekFood.priority + low/critBonus} from the raws — no new raws
 * field) but ONLY while a live hunt lock is held or the throttled sense probe finds an
 * actionable target; otherwise 0, so a hungry beast with no sensable prey keeps wandering
 * (GOAL_PURSUE wins) instead of dead-ending — the structural guarantee that the SEEK_FOOD trap
 * cannot be recreated. Band discipline: 655 (LOW) beats the wander job (~290) and yields to
 * FLEE (950); 1005 (CRITICAL) outranks FLEE — the identical starving-outranks-scared ladder the
 * citizen SEEK_FOOD already established.
 *
 * <p><b>Catch under the 2/cell cap:</b> contact fires at chebyshev {@value #CONTACT_RADIUS}, so
 * the predator never needs the prey's cell; chase hops route around full cells like walls. Two
 * predators adjacent to one mouse the same tick: ascending-id iteration means the lower id eats
 * and the higher id hits the DOWNED check and closes with {@code TARGET_LOST} — deterministic,
 * no double meal. The catch mints/sinks NO {@link ItemKinds#FOOD} and must never call
 * {@code ctx.recordFoodEaten} (that counter is FOOD-item-only), so the closed-supply identity
 * {@code minted == live + eaten} is untouched.
 *
 * <p><b>Hysteresis:</b> the trigger is HUNGER &lt; LOW (3000) and a catch restores
 * {@link FoodEconomy#EAT_RESTORE} (+8000), so post-meal hunger is always past RECOVERED — the
 * release condition moves the reserve 5000 beyond the trigger in one act, so the
 * SEEK_FOOD/RETURN_HOME flip-flop class cannot occur. Draw-free end to end (pure ascending-index
 * scans + integer deltas); the hunt lock rides the already-persisted
 * {@code targetKind}/{@code targetKey} pair and the revive rides {@code statusBits}/
 * {@code downedTimer} — zero new persisted scalars.
 */
public final class BeastHuntPolicy implements BehaviorPolicy {

    /** Sense-probe cadence (absolute tick % PERIOD == 0) — mirrors {@code ApprehendPolicy}. */
    static final int SENSE_PERIOD_TICKS = 10;
    /** Same-z chebyshev sense radius for prey acquisition. */
    static final int SENSE_RADIUS = 24;
    /** Catch distance: adjacency (the occupancy cap means the predator never enters the cell). */
    static final int CONTACT_RADIUS = 1;
    /**
     * Revive countdown stamped on a caught mouse's {@code downedTimer} (fits short). Tuned
     * DOWN from a first-cut 6000 by the 30k docks soak: realized per-mouse yield (catch
     * latency + bounded chase failures against real geometry) runs well under the na&iuml;ve
     * {@code DAY / (REVIVE + walkback)} theory, and at 6000 every thin cluster (the Gullet
     * pair, the lone K29 cat) periodically starved its predator through an all-down revive
     * trough. 3000 keeps a caught mouse visibly gone for an eighth of a day while giving
     * every committed den cluster a &ge;2x supply margin over its predators' demand.
     */
    static final short PREY_REVIVE_TICKS = 3000;
    /** A locked prey that got this far away is lost (defensive bound on a stale lock). */
    static final int LOSE_RADIUS = 2 * SENSE_RADIUS;
    /**
     * Chase ticks before a hunt is declared frozen and abandoned. A real chase closes in
     * &le; ~50 ticks (sense radius 24, speed 1); a chase that has run {@value} ticks is the
     * chokepoint-frozen kind the docks soak surfaced — the A* route to the prey EXISTS
     * (pathfinding is occupancy-blind) but its first hop is a cell parked at the 2/cell cap
     * for a whole shift, so the predator "chases" in place while this policy outranks the
     * wander forever and starves it. The budget rides the persisted {@code policyTimer}
     * scratch field (reserved for exactly this — no other policy writes it today); on
     * exhaustion the lock drops AND the wander goal target is cleared (the HeldPolicy
     * release precedent), so the very next job tick draws a fresh wander leg that walks the
     * beast OUT of the blocked corner instead of resuming the same doomed approach.
     */
    static final int CHASE_BUDGET_TICKS = 100;

    private static final ActorTypeId PREY_TYPE = ActorTypeId.of("mouse");

    @Override
    public PolicyId id() {
        return PolicyId.BEAST_HUNT;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        if (self.targetKind() == TargetKind.ACTOR) {
            return priced(self); // live hunt lock: never abandon mid-chase (APPREHEND precedent)
        }
        if (!NeedThresholds.isLow(self.need(Need.HUNGER))) {
            return 0;
        }
        if (ctx.tick() % SENSE_PERIOD_TICKS != 0) {
            return 0; // between sense boundaries: no acquisition (the throttle)
        }
        // Read-only acquisition probe; act() re-runs the identical deterministic scan to lock.
        return senseNearestPrey(self, ctx) != Actor.NONE ? priced(self) : 0;
    }

    /**
     * The exact SEEK_FOOD pricing, reusing the raws {@code seekFood} block (no new raws field):
     * {@code seekFood.priority + (critical ? critBonus : lowBonus)}. Gull: 655 LOW / 1005 CRITICAL.
     */
    private static int priced(Actor self) {
        int hunger = self.need(Need.HUNGER);
        NeedConfig cfg = self.stats().need(Need.HUNGER);
        int urgencyBonus = NeedThresholds.isCritical(hunger) ? cfg.critBonus() : cfg.lowBonus();
        return self.stats().seekFoodPriority() + urgencyBonus;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        if (self.targetKind() != TargetKind.ACTOR) {
            int preyId = senseNearestPrey(self, ctx); // byte-identical to score()'s probe
            if (preyId == Actor.NONE) {
                return; // defensive: only reachable if score() and act() ever diverged
            }
            self.setTarget(TargetKind.ACTOR, preyId);
            self.setPolicyTimer((short) 0); // a fresh lock starts a fresh chase budget
        }
        Actor prey = ctx.registry().get(self.targetKey());
        int selfCell = self.cell();
        int preyCell = prey.cell();
        if (prey.hasStatus(StatusBit.DOWNED) || PackedPos.z(preyCell) != PackedPos.z(selfCell)
                || ActorGeometry.chebyshev(selfCell, preyCell) > LOSE_RADIUS) {
            // Another predator got it first (or the lock went stale): close and re-sense later.
            dropLock(self);
            return;
        }
        if (ActorGeometry.chebyshev(selfCell, preyCell) <= CONTACT_RADIUS) {
            // CATCH: the mouse goes down with a revive countdown; the predator eats. No FOOD
            // item is minted or sunk and recordFoodEaten is deliberately NOT called (that
            // counter is FOOD-item-only — the conservation identity must stay untouched).
            prey.setStatus(StatusBit.DOWNED, true);
            prey.setDownedTimer(PREY_REVIVE_TICKS);
            prey.setLastReasonCode(ReasonCode.PREY_CAUGHT);
            self.applyNeedDelta(Need.HUNGER, FoodEconomy.EAT_RESTORE);
            self.setTarget(TargetKind.NONE, Actor.NONE);
            self.setPolicyTimer((short) 0);
            self.setLastReasonCode(ReasonCode.ATE_PREY);
            return;
        }
        int chaseTicks = self.policyTimer() + 1;
        if (chaseTicks > CHASE_BUDGET_TICKS) {
            // Frozen chase (see CHASE_BUDGET_TICKS): abandon AND clear the wander goal target
            // so the job's next tick draws a fresh leg away from the blocked corner.
            self.setGoalTarget(TargetKind.NONE, Actor.NONE);
            dropLock(self);
            return;
        }
        self.setPolicyTimer((short) chaseTicks);
        // CHASE: route-following, leash-ignoring (a hunt legitimately ranges past the roost
        // leash, exactly like SEEK_FOOD's walk). A route-failed chase drops the lock (bounded
        // abandon — the ApprehendPolicy pattern, backed by the 500-tick search cooldown).
        self.stepAlongRoute(preyCell, true, ctx::isWalkable, ctx.occupancy());
        self.setLastReasonCode(ReasonCode.HUNTING);
        if (self.routeFailedTo(preyCell)) {
            dropLock(self);
        }
    }

    private static void dropLock(Actor self) {
        self.setTarget(TargetKind.NONE, Actor.NONE);
        self.setPolicyTimer((short) 0);
        self.setLastReasonCode(ReasonCode.TARGET_LOST);
    }

    /**
     * The throttled prey sense: ascending-index registry scan (the {@code watchIsNearby}/
     * APPREHEND discipline), same z, skip self and every DOWNED mouse, chebyshev &le;
     * {@link #SENSE_RADIUS}; NEAREST by chebyshev wins, ascending index breaking ties (the
     * SeekFood nearest-scan shape). A candidate standing on a cell the last chase A* could
     * not route to ({@link Actor#routeFailedTo} — the SeekFood {@code skipRouteFailed}
     * precedent) is skipped, so a mouse in an unroutable pocket cannot deadlock the hunt by
     * being re-locked as "nearest" every cadence while a reachable mouse sits slightly
     * farther. No maps, no allocation, no draws.
     */
    private static int senseNearestPrey(Actor self, ActorContext ctx) {
        ActorRegistry registry = ctx.registry();
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        int best = Actor.NONE;
        int bestDist = SENSE_RADIUS + 1;
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            if (other.id() == self.id() || !other.typeId().equals(PREY_TYPE)) {
                continue;
            }
            int cell = other.cell();
            if (PackedPos.z(cell) != selfZ || other.hasStatus(StatusBit.DOWNED)
                    || self.routeFailedTo(cell)) {
                continue;
            }
            int d = ActorGeometry.chebyshev(selfCell, cell);
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }
}
