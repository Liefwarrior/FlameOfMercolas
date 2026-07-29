package com.trojia.client.inspect;

import com.trojia.client.hud.HudText;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.BankLedger;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.TradeGoods;

/**
 * WHAT AN ACTOR IS WORTH, on screen (S8 playtest fix): until this class existed, no UI in the
 * client read {@link BankLedger} at all — a player could cull a scalp, carry it across the ward,
 * sell it at a counter, and have no way to find out what they earned or whether they were richer
 * than when they started.
 *
 * <p><b>Royals and Coins are different money and the screen has to say so.</b> A Royal is a
 * ledger balance an {@link ItemKinds#ID_CARD} authorizes — the thing a counter sale settles into.
 * A Coin is physical specie in the pack. The bank counter is the only ramp between them, so
 * selling a scalp moves the Royals number and leaves the Coins number exactly where it was; a
 * player who cannot see both lines cannot tell a sale from a pickpocketing.
 *
 * <p>An actor carrying no card has no account to name, which is not the same as being broke:
 * {@link #NO_ACCOUNT} reads as {@code "(no account)"} rather than as a zero. Pure reads — no sim
 * writes, no draws.
 */
public final class Purse {

    /** {@link #royalsOf}'s "no card, so no account authorizes anything" sentinel. */
    public static final long NO_ACCOUNT = HudText.NO_ACCOUNT;

    private Purse() {
    }

    /**
     * The account this actor's carried ID card authorizes, or {@link Actor#NONE} — the same
     * {@code purchaseAuth} read the sell verb settles against (authorization is the stamped
     * account, not the carrier).
     */
    public static int accountOf(int actorId, ItemsLiteRegistry items) {
        int card = items.firstCarriedOfKind(actorId, ItemKinds.ID_CARD);
        return card == Actor.NONE ? Actor.NONE : items.get(card).accountId();
    }

    /** Banked Royals behind this actor's card, or {@link #NO_ACCOUNT} when nothing authorizes. */
    public static long royalsOf(int actorId, ItemsLiteRegistry items, BankLedger bank) {
        int account = accountOf(actorId, items);
        if (account < 0 || account >= bank.accountCount()) {
            return NO_ACCOUNT;
        }
        return bank.balanceOf(account);
    }

    /** Loose {@link ItemKinds#COIN} in this actor's pack — specie, not ledger. */
    public static int coinsOf(int actorId, ItemsLiteRegistry items) {
        return items.countCarriedOfKind(actorId, ItemKinds.COIN);
    }

    /** The one-line HUD readout for this actor: {@link HudText#purse}, live off both tables. */
    public static String line(int actorId, ItemsLiteRegistry items, BankLedger bank) {
        return HudText.purse(royalsOf(actorId, items, bank), coinsOf(actorId, items));
    }

    /**
     * A carried item's human name: the {@link TradeGoods} symbol with its underscores opened up
     * ({@code rat_scalp} &rarr; {@code rat scalp}), so a toast and the character sheet name the
     * same thing the same way. A kind the trade table does not describe keeps its raw id rather
     * than inventing a word for it.
     */
    public static String itemName(short kind) {
        String symbol = TradeGoods.symbolOf(kind);
        return symbol == null ? "kind " + kind : symbol.replace('_', ' ');
    }
}
