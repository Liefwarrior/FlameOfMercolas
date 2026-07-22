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
 * ApprehendPolicy}'s riot branch, retuned to a BRAWL signal): {@code >= RIOT_AGGRESSORS}
 * DISTINCT pushers, each with {@code >= RIOT_REPEAT_SHOVES} in-window shoves clustered within
 * {@code RIOT_RADIUS} of one anchor, bring the Watch — every clustered AGGRESSOR is sent home
 * under a fixed 24,000-tick house arrest; a one-shove passer-by is not touched; a busy door
 * (many distinct single shovers) is traffic, not a riot; guards are never arrested; the
 * response is idempotent (a second guard the same cadence tick finds nothing left to arrest).
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

    /** Records {@code n} shoves by {@code pusher} inside the chebyshev-6 cluster around (50,50). */
    private static void brawl(RiotContext ctx, Actor pusher, int n, long firstTick) {
        for (int i = 0; i < n; i++) {
            ctx.log.record(firstTick + 5L * i, cell(50 + i % 3, 50 + i % 2), pusher.id());
        }
    }

    @Test
    void riotSendsEveryClusteredAggressorHomeForADayButNeverAGuardNorAPasserBy() {
        ActorRegistry registry = new ActorRegistry();
        RiotContext ctx = new RiotContext(registry);
        Actor watchman = guard(registry, ctx, 40, 40);
        Actor[] brawlers = new Actor[ApprehendPolicy.RIOT_AGGRESSORS];
        for (int i = 0; i < brawlers.length; i++) {
            brawlers[i] = serf(registry, ctx, 50 + i, 50);
        }
        Actor passerBy = serf(registry, ctx, 52, 52);  // one squeeze-past inside the cluster
        Actor bystander = serf(registry, ctx, 90, 90); // repeat shover far away, outside it
        ctx.setTick(NOW);

        // RIOT_AGGRESSORS distinct aggressors, RIOT_REPEAT_SHOVES in-window shoves each, all
        // within chebyshev 6 of (50,50) — plus repeat shoves by the guard itself (guards
        // brawl too, never arrested), ONE shove by the passer-by (in the crowd, not of the
        // brawl), and a distant repeat shover (its own patch, no full cluster there).
        for (int i = 0; i < brawlers.length; i++) {
            brawl(ctx, brawlers[i], ApprehendPolicy.RIOT_REPEAT_SHOVES, NOW - 100 + 10L * i);
        }
        brawl(ctx, watchman, ApprehendPolicy.RIOT_REPEAT_SHOVES, NOW - 60);
        ctx.log.record(NOW - 20, cell(52, 51), passerBy.id());
        ctx.log.record(NOW - 20, cell(90, 90), bystander.id());
        ctx.log.record(NOW - 15, cell(90, 91), bystander.id());

        assertEquals(1500, Policies.APPREHEND.score(watchman, ctx),
                "the riot is sensed at the cadence boundary");

        Policies.APPREHEND.act(watchman, ctx);

        for (Actor brawler : brawlers) {
            assertTrue(brawler.hasStatus(StatusBit.HOUSE_ARREST),
                    "clustered aggressor #" + brawler.id() + " sent home");
            assertEquals(NOW + 24_000L, brawler.houseArrestUntilTick(), "exactly one day");
            assertEquals(ReasonCode.HOUSE_ARRESTED, brawler.lastReasonCode());
        }
        assertFalse(watchman.hasStatus(StatusBit.HOUSE_ARREST), "the Watch never arrests itself");
        assertFalse(passerBy.hasStatus(StatusBit.HOUSE_ARREST),
                "one squeeze-past inside the cluster is not brawling");
        assertFalse(bystander.hasStatus(StatusBit.HOUSE_ARREST),
                "a repeat shover outside the cluster is not part of this riot");
    }

    @Test
    void aBusyDoorOfDistinctSingleShoversIsTrafficNotARiot() {
        // The cap-1 lesson: every doorway crossing shoves once, so a crowded door produces
        // MANY in-window clustered shoves — all by DIFFERENT one-shove pushers. Twelve such
        // shoves (double the old raw-count threshold) must not read as a riot.
        ActorRegistry registry = new ActorRegistry();
        RiotContext ctx = new RiotContext(registry);
        Actor watchman = guard(registry, ctx, 40, 40);
        Actor[] crowd = new Actor[12];
        for (int i = 0; i < crowd.length; i++) {
            crowd[i] = serf(registry, ctx, 50 + i % 4, 50 + i % 3);
        }
        ctx.setTick(NOW);
        for (int i = 0; i < crowd.length; i++) {
            ctx.log.record(NOW - 120 + 10L * i, cell(50 + i % 2, 50), crowd[i].id());
        }

        assertEquals(0, Policies.APPREHEND.score(watchman, ctx),
                "twelve distinct single shovers are a queue, not a brawl");
        for (Actor serf : crowd) {
            assertFalse(serf.hasStatus(StatusBit.HOUSE_ARREST));
        }
    }

    @Test
    void responseIsIdempotentAcrossGuardsWithinOneCadenceTick() {
        ActorRegistry registry = new ActorRegistry();
        RiotContext ctx = new RiotContext(registry);
        Actor first = guard(registry, ctx, 40, 40);
        Actor second = guard(registry, ctx, 60, 60);
        Actor[] brawlers = new Actor[ApprehendPolicy.RIOT_AGGRESSORS];
        for (int i = 0; i < brawlers.length; i++) {
            brawlers[i] = serf(registry, ctx, 50 + i, 50);
        }
        ctx.setTick(NOW);
        for (int i = 0; i < brawlers.length; i++) {
            brawl(ctx, brawlers[i], ApprehendPolicy.RIOT_REPEAT_SHOVES, NOW - 100 + 10L * i);
        }

        Policies.APPREHEND.act(first, ctx);
        for (Actor brawler : brawlers) {
            assertTrue(brawler.hasStatus(StatusBit.HOUSE_ARREST));
        }

        // The second guard's score runs AFTER the first guard's act (ascending-id tick order):
        // every clustered aggressor is already corrected, so the riot is no longer actionable.
        assertEquals(0, Policies.APPREHEND.score(second, ctx),
                "nothing left to arrest -> no riot branch, no score");
    }

    @Test
    void subThresholdOrStaleShovingIsNotARiot() {
        ActorRegistry registry = new ActorRegistry();
        RiotContext ctx = new RiotContext(registry);
        Actor watchman = guard(registry, ctx, 40, 40);
        Actor[] brawlers = new Actor[ApprehendPolicy.RIOT_AGGRESSORS];
        for (int i = 0; i < brawlers.length; i++) {
            brawlers[i] = serf(registry, ctx, 50 + i, 50);
        }
        ctx.setTick(NOW + 1000); // make room for stale entries below

        // Full aggressors, one short of RIOT_AGGRESSORS...
        int oneShort = ApprehendPolicy.RIOT_AGGRESSORS - 1;
        for (int i = 0; i < oneShort; i++) {
            brawl(ctx, brawlers[i], ApprehendPolicy.RIOT_REPEAT_SHOVES, NOW + 1000 - 100 + 10L * i);
        }
        // ...and a last pusher whose earlier shoves are all OUTSIDE the window: one fresh
        // shove makes it a passer-by, not an aggressor — the stale rows must not count.
        for (int i = 0; i < ApprehendPolicy.RIOT_REPEAT_SHOVES - 1; i++) {
            ctx.log.record(NOW + 1000 - 400 - 5L * i, cell(50, 51), brawlers[oneShort].id());
        }
        ctx.log.record(NOW + 1000 - 20, cell(50, 51), brawlers[oneShort].id());

        assertEquals(0, Policies.APPREHEND.score(watchman, ctx),
                "one-short aggressors plus a stale-history passer-by are not a riot");
        for (Actor brawler : brawlers) {
            assertFalse(brawler.hasStatus(StatusBit.HOUSE_ARREST));
        }
    }
}
