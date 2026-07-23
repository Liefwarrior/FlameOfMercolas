package com.trojia.sim.actor;

import com.trojia.sim.world.Dir;
import com.trojia.sim.world.PackedPos;

/**
 * The abstract base owning ALL actor state and ALL shared verbs
 * (ACTORS-SPEC.md §1.1). Every field and every verb lives here or in the
 * {@link BehaviorPolicy}/{@code Job} libraries; subclasses (one thin file per
 * type, §1.4) add zero fields and zero verbs — the depth-2 guard
 * (Actor -&gt; type, never deeper) is enforced by the actor package's
 * ArchUnit purity/thin-subclass test suite.
 *
 * <p>Scope note for this F2.5 foundation milestone: the full sensing/
 * stimulus/witness apparatus (§2.4, §2.6) and the world-lane lookback for
 * movement collision (§2.5) are later extensions. {@link #tick} here runs the
 * spec's template shape (decay needs, select policy by draw-free argmax, act,
 * audit status) without the {@code StimulusSet} machinery; {@link #stepToward}
 * is a pure greedy Chebyshev walk with leash enforcement but no obstacle
 * lookup (no world/lanes are wired to actors yet — §2.3's "actors never
 * write lanes" holds trivially since there is nothing to write to).
 */
public abstract class Actor {

    /** Sentinel for "no cell / no target / no owner / no home / no job". */
    public static final int NONE = -1;

    /**
     * A narrow, decoupled walkability probe (§2.5 addendum): kept in this
     * package rather than depending on the full {@link ActorContext} surface,
     * so {@link #stepToward(int, boolean, WalkabilityQuery)} stays as
     * world-agnostic as reasonably possible. Production call sites pass
     * {@code ctx::isWalkable} (a method reference — {@link ActorContext}
     * doesn't need to implement this interface, structural fit is enough).
     */
    @FunctionalInterface
    public interface WalkabilityQuery {
        boolean isWalkable(int cell);
    }

    /**
     * The hard actor-actor occupancy cap: at most this many actors may share one tile cell,
     * enforced at movement commit ({@link #tryStep}) exactly like a wall — a full cell reads as
     * blocked, so wall-slide tries alternatives and, failing those, the tick is a deterministic
     * no-op. Spawn-time crowding is prevented separately by the scenario spawner.
     *
     * <p>Density revisit (Eli): ONE per square — "no more stacking actors, only one per square
     * from now on". Was 2. A cell blocked only by its single occupant can now be contested via
     * the shove verb ({@link PushMechanics}, {@link OccupancyQuery#tryPush}) instead of reading
     * as a permanent wall.
     */
    public static final int MAX_OCCUPANTS_PER_CELL = 1;

    /**
     * A narrow occupancy probe threaded alongside {@link WalkabilityQuery} into the world-aware
     * movers (§2.5 addendum, the "only 2 to a cell" rule). {@link #occupantsAt(int)} reports the
     * live occupant count of a candidate cell (excluding the moving actor, which still sits on its
     * own cell); {@link #onEnter(int, int)} lets the mover keep a shared occupancy index live as
     * it commits a step (decrement the vacated cell, increment the entered one). The {@link
     * #UNLIMITED} no-op is the default for the world-less/test overloads, so occupancy never
     * blocks and no index bookkeeping happens there — {@code ActorTest} and the collision-free
     * convenience paths are untouched.
     */
    public interface OccupancyQuery {

        /** No cap, no bookkeeping: {@code occupantsAt} is always 0, {@code onEnter} does nothing. */
        OccupancyQuery UNLIMITED = new OccupancyQuery() {
            @Override
            public int occupantsAt(int cell) {
                return 0;
            }

            @Override
            public void onEnter(int fromCell, int toCell) {
            }
        };

        /** Live occupant count of {@code cell} right now (not counting the moving actor). */
        int occupantsAt(int cell);

        /** Commit hook: the moving actor just left {@code fromCell} for {@code toCell}. */
        void onEnter(int fromCell, int toCell);

        /**
         * The shove hook (density revisit): {@code pusher}'s step onto {@code cell} is blocked
         * ONLY by occupancy — attempt to displace the occupant ({@link PushMechanics}). Returns
         * {@code true} iff the cell was vacated (the caller then commits the step). The default —
         * and {@link #UNLIMITED}, which never reports a full cell anyway — never pushes, so
         * world-less/test movers are byte-identical to before.
         */
        default boolean tryPush(Actor pusher, int cell) {
            return false;
        }
    }

    /**
     * REST reserve regained per tick while standing on the home cell (a "sleep"
     * recovery, §11.1). Chosen above every type's REST {@code decayPerKilotick}
     * so a night at home refills REST and the actor heads back out next day —
     * see {@link #recoverRestAtHome}.
     */
    private static final int REST_RECOVERED_PER_TICK_AT_HOME = 6;

    // NOTE: there is deliberately NO passive HUNGER-at-home recovery constant (the economy-loop
    // pass removed the {@code HUNGER_RECOVERED_PER_TICK_AT_HOME} crutch). HUNGER now rises ONLY by
    // eating a FOOD item (SeekFoodPolicy's eat action), never by standing on the home cell — so
    // food is a real economic good and starvation is possible. REST recovery at home stays (§11.1).

    // ---- identity ----
    private final int id;
    private final ActorTypeId typeId;
    private final ActorTypeStats stats;
    private Persona identity;

    // ---- position + facing ----
    private int cell;
    private byte facing;
    private int moveAccumTicks;

    // ---- needs vector (§3) ----
    private final short[] needs;
    private final int[] needAccum;

    // ---- inventory (§1.1, §11.2): carried items live ENTIRELY in ItemsLiteRegistry (indexed by
    // carrier), the single source of truth. There is deliberately no parallel per-actor id list
    // here — a short[] of item ids both duplicated that state and truncated once an id passed
    // Short.MAX_VALUE (landmine G); ItemsLite's dense-slot recycling keeps ids bounded and the
    // by-carrier queries make a mirror unnecessary. ----

    // ---- health-lite hook ----
    private short hp;
    private short statusBits;
    private short downedTimer;

    // ---- behavior state ----
    private byte policyOrdinal;
    private TargetKind targetKind = TargetKind.NONE;
    private int targetKey = NONE;
    private short policyTimer;
    private int anchorCell;
    private int ownerId = NONE;

    // ---- home (§11.1 addendum) ----
    private int homeId = NONE;

    // ---- job/goal state (§10.1 addendum) ----
    private short jobOrdinal = -1;
    private GoalState goalState = GoalState.SELECTING;
    private TargetKind goalTargetKind = TargetKind.NONE;
    private int goalTargetKey = NONE;
    private short goalProgress;
    private int goalCooldown;
    private int goalWorkTicks;

    // ---- arrest/custody state (ARREST-SPEC addendum): plain scalars, the same
    // goalProgress/goalWorkTicks precedent — not a side-table (this is per-actor
    // bookkeeping, not the relational many-to-one shape a registry solves) ----
    /** Tick the current custody sentence ends; only meaningful while {@code HELD}. */
    private long heldUntilTick;
    /** Villain arrest count so far; only Skyrunner escalation (maim/hang) reads it today. */
    private byte offenseCount;
    /**
     * The specific prison cell this actor is assigned to while {@code HELD} (Phase-2 STEP C,
     * Pass 10), or {@link #NONE} when free. De-scalarizes the single {@code arrestHoldCell}: at
     * arrest the lowest-free cell is stamped here, {@link HeldPolicy} escorts to it, and release
     * clears it back to {@link #NONE} — so six prisoners hold in six distinct cells, and the slot
     * frees on release. A persisted scalar (serialize/load/hash, the {@code heldUntilTick} triad).
     */
    private int assignedHoldCell = NONE;
    /**
     * The offender this guard is actively apprehending (law &amp; order pass, APPREHEND), or
     * {@link #NONE}. Set by the guard-side sense scan, cleared when the case closes (offender
     * complied, was arrested, or turned unreachable). While set, {@code ApprehendPolicy} scores a
     * high fixed constant so the guard never abandons an apprehension mid-chase. A persisted
     * scalar (serialize/load/hash — the {@code assignedHoldCell} triad).
     */
    private int apprehendTargetId = NONE;
    /**
     * The absolute tick this actor's move-along warning expires (only meaningful while
     * {@link StatusBit#MOVE_ALONG} is set): leave the zone before it and the warning clears
     * free; still in violation at/after it and the guard escalates to fine + arrest. Absolute
     * tick, never a countdown (determinism rule). Persisted (serialize/load/hash).
     */
    private long moveAlongUntilTick;
    /**
     * The absolute tick of this actor's last shove — as pusher OR pushee (being shoved staggers
     * you, the push-chain guard; see {@link PushMechanics}). Gate: a new shove needs
     * {@code tick - lastPushTick >= PUSH_COOLDOWN_TICKS}. Initialized one whole cooldown in the
     * past so a fresh actor can shove immediately at tick 0. A persisted scalar
     * (serialize/load/hash — the {@code heldUntilTick} triad).
     */
    private long lastPushTick = -PushMechanics.PUSH_COOLDOWN_TICKS;
    /**
     * The absolute tick this actor's shove-riot house arrest ends (only meaningful while
     * {@link StatusBit#HOUSE_ARREST} is set): {@link HouseArrestPolicy} routes the offender HOME
     * and holds it there sleeping until this deadline. Absolute tick, never a countdown. A
     * persisted scalar (serialize/load/hash — the {@code heldUntilTick} triad).
     */
    private long houseArrestUntilTick;
    /**
     * The absolute tick until which {@link BeastHuntPolicy} must NOT acquire a new hunt lock
     * (the futile-chase backoff, the gull#408/#410 starvation fix): a chase that exhausted its
     * whole tick budget without a catch — plug-frozen against parked actors, or orbiting an
     * enclosed-pocket prey it can route toward but never touch — is abandoned AND acquisition
     * is suppressed until this deadline, so GOAL_PURSUE's wander (with its drift/push escape
     * tooling and anchor-biased legs) actually gets a window long enough to change the
     * situation instead of being re-preempted at the very next sense cadence. Absolute tick,
     * never a countdown. A persisted scalar (serialize/load/hash — the {@code heldUntilTick}
     * triad).
     */
    private long huntBackoffUntilTick;

    // ---- Play mode (PLAY-MODE-SPEC.md §5.2/§6): plain scalar, the same goalProgress/
    // heldUntilTick precedent — per-frame input intent, not simulation state, so it is
    // deliberately NOT part of the ACTR persisted record (§6). ----
    /** Next cell to step toward under direct player control, or {@link #NONE} if none pending. */
    private int playerMoveTargetCell = NONE;
    /**
     * The actor this played actor intends to PICKPOCKET this tick (Sprint 2's play-mode
     * verb), or {@link #NONE}. Same contract as {@link #playerMoveTargetCell}: per-frame
     * input intent set by the observer's input layer, consumed (and reset) by
     * {@code PlayerControlPolicy.act}, never persisted.
     */
    private int playerPickpocketTargetId = NONE;
    /**
     * The actor this played actor intends to TALK to this tick (Sprint 3 quests — the
     * existing talk verb made sim-visible), or {@link #NONE}. Same contract as
     * {@link #playerMoveTargetCell}: per-frame input intent set by the observer's input
     * layer, consumed (and reset) by {@code PlayerControlPolicy.act} (which validates
     * adjacency and notes the fact of talking into the quest log), never persisted.
     * The presentation greet itself stays observer-side and sim-silent.
     */
    private int playerTalkTargetId = NONE;
    /**
     * Whether this played actor intends to EAT/BUY A MEAL this tick (Sprint 4 — the
     * played-actor eat verb; a played soul could previously starve, its SEEK_FOOD AI
     * permanently outscored by PLAYER_CONTROL). Same contract as
     * {@link #playerMoveTargetCell}: per-frame input intent set by the observer's input
     * layer, consumed (and reset) by {@code PlayerControlPolicy.act}, which resolves it
     * through {@code SeekFoodPolicy}'s existing eat-in-reach chain (carried ration, a
     * buyable counter at the player's OWN barter quote, the own larder, a commons, the
     * broke's scavenge). Never persisted.
     */
    private boolean playerEatIntent;

    // ---- cached A* route (§2.5 pathfinding addendum): a derived/recomputable cache, not
    // ground-truth state — the same "per-actor bookkeeping vs. registry" distinction the
    // heldUntilTick/offenseCount comment above already draws. Deliberately NOT part of
    // ActorsSystem.serialize/load/hashInto: findRoute is a pure function of
    // (startCell, targetCell, walkability), so losing the cache on load just costs one
    // recompute that reproduces the identical route — no determinism divergence. ----
    private static final int[] EMPTY_ROUTE = new int[0];
    private int[] cachedRoute = EMPTY_ROUTE;
    private short cachedRouteIndex;
    private int cachedRouteTargetCell = NONE;
    /** Ticks left before retrying a target whose search just failed (bounded backoff). */
    private short cachedRouteRetryCooldown;

    // ---- legibility (inspector line, §7.2/§10.5) ----
    private ReasonCode lastReasonCode;

    protected Actor(int id, ActorTypeId typeId, ActorTypeStats stats, int cell) {
        this.id = id;
        this.typeId = typeId;
        this.stats = stats;
        this.identity = Persona.of(id);
        this.cell = cell;
        this.anchorCell = cell;
        this.needs = new short[Need.COUNT];
        this.needAccum = new int[Need.COUNT];
        for (Need need : Need.values()) {
            this.needs[need.ordinal()] = (short) stats.need(need).start();
        }
    }

    /** Returns the type's static policy stack constant (§1.4). Never {@code null}. */
    protected abstract PolicyStack policies();

    // ======================================================================
    // The tick entry point (FINAL — the template is engine-owned, §1.1)
    // ======================================================================

    public final void tick(ActorContext ctx) {
        decayNeeds();
        recoverRestAtHome(ctx);
        tickGoalCooldown();
        int winningIndex = policies().selectIndex(this, ctx);
        if (winningIndex < 0) {
            return; // unreachable for a well-formed stack (always ends in an IDLE fallback)
        }
        policyOrdinal = (byte) winningIndex;
        policies().get(winningIndex).act(this, ctx);
        auditStatus();
    }

    private void decayNeeds() {
        for (Need need : Need.values()) {
            int i = need.ordinal();
            NeedConfig cfg = stats.need(need);
            needAccum[i] += cfg.decayPerKilotick();
            while (needAccum[i] >= 1000) {
                needAccum[i] -= 1000;
                needs[i] = (short) Math.max(0, needs[i] - 1);
            }
            if (cfg.recoverPerTick() > 0) {
                needs[i] = (short) NeedThresholds.clamp(needs[i] + cfg.recoverPerTick());
            }
        }
    }

    /**
     * Sleeping at home restores REST — the daily-cycle counterpart to
     * {@code RETURN_HOME} (§11.1). Without this, every need decays monotonically
     * (REST has no passive recovery in raws), so after roughly a day every actor
     * is permanently REST-low and {@code RETURN_HOME} pins the whole population at
     * home forever, freezing the sim. Recovering while standing on the home cell
     * closes the loop: work/roam by day → walk home → sleep restores REST →
     * REST-low clears → back out the next day. The rate outpaces every type's
     * REST decay so a night's stay refills it; it is a plain integer delta on the
     * home cell only, so it changes no determinism property.
     */
    private void recoverRestAtHome(ActorContext ctx) {
        if (homeId == NONE) {
            return;
        }
        if (cell == ctx.homes().get(homeId).homeCell()) {
            applyNeedDelta(Need.REST, REST_RECOVERED_PER_TICK_AT_HOME);
        }
    }

    private void tickGoalCooldown() {
        if (goalState == GoalState.COOLDOWN) {
            goalCooldown--;
            if (goalCooldown <= 0) {
                goalCooldown = 0;
                goalState = GoalState.SELECTING;
            }
        }
    }

    private void auditStatus() {
        if (downedTimer > 0) {
            downedTimer--;
            if (downedTimer == 0) {
                statusBits = StatusBit.clear(statusBits, StatusBit.DOWNED);
            }
        }
    }

    // ======================================================================
    // Shared verbs (§1.1) — implemented once, used by every policy/job
    // ======================================================================

    /** A world-less/test-convenience probe: every cell reads as walkable. */
    private static final WalkabilityQuery ALWAYS_WALKABLE = cell -> true;

    /**
     * Greedy Chebyshev-reducing step toward {@code targetCell} (§2.5), gated
     * by the type's {@code speedTicksPerStep} accumulator. Actors move on one
     * z-level only (top-down grid); a target on a different z is a no-op.
     * Leash-enforced unless {@code ignoresLeash} (FLEE/APPREHEND/RECAPTURE-
     * style overrides, §2.5).
     *
     * <p>Collision-free convenience overload (no world/lanes are consulted —
     * every candidate step reads as walkable): the documented "no-world / test
     * convenience" path, kept byte-identical to its original shape so
     * {@code ActorTest} needs no changes.
     */
    public final void stepToward(int targetCell, boolean ignoresLeash) {
        stepToward(targetCell, ignoresLeash, ALWAYS_WALKABLE, OccupancyQuery.UNLIMITED);
    }

    /** Convenience overload: leash-respecting step (the common case), no world lookup. */
    public final void stepToward(int targetCell) {
        stepToward(targetCell, false, ALWAYS_WALKABLE, OccupancyQuery.UNLIMITED);
    }

    /**
     * World-aware step with no occupancy cap ({@link OccupancyQuery#UNLIMITED}) — the pre-cap
     * overload, kept so the direct {@code stepToward}/{@code stepAlongRoute} test coverage
     * ({@code ActorStepTowardWalkabilityTest} et al.) is untouched. Production call sites pass a
     * real {@link OccupancyQuery} via the four-arg overload below.
     */
    public final void stepToward(int targetCell, boolean ignoresLeash, WalkabilityQuery walk) {
        stepToward(targetCell, ignoresLeash, walk, OccupancyQuery.UNLIMITED);
    }

    /**
     * Greedy Chebyshev-reducing step toward {@code targetCell} (§2.5), gated
     * by the type's {@code speedTicksPerStep} accumulator, world-aware: each
     * candidate step is checked against {@code walk} before being committed.
     * Actors move on one z-level only; a target on a different z is a no-op.
     * Leash-enforced unless {@code ignoresLeash}.
     *
     * <p>Wall-slide (§2.5 addendum): a blocked diagonal primary step retries
     * the two orthogonal component steps before giving up, so an actor
     * grazing a wall corner slides along it instead of freezing dead. Every
     * candidate (primary and each slide alternative) is independently
     * walkability- <em>and</em> leash-checked. A tick that finds every
     * candidate blocked is a deterministic no-op (same shape as today's
     * leash freeze) — {@code moveAccumTicks} still resets unconditionally
     * above, so a blocked tick still consumes the speed budget, matching
     * today's leash-block semantics.
     */
    public final void stepToward(int targetCell, boolean ignoresLeash, WalkabilityQuery walk,
            OccupancyQuery occ) {
        if (cell == targetCell) {
            return;
        }
        int tz = PackedPos.z(targetCell);
        int z = PackedPos.z(cell);
        if (z != tz) {
            return;
        }
        moveAccumTicks++;
        if (moveAccumTicks < stats.speedTicksPerStep()) {
            return;
        }
        moveAccumTicks = 0;
        int x = PackedPos.x(cell);
        int y = PackedPos.y(cell);
        int tx = PackedPos.x(targetCell);
        int ty = PackedPos.y(targetCell);
        int dx = Integer.compare(tx, x);
        int dy = Integer.compare(ty, y);
        if (tryStep(dx, dy, z, ignoresLeash, walk, occ)) {
            return; // primary step (diagonal or straight)
        }
        if (dx != 0 && dy != 0) {
            // Diagonal blocked -> wall-slide: try the two orthogonal component steps.
            if (tryStep(dx, 0, z, ignoresLeash, walk, occ)) {
                return;
            }
            if (tryStep(0, dy, z, ignoresLeash, walk, occ)) {
                return;
            }
        }
        // Every candidate blocked (wall, leash, or a full cell): deterministic no-op (§2.5).
    }

    /**
     * Attempts one candidate step {@code (dx, dy)} from the current cell on
     * z-level {@code z}: rejects it if {@code walk} reports it unwalkable,
     * then applies the existing leash math, then rejects it if {@code stepped}
     * is already at the {@link #MAX_OCCUPANTS_PER_CELL} occupancy cap (a full
     * cell behaves exactly like a wall). Commits {@code cell} and facing only
     * if all three checks pass, then notifies {@code occ} of the vacated/entered
     * cells so a shared occupancy index stays live mid-tick. Returns whether the
     * step was committed.
     *
     * <p><b>Solid-corner rule (PathFinder parity, beast-pass wedge fix):</b> a diagonal step
     * is additionally rejected when either orthogonal flank cell is unwalkable — exactly
     * {@code PathFinder}'s "never cut a solid diagonal wall corner" expansion rule. Without
     * it the two movers disagreed: a greedy/slid step could squeeze diagonally BETWEEN two
     * wall corners into a pocket whose every A* exit needs the cut A* refuses, permanently
     * sealing the actor in (the docks soak surfaced beasts — and, historically, tavern
     * patrons — starving in exactly such pockets). Movement and pathfinding now share one
     * notion of passable geometry, so an actor can never walk somewhere it cannot route back
     * out of. Flanks are checked against WALLS only (not occupancy), matching A*.
     */
    private boolean tryStep(int dx, int dy, int z, boolean ignoresLeash, WalkabilityQuery walk,
            OccupancyQuery occ) {
        int x = PackedPos.x(cell);
        int y = PackedPos.y(cell);
        int stepped = PackedPos.pack(x + dx, y + dy, z);
        if (!walk.isWalkable(stepped)) {
            return false;
        }
        if (dx != 0 && dy != 0
                && (!walk.isWalkable(PackedPos.pack(x + dx, y, z))
                        || !walk.isWalkable(PackedPos.pack(x, y + dy, z)))) {
            return false; // never cut a solid diagonal wall corner (PathFinder parity)
        }
        if (!ignoresLeash) {
            int newDist = ActorGeometry.chebyshev(stepped, anchorCell);
            // Relative (monotonic-improvement) check, not absolute containment: block only
            // when the step would leave the actor beyond its leash AND no closer to the
            // anchor than it already is. An absolute "> leashRadius" check on the
            // destination alone would permanently freeze an actor whose current cell is
            // already more than leashRadius away from anchorCell (e.g. after a content
            // change moves anchorCell post-spawn) — chebyshev distance can only shrink by 1
            // per step, so every step back home would itself still read "> leashRadius" and
            // be rejected forever. Allowing any step that does not increase the distance
            // keeps the leash effective (an in-leash actor still can't wander out) while
            // letting an out-of-leash actor walk itself back in, one cell at a time.
            int curDist = ActorGeometry.chebyshev(cell, anchorCell);
            if (newDist > stats.leashRadius() && newDist >= curDist) {
                return false; // leash holds; deterministic no-op (§2.5)
            }
        }
        if (occ.occupantsAt(stepped) >= MAX_OCCUPANTS_PER_CELL) {
            // Blocked ONLY by occupancy (walkable + leash-legal, someone is standing there):
            // try the shove (density revisit — PushMechanics via the query's tryPush hook,
            // cooldown-gated). Displaces the occupant to an adjacent free cell — or swaps
            // with it when none exists (the squeeze-past, PushMechanics' class doc) — and
            // vacates the square; a cooldown-failed shove leaves the cell a hard wall.
            if (!occ.tryPush(this, stepped)) {
                return false;
            }
        }
        int from = cell;
        cell = stepped;
        if (dx != 0) {
            facing = (byte) (dx < 0 ? Dir.WEST.ordinal() : Dir.EAST.ordinal());
        } else if (dy != 0) {
            facing = (byte) (dy < 0 ? Dir.NORTH.ordinal() : Dir.SOUTH.ordinal());
        }
        occ.onEnter(from, stepped);
        return true;
    }

    /** Bounded backoff before retrying a target whose bounded search just failed. */
    private static final int ROUTE_RETRY_COOLDOWN_TICKS = 500;

    /**
     * Route-following mover (§2.5 pathfinding addendum): walks toward
     * {@code targetCell} one hop at a time along a cached {@link PathFinder}
     * route instead of {@link #stepToward}'s pure greedy Chebyshev reduction —
     * fixes the class of stuck/looping actors a greedy walk (even with its
     * wall-slide fallback) cannot escape (a concave pocket, a door on the
     * wrong wall face, an interior obstacle wider than the slide's one-cell
     * retry).
     *
     * <p>The cache is keyed on {@code targetCell}: a target change forces a
     * fresh search (see {@link #replan}). Each per-tick hop still runs
     * through the existing, unmodified {@link #stepToward(int, boolean,
     * WalkabilityQuery)} — its leash math, speed accumulator, facing update,
     * and wall-slide fallback are all reused verbatim (the wall-slide branch
     * is inert-but-harmless here, since every A*-emitted waypoint is already
     * adjacent+walkable). The {@code chebyshev(...) == 1} adjacency guard
     * defends against a stale cache surviving an unrelated teleport (Play
     * mode's direct control, or a save/load): if the actor's position no
     * longer lines up with the next cached waypoint, it is forced to replan
     * rather than silently walking toward a now-meaningless stale cell.
     *
     * <p>A search that fails (unreachable/over-budget/cross-z/target
     * unwalkable) is not retried every tick — it cools down for
     * {@link #ROUTE_RETRY_COOLDOWN_TICKS} ticks first, a bounded backoff so a
     * genuinely-stuck actor cannot re-run an expensive failed search every
     * single tick.
     */
    public final void stepAlongRoute(int targetCell, boolean ignoresLeash, WalkabilityQuery walk) {
        stepAlongRoute(targetCell, ignoresLeash, walk, OccupancyQuery.UNLIMITED);
    }

    /**
     * Occupancy-aware route follower (the production overload): identical to
     * {@link #stepAlongRoute(int, boolean, WalkabilityQuery)} but threads the {@link
     * OccupancyQuery} into each per-tick hop, so a waypoint whose cell is already at the
     * occupancy cap is refused just like a wall — the {@code cell == before} branch below then
     * forces a replan, letting the actor route around the full cell instead of stalling on it.
     */
    public final void stepAlongRoute(int targetCell, boolean ignoresLeash, WalkabilityQuery walk,
            OccupancyQuery occ) {
        if (cell == targetCell) {
            return;
        }
        if (PackedPos.z(cell) != PackedPos.z(targetCell)) {
            return;
        }

        boolean cacheValid = targetCell == cachedRouteTargetCell
                && cachedRouteIndex < cachedRoute.length
                && ActorGeometry.chebyshev(cell, cachedRoute[cachedRouteIndex]) == 1;
        if (!cacheValid && targetCell == cachedRouteTargetCell
                && cachedRoute.length == 0 && cachedRouteRetryCooldown > 0) {
            cachedRouteRetryCooldown--;
            return; // cooling down after a bounded-search failure
        }
        if (!cacheValid) {
            replan(targetCell, walk);
            if (cachedRoute.length == 0) {
                return; // freshly failed: deterministic no-op this tick
            }
        }
        int waypoint = cachedRoute[cachedRouteIndex];
        int before = cell;
        stepToward(waypoint, ignoresLeash, walk, occ); // reuses the existing single-cell mover
        if (cell == waypoint) {
            cachedRouteIndex++;
        } else if (cell == before) {
            cachedRouteTargetCell = NONE; // blocked/full waypoint went stale -> force replan next call
        }
    }

    /**
     * Cross-z-capable route follower (Sprint 4 "the climb") — the OPT-IN overload: identical
     * to the four-arg {@link #stepAlongRoute(int, boolean, WalkabilityQuery, OccupancyQuery)}
     * for every same-z target (and byte-identical when {@code links} is
     * {@link ZLinkTable#EMPTY}), but a target on a DIFFERENT z is no longer a no-op: the
     * pure {@link ZRouter} resolves the next hop of the band-by-band climb — a same-z walk
     * to the chosen connector's near endpoint (the ordinary cached-A* leg machinery,
     * verbatim), or, standing on that endpoint, the vertical commit via
     * {@link #tryStepVertical}.
     *
     * <p><b>Opt-in only</b> (the Sprint-4 scope fence): the consumers passing a live table
     * are player movement, {@code ReturnHomePolicy}, {@code HeldPolicy}'s escort, and the
     * waypoint route patrol. Every other mover keeps the four-arg overload and the z-rule.
     *
     * <p><b>No new state.</b> The hop is recomputed per call as a pure function of
     * {@code (cell, targetCell, links)} — nothing persisted, so a save/load mid-climb
     * resumes byte-identically for free. An UNROUTABLE climb (a band crossing with no
     * baked connector) lands in the existing route-failure cache with the same bounded
     * {@link #ROUTE_RETRY_COOLDOWN_TICKS} cooldown, so {@link #routeFailedTo} reads true
     * and consumers' failed-leg handling (the patrol skip) works unchanged.
     */
    public final void stepAlongRoute(int targetCell, boolean ignoresLeash, WalkabilityQuery walk,
            OccupancyQuery occ, ZLinkTable links) {
        if (PackedPos.z(cell) == PackedPos.z(targetCell) || links.isEmpty()) {
            stepAlongRoute(targetCell, ignoresLeash, walk, occ);
            return;
        }
        if (targetCell == cachedRouteTargetCell && cachedRoute.length == 0
                && cachedRouteRetryCooldown > 0) {
            cachedRouteRetryCooldown--;
            return; // cooling down after an unroutable-climb verdict
        }
        int hop = ZRouter.nextHop(cell, targetCell, links);
        if (hop == NONE) {
            // Mirror the bounded-search failure shape so routeFailedTo(targetCell) reads
            // true and the cooldown throttles re-verdicts (both are derived cache state).
            cachedRouteTargetCell = targetCell;
            cachedRoute = EMPTY_ROUTE;
            cachedRouteIndex = 0;
            cachedRouteRetryCooldown = (short) ROUTE_RETRY_COOLDOWN_TICKS;
            return;
        }
        if (PackedPos.z(hop) != PackedPos.z(cell)) {
            tryStepVertical(hop, ignoresLeash, walk, occ, links);
        } else {
            stepAlongRoute(hop, ignoresLeash, walk, occ); // the ordinary same-z leg
        }
    }

    /**
     * Commits ONE vertical step across a baked connector (Sprint 4 "the climb"): from a
     * stair cell straight up/down its column, or across a ramp's diagonal exit. The one
     * new movement verb — everything else about the step matches {@link #tryStep}'s
     * discipline: gated by the type's speed accumulator, walkability-checked, leash-checked
     * unless {@code ignoresLeash} (x/y chebyshev, as everywhere), occupancy-capped with the
     * same shove escape hatch, facing updated from the x/y delta (a pure stair step keeps
     * facing), {@code occ.onEnter} notified. Only a {@code links}-baked pair may carry the
     * step — an arbitrary cross-z destination (a stale player intent, a mis-aimed call)
     * is a deterministic no-op. A committed step drops the cached same-z route wholesale
     * ({@link #invalidateRoute}): the old band's waypoints are meaningless on the new one.
     *
     * @return whether the step was committed
     */
    public final boolean tryStepVertical(int dest, boolean ignoresLeash, WalkabilityQuery walk,
            OccupancyQuery occ, ZLinkTable links) {
        if (!links.linked(cell, dest)) {
            return false; // only a baked connector carries a vertical step
        }
        moveAccumTicks++;
        if (moveAccumTicks < stats.speedTicksPerStep()) {
            return false;
        }
        moveAccumTicks = 0;
        if (!walk.isWalkable(dest)) {
            return false;
        }
        if (!ignoresLeash) {
            int newDist = ActorGeometry.chebyshev(dest, anchorCell);
            int curDist = ActorGeometry.chebyshev(cell, anchorCell);
            if (newDist > stats.leashRadius() && newDist >= curDist) {
                return false; // the leash holds across bands too (x/y geometry, as everywhere)
            }
        }
        if (occ.occupantsAt(dest) >= MAX_OCCUPANTS_PER_CELL && !occ.tryPush(this, dest)) {
            return false; // the stair head is a 1-per-square funnel like any other cell
        }
        int from = cell;
        cell = dest;
        int dx = Integer.compare(PackedPos.x(dest), PackedPos.x(from));
        int dy = Integer.compare(PackedPos.y(dest), PackedPos.y(from));
        if (dx != 0) {
            facing = (byte) (dx < 0 ? Dir.WEST.ordinal() : Dir.EAST.ordinal());
        } else if (dy != 0) {
            facing = (byte) (dy < 0 ? Dir.NORTH.ordinal() : Dir.SOUTH.ordinal());
        }
        occ.onEnter(from, dest);
        invalidateRoute(); // the same-z leg cache is meaningless on the new band
        return true;
    }

    private void replan(int targetCell, WalkabilityQuery walk) {
        cachedRouteTargetCell = targetCell;
        cachedRouteIndex = 0;
        // Salted with this actor's id (density revisit, "unique paths per person"): each actor
        // gets its own slightly-wiggly near-optimal route between shared endpoints, ending the
        // single-file convoys — while staying a pure function of (id, start, target, walk).
        // id + 1 because salt 0 is the "no jitter" sentinel and actor id 0 is real.
        int[] route = PathFinder.findRoute(cell, targetCell, walk, PathFinder.DEFAULT_MAX_NODES,
                id + 1);
        if (route == null) {
            cachedRoute = EMPTY_ROUTE;
            cachedRouteRetryCooldown = (short) ROUTE_RETRY_COOLDOWN_TICKS;
        } else {
            cachedRoute = route;
        }
    }

    /**
     * Drops the cached route wholesale (the shove verb's replan hook, {@link PushMechanics}): a
     * displaced actor's cached waypoints no longer line up with where it now stands, so the next
     * {@link #stepAlongRoute} call re-searches from the new cell instead of trusting a stale
     * plan. Also clears any retry-cooldown so the replan happens immediately. A pure cache
     * reset — the cache is already documented non-persisted/derived, so this adds no state.
     */
    public final void invalidateRoute() {
        cachedRoute = EMPTY_ROUTE;
        cachedRouteIndex = 0;
        cachedRouteTargetCell = NONE;
        cachedRouteRetryCooldown = 0;
    }

    /**
     * True iff the most recent {@link #stepAlongRoute} search to {@code target} found NO path (an
     * over-budget / genuinely-unreachable target) and that failure is still cached — i.e. walking
     * to {@code target} right now is a known no-op. A pure read of the route cache, so it adds no
     * persisted state and no determinism surface; it lets a food-seeking policy re-scan for a
     * routable source instead of pinning itself to an unroutable one and freezing (the stranding
     * failure the economy-loop reachability fix targets).
     */
    public final boolean routeFailedTo(int target) {
        return cachedRouteTargetCell == target && cachedRoute.length == 0;
    }

    /** Applies a saturating (clamped [0,10000]) delta to one need (§3.2). */
    public final void applyNeedDelta(Need need, int delta) {
        int i = need.ordinal();
        needs[i] = (short) NeedThresholds.clamp(needs[i] + delta);
    }

    // ======================================================================
    // Accessors
    // ======================================================================

    public final int id() {
        return id;
    }

    public final ActorTypeId typeId() {
        return typeId;
    }

    public final ActorTypeStats stats() {
        return stats;
    }

    public final Persona identity() {
        return identity;
    }

    public final void setIdentity(Persona identity) {
        this.identity = identity;
    }

    /**
     * Play mode's disguise verb (DECISIONS.md Identity row, PLAY-MODE-SPEC.md §5.3): presents
     * as {@code otherActorId} — an existing {@code ActorId} in the registry — instead of this
     * actor's true identity. Pass this actor's own {@link #id()} to drop the disguise. A plain
     * field rewrite, no validation that {@code otherActorId} resolves to a live actor: the
     * caller (Play mode's impersonation picker) only ever passes ids resolved via the same
     * {@code ActorPicker} the click-to-select panel already trusts.
     */
    public final void setActAs(int otherActorId) {
        this.identity = new Persona(identity.trueId(), otherActorId);
    }

    public final int cell() {
        return cell;
    }

    /** Direct placement (spawn bake / tests only — movement in-tick goes through {@link #stepToward}). */
    public final void setCell(int cell) {
        this.cell = cell;
    }

    public final byte facing() {
        return facing;
    }

    public final void setFacing(byte facing) {
        this.facing = facing;
    }

    public final int need(Need need) {
        return needs[need.ordinal()];
    }

    /** Defensive snapshot of the full needs vector, indexed by {@link Need#ordinal()}. */
    public final short[] needsSnapshot() {
        return needs.clone();
    }

    public final short hp() {
        return hp;
    }

    public final void setHp(short hp) {
        this.hp = hp;
    }

    public final short statusBits() {
        return statusBits;
    }

    public final void setStatusBits(short statusBits) {
        this.statusBits = statusBits;
    }

    public final boolean hasStatus(short bit) {
        return StatusBit.isSet(statusBits, bit);
    }

    public final void setStatus(short bit, boolean on) {
        statusBits = on ? StatusBit.set(statusBits, bit) : StatusBit.clear(statusBits, bit);
    }

    public final short downedTimer() {
        return downedTimer;
    }

    public final void setDownedTimer(short downedTimer) {
        this.downedTimer = downedTimer;
    }

    public final byte policyOrdinal() {
        return policyOrdinal;
    }

    public final void setPolicyOrdinal(byte policyOrdinal) {
        this.policyOrdinal = policyOrdinal;
    }

    public final TargetKind targetKind() {
        return targetKind;
    }

    public final int targetKey() {
        return targetKey;
    }

    public final void setTarget(TargetKind kind, int key) {
        this.targetKind = kind;
        this.targetKey = key;
    }

    public final short policyTimer() {
        return policyTimer;
    }

    public final void setPolicyTimer(short policyTimer) {
        this.policyTimer = policyTimer;
    }

    public final int anchorCell() {
        return anchorCell;
    }

    public final void setAnchorCell(int anchorCell) {
        this.anchorCell = anchorCell;
    }

    public final int ownerId() {
        return ownerId;
    }

    public final void setOwnerId(int ownerId) {
        this.ownerId = ownerId;
    }

    public final int homeId() {
        return homeId;
    }

    public final void setHomeId(int homeId) {
        this.homeId = homeId;
    }

    public final short jobOrdinal() {
        return jobOrdinal;
    }

    public final void setJobOrdinal(short jobOrdinal) {
        this.jobOrdinal = jobOrdinal;
    }

    public final GoalState goalState() {
        return goalState;
    }

    public final void setGoalState(GoalState goalState) {
        this.goalState = goalState;
    }

    public final TargetKind goalTargetKind() {
        return goalTargetKind;
    }

    public final int goalTargetKey() {
        return goalTargetKey;
    }

    public final void setGoalTarget(TargetKind kind, int key) {
        this.goalTargetKind = kind;
        this.goalTargetKey = key;
    }

    public final short goalProgress() {
        return goalProgress;
    }

    public final void setGoalProgress(short goalProgress) {
        this.goalProgress = goalProgress;
    }

    public final int goalCooldown() {
        return goalCooldown;
    }

    public final void setGoalCooldown(int goalCooldown) {
        this.goalCooldown = goalCooldown;
    }

    public final int goalWorkTicks() {
        return goalWorkTicks;
    }

    public final void setGoalWorkTicks(int goalWorkTicks) {
        this.goalWorkTicks = goalWorkTicks;
    }

    public final long heldUntilTick() {
        return heldUntilTick;
    }

    public final void setHeldUntilTick(long heldUntilTick) {
        this.heldUntilTick = heldUntilTick;
    }

    public final byte offenseCount() {
        return offenseCount;
    }

    public final void setOffenseCount(byte offenseCount) {
        this.offenseCount = offenseCount;
    }

    /** The prison cell assigned at arrest (Phase-2 STEP C), or {@link #NONE} when free. */
    public final int assignedHoldCell() {
        return assignedHoldCell;
    }

    /** Stamps (at arrest) or clears (with {@link #NONE}, at release) the assigned prison cell. */
    public final void setAssignedHoldCell(int cell) {
        this.assignedHoldCell = cell;
    }

    /** The offender this guard is actively apprehending, or {@link #NONE} (law &amp; order pass). */
    public final int apprehendTargetId() {
        return apprehendTargetId;
    }

    /** Locks (or, with {@link #NONE}, closes) this guard's active apprehension case. */
    public final void setApprehendTargetId(int actorId) {
        this.apprehendTargetId = actorId;
    }

    /** Absolute expiry tick of this actor's move-along warning (meaningful while MOVE_ALONG). */
    public final long moveAlongUntilTick() {
        return moveAlongUntilTick;
    }

    /** Stamps the move-along warning's absolute expiry tick (law &amp; order pass). */
    public final void setMoveAlongUntilTick(long tick) {
        this.moveAlongUntilTick = tick;
    }

    /** The absolute tick of this actor's last shove, as pusher or pushee (density revisit). */
    public final long lastPushTick() {
        return lastPushTick;
    }

    /** Stamps the shove-cooldown clock ({@link PushMechanics} commit, and serializer load). */
    public final void setLastPushTick(long tick) {
        this.lastPushTick = tick;
    }

    /** The absolute end tick of this actor's house arrest (meaningful while HOUSE_ARREST). */
    public final long houseArrestUntilTick() {
        return houseArrestUntilTick;
    }

    /** Stamps the house-arrest sentence's absolute end tick (shove-riot correction). */
    public final void setHouseArrestUntilTick(long tick) {
        this.houseArrestUntilTick = tick;
    }

    /** The absolute tick until which {@link BeastHuntPolicy} acquisition is suppressed. */
    public final long huntBackoffUntilTick() {
        return huntBackoffUntilTick;
    }

    /** Stamps the futile-chase backoff's absolute end tick ({@link BeastHuntPolicy}). */
    public final void setHuntBackoffUntilTick(long tick) {
        this.huntBackoffUntilTick = tick;
    }

    /** The pending Play-mode step target, or {@link #NONE} (PLAY-MODE-SPEC.md §5.2). */
    public final int playerMoveTargetCell() {
        return playerMoveTargetCell;
    }

    /** Sets (or clears, with {@link #NONE}) the pending Play-mode step target. */
    public final void setPlayerMoveTarget(int cell) {
        this.playerMoveTargetCell = cell;
    }

    /** The pending Play-mode pickpocket target actor id, or {@link #NONE} (Sprint 2). */
    public final int playerPickpocketTargetId() {
        return playerPickpocketTargetId;
    }

    /**
     * Sets (or clears, with {@link #NONE}) the pending Play-mode pickpocket intent: the
     * observer's input layer names an ADJACENT actor to lift from, and the next
     * {@code PlayerControlPolicy.act} resolves the {@link TheftMechanics#pickpocket}
     * contest and consumes the intent — the {@link #setPlayerMoveTarget} contract.
     */
    public final void setPlayerPickpocketTarget(int actorId) {
        this.playerPickpocketTargetId = actorId;
    }

    /** The pending Play-mode talk target actor id, or {@link #NONE} (Sprint 3 quests). */
    public final int playerTalkTargetId() {
        return playerTalkTargetId;
    }

    /**
     * Sets (or clears, with {@link #NONE}) the pending Play-mode talk intent: the
     * observer's input layer names an ADJACENT actor being talked to, and the next
     * {@code PlayerControlPolicy.act} validates reach, notes the talk into the quest
     * log and consumes the intent — the {@link #setPlayerMoveTarget} contract.
     */
    public final void setPlayerTalkTarget(int actorId) {
        this.playerTalkTargetId = actorId;
    }

    /** Whether a Play-mode eat/buy-a-meal intent is pending this tick (Sprint 4). */
    public final boolean playerEatIntent() {
        return playerEatIntent;
    }

    /**
     * Arms (or clears) the pending Play-mode eat intent: the observer's input layer sets
     * it on the eat keypress, and the next {@code PlayerControlPolicy.act} resolves it
     * through {@code SeekFoodPolicy}'s eat-in-reach chain and consumes it — the
     * {@link #setPlayerMoveTarget} contract.
     */
    public final void setPlayerEatIntent(boolean intent) {
        this.playerEatIntent = intent;
    }

    public final ReasonCode lastReasonCode() {
        return lastReasonCode;
    }

    public final void setLastReasonCode(ReasonCode reasonCode) {
        this.lastReasonCode = reasonCode;
    }
}
