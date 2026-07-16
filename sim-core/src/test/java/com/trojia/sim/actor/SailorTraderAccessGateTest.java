package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Living-docks Pass 4 (F3 data): the restricted-zone access gate against the two new dock
 * trades. A Farmer PRESENTING as a Sailor passes the shipyard (Sailor-only) gate and fails the
 * Trader gate; a plain peasant (serf.laborer) fails every gate; each trade passes only its own
 * gate. Every assertion runs through {@link ActorContext#canAccess}, which reads the PRESENTED
 * job (never {@code actor.jobOrdinal()} directly — reading the true job under a disguise would
 * be a bypass bug, PLAY-MODE §4).
 */
final class SailorTraderAccessGateTest {

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, 1);
    }

    /** A Farmer, a Sailor, a Trader and a plain laborer peasant, plus a Sailor gate and Trader gate. */
    private static final class Fixture {
        final ActorRegistry registry = new ActorRegistry();
        final NoOpActorContext ctx;
        final Actor farmer;
        final Actor sailor;
        final Actor trader;
        final Actor peasant;
        final RestrictedZone shipyardZone;
        final RestrictedZone traderZone;

        Fixture() {
            farmer = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(10, 10));
            sailor = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(20, 20));
            trader = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(30, 30));
            peasant = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(40, 40));
            ctx = new NoOpActorContext(registry);
            JobRegistry jobs = ctx.jobs();
            farmer.setJobOrdinal((short) jobs.ordinalOf(Job.Serf.Farmer.ID));
            sailor.setJobOrdinal((short) jobs.ordinalOf(Job.Maritime.Sailor.ID));
            trader.setJobOrdinal((short) jobs.ordinalOf(Job.Trade.Trader.ID));
            peasant.setJobOrdinal((short) jobs.ordinalOf(Job.Serf.Laborer.ID));
            shipyardZone = new RestrictedZone(Job.Maritime.Sailor.ID, cell(21, 20),
                    new int[] {cell(22, 20)});
            traderZone = new RestrictedZone(Job.Trade.Trader.ID, cell(31, 30),
                    new int[] {cell(32, 30)});
        }
    }

    @Test
    void farmerPresentingAsSailorPassesTheShipyardGateAndFailsTheTraderGate() {
        Fixture f = new Fixture();
        f.farmer.setActAs(f.sailor.id());

        assertEquals(Job.Maritime.Sailor.ID, f.ctx.presentedJob(f.farmer).id(),
                "the gate reads the presented (Sailor) job, not the true Farmer job");
        assertTrue(f.ctx.canAccess(f.farmer, f.shipyardZone),
                "a Farmer presenting as a Sailor passes the shipyard gate");
        assertFalse(f.ctx.canAccess(f.farmer, f.traderZone),
                "a presented Sailor still fails the Trader gate");
    }

    @Test
    void aPlainPeasantFailsEveryGate() {
        Fixture f = new Fixture();
        assertEquals(Job.Serf.Laborer.ID, f.ctx.presentedJob(f.peasant).id());
        assertFalse(f.ctx.canAccess(f.peasant, f.shipyardZone),
                "a plain laborer peasant is barred from the shipyard");
        assertFalse(f.ctx.canAccess(f.peasant, f.traderZone),
                "a plain laborer peasant is barred from the shop special-inventory zone");
    }

    @Test
    void eachTradePassesOnlyItsOwnGate() {
        Fixture f = new Fixture();
        assertTrue(f.ctx.canAccess(f.sailor, f.shipyardZone), "a Sailor enters the shipyard");
        assertFalse(f.ctx.canAccess(f.sailor, f.traderZone), "a Sailor is barred from the Trader zone");
        assertTrue(f.ctx.canAccess(f.trader, f.traderZone), "a Trader enters the shop zone");
        assertFalse(f.ctx.canAccess(f.trader, f.shipyardZone), "a Trader is barred from the shipyard");
    }

    @Test
    void droppingTheDisguiseRevertsGateAccessImmediately() {
        Fixture f = new Fixture();
        f.farmer.setActAs(f.sailor.id());
        assertTrue(f.ctx.canAccess(f.farmer, f.shipyardZone), "disguise takes effect immediately (no cache)");

        f.farmer.setActAs(f.farmer.id()); // drop it
        assertFalse(f.ctx.canAccess(f.farmer, f.shipyardZone),
                "dropping the Sailor disguise re-bars the Farmer from the shipyard immediately");
    }
}
