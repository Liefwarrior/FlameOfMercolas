package com.trojia.sim.json;

import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;

/**
 * An immutable JSON object that preserves <em>source insertion order</em>.
 *
 * <p>Members are stored in an {@code ArrayList}-backed immutable list in exactly the
 * order they appeared in the source text (or were supplied to the constructor).
 * Iteration never goes through a hash table, so member order is byte-for-byte
 * reproducible across runs and JVMs — a hard requirement for canonical raws
 * round-trips and content fingerprinting.</p>
 *
 * <p><strong>Duplicate keys are illegal.</strong> The parser rejects them with a
 * positioned {@link JsonParseException}; this constructor independently rejects them
 * with {@link IllegalArgumentException} so programmatically built trees obey the same
 * raws-hygiene rule.</p>
 *
 * <p>Name lookup ({@link #get(String)}) is a linear scan over the member list —
 * deliberately: raws objects are small, and a scan is trivially deterministic.
 * Names are compared by exact {@link String#equals(Object)} (case-sensitive, after
 * escape resolution).</p>
 *
 * <p>Equality is <em>order-sensitive</em>: two objects are equal iff they hold equal
 * members in the same order. This matches the canonical-writer guarantee that equal
 * trees produce identical text.</p>
 */
public final class JsonObject implements JsonValue {

    /**
     * One name/value pair of a {@link JsonObject}.
     *
     * @param name  the member name after escape resolution; never {@code null}
     * @param value the member value; never {@code null} (JSON {@code null} is
     *              represented by {@link JsonNull#INSTANCE})
     */
    public record Member(String name, JsonValue value) {

        /**
         * @throws NullPointerException if {@code name} or {@code value} is {@code null}
         */
        public Member {
            Objects.requireNonNull(name, "name");
            Objects.requireNonNull(value, "value");
        }
    }

    /** Members in insertion order; immutable. */
    private final List<Member> members;

    /**
     * Creates an object holding the given members in the given order.
     *
     * <p>The list is defensively copied; later mutation of the argument does not
     * affect this object.</p>
     *
     * @param members the members in the desired (insertion) order
     * @throws NullPointerException     if {@code members} or any element is {@code null}
     * @throws IllegalArgumentException if two members share the same name
     */
    public JsonObject(List<Member> members) {
        this.members = List.copyOf(members);
        Set<String> seen = new HashSet<>(); // membership checks only — never iterated
        for (Member member : this.members) {
            if (!seen.add(member.name())) {
                throw new IllegalArgumentException("duplicate key \"" + member.name() + "\"");
            }
        }
    }

    /**
     * Returns the members in insertion order.
     *
     * @return an immutable list; never {@code null}
     */
    public List<Member> members() {
        return members;
    }

    /**
     * Returns the number of members.
     *
     * @return the member count, {@code >= 0}
     */
    public int size() {
        return members.size();
    }

    /**
     * Returns whether a member with the given name exists.
     *
     * @param name the member name to look up; must not be {@code null}
     * @return {@code true} iff a member with exactly that name exists
     * @throws NullPointerException if {@code name} is {@code null}
     */
    public boolean has(String name) {
        return get(name) != null;
    }

    /**
     * Returns the value of the member with the given name, or {@code null} if absent.
     *
     * <p>Note the distinction: a member whose value is JSON {@code null} yields
     * {@link JsonNull#INSTANCE}; only a <em>missing</em> member yields {@code null}.</p>
     *
     * @param name the member name to look up; must not be {@code null}
     * @return the member's value, or {@code null} if no such member exists
     * @throws NullPointerException if {@code name} is {@code null}
     */
    public JsonValue get(String name) {
        Objects.requireNonNull(name, "name");
        for (Member member : members) {
            if (member.name().equals(name)) {
                return member.value();
            }
        }
        return null;
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof JsonObject that && members.equals(that.members);
    }

    @Override
    public int hashCode() {
        return members.hashCode();
    }

    /**
     * Returns the canonical JSON text of this object per {@link MiniJson#write(JsonValue)}.
     */
    @Override
    public String toString() {
        return MiniJson.write(this);
    }
}
