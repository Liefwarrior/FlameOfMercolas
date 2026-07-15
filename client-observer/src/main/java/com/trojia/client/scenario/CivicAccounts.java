package com.trojia.client.scenario;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.BankLedger;
import com.trojia.sim.actor.ItemsLiteRegistry;

/**
 * The Phase-0 economy bake pass: opens exactly one bank account per actor and
 * issues one {@link com.trojia.sim.actor.ItemKinds#ID_CARD} per citizen. Runs
 * AFTER the population's spawn walk so it can iterate the registry in ascending
 * {@code actorId} order — which makes each opened {@code accountId} equal its
 * actor's id (the "index == id" convention the ledger, {@code HomeRegistry} and
 * {@code ItemsLiteRegistry} all share).
 *
 * <p>No live bank building exists yet (Phase 0): accounts open at zero balance
 * and citizens carry only the loose Coins their starting-inventory pass minted —
 * deposits/withdrawals are exercised by unit tests against a synthetic vault, not
 * here. The ID_CARD each citizen carries is stamped with its own account; a
 * beast (an owned animal or a feral) is issued no card.
 */
final class CivicAccounts {

    private CivicAccounts() {
    }

    /**
     * Opens an account for every actor (ascending id ⇒ {@code accountId == actorId}) and mints one
     * stamped ID_CARD per non-beast actor. Deterministic: a fixed ascending scan, no RNG, no map
     * iteration.
     */
    static void bake(ActorRegistry registry, BankLedger bank, ItemsLiteRegistry items) {
        for (int id = 0; id < registry.size(); id++) {
            int accountId = bank.openAccount(); // ascending -> accountId == actorId
            if (isCitizen(registry.get(id))) {
                items.mintIdCard(id, accountId); // one card per citizen, stamped with its account
            }
        }
    }

    /** A citizen holds a bank identity; a beast (owned animal / feral) does not. */
    private static boolean isCitizen(Actor actor) {
        String type = actor.typeId().key();
        return !type.equals("animal") && !type.equals("feral");
    }
}
