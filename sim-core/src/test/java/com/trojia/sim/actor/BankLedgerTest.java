package com.trojia.sim.actor;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Pass 2 (F2) — the Royals ledger: ascending {@code accountId == index}, credit/debit/transfer/
 * balance verbs, insufficient-funds rejection, the hard {@code totalRoyals() == vault COIN count}
 * conservation invariant across a synthetic deposit/withdraw/transfer, and ID-card purchase auth.
 */
final class BankLedgerTest {

    private static final int VAULT_CELL = 999; // any non-SINK, non-zero cell (the bank's chest)

    @Test
    void openAccountAssignsAscendingIdsIndexedByActorId() {
        BankLedger bank = new BankLedger();
        assertEquals(0, bank.openAccount());
        assertEquals(1, bank.openAccount());
        assertEquals(2, bank.openAccount());
        assertEquals(3, bank.accountCount());
        assertEquals(0, bank.balanceOf(0));
        assertEquals(0, bank.totalRoyals());
    }

    @Test
    void transferMovesRoyalsAndRejectsInsufficientOrNegative() {
        BankLedger bank = new BankLedger();
        int a = bank.openAccount();
        int b = bank.openAccount();
        bank.credit(a, 100);

        assertTrue(bank.transfer(a, b, 40));
        assertEquals(60, bank.balanceOf(a));
        assertEquals(40, bank.balanceOf(b));
        assertEquals(100, bank.totalRoyals(), "a transfer never mints or burns");

        assertFalse(bank.transfer(a, b, 1000), "insufficient source balance is rejected");
        assertFalse(bank.transfer(a, b, -5), "a negative transfer is rejected");
        assertEquals(60, bank.balanceOf(a), "a rejected transfer leaves both balances untouched");
        assertEquals(40, bank.balanceOf(b));

        assertThrows(IllegalArgumentException.class, () -> bank.debit(b, 1000),
                "an over-debit throws (insufficient Royals)");
    }

    @Test
    void totalRoyalsEqualsVaultCoinCountAcrossDepositWithdrawAndTransfer() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int accA = bank.openAccount();
        int accB = bank.openAccount();
        int accShop = bank.openAccount();

        // Loose Coins minted to the two citizens; the vault chest starts empty.
        items.addCarried(accA, ItemKinds.COIN, 100);
        items.addCarried(accB, ItemKinds.COIN, 50);
        int mintedCoin = 150;
        assertInvariant(bank, items); // 0 == 0

        // Deposit: physical Coins move citizen-carry -> vault chest; ledger credits the account.
        deposit(bank, items, accA, 100);
        deposit(bank, items, accB, 50);
        assertEquals(150, bank.totalRoyals());
        assertInvariant(bank, items);

        // Purchase: a pure ledger transfer — no Coin moves, vault and totalRoyals both unchanged.
        assertTrue(bank.transfer(accA, accShop, 30));
        assertInvariant(bank, items);

        // Withdraw: ledger debits, physical Coins move vault -> citizen carry.
        withdraw(bank, items, accA, 20);
        assertEquals(130, bank.totalRoyals());
        assertInvariant(bank, items);

        // COIN conservation: minted == sum(accounts) [== vault] + loose + sunk.
        int loose = items.countCarriedOfKind(accA, ItemKinds.COIN)
                + items.countCarriedOfKind(accB, ItemKinds.COIN)
                + items.countCarriedOfKind(accShop, ItemKinds.COIN);
        long accounts = bank.totalRoyals();
        assertEquals(mintedCoin, accounts + loose + items.sunkOfKind(ItemKinds.COIN));
        assertEquals(20, loose);
    }

    @Test
    void purchaseAuthReadsTheStampedAccountOffTheCarriedCard() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int accA = bank.openAccount();
        int accShop = bank.openAccount();
        bank.credit(accA, 50);

        // A carries an ID_CARD stamped with its own account.
        int card = items.mintIdCard(accA, accA);
        int authorized = BankLedger.purchaseAuth(items.get(card));
        assertEquals(accA, authorized);

        // A pays the shop by transferring FROM the authorized account.
        assertTrue(bank.transfer(authorized, accShop, 30));
        assertEquals(20, bank.balanceOf(accA));
        assertEquals(30, bank.balanceOf(accShop));

        // Stolen card: lifted onto a thief, it still authorizes account A — so a raid drains A.
        int thief = 7;
        items.transferToActor(card, thief);
        assertEquals(accA, BankLedger.purchaseAuth(items.get(card)),
                "a stolen card authorizes its stamped account, not the carrier");
    }

    // ---- deposit/withdraw model a live counter's Coin<->Royal on/off-ramp against a synthetic vault

    private static void deposit(BankLedger bank, ItemsLiteRegistry items, int account, int amount) {
        int moved = items.moveCarriedToCell(account, VAULT_CELL, ItemKinds.COIN, amount);
        bank.credit(account, moved);
    }

    private static void withdraw(BankLedger bank, ItemsLiteRegistry items, int account, int amount) {
        bank.debit(account, amount);
        items.moveCellToCarried(VAULT_CELL, account, ItemKinds.COIN, amount);
    }

    private static void assertInvariant(BankLedger bank, ItemsLiteRegistry items) {
        assertEquals(bank.totalRoyals(), items.countOnCellOfKind(VAULT_CELL, ItemKinds.COIN),
                "hard invariant: totalRoyals() must equal the vault chest's COIN count");
    }
}
