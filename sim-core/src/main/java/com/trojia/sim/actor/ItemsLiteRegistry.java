package com.trojia.sim.actor;

import java.util.ArrayList;
import java.util.List;

/**
 * The ItemsLite side-table (ACTORS-SPEC.md §2.6): dense, sorted by
 * {@code itemId} (array index == itemId, the {@link HomeRegistry} /
 * {@link ActorRegistry} convention). The single source of truth for every
 * physical item's kind, owner, quantity and location — an actor carries nothing
 * that is not an entry here (there is no parallel per-actor id list to fall out
 * of sync or truncate).
 *
 * <p><b>Mutation model.</b> {@link ItemsLiteEntry} is an immutable record, so
 * "moving" an item replaces its dense slot with a fresh record of the same
 * {@code itemId} (never mutates in place). Consuming an item (a meal eaten,
 * cargo shipped off-map) relocates it to {@link #SINK_CELL} owned by nobody via
 * {@link #sink}: the slot is kept (so the array stays dense and round-trips) but
 * every by-owner / by-carrier query stops counting it — the economic
 * equivalent of destruction, while keeping strict conservation accounting
 * checkable ({@code total == live + sunk}).
 *
 * <p><b>Slot recycling.</b> {@link #sink} pushes the vacated slot onto a
 * deterministic LIFO free stack that the next {@link #mint} pops first, so the
 * dense array's size tracks the LIVE item count rather than growing without
 * bound under the district's constant churn (every meal minted then eaten, every
 * emptied purse minted anew). This also closes the {@code short}-id truncation
 * landmine: bounded live-item counts keep every {@code itemId} well within range
 * for the save format, and carried value never rides a truncating {@code short}
 * list on {@link Actor} in the first place — it lives here.
 */
public final class ItemsLiteRegistry {

    /**
     * The "void" cell consumed/exported items are parked on: owner and carrier
     * both {@link Actor#NONE}, cell {@code == SINK_CELL}. A real, valid packed
     * cell (world origin) that no Docks actor ever stands on (the fixture
     * offsets every cell by a full chunk), so it never collides with a live
     * item's location while satisfying {@link ItemsLiteEntry}'s "exactly one
     * location" invariant.
     */
    public static final int SINK_CELL = 0;

    private final List<ItemsLiteEntry> entries = new ArrayList<>();

    /**
     * Free (sunk) dense slots available for reuse — a deterministic LIFO stack of {@code itemId}s
     * a {@link #sink} has vacated. Reuse order is a plain LIFO of a fixed insertion order, so two
     * identical runs recycle identically.
     */
    private int[] freeStack = new int[16];
    private int freeCount;

    // ---------------------------------------------------------------- mint

    /** Mints a new item (accountId {@link Actor#NONE}). */
    public int mint(short kindId, int ownerActorId, int carriedBy, int cell, int quantity) {
        return mint(kindId, ownerActorId, carriedBy, cell, quantity, Actor.NONE);
    }

    /**
     * Mints a new item carried by {@code carriedBy} (or on {@code cell} if {@code carriedBy} is
     * NONE), stamping {@code accountId} (only meaningful for {@link ItemKinds#ID_CARD}). Recycles a
     * vacated dense slot first when one is available (deterministic LIFO). {@code quantity} is an
     * {@code int} (STEP A money-width fix): a vault COIN stack counts a whole district's Royals.
     */
    public int mint(short kindId, int ownerActorId, int carriedBy, int cell, int quantity,
            int accountId) {
        if (freeCount > 0) {
            int itemId = freeStack[--freeCount]; // recycle a vacated slot
            entries.set(itemId, new ItemsLiteEntry(itemId, kindId, ownerActorId, carriedBy, cell,
                    quantity, accountId));
            return itemId;
        }
        int itemId = entries.size();
        entries.add(new ItemsLiteEntry(itemId, kindId, ownerActorId, carriedBy, cell, quantity,
                accountId));
        return itemId;
    }

    /** Convenience: mint one unit carried and owned by {@code actorId}. */
    public int mintCarried(short kindId, int actorId) {
        return mint(kindId, actorId, actorId, Actor.NONE, 1);
    }

    /** Convenience: mint one unit sitting on {@code cell}, owned by {@code ownerActorId} (or NONE). */
    public int mintOnCell(short kindId, int ownerActorId, int cell) {
        return mint(kindId, ownerActorId, Actor.NONE, cell, 1);
    }

    /**
     * Mints the one {@link ItemKinds#ID_CARD} a citizen carries, owned by and carried by
     * {@code holderId}, stamped with the {@code accountId} it authorizes (== the holder's account
     * at bake, but the stamp — not the holder — is what {@link BankLedger#purchaseAuth} reads, so
     * the card keeps authorizing that account even if it is later lifted onto a thief).
     */
    public int mintIdCard(int holderId, int accountId) {
        return mint(ItemKinds.ID_CARD, holderId, holderId, Actor.NONE, 1, accountId);
    }

    public int size() {
        return entries.size();
    }

    public ItemsLiteEntry get(int itemId) {
        return entries.get(itemId);
    }

    // ---------------------------------------------------------------- persistence (recycling state)

    /**
     * Free-slot stack support for save/load: the recycling free stack is genuine state (it decides
     * which {@code itemId} the next {@link #mint} reuses), so it round-trips rather than being
     * recomputed. {@link #freeSlotCount()} / {@link #freeSlotAt(int)} read it in LIFO push order;
     * {@link #restoreFreeSlot(int)} rebuilds it on load AFTER every entry has been re-minted (so
     * mints during load append and keep {@code itemId} order, then the stack is restored verbatim).
     */
    public int freeSlotCount() {
        return freeCount;
    }

    public int freeSlotAt(int index) {
        return freeStack[index];
    }

    /** Pushes {@code itemId} back onto the free stack (load-time reconstruction only). */
    public void restoreFreeSlot(int itemId) {
        if (freeCount == freeStack.length) {
            freeStack = java.util.Arrays.copyOf(freeStack, freeStack.length * 2);
        }
        freeStack[freeCount++] = itemId;
    }

    // ---------------------------------------------------------------- moves (transfers)

    /**
     * Transfers item {@code itemId} to {@code toActorId} (now owner and carrier); the ItemsLite
     * half of an {@code ItemTransferred} event (§2.8). Preserves kind, quantity and the stamped
     * {@code accountId} — a move creates/destroys nothing and never re-stamps a card.
     */
    public void transferToActor(int itemId, int toActorId) {
        ItemsLiteEntry e = entries.get(itemId);
        entries.set(itemId, new ItemsLiteEntry(itemId, e.kindId(), toActorId,
                toActorId, Actor.NONE, e.quantity(), e.accountId()));
    }

    /** Places item {@code itemId} on {@code cell}, owned by {@code ownerActorId} (or {@link Actor#NONE}). */
    public void placeOnCell(int itemId, int ownerActorId, int cell) {
        ItemsLiteEntry e = entries.get(itemId);
        entries.set(itemId, new ItemsLiteEntry(itemId, e.kindId(), ownerActorId,
                Actor.NONE, cell, e.quantity(), e.accountId()));
    }

    /**
     * Consumes item {@code itemId} — a meal eaten, cargo shipped off-map: parks it on
     * {@link #SINK_CELL} owned by nobody. Still present (dense slot kept), but no by-owner /
     * by-carrier query counts it again; the slot is offered to the next {@link #mint}.
     */
    public void sink(int itemId) {
        if (isSunk(itemId)) {
            return; // already vacated — never double-free a slot
        }
        ItemsLiteEntry e = entries.get(itemId);
        entries.set(itemId, new ItemsLiteEntry(itemId, e.kindId(), Actor.NONE,
                Actor.NONE, SINK_CELL, e.quantity(), e.accountId()));
        if (freeCount == freeStack.length) {
            freeStack = java.util.Arrays.copyOf(freeStack, freeStack.length * 2);
        }
        freeStack[freeCount++] = itemId; // available for the next mint to recycle
    }

    /** {@code true} iff {@code itemId} has been consumed/exported (sunk). */
    public boolean isSunk(int itemId) {
        ItemsLiteEntry e = entries.get(itemId);
        return e.locationCarriedBy() == Actor.NONE && e.locationCell() == SINK_CELL
                && e.ownerActorId() == Actor.NONE;
    }

    // ---------------------------------------------------------------- queries

    /** Count of units of {@code kindId} currently carried by {@code actorId} (a purse / stock probe). */
    public int countCarriedOfKind(int actorId, short kindId) {
        int total = 0;
        for (ItemsLiteEntry e : entries) {
            if (e.locationCarriedBy() == actorId && e.kindId() == kindId) {
                total += e.quantity();
            }
        }
        return total;
    }

    /** The first (lowest-id) item of {@code kindId} carried by {@code actorId}, or {@link Actor#NONE}. */
    public int firstCarriedOfKind(int actorId, short kindId) {
        for (int i = 0; i < entries.size(); i++) {
            ItemsLiteEntry e = entries.get(i);
            if (e.locationCarriedBy() == actorId && e.kindId() == kindId) {
                return i;
            }
        }
        return Actor.NONE;
    }

    /**
     * Every live (non-sunk) item carried by {@code actorId}, ascending {@code itemId} — the
     * inspector's purse view now that {@link Actor} keeps no parallel id list. A fresh list, safe
     * for the caller to hold.
     */
    public List<ItemsLiteEntry> carriedBy(int actorId) {
        List<ItemsLiteEntry> carried = new ArrayList<>();
        for (int i = 0; i < entries.size(); i++) {
            ItemsLiteEntry e = entries.get(i);
            if (e.locationCarriedBy() == actorId) {
                carried.add(e);
            }
        }
        return carried;
    }

    /** The first (lowest-id) item of {@code kindId} sitting on {@code cell}, or {@link Actor#NONE}. */
    public int firstOnCellOfKind(int cell, short kindId) {
        if (cell == SINK_CELL) {
            return Actor.NONE; // the void is never a live location
        }
        for (int i = 0; i < entries.size(); i++) {
            ItemsLiteEntry e = entries.get(i);
            if (e.locationCarriedBy() == Actor.NONE && e.locationCell() == cell
                    && e.kindId() == kindId) {
                return i;
            }
        }
        return Actor.NONE;
    }

    /** Count of units of {@code kindId} sitting on {@code cell} (a vault chest / staged cargo probe). */
    public int countOnCellOfKind(int cell, short kindId) {
        if (cell == SINK_CELL) {
            return 0;
        }
        int total = 0;
        for (ItemsLiteEntry e : entries) {
            if (e.locationCarriedBy() == Actor.NONE && e.locationCell() == cell
                    && e.kindId() == kindId) {
                total += e.quantity();
            }
        }
        return total;
    }

    /** Total units of {@code kindId} ever minted (held anywhere + sunk) — the conservation baseline. */
    public int totalOfKind(short kindId) {
        int total = 0;
        for (ItemsLiteEntry e : entries) {
            if (e.kindId() == kindId) {
                total += e.quantity();
            }
        }
        return total;
    }

    /** Total units of {@code kindId} that have been consumed/exported (sunk). */
    public int sunkOfKind(short kindId) {
        int total = 0;
        for (int i = 0; i < entries.size(); i++) {
            if (entries.get(i).kindId() == kindId && isSunk(i)) {
                total += entries.get(i).quantity();
            }
        }
        return total;
    }

    /**
     * Units of {@code kindId} still in circulation (minted minus consumed/exported) — the
     * conservation baseline the economy holds invariant across every transfer. A transfer that
     * empties a stack sinks it and credits the counterpart, leaving {@code live} unchanged; only a
     * genuine mint raises it, only a {@link #sink} lowers it.
     */
    public int liveOfKind(short kindId) {
        return totalOfKind(kindId) - sunkOfKind(kindId);
    }

    /** Total units across every kind, minted (held + sunk) — the overall conservation baseline. */
    public int total() {
        int total = 0;
        for (ItemsLiteEntry e : entries) {
            total += e.quantity();
        }
        return total;
    }

    /** Total sunk units across every kind. */
    public int sunk() {
        int total = 0;
        for (int i = 0; i < entries.size(); i++) {
            if (isSunk(i)) {
                total += entries.get(i).quantity();
            }
        }
        return total;
    }

    // ---------------------------------------------------------------- counted-stack ops

    private void replaceQuantity(int itemId, int newQuantity) {
        ItemsLiteEntry e = entries.get(itemId);
        entries.set(itemId, new ItemsLiteEntry(itemId, e.kindId(), e.ownerActorId(),
                e.locationCarriedBy(), e.locationCell(), newQuantity, e.accountId()));
    }

    /**
     * The largest number of units that can be added to a stack currently holding {@code existing}
     * without overflowing {@code int} (STEP A money-width guard) — {@code n}, clamped so the sum
     * never wraps negative. With {@code int} quantities the ceiling is {@code Integer.MAX_VALUE},
     * far above any realistic Coin/Food supply, so the clamp is a safety net, not a live limiter.
     */
    private static int addableWithoutOverflow(int existing, int n) {
        long headroom = (long) Integer.MAX_VALUE - existing;
        return (int) Math.min(n, headroom);
    }

    /**
     * Credits up to {@code n} units of {@code kindId} to {@code actorId}'s carried stack. Returns
     * the amount actually added (which the overflow guard can only reduce below {@code n} at the
     * {@code int} ceiling — a case the closed money/food supply never reaches). The move verbs read
     * this return so a deposit credits the ledger by exactly what landed, never by what was removed.
     */
    public int addCarried(int actorId, short kindId, int n) {
        if (n <= 0) {
            return 0;
        }
        int existing = firstCarriedOfKind(actorId, kindId);
        if (existing != Actor.NONE) {
            int add = addableWithoutOverflow(entries.get(existing).quantity(), n);
            replaceQuantity(existing, entries.get(existing).quantity() + add);
            return add;
        }
        int add = addableWithoutOverflow(0, n);
        mint(kindId, actorId, actorId, Actor.NONE, add);
        return add;
    }

    /**
     * Removes up to {@code n} units of {@code kindId} from {@code actorId}'s carried stacks,
     * sinking any stack it empties. Returns the amount actually taken (for a consume/EAT this is
     * the destruction; for a move the counterpart is credited via {@link #addCarried}).
     */
    public int takeCarried(int actorId, short kindId, int n) {
        int remaining = n;
        for (int i = 0; i < entries.size() && remaining > 0; i++) {
            ItemsLiteEntry e = entries.get(i);
            if (e.locationCarriedBy() != actorId || e.kindId() != kindId) {
                continue;
            }
            if (e.quantity() <= remaining) {
                remaining -= e.quantity();
                sink(i);
            } else {
                replaceQuantity(i, e.quantity() - remaining);
                remaining = 0;
            }
        }
        return n - remaining;
    }

    /**
     * Moves up to {@code n} units of {@code kindId} from {@code fromId} to {@code toId}. Returns the
     * amount that actually LANDED in the destination (STEP A fix), not the amount removed from the
     * source — the deposit pattern credits Royals by this return, so it must equal what arrived.
     */
    public int moveCarried(int fromId, int toId, short kindId, int n) {
        int taken = takeCarried(fromId, kindId, n);
        return addCarried(toId, kindId, taken);
    }

    /**
     * Stages up to {@code n} units of {@code kindId} on {@code cell}, owned by nobody (a vault /
     * cargo cell). Returns the amount actually added (overflow-guarded, STEP A money-width fix).
     */
    public int addOnCell(int cell, short kindId, int n) {
        if (n <= 0 || cell == SINK_CELL) {
            return 0;
        }
        int existing = firstOnCellOfKind(cell, kindId);
        if (existing != Actor.NONE) {
            int add = addableWithoutOverflow(entries.get(existing).quantity(), n);
            replaceQuantity(existing, entries.get(existing).quantity() + add);
            return add;
        }
        int add = addableWithoutOverflow(0, n);
        mint(kindId, Actor.NONE, Actor.NONE, cell, add);
        return add;
    }

    /** Removes up to {@code n} units of {@code kindId} from {@code cell}, sinking emptied stacks. Returns taken. */
    public int takeOnCell(int cell, short kindId, int n) {
        if (cell == SINK_CELL) {
            return 0;
        }
        int remaining = n;
        for (int i = 0; i < entries.size() && remaining > 0; i++) {
            ItemsLiteEntry e = entries.get(i);
            if (e.locationCarriedBy() != Actor.NONE || e.locationCell() != cell
                    || e.kindId() != kindId) {
                continue;
            }
            if (e.quantity() <= remaining) {
                remaining -= e.quantity();
                sink(i);
            } else {
                replaceQuantity(i, e.quantity() - remaining);
                remaining = 0;
            }
        }
        return n - remaining;
    }

    /**
     * Moves up to {@code n} units of {@code kindId} from {@code actorId}'s carry onto {@code cell}.
     * Returns the amount that LANDED on the cell (STEP A fix) — a counter deposit credits Royals by
     * exactly this, so it must equal the destination delta, not the source removal.
     */
    public int moveCarriedToCell(int actorId, int cell, short kindId, int n) {
        int taken = takeCarried(actorId, kindId, n);
        return addOnCell(cell, kindId, taken);
    }

    /**
     * Moves up to {@code n} units of {@code kindId} off {@code cell} into {@code actorId}'s carry.
     * Returns the amount that LANDED in the actor's carry (STEP A fix).
     */
    public int moveCellToCarried(int cell, int actorId, short kindId, int n) {
        int taken = takeOnCell(cell, kindId, n);
        return addCarried(actorId, kindId, taken);
    }
}
