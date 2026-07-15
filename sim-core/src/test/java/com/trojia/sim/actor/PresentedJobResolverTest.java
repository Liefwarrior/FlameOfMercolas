package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Pass 3 (F3) — {@code ActorContext.presentedJob} + restricted-zone gates. The resolver reads the
 * PRESENTED identity (the inverse of {@code wielderId}); a Farmer presenting as a Guard passes
 * Guard-only gates and fails Farmer-only gates, and the read is live (a {@code setActAs} takes
 * effect immediately — no cache).
 */
final class PresentedJobResolverTest {

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, 1);
    }

    /** A Farmer (Serf on serf.farmer) and a Guard (militia_watch on watch.patrol) in one registry. */
    private static final class Fixture {
        final ActorRegistry registry = new ActorRegistry();
        final NoOpActorContext ctx;
        final Actor farmer;
        final Actor guard;
        final RestrictedZone farmZone;
        final RestrictedZone guardZone;

        Fixture() {
            farmer = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(10, 10));
            guard = registry.spawn(MilitiaWatch.TYPE, ActorTestFixtures.stats(MilitiaWatch.TYPE),
                    cell(20, 20));
            ctx = new NoOpActorContext(registry);
            JobRegistry jobs = ctx.jobs();
            farmer.setJobOrdinal((short) jobs.ordinalOf(Job.Serf.Farmer.ID));
            guard.setJobOrdinal((short) jobs.ordinalOf(Job.Watch.Patrol.ID));
            farmZone = new RestrictedZone(Job.Serf.Farmer.ID, cell(11, 10), new int[] {cell(12, 10)});
            guardZone = new RestrictedZone(Job.Watch.Patrol.ID, cell(21, 20), new int[] {cell(22, 20)});
        }
    }

    @Test
    void undisguisedActorPresentsItsOwnJobAndGatesAccordingly() {
        Fixture f = new Fixture();
        assertEquals(Job.Serf.Farmer.ID, f.ctx.presentedJob(f.farmer).id());
        assertTrue(f.ctx.canAccess(f.farmer, f.farmZone), "a Farmer enters a Farmer-only zone");
        assertFalse(f.ctx.canAccess(f.farmer, f.guardZone), "a Farmer is barred from a Guard zone");
    }

    @Test
    void farmerPresentingAsGuardPassesGuardGatesAndFailsFarmerGates() {
        Fixture f = new Fixture();
        f.farmer.setActAs(f.guard.id());

        assertEquals(Job.Watch.Patrol.ID, f.ctx.presentedJob(f.farmer).id(),
                "presentedJob resolves the disguised-as actor's job, not the true job");
        assertTrue(f.ctx.canAccess(f.farmer, f.guardZone), "presenting as a Guard opens Guard gates");
        assertFalse(f.ctx.canAccess(f.farmer, f.farmZone),
                "while presenting as a Guard the actor no longer satisfies its true Farmer gate");
    }

    @Test
    void resolverIsALiveReadWithNoCache() {
        Fixture f = new Fixture();
        assertTrue(f.ctx.canAccess(f.farmer, f.farmZone));

        f.farmer.setActAs(f.guard.id());
        assertTrue(f.ctx.canAccess(f.farmer, f.guardZone), "disguise takes effect immediately");
        assertFalse(f.ctx.canAccess(f.farmer, f.farmZone));

        f.farmer.setActAs(f.farmer.id()); // drop the disguise
        assertTrue(f.ctx.canAccess(f.farmer, f.farmZone), "dropping the disguise reverts immediately");
        assertFalse(f.ctx.canAccess(f.farmer, f.guardZone));
    }

    @Test
    void anActorWithNoJobResolvesNullAndPassesNoGate() {
        ActorRegistry registry = new ActorRegistry();
        Actor jobless = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(5, 5));
        NoOpActorContext ctx = new NoOpActorContext(registry);
        assertNull(ctx.presentedJob(jobless), "jobOrdinal -1 resolves to no job");
        RestrictedZone anyZone = new RestrictedZone(Job.Serf.Farmer.ID, Actor.NONE,
                new int[] {cell(5, 5)});
        assertFalse(ctx.canAccess(jobless, anyZone));
    }

    @Test
    void restrictedZoneTableLooksUpZonesByCellDeterministically() {
        RestrictedZone zone = new RestrictedZone(Job.Watch.Patrol.ID, cell(1, 1),
                new int[] {cell(2, 2), cell(3, 3)});
        RestrictedZoneTable table = new RestrictedZoneTable(List.of(zone));
        assertEquals(1, table.size());
        assertSame(zone, table.zoneAt(cell(2, 2)));
        assertSame(zone, table.zoneAt(cell(3, 3)));
        assertNull(table.zoneAt(cell(9, 9)), "an untagged cell is unrestricted");
        assertEquals(0, RestrictedZoneTable.EMPTY.size());
    }
}
