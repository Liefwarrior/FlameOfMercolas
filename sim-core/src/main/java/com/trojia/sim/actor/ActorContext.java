package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;

/**
 * The per-tick facade handed to every {@link Actor#tick} and
 * {@link BehaviorPolicy} (ACTORS-SPEC.md §2). Mirrors
 * {@code com.trojia.sim.engine.TickContext}'s shape but scoped to the actor
 * system: actors never touch world lanes directly (§2.3), so this exposes
 * only actor-owned registries, named-draw access, and the small set of
 * cross-actor queries the starter policy library needs.
 */
public interface ActorContext {

    /** The tick currently being simulated. */
    long tick();

    /** The one persisted RNG seed (ARCHITECTURE.md §1.1 #16). */
    long worldSeed();

    /** The actor registry (ascending-id iteration, spatial/home/job lookups). */
    ActorRegistry registry();

    /** The Home side-table (ACTORS-SPEC.md §11.1). */
    HomeRegistry homes();

    /** The relationship side-table (ACTORS-SPEC.md §11.3). */
    RelationshipRegistry relationships();

    /** The ItemsLite side-table (ACTORS-SPEC.md §2.6, §11.2). */
    ItemsLiteRegistry items();

    /**
     * The Royals ledger (Phase-0 economy F2). The world-less bootstrap returns a degraded, empty
     * {@link BankLedger} (no accounts opened) — the {@code arrestHoldCell == NONE} analogue.
     */
    BankLedger bankAccounts();

    /**
     * The baked restricted-zone side-table (Phase-0 job/access F3), injected like {@code
     * arrestHoldCell}. {@link RestrictedZoneTable#EMPTY} where no zones are wired (Phase 0's live
     * district, and the world-less bootstrap).
     */
    RestrictedZoneTable restrictedZones();

    /** The bound Job taxonomy (ACTORS-SPEC.md §10.2). */
    JobRegistry jobs();

    /**
     * The job an actor is PRESENTING (Phase-0 job/access F3) — the exact inverse of {@link
     * #wielderId()}: resolve {@code actor.identity().presentedId()} to that actor's {@code
     * jobOrdinal} and return its bound {@link Job}; return the actor's own job when it is not
     * disguised. A live registry read (no cache), so a {@code setActAs} takes effect immediately.
     * {@code null} if the resolved actor holds no job.
     *
     * <p>Every access gate must read THIS, never {@code actor.jobOrdinal()} directly — reading the
     * true job under a disguise is a bypass bug (PLAY-MODE §4). Guarded against an out-of-range
     * presented id (a stale/never-spawned target resolves as "self").
     */
    default Job presentedJob(Actor actor) {
        int presentedId = actor.identity().presentedId();
        Actor presented = (presentedId == actor.id()
                || presentedId < 0 || presentedId >= registry().size())
                ? actor
                : registry().get(presentedId);
        short ordinal = presented.jobOrdinal();
        return ordinal >= 0 ? jobs().get(ordinal) : null;
    }

    /**
     * Access gate (Phase-0 job/access F3): {@code true} iff the actor's PRESENTED job satisfies
     * {@code zone}'s required job. A Farmer presenting as a Guard passes a Guard-only zone and
     * fails a Farmer-only zone. Reads {@link #presentedJob(Actor)} — never the true job.
     */
    default boolean canAccess(Actor actor, RestrictedZone zone) {
        Job presented = presentedJob(actor);
        return presented != null && presented.id().equals(zone.requiredJob());
    }

    /**
     * One named draw (ACTORS-SPEC.md §2.2): {@code spatialKey = actorId}
     * always; {@code drawIndex} is the per-actor per-tick counter the caller
     * maintains (shared across every stream, the pinned attribution rule).
     */
    long draw(ActorRngStream stream, int actorId, int drawIndex);

    /**
     * The next draw index for {@code actorId} this tick, incrementing the
     * shared per-actor counter (ACTORS-SPEC.md §2.2's "one counter per actor
     * shared across all streams"). Convenience over tracking indices by hand.
     */
    int nextDrawIndex(int actorId);

    /**
     * The cell of the actor currently presenting as the Wielder (the first
     * bound {@code Job.FlameOfMerc} holder, ascending id), or
     * {@link Actor#NONE} if none is spawned. Backs {@code DEFER_WIELDER}
     * (§1.3) — deference reads the PRESENTED identity per the DECISIONS
     * seam, so a disguised Wielder (presented != true) does not trigger it.
     */
    int wielderCell();

    /** The actor id backing {@link #wielderCell()}, or {@link Actor#NONE} if none is spawned. */
    int wielderId();

    /**
     * The one well-known arrest holding-cell (ARREST-SPEC addendum): the walkable floor cell
     * beside the K34 Guardhouse's cage bars, wired in at scenario-bake time (mirrors how
     * {@link #wielderCell()}/homes are populated) — a fixed baked cell, not a spatial query.
     * {@link Actor#NONE} for scenarios/tests that never wire one (e.g. the world-less
     * bootstrap): {@link HeldPolicy} degrades to "hold in place" when this is unset.
     */
    int arrestHoldCell();

    /**
     * The bank vault chest cell (Phase-2 STEP B), holding the single COIN stack that backs every
     * Royal ({@code countOnCellOfKind(vaultChestCell, COIN) == bankAccounts().totalRoyals()}).
     * {@link Actor#NONE} where no live bank is wired (world-less bootstrap, economy-free tests) —
     * {@link BankVerbs} deposit/withdraw degrade to no-ops.
     */
    default int vaultChestCell() {
        return Actor.NONE;
    }

    /**
     * The banker's counter cell (Phase-2 STEP B): deposit/withdraw fire when a citizen with an ID
     * reaches it. {@link Actor#NONE} where no live bank is wired.
     */
    default int bankerCell() {
        return Actor.NONE;
    }

    /**
     * The bank's deterministic waiting queue (Phase-2 STEP B). {@link BankQueue#EMPTY} where no
     * live bank is wired.
     */
    default BankQueue bankQueue() {
        return BankQueue.EMPTY;
    }

    /**
     * The baked multi-cell prison registry (Phase-2 STEP C): the K34 holding cells an arrest
     * assigns from (lowest-free, ascending). {@link PrisonCellRegistry#EMPTY} where none are wired
     * — custody then falls back to the single {@link #arrestHoldCell()} (or "hold in place").
     */
    default PrisonCellRegistry prisonCells() {
        return PrisonCellRegistry.EMPTY;
    }

    /**
     * The baked FOOD-distribution side-table (the economy-loop pass): the vending z:+11 shops,
     * the free commons cells, and the guaranteed home larders {@link SeekFoodPolicy} consults and
     * the periodic import restocks. {@link FoodMarket#EMPTY} where no live district is wired
     * (world-less bootstrap, economy-free tests) — the eat machine then finds no shop/commons and
     * every hungry actor falls to the home-larder / starve branch.
     */
    default FoodMarket foodMarket() {
        return FoodMarket.EMPTY;
    }

    /**
     * Records {@code n} units of FOOD minted at runtime (a farm work-unit yield) for the
     * closed-supply conservation proof {@code minted == held(live) + eaten}. Pure accounting —
     * read by no behavior, so it changes no determinism property; a no-op where unwired.
     */
    default void recordFoodMinted(int n) {
    }

    /** Records {@code n} units of FOOD eaten (sunk) for the conservation proof; pure accounting. */
    default void recordFoodEaten(int n) {
    }

    /**
     * Pure read (§2.3 "actors never write lanes"): {@code true} if {@code cell}
     * is safe to step onto right now ({@code com.trojia.sim.world.Walkability
     * .isWalkable}). Systems bound to no world (the headless world-less
     * bootstrap, {@code ActorsDemoMain}) return {@code true} unconditionally —
     * there is nothing to collide with.
     */
    boolean isWalkable(int cell);

    /**
     * The live actor-actor occupancy view for this tick (the "only 2 to a cell" cap, {@link
     * Actor#MAX_OCCUPANTS_PER_CELL}). {@link ActorsSystem} rebuilds a shared {@link
     * OccupancyIndex} from every actor's cell before ticking and returns a view whose {@code
     * occupantsAt} reads it and whose {@code onEnter} keeps it live as actors move; the world-less
     * bootstrap and test doubles return {@link Actor.OccupancyQuery#UNLIMITED} (no cap). Movement
     * call sites pass this into {@code stepToward}/{@code stepAlongRoute}.
     */
    Actor.OccupancyQuery occupancy();
}
