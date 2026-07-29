package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.engine.SimulationSystem;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * THE MUTATION PROOF for the repaired money-conservation check.
 *
 * <p>The old proof derived {@code loose = minted - vault - sunk} and then asserted
 * {@code minted == vault + loose + sunk}, which is {@code minted == minted}. It printed PASS
 * for several sprints and could not have printed anything else. A replacement that is merely
 * "different code" is worth nothing; the only thing that makes it a check is that it FAILS
 * when the money supply is broken. So these tests break it on purpose, in both directions:
 *
 * <ul>
 *   <li>counterfeit — Royals minted from nowhere ⇒ the closed-supply line must go false;</li>
 *   <li>burn — Royals destroyed ⇒ it must go false as well (a one-sided check would miss
 *       half the ways an economy leaks);</li>
 *   <li>a pure MOVE (purse → vault) must keep it true, or the check is just noise that fires
 *       on ordinary trade.</li>
 * </ul>
 *
 * <p>The last test is the real soak version: a live district, real wages and real
 * pickpocketing, and the supply still closed at the end.
 */
class CoinCensusMutationTest {

    /** Long enough for wages, counter traffic and ambient theft to move real money. */
    private static final int SOAK_TICKS = 3_000;

    @Test
    void counterfeitingBreaksTheClosedSupplyCheck() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation pop = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        int vaultCell = DocksPopulation.bankVaultChestCell();
        ItemsLiteRegistry items = pop.items();

        CoinCensus before = CoinCensus.of(items, vaultCell);
        assertTrue(CoinCensus.supplyClosed(before, CoinCensus.of(items, vaultCell)),
                "an untouched supply is closed against itself");

        int purse = firstCoinCarrier(items);
        assertNotEquals(Actor.NONE, purse, "the bake must put Royals in at least one pocket");
        assertEquals(7, items.addCarried(purse, ItemKinds.COIN, 7), "7 Royals conjured");

        CoinCensus after = CoinCensus.of(items, vaultCell);
        assertFalse(CoinCensus.supplyClosed(before, after),
                "7 Royals appeared from nowhere and the check said nothing — "
                        + "this is exactly the tautology S8 removed");
        assertEquals(before.live() + 7, after.live(), "and it names the size of the hole");
        assertEquals(before.loose() + 7, after.loose(),
                "loose coin is COUNTED, so it moved with the counterfeit");
    }

    @Test
    void burningCoinBreaksTheClosedSupplyCheckToo() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation pop = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        int vaultCell = DocksPopulation.bankVaultChestCell();
        ItemsLiteRegistry items = pop.items();

        CoinCensus before = CoinCensus.of(items, vaultCell);
        int taken = items.takeOnCell(vaultCell, ItemKinds.COIN, 500);
        assertEquals(500, taken, "500 Royals lifted out of the vault and destroyed");

        CoinCensus after = CoinCensus.of(items, vaultCell);
        assertFalse(CoinCensus.supplyClosed(before, after),
                "destroyed specie must fail the check, not just conjured specie");
        assertEquals(before.live() - 500, after.live());
    }

    @Test
    void anOrdinaryMoveKeepsTheSupplyClosed() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation pop = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        int vaultCell = DocksPopulation.bankVaultChestCell();
        ItemsLiteRegistry items = pop.items();

        CoinCensus before = CoinCensus.of(items, vaultCell);
        int purse = firstCoinCarrier(items);
        int moved = items.moveCarriedToCell(purse, vaultCell, ItemKinds.COIN, 3);
        assertEquals(3, moved);

        CoinCensus after = CoinCensus.of(items, vaultCell);
        assertTrue(CoinCensus.supplyClosed(before, after),
                "a pure move must NOT trip the check, or it is noise rather than a proof");
        assertEquals(before.vault() + 3, after.vault(), "the money went where it was sent");
        assertEquals(before.loose() - 3, after.loose());
    }

    @Test
    void aLiveDistrictKeepsItsMoneySupplyClosedAcrossASoak() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation pop = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        int vaultCell = DocksPopulation.bankVaultChestCell();
        CoinCensus atBake = CoinCensus.of(pop.items(), vaultCell);

        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(pop.system()));
        for (int t = 0; t < SOAK_TICKS; t++) {
            driver.requestStep();
        }

        CoinCensus atEnd = CoinCensus.of(pop.items(), vaultCell);
        assertTrue(CoinCensus.supplyClosed(atBake, atEnd),
                "the ward minted or burned Royals over " + SOAK_TICKS + " ticks: bake live="
                        + atBake.live() + " end live=" + atEnd.live());
        assertEquals(pop.bankAccounts().totalRoyals(), atEnd.vault(),
                "the ledger and the physical vault stack must still agree");
        // Distribution, not a total: a total can be one hoarder. The bake spreads pocket money
        // across the district, and a soak must not quietly funnel it into a single purse.
        assertTrue(atEnd.purses() > 1,
                "loose coin collapsed into " + atEnd.purses() + " purse(s)");
        assertTrue(atEnd.fattest() < atEnd.carried(),
                "one soul holds every loose Royal (fattest=" + atEnd.fattest()
                        + " of carried=" + atEnd.carried() + ")");
    }

    private static int firstCoinCarrier(ItemsLiteRegistry items) {
        for (int i = 0; i < items.size(); i++) {
            var e = items.get(i);
            if (e.kindId() == ItemKinds.COIN && e.locationCarriedBy() != Actor.NONE
                    && !items.isSunk(i)) {
                return e.locationCarriedBy();
            }
        }
        return Actor.NONE;
    }
}
