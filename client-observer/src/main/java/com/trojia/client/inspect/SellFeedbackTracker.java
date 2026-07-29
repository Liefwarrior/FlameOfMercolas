package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.BankLedger;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.TradeGoods;

import java.util.function.IntSupplier;

/**
 * Narrates the played actor's counter sale as a toast (S8 — the {@link CullFeedbackTracker}
 * pattern applied to the counter): after {@code B} arms the sell intent, the sim resolves it next
 * tick through {@code SellVerb.sellMaterialsInReach} and stamps SOLD_GOODS or NO_BUYER_IN_REACH;
 * this tracker reads the stamp and toasts the human sentence. No check line here — a counter sale
 * is an exchange at a published price, not a contest, so there are no dice to show (the
 * formatter's no-formula-duplication rule). Zero sim writes; GL-free.
 *
 * <p><b>The toast names what was sold, how many, and for how much</b> (S8 playtest fix — it
 * shipped as a bare "your goods are sold", which is the end of the sprint's whole loop reported
 * as a shrug). The sim's stamp carries no detail, so the detail is measured client-side: {@link
 * #arm()} photographs the pack's priced MATERIALS stacks and the banked balance, and the
 * resolving tick's diff is the receipt. Measured, not predicted — a counter that could only
 * afford part of the lot is reported at what it actually paid.
 */
public final class SellFeedbackTracker {

    /** How many executed ticks an armed intent waits for its outcome stamp. */
    public static final int PENDING_TICKS = 10;

    /** The fallback sale line, for a sale whose detail could not be measured. */
    public static final String SOLD = "Coins on the counter -- your goods are sold.";
    public static final String NO_BUYER = "No counter within reach is buying.";

    private final ActorRegistry registry;
    private final ToastQueue toasts;
    private final IntSupplier playedActorId;
    /** The pack the receipt is measured against. */
    private final ItemsLiteRegistry items;
    /** The Royals ledger the receipt's amount is measured against. */
    private final BankLedger bank;

    private int pendingTicks;
    /** Per-{@link TradeGoods} row: units carried when {@code B} was pressed. */
    private final int[] carriedBefore = new int[TradeGoods.count()];
    private long royalsBefore;
    /** The actor the snapshot belongs to; a different one means the receipt is not ours. */
    private int snapshotActorId = Actor.NONE;

    public SellFeedbackTracker(ActorRegistry registry, ToastQueue toasts,
            IntSupplier playedActorId, ItemsLiteRegistry items, BankLedger bank) {
        this.registry = registry;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
        this.items = items;
        this.bank = bank;
    }

    /**
     * Arms the outcome watch (called by {@code SellInput.applySell} beside the intent) and
     * photographs the pack + purse, so the resolving tick can be read as a receipt.
     */
    public void arm() {
        pendingTicks = PENDING_TICKS;
        snapshotActorId = playedActorId.getAsInt();
        if (snapshotActorId == Actor.NONE) {
            return;
        }
        for (int i = 0; i < TradeGoods.count(); i++) {
            carriedBefore[i] = items.countCarriedOfKind(snapshotActorId, TradeGoods.at(i).kind());
        }
        royalsBefore = Purse.royalsOf(snapshotActorId, items, bank);
    }

    /** Call once per executed tick, after the tick ran (the driver's after-tick seam). */
    public void afterTick(long tick) {
        if (pendingTicks <= 0) {
            return;
        }
        int played = playedActorId.getAsInt();
        if (played == Actor.NONE) {
            pendingTicks = 0;
            return;
        }
        ReasonCode reason = registry.get(played).lastReasonCode();
        String line = reason == ReasonCode.SOLD_GOODS ? receiptLine(played) : outcomeLine(reason);
        if (line != null) {
            toasts.add(line);
            pendingTicks = 0;
            return;
        }
        pendingTicks--;
    }

    /**
     * The sale as a receipt: every priced stack that left the pack, named and counted, the Royals
     * that arrived for them, and the purse they landed in. Falls back to {@link #SOLD} when the
     * snapshot belongs to somebody else or nothing measurable moved.
     */
    private String receiptLine(int seller) {
        if (seller != snapshotActorId) {
            return SOLD;
        }
        StringBuilder sold = new StringBuilder();
        int lots = 0;
        for (int i = 0; i < TradeGoods.count(); i++) {
            short kind = TradeGoods.at(i).kind();
            int moved = carriedBefore[i] - items.countCarriedOfKind(seller, kind);
            if (moved <= 0) {
                continue;
            }
            if (lots++ > 0) {
                sold.append(", ");
            }
            sold.append(moved).append(" x ").append(Purse.itemName(kind));
        }
        if (lots == 0) {
            return SOLD;
        }
        long royalsNow = Purse.royalsOf(seller, items, bank);
        StringBuilder line = new StringBuilder("Sold ").append(sold);
        if (royalsNow != Purse.NO_ACCOUNT && royalsBefore != Purse.NO_ACCOUNT) {
            line.append(" for ").append(royalsNow - royalsBefore).append(" Royals -- purse ")
                    .append(royalsNow).append(" Royals.");
        } else {
            line.append(" at the counter.");
        }
        return line.toString();
    }

    /** The toast for a sell-outcome reason stamp, or {@code null} for any other reason. */
    public static String outcomeLine(ReasonCode reason) {
        if (reason == null) {
            return null;
        }
        return switch (reason) {
            case SOLD_GOODS -> SOLD;
            case NO_BUYER_IN_REACH -> NO_BUYER;
            default -> null;
        };
    }
}
