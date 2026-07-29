package com.trojia.client.scenario;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ItemsLiteEntry;
import com.trojia.sim.actor.ItemsLiteRegistry;

/**
 * An INDEPENDENT census of every Royal in the ward, taken by walking ItemsLite entry by entry.
 *
 * <p><b>Why this exists.</b> The money-conservation proof used to derive its own answer:
 * {@code looseCoin = minted - vault - sunk}, and then assert
 * {@code minted == vault + loose + sunk}. Substitute and the assertion reads
 * {@code minted == minted}. It could never fail, and it printed PASS for several sprints while
 * the ward's money supply went effectively unchecked. Every term here is measured, never
 * back-solved, so the identity has something to disagree with.
 *
 * <p><b>The load-bearing invariant is {@link #live()}, taken twice.</b> COIN is minted exactly
 * once, at bake; nothing in the sim mints or burns Royals, only moves them. So the real
 * statement is <em>live coin at bake == live coin at the end of the soak</em> — a comparison
 * between two independent scans of two different world states, which a stray mint, a dropped
 * stack, or an off-by-one in a counter transfer will break. The vault/carried/ground split is
 * a partition of that same scan: useful for reading where the money went, but not by itself a
 * proof of anything.
 *
 * <p><b>{@code sunk} is deliberately NOT part of the invariant.</b>
 * {@link ItemsLiteRegistry#sink} keeps a vacated stack's old quantity on the entry, so a COIN
 * stack that was fully MOVED to a counter still reads as its old size until the dense slot is
 * recycled by the next mint. Sunk COIN is therefore a phantom figure, not destroyed specie —
 * it is reported for visibility and excluded from the identity. (The old check summed it into
 * "minted", which is the second reason that line meant nothing.)
 *
 * @param vault    Royals sitting in the bank vault chest cell
 * @param carried  Royals in purses (carried by a living actor)
 * @param ground   Royals on any other cell — counters, strongboxes, anything staged in the world
 * @param sunk     Royals on vacated (moved-out) stacks; phantom, see above
 * @param purses   distinct souls carrying at least one Royal — a total can be one hoarder
 * @param fattest  the largest single purse, so the distribution behind {@code carried} is legible
 */
public record CoinCensus(int vault, int carried, int ground, int sunk, int purses, int fattest) {

    /** Royals that are not in the vault: purses plus anything staged on a cell. */
    public int loose() {
        return carried + ground;
    }

    /** Royals in circulation — the closed-supply figure this proof compares across time. */
    public int live() {
        return vault + carried + ground;
    }

    /** The partition total this scan saw, phantom sunk entries included. */
    public int scanned() {
        return live() + sunk;
    }

    /**
     * Walks every ItemsLite slot in ascending dense-index order (report-deterministic) and
     * classifies each COIN stack exactly once. Nothing here reads another term to compute a
     * term.
     *
     * @param items     the live item registry
     * @param vaultCell the bank vault chest cell ({@code DocksPopulation.bankVaultChestCell()})
     */
    public static CoinCensus of(ItemsLiteRegistry items, int vaultCell) {
        int vault = 0;
        int carried = 0;
        int ground = 0;
        int sunk = 0;
        int purses = 0;
        int fattest = 0;
        for (int i = 0; i < items.size(); i++) {
            ItemsLiteEntry e = items.get(i);
            if (e.kindId() != ItemKinds.COIN) {
                continue;
            }
            int q = e.quantity();
            if (items.isSunk(i)) {
                sunk += q;
            } else if (e.locationCarriedBy() != Actor.NONE) {
                carried += q;
                purses++;
                fattest = Math.max(fattest, q);
            } else if (e.locationCell() == vaultCell) {
                vault += q;
            } else {
                ground += q;
            }
        }
        return new CoinCensus(vault, carried, ground, sunk, purses, fattest);
    }

    /**
     * The closed-supply verdict: no Royal was created or destroyed between the two censuses.
     * This is the line that can actually fail — {@code CoinCensusMutationTest} breaks it on
     * purpose, in both directions, to prove it.
     */
    public static boolean supplyClosed(CoinCensus atBake, CoinCensus now) {
        return atBake.live() == now.live();
    }
}
