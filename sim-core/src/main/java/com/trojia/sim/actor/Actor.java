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
     * REST reserve regained per tick while standing on the home cell (a "sleep"
     * recovery, §11.1). Chosen above every type's REST {@code decayPerKilotick}
     * so a night at home refills REST and the actor heads back out next day —
     * see {@link #recoverRestAtHome}.
     */
    private static final int REST_RECOVERED_PER_TICK_AT_HOME = 6;

    /**
     * HUNGER reserve regained per tick while standing on the home cell — the
     * {@code SEEK_FOOD} counterpart to {@link #REST_RECOVERED_PER_TICK_AT_HOME}
     * (the needs-hierarchy pass, ACTORS-SPEC.md §3.3). Chosen with the same
     * "comfortably outpaces every type's worst-case decay" headroom style
     * (~6x the Harbor Gull's worst-case 2.0/tick HUNGER decay, versus REST's
     * constant's ~7.5x margin over the Dock Dog's 0.8/tick REST decay) — see
     * {@link #recoverHungerAtHome}.
     */
    private static final int HUNGER_RECOVERED_PER_TICK_AT_HOME = 12;

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

    // ---- inventory-lite (§1.1, §11.2 quantity rides ItemsLiteRegistry, not here) ----
    private final short[] inventory;
    private byte inventoryCount;

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
        this.inventory = new short[stats.inventoryCap()];
    }

    /** Returns the type's static policy stack constant (§1.4). Never {@code null}. */
    protected abstract PolicyStack policies();

    // ======================================================================
    // The tick entry point (FINAL — the template is engine-owned, §1.1)
    // ======================================================================

    public final void tick(ActorContext ctx) {
        decayNeeds();
        recoverRestAtHome(ctx);
        recoverHungerAtHome(ctx);
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

    /**
     * Sleeping at home also restores HUNGER — the {@code SEEK_FOOD} daily-cycle
     * counterpart to {@link #recoverRestAtHome} (the needs-hierarchy pass,
     * ACTORS-SPEC.md §3.3). Deliberate scope cut: no food-establishment routing
     * exists (no economy machinery to consume/track a meal, and no sim-core-
     * visible tagging of which map fixtures count as "food" — that's
     * observer/tools-layer content, and sim-core must not import client/tools).
     * Recovering at home is symmetric with REST, ships with zero new plumbing,
     * and closes the same "actor never satisfies HUNGER, freezes seeking food
     * forever" loop that {@link #recoverRestAtHome} closes for REST.
     */
    private void recoverHungerAtHome(ActorContext ctx) {
        if (homeId == NONE) {
            return;
        }
        if (cell == ctx.homes().get(homeId).homeCell()) {
            applyNeedDelta(Need.HUNGER, HUNGER_RECOVERED_PER_TICK_AT_HOME);
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
        stepToward(targetCell, ignoresLeash, ALWAYS_WALKABLE);
    }

    /** Convenience overload: leash-respecting step (the common case), no world lookup. */
    public final void stepToward(int targetCell) {
        stepToward(targetCell, false, ALWAYS_WALKABLE);
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
    public final void stepToward(int targetCell, boolean ignoresLeash, WalkabilityQuery walk) {
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
        if (tryStep(dx, dy, z, ignoresLeash, walk)) {
            return; // primary step (diagonal or straight)
        }
        if (dx != 0 && dy != 0) {
            // Diagonal blocked -> wall-slide: try the two orthogonal component steps.
            if (tryStep(dx, 0, z, ignoresLeash, walk)) {
                return;
            }
            if (tryStep(0, dy, z, ignoresLeash, walk)) {
                return;
            }
        }
        // Every candidate blocked: deterministic no-op (§2.5).
    }

    /**
     * Attempts one candidate step {@code (dx, dy)} from the current cell on
     * z-level {@code z}: rejects it if {@code walk} reports it unwalkable,
     * then applies the existing leash math; commits {@code cell} and facing
     * only if both checks pass. Returns whether the step was committed.
     */
    private boolean tryStep(int dx, int dy, int z, boolean ignoresLeash, WalkabilityQuery walk) {
        int x = PackedPos.x(cell);
        int y = PackedPos.y(cell);
        int stepped = PackedPos.pack(x + dx, y + dy, z);
        if (!walk.isWalkable(stepped)) {
            return false;
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
        cell = stepped;
        if (dx != 0) {
            facing = (byte) (dx < 0 ? Dir.WEST.ordinal() : Dir.EAST.ordinal());
        } else if (dy != 0) {
            facing = (byte) (dy < 0 ? Dir.NORTH.ordinal() : Dir.SOUTH.ordinal());
        }
        return true;
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

    public final ReasonCode lastReasonCode() {
        return lastReasonCode;
    }

    public final void setLastReasonCode(ReasonCode reasonCode) {
        this.lastReasonCode = reasonCode;
    }

    // ---- inventory-lite (item ids only; ItemsLiteRegistry §2.6/§11.2 holds the entries) ----

    public final byte inventoryCount() {
        return inventoryCount;
    }

    public final short inventoryItemAt(int slot) {
        return inventory[slot];
    }

    /** Appends an item id at the next free slot (canonical pickup order, §1.1); {@code false} if full. */
    public final boolean addInventoryItem(short itemId) {
        if (inventoryCount >= inventory.length) {
            return false;
        }
        inventory[inventoryCount++] = itemId;
        return true;
    }
}
