package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Serf;
import org.junit.jupiter.api.Test;

import com.trojia.sim.world.PackedPos;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Shove-riot detection and the house-arrest correction (density revisit, {@link
 * ApprehendPolicy}'s riot branch): {@code >= RIOT_SHOVES} in-window shoves clustered within
 * {@code RIOT_RADIUS} of one anchor bring the Watch — every clustered shover is sent home
 * under a fixed 24,000-tick house arrest; guards are never arrested; the response is
 * idempotent (a second guard the same cadence tick finds nothing left to arrest).
 */
final class ShoveRiotHouseArrestTest {

    private static final int Z = 11;
    private static final long NOW = 600; // a sense-cadence boundary (% 10 == 0)

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /** Ctx double with a live shove log. */
    private static final class RiotContext extends NoOpActorContext {
        private final ShoveLog log = new ShoveLog(64);

        RiotContext(ActorRegistry registry) {
            super(registry);
        }

        @Override
        public ShoveLog shoveLog() {
            return log;
        }
    }

    private static Actor guard(ActorRegistry registry, ActorContext ctx, int x, int y) {
        Actor guard = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.stats(MilitiaWatch.TYPE), cell(x, y));
        guard.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
        return guard;
    }

    private static Actor serf(ActorRegistry registry, ActorContext ctx, int x, int y) {
        Actor serf = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), cell(x, y));
        serf.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Serf.Laborer.ID));
        return serf;
    }

    @Test
    void riotSendsEveryClusteredShoverHomeForADayButNeverAGuard() {
        ActorRegistry registry = new ActorRegistry();
        RiotContext ctx = new RiotContext(registry);
        Actor watchman = guard(registry, ctx, 40, 40);
        Actor brawlerA = serf(registry, ctx, 50, 50);
        Actor brawlerB = serf(registry, ctx, 51, 50);
        Actor bystander = serf(registry, ctx, 90, 90); // shoved far away, outside the cluster
        ctx.setTick(NOW);

        // Six in-window shoves inside a chebyshev-6 cluster around (50,50), split between the
        // two brawlers AND one by the guard itself (guards shove too — but are never arrested).
        ctx.log.record(NOW - 50, cell(50, 50), brawlerA.id());
        ctx.log.record(NOW - 40, cell(51, 50), brawlerB.id());
        ctx.log.record(NOW - 30, cell(52, 51), brawlerA.id());
        ctx.log.record(NOW - 20, cell(49, 49), brawlerB.id());
        ctx.log.record(NOW - 10, cell(50, 52), watchman.id());
        ctx.log.record(NOW - 5, cell(51, 51), brawlerA.id());
        // A distant shove (same window, far cell): part of no cluster, never arrested.
        ctx.log.record(NOW - 5, cell(90, 90), bystander.id());

        assertEquals(1500, Policies.APPREHEND.score(watchman, ctx),
                "the riot is sensed at the cadence boundary");

        Policies.APPREHEND.act(watchman, ctx);

        assertTrue(brawlerA.hasStatus(StatusBit.HOUSE_ARREST), "clustered shover A sent home");
        assertTrue(brawlerB.hasStatus(StatusBit.HOUSE_ARREST), "clustered shover B sent home");
        assertEquals(NOW + 24_000L, brawlerA.houseArrestUntilTick(), "exactly one day");
        assertEquals(NOW + 24_000L, brawlerB.houseArrestUntilTick());
        assertEquals(ReasonCode.HOUSE_ARRESTED, brawlerA.lastReasonCode());
        assertFalse(watchman.hasStatus(StatusBit.HOUSE_ARREST), "the Watch never arrests itself");
        assertFalse(bystander.hasStatus(StatusBit.HOUSE_ARREST),
                "a lone shover outside the cluster is not part of the riot");
    }

    @Test
    void responseIsIdempotentAcrossGuardsWithinOneCadenceTick() {
        ActorRegistry registry = new ActorRegistry();
        RiotContext ctx = new RiotContext(registry);
        Actor first = guard(registry, ctx, 40, 40);
        Actor second = guard(registry, ctx, 60, 60);
        Actor brawler = serf(registry, ctx, 50, 50);
        ctx.setTick(NOW);
        for (int i = 0; i < 6; i++) {
            ctx.log.record(NOW - 30 + i, cell(50 + i % 2, 50), brawler.id());
        }

        Policies.APPREHEND.act(first, ctx);
        assertTrue(brawler.hasStatus(StatusBit.HOUSE_ARREST));

        // The second guard's score runs AFTER the first guard's act (ascending-id tick order):
        // every clustered shover is already corrected, so the riot is no longer actionable.
        assertEquals(0, Policies.APPREHEND.score(second, ctx),
                "nothing left to arrest -> no riot branch, no score");
    }

    @Test
    void subThresholdOrStaleShovingIsNotARiot() {
        ActorRegistry registry = new ActorRegistry();
        RiotContext ctx = new RiotContext(registry);
        Actor watchman = guard(registry, ctx, 40, 40);
        Actor brawler = serf(registry, ctx, 50, 50);
        ctx.setTick(NOW + 1000); // make room for stale entries below

        // Five recent clustered shoves: one short of RIOT_SHOVES.
        for (int i = 0; i < 5; i++) {
            ctx.log.record(NOW + 1000 - 10 - i, cell(50, 50 + i % 3), brawler.id());
        }
        // A sixth shove in the same cluster but OUTSIDE the 600-tick window: must not count.
        ctx.log.record(NOW + 1000 - 700, cell(50, 51), brawler.id());

        assertEquals(0, Policies.APPREHEND.score(watchman, ctx),
                "five in-window shoves are not excessive; the stale sixth does not count");
        assertFalse(brawler.hasStatus(StatusBit.HOUSE_ARREST));
    }
}
