package com.trojia.sim.actor;

/**
 * The baked payroll table (Phase-2 STEP B, Pass 9): who earns what, drawn from which finite
 * employer pool account, on what fixed cadence. Injected through the {@link ActorsSystem}
 * constructor like the other civic seams — immutable baked config, so it rides no save; its
 * only mutable effect is on {@link BankLedger} balances, which are serialized and hashed.
 *
 * <p><b>Wages are transfers, never mints.</b> Every payday {@link ActorsSystem#tick} does
 * {@code bank.transfer(employerAccount -> workerAccount, wage)} in ascending actor-id order;
 * an insufficient employer pool simply skips that wage (the transfer returns {@code false}) —
 * money is moved out of a finite pool, never conjured, so {@link BankLedger#totalRoyals()}
 * (and thus the vault COIN count) is invariant across every payday.
 *
 * <p><b>Determinism.</b> Cadence is {@code tick % periodTicks == 0} against the absolute sim
 * tick (no wall clock); the worker walk is ascending id over a dense {@code long[]} indexed by
 * actorId (no map/insertion-order iteration). Purity-gate clean: {@code int} + {@code long[]}.
 */
public final class Payroll {

    /** The degraded "no payroll" table the world-less/no-bank bake injects. */
    public static final Payroll NONE = new Payroll(Actor.NONE, 0, new long[0]);

    private final int employerAccountId;
    private final int periodTicks;
    private final long[] wagePerActor;

    public Payroll(int employerAccountId, int periodTicks, long[] wagePerActor) {
        this.employerAccountId = employerAccountId;
        this.periodTicks = periodTicks;
        this.wagePerActor = wagePerActor.clone();
    }

    /** {@code true} iff a real employer pool + cadence are wired (a live district), not {@link #NONE}. */
    public boolean isWired() {
        return employerAccountId != Actor.NONE && periodTicks > 0;
    }

    public int employerAccountId() {
        return employerAccountId;
    }

    public int periodTicks() {
        return periodTicks;
    }

    /** The per-period wage owed to {@code actorId} (0 for beasts, villains, and the unemployed). */
    public long wageForActor(int actorId) {
        return actorId >= 0 && actorId < wagePerActor.length ? wagePerActor[actorId] : 0L;
    }

    /** {@code true} exactly on a payday tick (fixed cadence against the absolute sim tick). */
    public boolean isPayday(long tick) {
        return isWired() && tick > 0 && tick % periodTicks == 0;
    }
}
