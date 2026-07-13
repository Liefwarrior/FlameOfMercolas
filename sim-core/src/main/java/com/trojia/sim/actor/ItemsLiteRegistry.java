package com.trojia.sim.actor;

import java.util.ArrayList;
import java.util.List;

/**
 * The ItemsLite side-table (ACTORS-SPEC.md §2.6): dense, sorted by
 * {@code itemId} (array index == itemId, the {@link HomeRegistry} /
 * {@link ActorRegistry} convention). Minted at import/bake time and by
 * VEND/GIVE_ALMS-equivalent verbs (not built in this foundation milestone —
 * {@link #mint} is the shared entry point a later policy would call).
 */
public final class ItemsLiteRegistry {

    private final List<ItemsLiteEntry> entries = new ArrayList<>();

    /** Mints a new item carried by {@code carriedBy} (or on {@code cell} if {@code carriedBy} is NONE). */
    public int mint(short kindId, int ownerActorId, int carriedBy, int cell, short quantity) {
        int itemId = entries.size();
        entries.add(new ItemsLiteEntry(itemId, kindId, ownerActorId, carriedBy, cell, quantity));
        return itemId;
    }

    public int size() {
        return entries.size();
    }

    public ItemsLiteEntry get(int itemId) {
        return entries.get(itemId);
    }
}
