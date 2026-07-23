package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorGeometry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.world.PackedPos;

/**
 * The one shared "who is within reach" rule for Play mode's adjacency verbs (Sprint 2 talk +
 * pickpocket): the LOWEST-id actor standing within Chebyshev reach 1 of the played actor on
 * the same z-level. Lowest-id is the district's universal tiebreak (the {@code ActorPicker} /
 * {@code TheftMechanics.nearestMark} convention), so the verb target is deterministic and
 * arguable-about after the fact. GL-free.
 */
public final class AdjacentTargets {

    /** Chebyshev reach of an adjacency verb — mirrors {@code TheftMechanics.PICKPOCKET_REACH}. */
    public static final int REACH = 1;

    private AdjacentTargets() {
    }

    /**
     * The lowest-id actor adjacent to {@code selfId} (same z, Chebyshev &le; {@link #REACH},
     * never self), or {@link Actor#NONE}. {@code includeExecuted} keeps gibbeted bodies
     * targetable — the TALK verb wants them (the {@code mood.dead} table is authored: the
     * gibbet does not greet, but it is very much part of the ward's conversation); the
     * PICKPOCKET verb excludes them ({@code TheftMechanics} refuses executed victims, so
     * offering one would be a dead-end intent).
     */
    public static int lowestIdAdjacent(ActorRegistry registry, int selfId,
            boolean includeExecuted) {
        int selfCell = registry.get(selfId).cell();
        int selfZ = PackedPos.z(selfCell);
        for (int i = 0; i < registry.size(); i++) {
            if (i == selfId) {
                continue;
            }
            Actor other = registry.get(i);
            if (!includeExecuted && other.hasStatus(StatusBit.EXECUTED)) {
                continue;
            }
            if (PackedPos.z(other.cell()) != selfZ
                    || ActorGeometry.chebyshev(selfCell, other.cell()) > REACH) {
                continue;
            }
            return i;
        }
        return Actor.NONE;
    }
}
