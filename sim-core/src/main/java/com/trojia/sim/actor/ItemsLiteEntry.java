package com.trojia.sim.actor;

/**
 * One ItemsLite entry (ACTORS-SPEC.md §2.6, §11.2): {@code itemId -> (kindId,
 * ownerActorId|NONE, location: carriedBy|cell)}, plus the addendum's one real
 * addition, {@code quantity} — a plain counter, never merged/split/consumed
 * by any policy in this milestone; every entry is exactly {@code 1} until a
 * future items-raws system declares a kind stackable (§11.2, test A53).
 *
 * @param itemId            registry-assigned id (dense)
 * @param kindId             raws item-kind id (future items registry; opaque here)
 * @param ownerActorId       owning actor, or {@link Actor#NONE}
 * @param locationCarriedBy  carrying actor id, or {@link Actor#NONE}
 * @param locationCell       cell it sits on, or {@link Actor#NONE} — mutually
 *                          exclusive with {@code locationCarriedBy}
 * @param quantity           stack count; {@code == 1} unless the kind is stackable
 */
public record ItemsLiteEntry(
        int itemId, short kindId, int ownerActorId,
        int locationCarriedBy, int locationCell, short quantity) {

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
}
