package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.RestrictedZoneTable;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The ward's night roster, pinned at the bake.
 *
 * <p>Eli's ruling: keep most of the Watch home at night — that is the oscillation fix and it
 * must survive — but leave a SMALL shift genuinely on the streets, so being out after dark
 * carries real but reduced risk. Both halves of that are assertable, and both are asserted
 * here, because "skeleton" degrades in two opposite directions and each has already bitten
 * this sprint once: too many rostered and the night oscillation comes back; none rostered and
 * arrest-by-exposure is a daytime-only mechanic again.
 */
class DocksNightRosterBakeTest {

    /** Small enough to still be a skeleton; enough to leave a lamp lit in three quarters. */
    private static final int ROSTER_MIN = 2;
    private static final int ROSTER_MAX = 5;

    private static DocksPopulation population;
    private static int nightOrdinal;
    private static int patrolOrdinal;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        nightOrdinal = population.jobs().ordinalOf(Job.Watch.NightWatch.ID);
        patrolOrdinal = population.jobs().ordinalOf(Job.Watch.Patrol.ID);
    }

    private static List<Integer> rosterIds() {
        ActorRegistry registry = population.registry();
        List<Integer> ids = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            if (registry.get(i).jobOrdinal() == nightOrdinal) {
                ids.add(i);
            }
        }
        return ids;
    }

    @Test
    void theNightRosterIsSmallAndRealAndAllWatch() {
        List<Integer> roster = rosterIds();
        assertTrue(roster.size() >= ROSTER_MIN,
                "the ward must not be unpoliced after dark -- night roster is " + roster.size());
        assertTrue(roster.size() <= ROSTER_MAX,
                "a SKELETON watch: rostering the whole Watch at night is not the fix, it is"
                        + " the old bug with the clock turned over -- night roster is "
                        + roster.size());
        for (int id : roster) {
            assertEquals("militia_watch", population.registry().get(id).typeId().key(),
                    "actor#" + id + " is on the night roster but is not a Watch");
        }
    }

    @Test
    void mostOfTheWatchStillGoesHomeAtDusk() {
        ActorRegistry registry = population.registry();
        int watch = 0;
        int day = 0;
        for (int i = 0; i < registry.size(); i++) {
            if (!registry.get(i).typeId().key().equals("militia_watch")) {
                continue;
            }
            watch++;
            if (registry.get(i).jobOrdinal() == patrolOrdinal) {
                day++;
            }
        }
        assertEquals(watch, day + rosterIds().size(),
                "every Watch is on exactly one of the two shifts");
        assertTrue(day * 2 > watch,
                "the day shift must remain the clear majority (" + day + " of " + watch + ")");
    }

    @Test
    void noRosteredSoulLivesOrBeatsInsideAJobGatedRestrictedZone() {
        // The K34 guardhouse interior, the bank vault and the bank hall are all gated on
        // watch.patrol specifically. Moving a soul that lives or works inside one of them onto
        // a different job would turn a guard into a trespasser in its own guardhouse and hand
        // the ward's APPREHEND pipeline a permanent false positive. The roster is drawn from
        // souls that stand clear of all of them; this pins that so a future roster edit
        // cannot quietly break it.
        RestrictedZoneTable zones = DocksPopulation.restrictedZoneTable();
        for (int id : rosterIds()) {
            Actor a = population.registry().get(id);
            int home = population.homes().get(a.homeId()).homeCell();
            assertEquals(Actor.NONE, zoneAt(zones, home),
                    "night-roster actor#" + id + " sleeps inside restricted zone "
                            + zoneAt(zones, home) + " at " + xyz(home));
            assertEquals(Actor.NONE, zoneAt(zones, a.anchorCell()),
                    "night-roster actor#" + id + " works inside restricted zone "
                            + zoneAt(zones, a.anchorCell()) + " at " + xyz(a.anchorCell()));
        }
    }

    @Test
    void theNightRosterIsSpreadOverTheWardRatherThanStackedInOneRoom() {
        // Three souls in one bunkroom would be a night shift on paper and a pile-up in fact.
        List<Integer> roster = rosterIds();
        for (int i = 0; i < roster.size(); i++) {
            Actor a = population.registry().get(roster.get(i));
            for (int j = i + 1; j < roster.size(); j++) {
                Actor b = population.registry().get(roster.get(j));
                assertNotEquals(a.anchorCell(), b.anchorCell(),
                        "night-roster #" + roster.get(i) + " and #" + roster.get(j)
                                + " share a work anchor");
                if (PackedPos.z(a.anchorCell()) != PackedPos.z(b.anchorCell())) {
                    continue; // different bands cannot collide at all
                }
                int dx = Math.abs(PackedPos.x(a.anchorCell()) - PackedPos.x(b.anchorCell()));
                int dy = Math.abs(PackedPos.y(a.anchorCell()) - PackedPos.y(b.anchorCell()));
                assertTrue(Math.max(dx, dy) > 12,
                        "night-roster #" + roster.get(i) + " and #" + roster.get(j)
                                + " beat within one another's radius-6 square (chebyshev "
                                + Math.max(dx, dy) + ") -- that is a night pile-up waiting");
            }
        }
    }

    @Test
    void theRoofWatchIsOnTheNightRoster() {
        // The one authored roster line that was plainly wrong: villain.skyrunner's rhythm
        // window is [18000,24000), and the single Watch in the ward posted against roof-runners
        // (the ARREST-SPEC addendum's C2 roof deck) was asleep through every minute of it.
        ActorRegistry registry = population.registry();
        int roofPost = DocksPopulation.c2RoofWatchpostCell();
        int found = Actor.NONE;
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (a.typeId().key().equals("militia_watch") && a.anchorCell() == roofPost) {
                found = i;
            }
        }
        assertNotEquals(Actor.NONE, found, "the C2 roof watch post is manned");
        assertEquals(nightOrdinal, registry.get(found).jobOrdinal(),
                "the roof watch must work the hours the roof-runners do");
    }

    private static int zoneAt(RestrictedZoneTable zones, int cell) {
        for (int z = 0; z < zones.size(); z++) {
            if (zones.get(z).contains(cell)) {
                return z;
            }
        }
        return Actor.NONE;
    }

    private static String xyz(int cell) {
        return "(" + PackedPos.x(cell) + "," + PackedPos.y(cell) + "," + PackedPos.z(cell) + ")";
    }
}
