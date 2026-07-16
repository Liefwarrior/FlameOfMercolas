package com.trojia.client.scenario;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.BankLedger;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.Payroll;

/**
 * The economy bake pass (Phase-2 STEP B, Pass 9): a CLOSED-supply seed of the Royals economy.
 * Runs AFTER the population's spawn walk so it can iterate the registry in ascending {@code
 * actorId} order — which makes each opened {@code accountId} equal its actor's id (the "index ==
 * id" convention the ledger, {@code HomeRegistry} and {@code ItemsLiteRegistry} all share).
 *
 * <p><b>What it seeds (all minting happens here, at bake — never at runtime):</b>
 * <ul>
 *   <li>one bank account per actor, credited a starting balance by a simple wealth tier
 *       ({@link #seedRoyalsFor}: shopkeeper &gt; clergy &gt; watch &gt; keeper &gt; serf &gt;
 *       wastrel, beasts 0) — the stratification circulation later reshapes;</li>
 *   <li>one stamped {@link ItemKinds#ID_CARD} per citizen (a beast gets none);</li>
 *   <li>a single finite EMPLOYER pool account (id == {@code registry.size()}), seeded to cover
 *       many paydays, that live wages draw from — never minted;</li>
 *   <li>the vault chest's single COIN stack, set to {@code totalRoyals()} so the hard conservation
 *       invariant {@code totalRoyals() == vault COIN count} holds the instant the bake finishes.</li>
 * </ul>
 * Loose pocket Coins are the separate shadow currency the starting-inventory pass already minted
 * onto some persons; they are additional to (never part of) the vault-backed Royals.
 */
final class CivicAccounts {

    // ---- wealth tiers (starting Royals balance by actor type; tunable) ----
    private static final long SEED_SHOPKEEPER = 200;
    private static final long SEED_CLERGY = 150;
    private static final long SEED_WATCH = 120;
    private static final long SEED_KEEPER = 80;
    private static final long SEED_SERF = 60;
    private static final long SEED_WASTREL = 20;

    // ---- wage tiers (Royals paid per WAGE_PERIOD to the employed; villains/beasts earn 0) ----
    private static final long WAGE_SHOPKEEPER = 15;
    private static final long WAGE_CLERGY = 12;
    private static final long WAGE_WATCH = 12;
    private static final long WAGE_KEEPER = 8;
    private static final long WAGE_SERF = 10;

    /** Wage cadence: every quarter-day (DailyRhythm.DAY = 24000), so a 20k-tick run sees 3 paydays. */
    static final int WAGE_PERIOD = 6_000;
    /** Paydays the employer pool is seeded to cover up front (generous; the pool is finite regardless). */
    private static final int EMPLOYER_PERIODS_FUNDED = 400;

    private CivicAccounts() {
    }

    /**
     * Phase-0 substrate bake (the CompoundBlock demo, which has no live bank): opens one
     * zero-balance account per actor (ascending id ⇒ {@code accountId == actorId}) and mints one
     * stamped ID_CARD per citizen — no wealth seed, no employer pool, no vault stock. Deterministic.
     */
    static void bake(ActorRegistry registry, BankLedger bank, ItemsLiteRegistry items) {
        for (int id = 0; id < registry.size(); id++) {
            int accountId = bank.openAccount(); // ascending -> accountId == actorId
            if (isCitizen(registry.get(id))) {
                items.mintIdCard(id, accountId);
            }
        }
    }

    /**
     * Seeds the closed Royals economy and returns the {@link Payroll} wiring the sim ticks wages
     * from. Deterministic: a fixed ascending scan, no RNG, no map iteration. Asserts the hard
     * conservation invariant before returning.
     *
     * @param vaultChestCell the world-packed vault chest cell to stock with the backing COIN stack
     */
    static Payroll bake(ActorRegistry registry, BankLedger bank, ItemsLiteRegistry items,
            int vaultChestCell) {
        long[] wagePerActor = new long[registry.size()];
        for (int id = 0; id < registry.size(); id++) {
            int accountId = bank.openAccount(); // ascending -> accountId == actorId
            Actor actor = registry.get(id);
            long seed = seedRoyalsFor(actor);
            if (seed > 0) {
                bank.credit(accountId, seed);
            }
            wagePerActor[id] = wageFor(actor);
            if (isCitizen(actor)) {
                items.mintIdCard(id, accountId); // one card per citizen, stamped with its account
            }
        }

        // The finite employer pool (id == registry.size()): live wages TRANSFER from it, never mint.
        int employerAccount = bank.openAccount();
        long payrollPerPeriod = 0;
        for (long wage : wagePerActor) {
            payrollPerPeriod += wage;
        }
        bank.credit(employerAccount, payrollPerPeriod * EMPLOYER_PERIODS_FUNDED);

        // Stock the vault chest so totalRoyals() == vault COIN count (the hard invariant). Every
        // seeded Royal — citizen balances AND the employer pool — is backed by a coin in the vault.
        long totalRoyals = bank.totalRoyals();
        items.addOnCell(vaultChestCell, ItemKinds.COIN, (int) totalRoyals);

        long vaultCoins = items.countOnCellOfKind(vaultChestCell, ItemKinds.COIN);
        if (vaultCoins != totalRoyals) {
            throw new IllegalStateException("economy bake broke conservation: totalRoyals="
                    + totalRoyals + " != vault COIN count=" + vaultCoins);
        }
        return new Payroll(employerAccount, WAGE_PERIOD, wagePerActor);
    }

    /** Starting Royals by wealth tier (beasts and unknown types get none). */
    private static long seedRoyalsFor(Actor actor) {
        return switch (actor.typeId().key()) {
            case "shopkeeper" -> SEED_SHOPKEEPER;
            case "priest_of_the_flame", "disciple_of_the_flame" -> SEED_CLERGY;
            case "militia_watch" -> SEED_WATCH;
            case "animal_keeper" -> SEED_KEEPER;
            case "serf" -> SEED_SERF;
            case "wastrel" -> SEED_WASTREL;
            default -> 0L; // animal, feral, anything else
        };
    }

    /** Per-period wage by tier; wastrels (incl. villains under cover) and beasts earn nothing. */
    private static long wageFor(Actor actor) {
        return switch (actor.typeId().key()) {
            case "shopkeeper" -> WAGE_SHOPKEEPER;
            case "priest_of_the_flame", "disciple_of_the_flame" -> WAGE_CLERGY;
            case "militia_watch" -> WAGE_WATCH;
            case "animal_keeper" -> WAGE_KEEPER;
            case "serf" -> WAGE_SERF;
            default -> 0L;
        };
    }

    /** A citizen holds a bank identity; a beast (owned animal / feral) does not. */
    private static boolean isCitizen(Actor actor) {
        String type = actor.typeId().key();
        return !type.equals("animal") && !type.equals("feral");
    }
}
