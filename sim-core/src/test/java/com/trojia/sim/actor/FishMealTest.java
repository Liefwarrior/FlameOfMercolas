package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Shopkeeper;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 6 slice 4, the demand side of the fish loop: a FISH is a meal. A hungry actor eats
 * its own carried catch in place (the {@code SeekFoodPolicy} step-1 fast path), and a counter
 * whose FOOD shelf is bare but that holds bought-in FISH still counts as a stocked,
 * targetable market for a hungry solvent buyer — so the fish a shop buys off citizens flows
 * back out as meals instead of shelving forever.
 */
final class FishMealTest {

    private static final int Z = 1;

    @Test
    void aHungryActorEatsItsOwnCarriedCatchInPlace() {
        ActorRegistry registry = new ActorRegistry();
        Actor fisher = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                PackedPos.pack(5, 5, Z));
        NoOpActorContext ctx = new NoOpActorContext(registry);
        fisher.setHomeId(ctx.homes().addHome(PackedPos.pack(0, 0, Z)));
        ctx.items().addCarried(fisher.id(), ItemKinds.FISH, 2);
        fisher.applyNeedDelta(Need.HUNGER, -8100); // below CRITICAL

        int before = fisher.need(Need.HUNGER);
        Policies.SEEK_FOOD.act(fisher, ctx);

        assertEquals(1, ctx.items().countCarriedOfKind(fisher.id(), ItemKinds.FISH),
                "one carried fish eaten in place");
        assertTrue(fisher.need(Need.HUNGER) >= before + FoodEconomy.EAT_RESTORE - 100,
                "the fish restored HUNGER like any meal");
        assertEquals(ReasonCode.ATE_FOOD, fisher.lastReasonCode());
    }

    @Test
    void aFishOnlyCounterStillFeedsASolventBuyer() {
        ActorRegistry registry = new ActorRegistry();
        int shopCell = PackedPos.pack(6, 5, Z);
        Actor buyer = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                PackedPos.pack(5, 5, Z));
        Actor shop = registry.spawn(Shopkeeper.TYPE, ActorTestFixtures.stats(Shopkeeper.TYPE),
                shopCell);
        FoodMarket market = new FoodMarket(new int[] {shop.id()}, new int[0], new int[0]);
        NoOpActorContext ctx = new NoOpActorContext(registry) {
            @Override
            public FoodMarket foodMarket() {
                return market;
            }
        };
        buyer.setHomeId(ctx.homes().addHome(PackedPos.pack(0, 0, Z)));
        shop.setHomeId(ctx.homes().addHome(shopCell));
        int accBuyer = ctx.bankAccounts().openAccount();
        int accShop = ctx.bankAccounts().openAccount();
        assertEquals(buyer.id(), accBuyer);
        assertEquals(shop.id(), accShop);
        ctx.bankAccounts().credit(accBuyer, 50);
        ctx.items().mintIdCard(buyer.id(), accBuyer);
        ctx.items().addCarried(shop.id(), ItemKinds.FISH, 3); // fish-ONLY stock, no FOOD
        buyer.applyNeedDelta(Need.HUNGER, -8100); // below CRITICAL
        long totalBefore = ctx.bankAccounts().totalRoyals();

        Policies.SEEK_FOOD.act(buyer, ctx);

        assertEquals(2, ctx.items().countCarriedOfKind(shop.id(), ItemKinds.FISH),
                "the fish-only counter vended a fish meal");
        assertEquals(ReasonCode.BOUGHT_FOOD, buyer.lastReasonCode());
        assertTrue(ctx.bankAccounts().balanceOf(accShop) > 0, "the counter was PAID");
        assertEquals(totalBefore, ctx.bankAccounts().totalRoyals(), "conservation exact");
    }
}
