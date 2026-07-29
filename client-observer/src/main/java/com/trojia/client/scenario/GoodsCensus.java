package com.trojia.client.scenario;

import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorsSystem;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.TradeGoods;

/**
 * One S8 trade good's closed-supply reading, taken the way {@code CoinCensus} taught this
 * repo to take one: a COUNTER kept at the mint site, checked against an INDEPENDENT physical
 * scan of {@link ItemsLiteRegistry}. Nothing here is derived from anything else, which is the
 * whole difference between a check and the tautology that printed PASS for several sprints.
 *
 * <p>Per kind, never lumped. A single "goods minted" total is satisfied by one busy yard while
 * three others mint nothing, so each kind carries its own identity
 * ({@code minted == live + sunk}) and each can fail alone.
 *
 * <p>The sunk side reads {@link ActorsSystem#goodsSunk} and deliberately NOT
 * {@code items.sunkOfKind}: {@link ItemsLiteRegistry#sink} leaves a vacated slot's old
 * quantity in place, so a stack that was fully MOVED to a counter still reads at its old size
 * and would be counted as a phantom sink. ({@code liveOfKind} is safe — the phantom cancels
 * between total and sunk.)
 *
 * <p>The holder columns are the honesty columns: {@code holders} and {@code fattest} are what
 * separate "200 units across the ward" from "200 units in one hoarder's sack".
 */
public record GoodsCensus(short kind, String symbol, long minted, int live, long sunk,
        int holders, int fattest, int fattestId) {

    /**
     * The seven kinds S8 adds, in ascending {@link ItemKinds} id order: the four craft-yard
     * materials, then the three vermin scalps (no human scalp — combat is out of this arc).
     */
    public static final short[] KINDS = {
        ItemKinds.CORDAGE, ItemKinds.PITCH, ItemKinds.BARREL_STOCK, ItemKinds.SALT,
        ItemKinds.RAT_SCALP, ItemKinds.GULL_SCALP, ItemKinds.CAT_SCALP};

    /** Whether this kind's supply is closed: every unit minted is still held or was sunk. */
    public boolean closed() {
        return minted == live + sunk;
    }

    /** The kind's trade category (all seven are MATERIALS — Eli's ruling on scalps). */
    public TradeGoods.Category category() {
        return TradeGoods.categoryOf(kind);
    }

    /**
     * Takes {@code kind}'s reading right now: the mint/sink counters off {@code system}, the
     * live total and the holder distribution off an ascending-index scan of the registry.
     */
    public static GoodsCensus of(ActorsSystem system, ItemsLiteRegistry items,
            ActorRegistry registry, short kind) {
        int holders = 0;
        int fattest = 0;
        int fattestId = -1;
        for (int i = 0; i < registry.size(); i++) {
            int held = items.countCarriedOfKind(i, kind);
            if (held > 0) {
                holders++;
                if (held > fattest) {
                    fattest = held;
                    fattestId = i;
                }
            }
        }
        return new GoodsCensus(kind, TradeGoods.symbolOf(kind), system.goodsMinted(kind),
                items.liveOfKind(kind), system.goodsSunk(kind), holders, fattest, fattestId);
    }
}
