package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

import java.util.ArrayList;
import java.util.List;

/**
 * The deterministic household/employment bake pass (ACTORS-SPEC.md §11.4).
 * Runs once, after every actor in the scenario has an ActorId and a
 * {@code homeId} of {@link Actor#NONE} — the addendum's "one more sub-pass at
 * the end of" the existing spawn-bake order.
 *
 * <p><strong>Scope trim (documented):</strong> §11.4 step 1 ("authored homes
 * first, draw-free, from the map's {@code schedule_anchor} prop") requires
 * the Tiled importer/ActorSeeder, which does not exist yet in this codebase
 * (M1 shipped the world importer, not the actor spawn-table one) — this
 * foundation milestone implements steps 2-4 (crowd-filler households,
 * employer/employee, flavor edges) over whatever actor list the caller
 * passes in, all still un-anchored. A later milestone wires step 1 in front
 * of this pass without changing its contract.
 */
public final class HouseholdFormer {

    /** The tick value bake-time named draws are addressed at (before tick 1). */
    public static final long BAKE_TICK = 0L;

    private HouseholdFormer() {
    }

    /**
     * §11.4 step 2: groups un-housed actors (ascending ActorId) into
     * households of a named-drawn size, one shared {@link Home} per group at
     * the group leader's current cell, with a {@code HOUSEHOLD} edge between
     * every pair.
     */
    public static void formHouseholds(List<Actor> unhoused, HomeRegistry homes,
            RelationshipRegistry relationships, long worldSeed, HouseholdRaws raws) {
        int i = 0;
        while (i < unhoused.size()) {
            Actor leader = unhoused.get(i);
            long draw = NamedDraws.draw(ActorRngStream.HOUSEHOLD_SIZE, worldSeed, BAKE_TICK,
                    leader.id(), 0);
            int size = NamedDraws.weightedPick(draw, raws.householdSizeWeights()) + 1;
            int groupSize = Math.min(size, unhoused.size() - i);
            List<Actor> group = unhoused.subList(i, i + groupSize);

            int homeId = homes.addHome(leader.cell());
            for (Actor member : group) {
                member.setHomeId(homeId);
            }
            for (int a = 0; a < group.size(); a++) {
                for (int b = a + 1; b < group.size(); b++) {
                    relationships.addSymmetric(group.get(a).id(), group.get(b).id(),
                            RelationshipKind.HOUSEHOLD);
                }
            }
            i += groupSize;
        }
    }

    /**
     * §11.4 step 3: draws a staff count and hires the nearest unassigned
     * candidates by {@code (Chebyshev distance, actorId asc)}, emitting one
     * {@code EMPLOYER} edge (employer -&gt; hire) per hire.
     *
     * @return the hired actor ids, in hire order
     */
    public static List<Integer> formEmployment(Actor employer, List<Actor> candidates,
            RelationshipRegistry relationships, long worldSeed, HouseholdRaws raws) {
        long draw = NamedDraws.draw(ActorRngStream.HOUSEHOLD_STAFF_COUNT, worldSeed, BAKE_TICK,
                employer.id(), 0);
        int range = raws.staffCountMax() - raws.staffCountMin() + 1;
        int staffCount = raws.staffCountMin() + (int) Long.remainderUnsigned(draw, range);
        staffCount = Math.min(staffCount, candidates.size());

        List<Actor> ranked = new ArrayList<>(candidates);
        ranked.sort((a, b) -> {
            int da = PackedPos.z(a.cell()) == PackedPos.z(employer.cell())
                    ? chebyshev(a.cell(), employer.cell()) : Integer.MAX_VALUE;
            int db = PackedPos.z(b.cell()) == PackedPos.z(employer.cell())
                    ? chebyshev(b.cell(), employer.cell()) : Integer.MAX_VALUE;
            if (da != db) {
                return Integer.compare(da, db);
            }
            return Integer.compare(a.id(), b.id());
        });

        List<Integer> hired = new ArrayList<>(staffCount);
        for (int i = 0; i < staffCount; i++) {
            Actor hire = ranked.get(i);
            relationships.addDirected(employer.id(), hire.id(), RelationshipKind.EMPLOYER);
            hired.add(hire.id());
        }
        return hired;
    }

    /**
     * The §4.5 Priest&harr;Disciple invariant pairing: gets its
     * {@code EMPLOYER} edge for free, no draw consumed (§11.4 step 3).
     */
    public static void bindMentorPairFree(Actor priest, Actor disciple,
            RelationshipRegistry relationships) {
        relationships.addDirected(priest.id(), disciple.id(), RelationshipKind.EMPLOYER);
    }

    private static int chebyshev(int cellA, int cellB) {
        return ActorGeometry.chebyshev(cellA, cellB);
    }
}
