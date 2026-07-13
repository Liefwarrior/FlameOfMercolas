package com.trojia.sim.actor;

/**
 * The identity seam mandated by DECISIONS.md ("Identity (FOR LATER)"): every
 * actor carries a true identity and a presented identity, presented defaulting
 * to true at spawn. ALL social reads (law, deference, witness reports, logs,
 * faces) use {@link #presentedId()} — never {@link #trueId()} — per the
 * ACTORS-SPEC.md §1.1 contract. Both slots are {@code ActorId}-shaped ints (the
 * TROJSAV {@code ACTR} record stores them as {@code personaTrue}/
 * {@code personaPresented}, ACTORS-SPEC.md §2.8); full disguise gameplay
 * ({@code setActAs()}) lands with Play mode — this record is only the seam.
 *
 * @param trueId      the actor's real identity (its own {@code ActorId} value)
 * @param presentedId the identity every social system reads; equals
 *                    {@code trueId} until a future {@code setActAs()}
 */
public record Persona(int trueId, int presentedId) {

    /** The default persona for a freshly spawned actor: presented == true. */
    public static Persona of(int actorId) {
        return new Persona(actorId, actorId);
    }

    /** Whether this actor is currently presenting as someone/something else. */
    public boolean isDisguised() {
        return trueId != presentedId;
    }
}
