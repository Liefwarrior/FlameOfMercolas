package com.trojia.sim.actor;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Sprint 6 slice 4 (Eli's bug 5 — "shopkeepers to both sell AND buy... a simple exchange of
 * either coin for item or item for coin"): the buy-side counter. {@link BankVerbs#sellToShop}
 * is the citizen coin FAUCET — the shop pays from its OWN ledger pocket (a transfer, never a
 * mint; a broke shop clamps the lot), the items MOVE seller → shop — and
 * {@link BankVerbs#buyMeal} closes the loop by vending the bought FISH back out as a meal
 * (FOOD first, FISH when the food shelf is bare). Money conservation is exact after every op.
 */
final class BuySideVerbsTest {

    private static final int SELLER = 0;
    private static final int SHOP = 1;
    private static final int BUYER = 2;

    @Test
    void sellToShopPaysCoinForItemFromTheShopsOwnPocket() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int accSeller = bank.openAccount();
        int accShop = bank.openAccount();
        bank.credit(accShop, 50);
        long totalBefore = bank.totalRoyals();
        items.addCarried(SELLER, ItemKinds.FISH, 4);
        ItemsLiteEntry card = items.get(items.mintIdCard(SELLER, accSeller));

        int sold = BankVerbs.sellToShop(bank, items, SELLER, SHOP, card, 3, ItemKinds.FISH, 4);

        assertEquals(4, sold, "the whole catch sells");
        assertEquals(12, bank.balanceOf(accSeller), "coin FOR item: 4 x 3 Royals to the seller");
        assertEquals(38, bank.balanceOf(accShop), "paid from the shop's own pocket");
        assertEquals(totalBefore, bank.totalRoyals(), "a transfer, never a mint");
        assertEquals(0, items.countCarriedOfKind(SELLER, ItemKinds.FISH));
        assertEquals(4, items.countCarriedOfKind(SHOP, ItemKinds.FISH), "item FOR coin: moved, not sunk");
    }

    @Test
    void aBrokeShopClampsTheLotToWhatItCanAfford() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int accSeller = bank.openAccount();
        int accShop = bank.openAccount();
        bank.credit(accShop, 7); // can afford 2 fish at 3 apiece, not 3
        items.addCarried(SELLER, ItemKinds.FISH, 3);
        ItemsLiteEntry card = items.get(items.mintIdCard(SELLER, accSeller));

        int sold = BankVerbs.sellToShop(bank, items, SELLER, SHOP, card, 3, ItemKinds.FISH, 3);

        assertEquals(2, sold, "the lot clamps to the shop's pocket — a smaller honest exchange");
        assertEquals(6, bank.balanceOf(accSeller));
        assertEquals(1, bank.balanceOf(accShop));
        assertEquals(1, items.countCarriedOfKind(SELLER, ItemKinds.FISH), "the unsold fish stays");
        assertEquals(2, items.countCarriedOfKind(SHOP, ItemKinds.FISH));
    }

    @Test
    void noCardOrNothingCarriedSellsNothing() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        bank.openAccount();
        int accShop = bank.openAccount();
        bank.credit(accShop, 50);
        items.addCarried(SELLER, ItemKinds.FISH, 2);

        assertEquals(0, BankVerbs.sellToShop(bank, items, SELLER, SHOP, null, 3,
                ItemKinds.FISH, 2), "no ID card -> no institutional exchange");
        assertEquals(0, BankVerbs.sellToShop(bank, items, BUYER, SHOP,
                items.get(items.mintIdCard(BUYER, 0)), 3, ItemKinds.FISH, 2),
                "carrying none of the kind -> nothing to sell");
        assertEquals(50, bank.balanceOf(accShop), "no coin moved either way");
    }

    @Test
    void buyMealVendsFoodFirstThenTheBoughtFish() {
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        bank.openAccount();
        int accShop = bank.openAccount();
        int accBuyer = bank.openAccount();
        bank.credit(accBuyer, 20);
        long totalBefore = bank.totalRoyals();
        items.addCarried(SHOP, ItemKinds.FOOD, 1);
        items.addCarried(SHOP, ItemKinds.FISH, 1);
        ItemsLiteEntry card = items.get(items.mintIdCard(BUYER, accBuyer));

        assertEquals(ItemKinds.FOOD,
                BankVerbs.buyMeal(bank, items, BUYER, SHOP, card, 5), "FOOD shelf first");
        assertEquals(ItemKinds.FISH,
                BankVerbs.buyMeal(bank, items, BUYER, SHOP, card, 5),
                "the bought fish vends once the food shelf is bare — the loop closes");
        assertEquals(0, BankVerbs.buyMeal(bank, items, BUYER, SHOP, card, 5),
                "a bare counter vends nothing");
        assertEquals(10, bank.balanceOf(accBuyer));
        assertEquals(10, bank.balanceOf(accShop));
        assertEquals(totalBefore, bank.totalRoyals(), "transfers only — conservation exact");
        assertEquals(1, items.countCarriedOfKind(BUYER, ItemKinds.FOOD));
        assertEquals(1, items.countCarriedOfKind(BUYER, ItemKinds.FISH));
    }

    @Test
    void theFullFaucetLoopConservesEveryRoyalAndEveryFish() {
        // fisher sells 3 fish to the shop; a hungry buyer buys one back as a meal: every
        // Royal is a transfer, every fish a move — the closed-supply identities hold.
        BankLedger bank = new BankLedger();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int accFisher = bank.openAccount();
        int accShop = bank.openAccount();
        int accBuyer = bank.openAccount();
        bank.credit(accShop, 30);
        bank.credit(accBuyer, 10);
        long totalBefore = bank.totalRoyals();
        items.addCarried(SELLER, ItemKinds.FISH, 3);
        int fishMinted = 3;
        ItemsLiteEntry fisherCard = items.get(items.mintIdCard(SELLER, accFisher));
        ItemsLiteEntry buyerCard = items.get(items.mintIdCard(BUYER, accBuyer));

        BankVerbs.sellToShop(bank, items, SELLER, SHOP, fisherCard,
                FoodEconomy.FISH_BUY_PRICE, ItemKinds.FISH, 3);
        BankVerbs.buyMeal(bank, items, BUYER, SHOP, buyerCard, FoodEconomy.FOOD_PRICE);

        assertEquals(totalBefore, bank.totalRoyals(), "money conservation EXACT");
        assertEquals(fishMinted, items.liveOfKind(ItemKinds.FISH),
                "every fish is still live somewhere (sold + vended = moves, never mints/sinks)");
        assertEquals(9, bank.balanceOf(accFisher), "the fisher earned coin — the faucet");
        assertEquals(26, bank.balanceOf(accShop), "the shop paid 9, took 5 back — recirculation");
        assertEquals(5, bank.balanceOf(accBuyer));
    }
}
