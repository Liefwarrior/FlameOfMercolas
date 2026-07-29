package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.TradeGoods;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * THE MUTATION PROOF for the seven S8 trade-good conservation lines — the
 * {@code CoinCensusMutationTest} discipline applied to the new kinds, for the same reason: a
 * conservation line that cannot fail is decoration, and this repo shipped one of those for
 * several sprints before S8 caught it.
 *
 * <p>Each of the seven kinds is broken ON ITS OWN, in both directions, so nobody can claim
 * "the goods lines pass" on the strength of one kind:
 *
 * <ul>
 *   <li>counterfeit — units conjured into a purse with no mint recorded ⇒ that kind's line
 *       must go false, and ONLY that kind's;</li>
 *   <li>phantom mint — a mint recorded with no item produced ⇒ false as well (a one-sided
 *       check misses half the ways a supply leaks);</li>
 *   <li>burn — units destroyed out of circulation ⇒ false;</li>
 *   <li>ordinary MOVE between two souls ⇒ still TRUE, or the check is just noise that fires
 *       on normal trade.</li>
 * </ul>
 */
class GoodsConservationMutationTest {

    private static DocksPopulation freshWard() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        return DocksPopulation.build(loaded.worldSeed(), loaded.world());
    }

    private static GoodsCensus census(DocksPopulation pop, short kind) {
        return GoodsCensus.of(pop.system(), pop.items(), pop.registry(), kind);
    }

    @Test
    void theSevenKindsAreTheFourMaterialsAndTheThreeVerminScalps() {
        assertEquals(7, GoodsCensus.KINDS.length);
        for (short kind : GoodsCensus.KINDS) {
            assertEquals(TradeGoods.Category.MATERIALS, TradeGoods.categoryOf(kind),
                    TradeGoods.symbolOf(kind) + " must sell as MATERIALS");
        }
    }

    @Test
    void everyKindStartsClosedAtBake() {
        DocksPopulation pop = freshWard();
        for (short kind : GoodsCensus.KINDS) {
            GoodsCensus c = census(pop, kind);
            assertTrue(c.closed(), c.symbol() + " must be closed at bake");
            assertEquals(0, c.minted(), c.symbol() + " is not seeded at bake");
        }
    }

    @Test
    void counterfeitBreaksExactlyTheKindItTouches() {
        for (short kind : GoodsCensus.KINDS) {
            DocksPopulation pop = freshWard();
            ItemsLiteRegistry items = pop.items();
            int purse = firstCoinCarrier(items);
            assertNotEquals(Actor.NONE, purse, "the bake must put someone in a pocket");

            assertEquals(9, items.addCarried(purse, kind, 9), "9 units conjured");

            GoodsCensus broken = census(pop, kind);
            assertFalse(broken.closed(), TradeGoods.symbolOf(kind)
                    + ": 9 units appeared from nowhere and the line still said PASS");
            assertEquals(9, broken.live(), "and the line names the size of the hole");
            assertEquals(1, broken.holders(), "one distinct holder, counted not derived");

            for (short other : GoodsCensus.KINDS) {
                if (other != kind) {
                    assertTrue(census(pop, other).closed(), TradeGoods.symbolOf(other)
                            + " must be unaffected — these are seven independent lines");
                }
            }
        }
    }

    @Test
    void aPhantomMintBreaksTheOtherDirection() {
        for (short kind : GoodsCensus.KINDS) {
            DocksPopulation pop = freshWard();
            pop.system().recordGoodsMintedAtBake(kind, 4);
            GoodsCensus broken = census(pop, kind);
            assertFalse(broken.closed(), TradeGoods.symbolOf(kind)
                    + ": 4 units were booked and never produced, and the line said PASS");
            assertEquals(4, broken.minted());
            assertEquals(0, broken.live());
        }
    }

    @Test
    void burningStockBreaksTheLineToo() {
        for (short kind : GoodsCensus.KINDS) {
            DocksPopulation pop = freshWard();
            ItemsLiteRegistry items = pop.items();
            int purse = firstCoinCarrier(items);
            items.addCarried(purse, kind, 6);
            pop.system().recordGoodsMintedAtBake(kind, 6);
            assertTrue(census(pop, kind).closed(), "an honest mint books straight");

            assertEquals(6, items.takeCarried(purse, kind, 6), "6 units destroyed");
            assertFalse(census(pop, kind).closed(), TradeGoods.symbolOf(kind)
                    + ": stock vanished out of circulation and the line said PASS");
        }
    }

    @Test
    void anOrdinaryMoveKeepsEveryLineTrue() {
        DocksPopulation pop = freshWard();
        ItemsLiteRegistry items = pop.items();
        int from = firstCoinCarrier(items);
        int to = secondCoinCarrier(items, from);
        assertNotEquals(Actor.NONE, to, "the bake must put two souls in pockets");
        for (short kind : GoodsCensus.KINDS) {
            items.addCarried(from, kind, 5);
            pop.system().recordGoodsMintedAtBake(kind, 5);
        }
        for (short kind : GoodsCensus.KINDS) {
            assertEquals(3, items.moveCarried(from, to, kind, 3));
            GoodsCensus c = census(pop, kind);
            assertTrue(c.closed(), TradeGoods.symbolOf(kind)
                    + ": a plain move must not trip the check");
            assertEquals(5, c.live(), "a move conserves");
            assertEquals(2, c.holders(), "and the distribution now shows two souls");
        }
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

    private static int secondCoinCarrier(ItemsLiteRegistry items, int notThis) {
        for (int i = 0; i < items.size(); i++) {
            var e = items.get(i);
            if (e.kindId() == ItemKinds.COIN && e.locationCarriedBy() != Actor.NONE
                    && e.locationCarriedBy() != notThis && !items.isSunk(i)) {
                return e.locationCarriedBy();
            }
        }
        return Actor.NONE;
    }
}
