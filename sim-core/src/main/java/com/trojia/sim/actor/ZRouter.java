package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * The pure cross-z hop planner (Sprint 4 "the climb"): given an actor's cell, a target
 * on a DIFFERENT z, and the baked {@link ZLinkTable}, resolves the single next hop of
 * the climb — a stateless, draw-free pure function, recomputed per tick (the table is
 * tiny), so no route state is ever persisted and a save/load mid-climb resumes
 * byte-identically for free.
 *
 * <p><b>The plan shape.</b> Connectors only ever join adjacent z-levels, so a cross-z
 * route is a monotonic band-by-band climb (or descent): walk a same-z leg to the near
 * endpoint of a chosen connector, commit the vertical step to its far endpoint, repeat
 * on the new band. {@link #nextHop} returns exactly one of:
 * <ul>
 *   <li>a <b>same-z cell</b> — the chosen connector's near endpoint, to be walked to
 *       with the ordinary cached-A* leg machinery ({@link Actor#stepAlongRoute});</li>
 *   <li>a <b>cross-z cell</b> — the connector's far endpoint, returned when the actor
 *       already STANDS on the near endpoint (the caller commits it via
 *       {@link Actor#tryStepVertical});</li>
 *   <li>{@link Actor#NONE} — some band crossing on the monotonic path has no baked
 *       connector at all: the climb is unroutable by construction.</li>
 * </ul>
 *
 * <p><b>Connector choice (deterministic).</b> Among the links crossing the actor's
 * current band in the needed direction, the one minimizing
 * {@code chebyshev(cell, near) + chebyshev(far, target)} wins (x/y distance only — the
 * same plane geometry every mover uses); ties break to the LOWEST baked link index (the
 * fixed extract scan order). Pure integer math, no draws, no state.
 *
 * <p><b>Honest limitation (declared).</b> The planner assumes a same-z leg to the chosen
 * near endpoint is walkable-reachable; when it is not (a connector across water, a walled
 * pocket), the leg's own bounded A* fails and cools down exactly like any unreachable
 * same-z target — the actor retries on the existing 500-tick cadence rather than
 * replanning around the bad connector. The bake-time reachability audit is the tool that
 * keeps such connectors out of authored content.
 */
public final class ZRouter {

    private ZRouter() {
    }

    /**
     * The next hop of the climb from {@code cell} toward {@code targetCell} (see the
     * class doc for the three outcomes). {@code cell} and {@code targetCell} must be on
     * different z-levels; same-z inputs return {@link Actor#NONE} (that plane's routing
     * belongs to the ordinary planner).
     */
    public static int nextHop(int cell, int targetCell, ZLinkTable links) {
        int z = PackedPos.z(cell);
        int targetZ = PackedPos.z(targetCell);
        if (z == targetZ || links.isEmpty()) {
            return Actor.NONE;
        }
        // Feasibility: every band crossing of the monotonic path needs at least one link.
        int loZ = Math.min(z, targetZ);
        int hiZ = Math.max(z, targetZ);
        for (int zi = loZ; zi < hiZ; zi++) {
            if (!links.anyLinkAtZ(zi)) {
                return Actor.NONE; // a whole band gap has no connector: unroutable
            }
        }
        boolean climbing = targetZ > z;
        int crossingZ = climbing ? z : z - 1; // links are keyed by their LOW endpoint's z
        int bestNear = Actor.NONE;
        int bestFar = Actor.NONE;
        int bestCost = Integer.MAX_VALUE;
        for (int i = 0; i < links.linkCount(); i++) {
            int lowCell = links.low(i);
            if (PackedPos.z(lowCell) != crossingZ) {
                continue;
            }
            int highCell = links.high(i);
            int near = climbing ? lowCell : highCell;
            int far = climbing ? highCell : lowCell;
            int cost = ActorGeometry.chebyshev(cell, near)
                    + ActorGeometry.chebyshev(far, targetCell);
            if (cost < bestCost) { // strict <: ties keep the lowest baked index
                bestCost = cost;
                bestNear = near;
                bestFar = far;
            }
        }
        if (bestNear == Actor.NONE) {
            return Actor.NONE; // defensive: anyLinkAtZ above makes this unreachable
        }
        return cell == bestNear ? bestFar : bestNear;
    }
}
