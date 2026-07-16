package com.trojia.sim.actor;

import com.trojia.sim.actor.job.JobBinder;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.engine.EngineConfig;
import com.trojia.sim.engine.SimulationEngine;
import com.trojia.sim.engine.Simulations;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Pass 9 (Feature 4) — live wages: on a fixed-cadence payday {@link ActorsSystem#tick} transfers
 * each worker's wage from a finite employer pool, ascending id. Wages are TRANSFERS, never mints —
 * the employer pool drains, workers rise, and {@link BankLedger#totalRoyals()} is invariant across
 * every payday (the fix for the held branch's minted-wage rot). An insufficient pool skips a wage,
 * never conjures Royals.
 */
final class ActorsSystemPayrollTest {

    private static Path committedJobsJson() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws").resolve("jobs").resolve("jobs.json");
            if (Files.isRegularFile(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws/jobs/jobs.json not found");
    }

    private static final int PERIOD = 4;
    private final ActorTypeStatsTable typeStats =
            ActorTypeStatsTable.of(List.of(ActorTestFixtures.stats(Serf.TYPE)));
    private final JobRegistry jobs = JobBinder.bind(committedJobsJson(), ActorTypes.allTypeIds());

    /** Two workers (ids 0,1) each earning {@code wage}, drawn from a pool seeded to {@code poolSeed}. */
    private ActorsSystem build(BankLedger bank, long wage, long poolSeed) {
        ActorRegistry registry = new ActorRegistry();
        registry.spawn(Serf.TYPE, typeStats.get(Serf.TYPE), PackedPos.pack(5, 5, 1));
        registry.spawn(Serf.TYPE, typeStats.get(Serf.TYPE), PackedPos.pack(6, 5, 1));
        bank.openAccount(); // acct 0 == worker 0
        bank.openAccount(); // acct 1 == worker 1
        int employer = bank.openAccount(); // acct 2 == the finite pool
        bank.credit(employer, poolSeed);
        Payroll payroll = new Payroll(employer, PERIOD, new long[] {wage, wage});
        CivicFixtures fixtures = new CivicFixtures(Actor.NONE, RestrictedZoneTable.EMPTY,
                Actor.NONE, Actor.NONE, BankQueue.EMPTY, PrisonCellRegistry.EMPTY, payroll);
        return new ActorsSystem(99L, typeStats, jobs, registry, new HomeRegistry(),
                new RelationshipRegistry(), new ItemsLiteRegistry(), bank, null, fixtures);
    }

    @Test
    void wagesTransferFromTheEmployerPoolAndNeverMint() {
        BankLedger bank = new BankLedger();
        ActorsSystem system = build(bank, 10, 1_000);
        SimulationEngine engine = Simulations.create(new EngineConfig(99L), List.of(system));
        long totalBefore = bank.totalRoyals();
        assertEquals(1_000, totalBefore);

        engine.step(PERIOD); // reach the first payday (tick == PERIOD)
        assertEquals(10, bank.balanceOf(0), "worker 0 paid one wage");
        assertEquals(10, bank.balanceOf(1), "worker 1 paid one wage");
        assertEquals(980, bank.balanceOf(2), "the pool drained by exactly the two wages");
        assertEquals(totalBefore, bank.totalRoyals(), "a wage is a transfer: total money is invariant");

        engine.step(PERIOD * 2); // two more paydays
        assertEquals(30, bank.balanceOf(0), "three paydays -> 3 wages each");
        assertEquals(30, bank.balanceOf(1));
        assertEquals(940, bank.balanceOf(2), "pool keeps draining, never re-minted");
        assertEquals(totalBefore, bank.totalRoyals(), "still conserved after multiple paydays");
    }

    @Test
    void anInsufficientPoolSkipsTheWageAndNeverConjuresRoyals() {
        BankLedger bank = new BankLedger();
        // Pool seeded 15: enough for worker 0's wage (10), not for worker 1's (would need 10 more).
        ActorsSystem system = build(bank, 10, 15);
        SimulationEngine engine = Simulations.create(new EngineConfig(99L), List.of(system));

        engine.step(PERIOD);
        assertEquals(10, bank.balanceOf(0), "worker 0 (lower id) is paid first");
        assertEquals(0, bank.balanceOf(1), "the drained pool can't cover worker 1 -> wage SKIPPED");
        assertEquals(5, bank.balanceOf(2), "pool holds the un-payable remainder, not minted negative");
        assertEquals(15, bank.totalRoyals(), "no Royals conjured: total is exactly the seed");
    }

    @Test
    void noWagesArePaidBeforeTheFirstPaydayTick() {
        BankLedger bank = new BankLedger();
        ActorsSystem system = build(bank, 10, 1_000);
        SimulationEngine engine = Simulations.create(new EngineConfig(99L), List.of(system));

        engine.step(PERIOD - 1); // ticks 1..3, no payday yet
        assertEquals(0, bank.balanceOf(0));
        assertEquals(0, bank.balanceOf(1));
        assertEquals(1_000, bank.balanceOf(2), "pool untouched before the first payday");
        assertTrue(bank.totalRoyals() == 1_000);
    }
}
