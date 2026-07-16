package com.trojia.sim.actor;

/**
 * One ItemsLite entry (ACTORS-SPEC.md §2.6, §11.2): {@code itemId -> (kindId,
 * ownerActorId|NONE, location: carriedBy|cell)}, plus a {@code quantity} counter
 * (the counted-stack model: money/food/cargo are one entry per owner/cell, not
 * one entry per unit) and a {@code accountId} the item may carry stamped.
 *
 * <p><b>Stamped {@code accountId} (Phase-0 economy F2).</b> An {@link
 * ItemKinds#ID_CARD} carries the bank {@code accountId} it authorizes, stamped
 * once at mint and preserved verbatim across every transfer/move (a stolen card
 * still authorizes its original account). Every non-card item carries {@link
 * Actor#NONE} here. The stamp is independent of {@code ownerActorId} and of the
 * carrier so that ownership/possession changes never silently re-point a card.
 *
 * @param itemId            registry-assigned id (dense)
 * @param kindId             raws item-kind id ({@link ItemKinds})
 * @param ownerActorId       owning actor, or {@link Actor#NONE}
 * @param locationCarriedBy  carrying actor id, or {@link Actor#NONE}
 * @param locationCell       cell it sits on, or {@link Actor#NONE} — mutually
 *                          exclusive with {@code locationCarriedBy}
 * @param quantity           stack count; {@code >= 1}. An {@code int} (not a
 *                          {@code short}): a vault COIN stack holds a whole
 *                          district's Royals ({@link BankLedger#totalRoyals()}),
 *                          which a {@code short}'s 32767 ceiling would clamp and
 *                          silently destroy specie (Phase-2 STEP A money-width fix)
 * @param accountId          bank account this item authorizes (ID_CARD), else
 *                          {@link Actor#NONE}
 */
public record ItemsLiteEntry(
        int itemId, short kindId, int ownerActorId,
        int locationCarriedBy, int locationCell, int quantity, int accountId) {

    public ItemsLiteEntry {
        if (locationCarriedBy != Actor.NONE && locationCell != Actor.NONE) {
            throw new IllegalArgumentException(
                    "locationCarriedBy and locationCell are mutually exclusive");
        }
        if (locationCarriedBy == Actor.NONE && locationCell == Actor.NONE) {
            throw new IllegalArgumentException(
                    "an item must have exactly one location: carriedBy or cell");
        }
        if (quantity < 1) {
            throw new IllegalArgumentException("quantity must be >= 1: " + quantity);
        }
    }

    /** Convenience for the common non-account item (stamps {@link Actor#NONE}). */
    public ItemsLiteEntry(int itemId, short kindId, int ownerActorId,
            int locationCarriedBy, int locationCell, int quantity) {
        this(itemId, kindId, ownerActorId, locationCarriedBy, locationCell, quantity, Actor.NONE);
    }
}
