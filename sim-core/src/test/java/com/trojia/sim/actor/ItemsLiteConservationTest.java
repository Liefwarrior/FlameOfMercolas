package com.trojia.sim.actor;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;

/**
 * Pass 1 (F1) — counted-stack ItemsLite: conservation accounting ({@code minted == held + sunk})
 * across a synthetic buy/eat/deposit, deterministic LIFO slot recycling, and stamped-{@code
 * accountId} preservation across moves.
 */
final class ItemsLiteConservationTest {

    private static final int BUYER = 0;
    private static final int SHOP = 1;
    private static final int VAULT_CELL = 12_345; // any non-SINK, non-zero cell

    @Test
    void conservationHoldsAcrossBuyEatAndDeposit() {
        ItemsLiteRegistry items = new ItemsLiteRegistry();

        // Mint: the buyer's purse (10 Coins) and the shop's larder (5 Food).
        items.addCarried(BUYER, ItemKinds.COIN, 10);
        items.addCarried(SHOP, ItemKinds.FOOD, 5);
        int mintedCoin = 10;
        int mintedFood = 5;
        assertConserved(items, mintedCoin, mintedFood);

        // Buy 2 Food for 6 Coins — two moves; a move creates/destroys nothing, so totals hold.
        assertEquals(2, items.moveCarried(SHOP, BUYER, ItemKinds.FOOD, 2));
        assertEquals(6, items.moveCarried(BUYER, SHOP, ItemKinds.COIN, 6));
        assertEquals(4, items.countCarriedOfKind(BUYER, ItemKinds.COIN));
        assertEquals(2, items.countCarriedOfKind(BUYER, ItemKinds.FOOD));
        assertEquals(6, items.countCarriedOfKind(SHOP, ItemKinds.COIN));
        assertConserved(items, mintedCoin, mintedFood);

        // Eat the buyer's whole Food stack — consumption sinks it (held -> sunk), total unchanged.
        assertEquals(2, items.takeCarried(BUYER, ItemKinds.FOOD, 2));
        assertEquals(0, items.countCarriedOfKind(BUYER, ItemKinds.FOOD));
        assertEquals(2, items.sunkOfKind(ItemKinds.FOOD));
        assertEquals(mintedFood - 2, items.liveOfKind(ItemKinds.FOOD)); // minted == held + sunk
        assertConserved(items, mintedCoin, mintedFood);

        // Deposit the buyer's Coins into the vault cell — carry -> cell, still conserved.
        assertEquals(4, items.moveCarriedToCell(BUYER, VAULT_CELL, ItemKinds.COIN, 4));
        assertEquals(4, items.countOnCellOfKind(VAULT_CELL, ItemKinds.COIN));
        assertEquals(0, items.countCarriedOfKind(BUYER, ItemKinds.COIN));
        assertConserved(items, mintedCoin, mintedFood);
    }

    /** Every kind holds {@code total == live + sunk}, and no move/sink ever changed a kind's total. */
    private static void assertConserved(ItemsLiteRegistry items, int mintedCoin, int mintedFood) {
        assertEquals(mintedCoin, items.totalOfKind(ItemKinds.COIN));
        assertEquals(mintedFood, items.totalOfKind(ItemKinds.FOOD));
        assertEquals(items.totalOfKind(ItemKinds.COIN),
                items.liveOfKind(ItemKinds.COIN) + items.sunkOfKind(ItemKinds.COIN));
        assertEquals(items.totalOfKind(ItemKinds.FOOD),
                items.liveOfKind(ItemKinds.FOOD) + items.sunkOfKind(ItemKinds.FOOD));
        assertEquals(items.total(), items.liveOfKind(ItemKinds.COIN) + items.liveOfKind(ItemKinds.FOOD)
                + items.sunk());
    }

    @Test
    void slotRecyclingIsDeterministicLifoAndTwinRunIdentical() {
        int[] first = recycleSequence();
        int[] second = recycleSequence();
        assertEquals(java.util.Arrays.toString(new int[] {0, 1, 2, 1, 0, 2, 3}),
                java.util.Arrays.toString(first),
                "recycling must pop the free stack LIFO and append only when it is empty");
        assertEquals(java.util.Arrays.toString(first), java.util.Arrays.toString(second),
                "two identical mint/sink sequences must assign identical itemIds (determinism)");
    }

    /** Mints a/b/c, sinks b, mints d (recycles 1); sinks c,a; mints e,f (recycle 0,2); mints g (append 3). */
    private static int[] recycleSequence() {
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int a = items.mintCarried(ItemKinds.COIN, 10);
        int b = items.mintCarried(ItemKinds.COIN, 11);
        int c = items.mintCarried(ItemKinds.COIN, 12);
        items.sink(b);
        int d = items.mintCarried(ItemKinds.COIN, 13); // recycles b's slot (LIFO)
        items.sink(c);
        items.sink(a);
        int e = items.mintCarried(ItemKinds.COIN, 14); // recycles a's slot (last sunk)
        int f = items.mintCarried(ItemKinds.COIN, 15); // recycles c's slot
        int g = items.mintCarried(ItemKinds.COIN, 16); // free stack empty -> append
        return new int[] {a, b, c, d, e, f, g};
    }

    @Test
    void quantityExceedsShortCeilingWithoutClampingOrTruncating() {
        // STEP A money-width fix: a vault-sized COIN stack (well past a short's 32767 ceiling) must
        // survive add/take/move intact — the old short quantity silently clamped and destroyed units.
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int big = 2_000_000; // > Short.MAX_VALUE (32767), the amount a short would have truncated

        assertEquals(big, items.addOnCell(VAULT_CELL, ItemKinds.COIN, big),
                "the full amount is added, not clamped to Short.MAX_VALUE");
        assertEquals(big, items.countOnCellOfKind(VAULT_CELL, ItemKinds.COIN));
        assertEquals(big, items.totalOfKind(ItemKinds.COIN), "no truncation of the minted total");

        // A single add on top of a near-2M stack still lands (int headroom), not wrapping.
        assertEquals(500_000, items.addOnCell(VAULT_CELL, ItemKinds.COIN, 500_000));
        assertEquals(big + 500_000, items.countOnCellOfKind(VAULT_CELL, ItemKinds.COIN));

        // Move the whole vault stack into a carrier: the move returns what LANDED, and it is exact.
        int moved = items.moveCellToCarried(VAULT_CELL, BUYER, ItemKinds.COIN, big + 500_000);
        assertEquals(big + 500_000, moved, "moveCellToCarried returns the dest-added amount, exact");
        assertEquals(big + 500_000, items.countCarriedOfKind(BUYER, ItemKinds.COIN));
        assertEquals(0, items.countOnCellOfKind(VAULT_CELL, ItemKinds.COIN));
        assertEquals(big + 500_000, items.totalOfKind(ItemKinds.COIN), "conserved across the move");
    }

    @Test
    void moveReturnsDestinationAddedAmount() {
        // STEP A: the deposit pattern credits Royals by the move's return, so it must equal what
        // actually landed in the destination (not merely what was removed from the source).
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        items.addCarried(BUYER, ItemKinds.COIN, 40);
        assertEquals(25, items.moveCarried(BUYER, SHOP, ItemKinds.COIN, 25),
                "returns the amount added to the destination");
        assertEquals(25, items.countCarriedOfKind(SHOP, ItemKinds.COIN));
        assertEquals(15, items.countCarriedOfKind(BUYER, ItemKinds.COIN));
        assertEquals(15, items.moveCarriedToCell(BUYER, VAULT_CELL, ItemKinds.COIN, 100),
                "capped at what the source held; returns the dest-added amount");
        assertEquals(15, items.countOnCellOfKind(VAULT_CELL, ItemKinds.COIN));
    }

    @Test
    void stampedAccountIdSurvivesTransferAndMove() {
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int card = items.mintIdCard(5, 42);
        assertEquals(42, items.get(card).accountId());
        assertEquals(42, BankLedger.purchaseAuth(items.get(card)));

        // A "stolen" card lifted onto actor 9 still authorizes account 42, not 9.
        items.transferToActor(card, 9);
        assertEquals(9, items.get(card).locationCarriedBy());
        assertEquals(42, items.get(card).accountId());
        assertEquals(42, BankLedger.purchaseAuth(items.get(card)));

        // Dropping it on a cell also preserves the stamp.
        items.placeOnCell(card, Actor.NONE, VAULT_CELL);
        assertEquals(42, items.get(card).accountId());

        // A non-card item authorizes nothing.
        int coin = items.mintCarried(ItemKinds.COIN, 1);
        assertNotEquals(ItemKinds.ID_CARD, items.get(coin).kindId());
        assertEquals(Actor.NONE, BankLedger.purchaseAuth(items.get(coin)));
    }
}
