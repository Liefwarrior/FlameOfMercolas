package com.trojia.sim.actor;

/**
 * A residence (ACTORS-SPEC.md §11.1): {@code homeCell} is a PropertyIndex
 * "sleeping spot" tile. Occupants are DERIVED, never stored — "who lives at
 * Home #H" is a scan of actors where {@code actor.homeId() == H}, ascending
 * ActorId (the §2.9.5/§10.4 single-source-of-truth rule, applied a third
 * time) — see {@link HomeRegistry#occupantsOf}.
 *
 * @param homeId   the registry-assigned id (dense, 0-based)
 * @param homeCell the sleeping-spot cell (PackedPos)
 */
public record Home(int homeId, int homeCell) {
}
