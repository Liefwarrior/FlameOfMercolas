package com.trojia.client.scenario;

import java.util.List;

/**
 * The bake-side identity table (Sprint 1 "Every Soul Has a Name"): one immutable
 * {@link Identity} row per ActorId, indexed by id. Built once by {@link NameForge} as a pure
 * function of the finished bake (registry + homes + relationships + jobs + name raws) under
 * the {@code identity.names} named RNG stream — it is scenario data, never sim-core state:
 * nothing on the tick path ever reads it, it is never serialized into a TROJSAV, and it is
 * never hashed into the ACTORS section (the twin-run tick hash is provably identical with or
 * without it — see {@code DocksIdentityDeterminismTest}).
 *
 * <p>Designed liftable (flagged to SIM-CORE in the sprint plan): a plain int-indexed row
 * table with a canonical text form ({@link #canonicalTable()}), so the day witness reports
 * or event logs want names, the shape moves without redesign.
 */
public final class IdentityRegistry {

    /**
     * One actor's forged identity.
     *
     * @param actorId   the ActorId this row belongs to (== its table index)
     * @param givenName the drawn given name ({@code ""} for notables and the unnamed)
     * @param surname   the household surname ({@code ""} where none applies)
     * @param fullName  the display name: {@code given surname} for forged citizens, the
     *                  authored name for notables, a kennel name for owned beasts, a plain
     *                  descriptor ("a wharf mouse") for the deliberately nameless
     * @param epithet   the byname shown after the name ({@code ""} when none)
     * @param bio       the one-line templated bio, or the notable's authored 2-3 sentences
     * @param named     whether this row is a real name (false for ferals/mice/cats — the
     *                  deliberately nameless, per the sprint plan)
     * @param notableId the {@code notables.json} id bound to this actor, or {@code null}
     */
    public record Identity(int actorId, String givenName, String surname, String fullName,
            String epithet, String bio, boolean named, String notableId) {
    }

    /** The empty table — what a fixture without a NameForge pass exposes. */
    public static final IdentityRegistry EMPTY = new IdentityRegistry(List.of());

    private final List<Identity> rows;

    IdentityRegistry(List<Identity> rows) {
        this.rows = List.copyOf(rows);
    }

    public int size() {
        return rows.size();
    }

    /** The identity of {@code actorId}; throws if the id is out of range. */
    public Identity get(int actorId) {
        return rows.get(actorId);
    }

    /**
     * The whole table in one canonical text form — one {@code |}-separated row per actor in
     * ascending id order, {@code \n}-terminated. The twin-bake byte-identity gate compares
     * exactly this string; it is also what a future save-side lift would fingerprint.
     */
    public String canonicalTable() {
        StringBuilder out = new StringBuilder(rows.size() * 96);
        for (Identity row : rows) {
            out.append(row.actorId()).append('|').append(row.givenName()).append('|')
                    .append(row.surname()).append('|').append(row.fullName()).append('|')
                    .append(row.epithet()).append('|').append(row.named()).append('|')
                    .append(row.notableId() == null ? "-" : row.notableId()).append('|')
                    .append(row.bio()).append('\n');
        }
        return out.toString();
    }
}
