package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobId;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Living-docks Pass 4: the two new dock-trade leaves ({@code maritime.sailor}, {@code
 * trade.trader}) are bound from the committed {@code jobs.json} and genuinely pursue the shared
 * anchor-cycle at their work anchor — accruing work units and reaching completion — exactly like
 * the other placed-worker jobs (Farmer/Stallkeep). Driven through the bound {@link Job} leaf
 * itself (not {@code JobBehaviors} directly) so it proves the leaf-to-behavior wiring.
 */
final class DockTradeJobsPursueTest {

    private static final int Z = 9;

    /** An in-place worker (anchor == home == cell) of {@code jobId} accrues and completes on shift. */
    private static void assertAccruesAndCompletesAtAnchor(JobId jobId) {
        ActorRegistry registry = new ActorRegistry();
        int anchor = PackedPos.pack(50, 50, Z);
        Actor worker = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), anchor);
        worker.setAnchorCell(anchor); // anchor == spawn cell, no distinct home -> in-place accrual

        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(5_000); // tick-of-day 5000 is inside both trades' rhythm windows -> ON SHIFT
        JobRegistry jobs = ctx.jobs();
        int ordinal = jobs.ordinalOf(jobId);
        assertTrue(ordinal >= 0, jobId + " must be bound from the committed jobs.json");
        worker.setJobOrdinal((short) ordinal);
        Job job = jobs.get(ordinal);

        job.selectTarget(worker, ctx);
        int guard = 0;
        while (!job.isComplete(worker, ctx) && guard++ < 100_000) {
            job.pursue(worker, ctx);
        }
        assertTrue(job.isComplete(worker, ctx),
                jobId + " must accrue work at its anchor and reach completion");
        assertEquals(anchor, worker.cell(), "an in-place worker never leaves its own anchor cell");
        assertTrue(worker.goalProgress() > 0, jobId + " must have accrued at least one work unit");
    }

    @Test
    void sailorCrewsItsShipAnchorToCompletion() {
        assertAccruesAndCompletesAtAnchor(Job.Maritime.Sailor.ID);
    }

    @Test
    void traderVendsAtItsShopAnchorToCompletion() {
        assertAccruesAndCompletesAtAnchor(Job.Trade.Trader.ID);
    }
}
