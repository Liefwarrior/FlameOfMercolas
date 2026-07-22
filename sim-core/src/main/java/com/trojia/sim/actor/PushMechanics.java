package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * The shove verb (density revisit, Eli's push mechanic): when a mover's step is blocked ONLY by
 * the occupancy cap (walkable floor, leash fine, but somebody is standing there), it may — on a
 * {@link #PUSH_COOLDOWN_TICKS}-tick cooldown — displace the occupant to a deterministic adjacent
 * free walkable cell and take the vacated square. Stateless static verbs (the
 * {@link PathFinder}/{@code JobBehaviors} precedent), called from {@link ActorsSystem}'s live
 * occupancy view via {@link Actor.OccupancyQuery#tryPush}.
 *
 * <p><b>No push chains, by construction and by stagger.</b> The displaced occupant is only ever
 * moved into a cell that is already FREE (occupancy 0), so one shove can never mechanically
 * cascade into another. Additionally the displacement stamps the pushee's own {@code
 * lastPushTick} (being shoved staggers you), so a just-displaced actor cannot itself shove
 * anyone this tick — or for a full cooldown — closing the chain-reaction loophole where a
 * displaced actor's own later-in-tick move starts a conga line.
 *
 * <p><b>Determinism.</b> Ascending fixed-order direction scan for the displacement target
 * (W, E, N, S, then the four diagonals — {@link PathFinder}'s neighbor order), ascending-id
 * registry scan for the occupant, absolute-tick cooldown arithmetic, no draws.
 */
public final class PushMechanics {

    /** Cooldown between successful shoves by (or against) one actor: 10 ticks = 10 seconds. */
    public static final long PUSH_COOLDOWN_TICKS = 10;

    /** Fixed displacement-scan order — W, E, N, S, then diagonals (PathFinder's neighbor order). */
    private static final int[] DX = {-1, 1, 0, 0, -1, 1, -1, 1};
    private static final int[] DY = {0, 0, -1, 1, -1, -1, 1, 1};

    private PushMechanics() {
    }

    /**
     * Attempts the shove: {@code pusher} wants {@code contestedCell} (already checked walkable,
     * leash-legal, and at the occupancy cap by {@link Actor}'s step gate). Returns {@code true}
     * iff the occupant was displaced — the contested cell is then free and the caller commits
     * the pusher's step onto it.
     *
     * <ol>
     *   <li><b>Cooldown</b>: {@code tick - pusher.lastPushTick() >= PUSH_COOLDOWN_TICKS}.</li>
     *   <li><b>Occupant</b>: the lowest-id actor standing on {@code contestedCell}.</li>
     *   <li><b>Displacement cell</b>: the first adjacent same-z cell in the fixed scan order
     *       that is walkable, not a solid-corner diagonal cut (PathFinder parity — a shove must
     *       never seal an actor into an A*-unreachable pocket), and currently FREE (occupancy 0
     *       — the cap is never violated, and no chain push can occur).</li>
     *   <li><b>Commit</b>: pushee moves ({@code occ.onEnter} keeps the shared index live), its
     *       cached route is invalidated (force replan around wherever it now stands), both
     *       parties' {@code lastPushTick} stamp to {@code tick} (cooldown + stagger), and the
     *       shove is recorded in {@code log} for riot detection.</li>
     * </ol>
     */
    public static boolean tryPush(Actor pusher, int contestedCell, ActorRegistry registry,
            long tick, Actor.WalkabilityQuery walk, Actor.OccupancyQuery occ, ShoveLog log) {
        if (tick - pusher.lastPushTick() < PUSH_COOLDOWN_TICKS) {
            return false; // still winded from the last shove (or from being shoved)
        }
        Actor pushee = occupantOf(contestedCell, pusher, registry);
        if (pushee == null) {
            return false; // defensive: the index said full but no actor stands there
        }
        int dest = displacementCell(contestedCell, walk, occ);
        if (dest == Actor.NONE) {
            return false; // nowhere to shove them: the step stays blocked, like a wall
        }
        pushee.setCell(dest);
        occ.onEnter(contestedCell, dest);
        pushee.invalidateRoute();
        pushee.setLastPushTick(tick);  // stagger: a just-shoved actor cannot shove this tick
        pusher.setLastPushTick(tick);
        log.record(tick, contestedCell, pusher.id());
        return true;
    }

    /** The lowest-id actor (never {@code pusher}) standing on {@code cell}, or {@code null}. */
    private static Actor occupantOf(int cell, Actor pusher, ActorRegistry registry) {
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (actor.id() != pusher.id() && actor.cell() == cell) {
                return actor;
            }
        }
        return null;
    }

    /**
     * First adjacent free walkable same-z cell of {@code contestedCell} in the fixed scan order,
     * or {@link Actor#NONE}. Diagonal displacements are flank-checked against walls (never cut a
     * solid diagonal corner — movement/pathfinding parity), and only a cell with occupancy 0
     * qualifies, so the cap holds and the displaced actor can always route back out.
     */
    private static int displacementCell(int contestedCell, Actor.WalkabilityQuery walk,
            Actor.OccupancyQuery occ) {
        int x = PackedPos.x(contestedCell);
        int y = PackedPos.y(contestedCell);
        int z = PackedPos.z(contestedCell);
        for (int d = 0; d < DX.length; d++) {
            int nx = x + DX[d];
            int ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx > PackedPos.X_MASK || ny > PackedPos.Y_MASK) {
                continue;
            }
            int cell = PackedPos.pack(nx, ny, z);
            if (!walk.isWalkable(cell)) {
                continue;
            }
            if (DX[d] != 0 && DY[d] != 0
                    && (!walk.isWalkable(PackedPos.pack(nx, y, z))
                            || !walk.isWalkable(PackedPos.pack(x, ny, z)))) {
                continue; // never shove someone through a solid diagonal wall corner
            }
            if (occ.occupantsAt(cell) > 0) {
                continue; // only a FREE cell: the cap holds and no push chain can start
            }
            return cell;
        }
        return Actor.NONE;
    }
}
