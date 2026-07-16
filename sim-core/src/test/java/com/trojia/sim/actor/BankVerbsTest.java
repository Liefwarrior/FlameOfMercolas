package com.trojia.sim.actor;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Pass 9 (Feature 4) — the counter-side money verbs ({@link BankVerbs}): deposit/withdraw as the
 * only Coin&lt;-&gt;Royal on/off-ramp (physical vault Coins move in lockstep with the ledger so
 * {@code totalRoyals() == vault COIN count} holds after every op), and {@code buyFood} as an
 * ID-authorized Royal transfer that refuses loose Coins and rejects an insufficient balance. The
 * card the buyer <em>carries</em> is what authorizes the account (stolen-card semantics).
 */
final class BankVerbsTest {

    private static final int VAULT = 7_777; // any non-SINK, non-zero cell (the bank's chest)
    private static final int A = 0;
    private static final int B = 1;
    private static final int SHOP = 2;

    /** The hard invariant, asserted after every verb. */
    private static void assertInvariant(BankLedger bank, ItemsLiteRegistry items) {
        assertEquals(bank.totalRoyals(), items.countOnCellOfKind(VAULT, ItemKinds.COIN),
                "totalRoyals() must equal the vault chest's COIN count");
    }

    @Test
    void depositWithdrawAndBuyFoodConserveMoneyAndHoldTheVaultInvariant() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int accA = bank.openAccount();
        int accB = bank.openAccount();
        int accShop = bank.openAccount();
        assertEquals(A, accA);
        assertEquals(B, accB);
        assertEquals(SHOP, accShop);

        // Seed: loose pocket Coins on A and B; the shop stocks FOOD; the vault starts empty.
        items.addCarried(A, ItemKinds.COIN, 100);
        items.addCarried(B, ItemKinds.COIN, 40);
        items.addCarried(SHOP, ItemKinds.FOOD, 10);
        int mintedCoin = 140;
        int mintedFood = 10;
        ItemsLiteEntry cardA = items.get(items.mintIdCard(A, accA));

        // Deposit A's whole purse: loose Coin -> vault, ledger credited by exactly what landed.
        assertEquals(100, BankVerbs.depositCoins(bank, items, VAULT, A, cardA));
        assertEquals(100, bank.balanceOf(accA));
        assertEquals(0, items.countCarriedOfKind(A, ItemKinds.COIN), "A's pocket Coins are all banked");
        assertInvariant(bank, items);

        // Buy 3 FOOD for 30 Royals: a pure ledger transfer A->shop; NO Coin moves; FOOD moves.
        assertTrue(BankVerbs.buyFood(bank, items, A, accShop, cardA, 30, 3));
        assertEquals(70, bank.balanceOf(accA));
        assertEquals(30, bank.balanceOf(accShop));
        assertEquals(3, items.countCarriedOfKind(A, ItemKinds.FOOD));
        assertEquals(7, items.countCarriedOfKind(SHOP, ItemKinds.FOOD));
        assertInvariant(bank, items); // vault + totalRoyals both unchanged by a transfer

        // Withdraw 20 Royals: ledger debits, physical Coins move vault -> A's carry.
        assertEquals(20, BankVerbs.withdrawRoyals(bank, items, VAULT, A, cardA, 20));
        assertEquals(50, bank.balanceOf(accA));
        assertEquals(20, items.countCarriedOfKind(A, ItemKinds.COIN));
        assertInvariant(bank, items);

        // COIN conservation: minted == vault(==sum accounts) + loose-on-persons + sunk.
        int loose = items.countCarriedOfKind(A, ItemKinds.COIN)
                + items.countCarriedOfKind(B, ItemKinds.COIN)
                + items.countCarriedOfKind(SHOP, ItemKinds.COIN);
        int vault = items.countOnCellOfKind(VAULT, ItemKinds.COIN);
        assertEquals(mintedCoin, vault + loose + items.sunkOfKind(ItemKinds.COIN));
        // FOOD conservation: only a move happened (no eating), so nothing sank.
        assertEquals(mintedFood, items.totalOfKind(ItemKinds.FOOD));
        assertEquals(0, items.sunkOfKind(ItemKinds.FOOD));
    }

    @Test
    void buyFoodRefusesWhenTheBuyerHasNoIdCard() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int accA = bank.openAccount();
        int accShop = bank.openAccount();
        bank.credit(accA, 100);
        items.addCarried(A, ItemKinds.COIN, 500); // A is flush with loose Coins...
        items.addCarried(SHOP, ItemKinds.FOOD, 5);

        // ...but institutions take Royals-with-ID only. No card carried -> purchaseAuth is NONE.
        int noCard = items.firstCarriedOfKind(A, ItemKinds.ID_CARD); // Actor.NONE
        ItemsLiteEntry card = noCard == Actor.NONE ? null : items.get(noCard);
        assertFalse(BankVerbs.buyFood(bank, items, A, accShop, card, 30, 1),
                "loose Coins are refused; no ID card means no institutional purchase");
        assertEquals(100, bank.balanceOf(accA), "no Royals moved");
        assertEquals(500, items.countCarriedOfKind(A, ItemKinds.COIN), "loose Coins untouched");
        assertEquals(5, items.countCarriedOfKind(SHOP, ItemKinds.FOOD), "no FOOD moved");
    }

    @Test
    void buyFoodRejectsAnInsufficientBalanceBeforeAnyFoodMoves() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int accA = bank.openAccount();
        int accShop = bank.openAccount();
        bank.credit(accA, 10); // too little for the 30-Royal loaf
        items.addCarried(A, ItemKinds.COIN, 1000); // loose Coins can't cover it either — refused
        items.addCarried(SHOP, ItemKinds.FOOD, 5);
        ItemsLiteEntry cardA = items.get(items.mintIdCard(A, accA));

        assertFalse(BankVerbs.buyFood(bank, items, A, accShop, cardA, 30, 1),
                "an insufficient Royal balance rejects the whole purchase");
        assertEquals(10, bank.balanceOf(accA), "balance untouched by a rejected buy");
        assertEquals(0, bank.balanceOf(accShop));
        assertEquals(5, items.countCarriedOfKind(SHOP, ItemKinds.FOOD), "no FOOD moved on rejection");
        assertEquals(0, items.countCarriedOfKind(A, ItemKinds.FOOD));
    }

    @Test
    void purchaseAuthReadsTheCarriedCardSoAStolenCardDrainsItsStampedAccount() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int accVictim = bank.openAccount();
        int accShop = bank.openAccount();
        bank.credit(accVictim, 200);
        int thief = 9;

        // The victim's card is lifted onto the thief; it still authorizes the victim's account.
        int card = items.mintIdCard(accVictim, accVictim);
        items.transferToActor(card, thief);
        assertEquals(accVictim, BankLedger.purchaseAuth(items.get(card)));

        // The thief buys FOOD "on the victim's tab": the transfer drains the VICTIM, not the thief.
        items.addCarried(accShop, ItemKinds.FOOD, 3); // FOOD lives on the shop's own account/actor
        assertTrue(BankVerbs.buyFood(bank, items, thief, accShop, items.get(card), 50, 1));
        assertEquals(150, bank.balanceOf(accVictim), "the stamped account paid");
        assertEquals(50, bank.balanceOf(accShop));
        assertEquals(1, items.countCarriedOfKind(thief, ItemKinds.FOOD), "the thief got the FOOD");
    }

    @Test
    void depositAndWithdrawDegradeToNoOpWhenNoVaultOrCardIsWired() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int accA = bank.openAccount();
        bank.credit(accA, 50);
        items.addCarried(A, ItemKinds.COIN, 30);
        ItemsLiteEntry cardA = items.get(items.mintIdCard(A, accA));

        assertEquals(0, BankVerbs.depositCoins(bank, items, Actor.NONE, A, cardA),
                "no vault wired -> deposit is a no-op");
        assertEquals(0, BankVerbs.depositCoins(bank, items, VAULT, A, null),
                "no card -> deposit is a no-op");
        assertEquals(0, BankVerbs.withdrawRoyals(bank, items, VAULT, A, cardA, 1000),
                "insufficient balance -> withdraw rejected");
        assertEquals(50, bank.balanceOf(accA), "no partial state change on a rejected withdraw");
        assertEquals(30, items.countCarriedOfKind(A, ItemKinds.COIN));
    }
}
