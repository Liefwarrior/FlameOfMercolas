package com.trojia.sim.actor;

import java.util.Objects;

/**
 * A raws-authored actor type key (ACTORS-SPEC.md §1.1: {@code typeId}),
 * e.g. {@code "militia_watch"}, {@code "animal_dock_dog"}. Ordered
 * lexicographically so registries can sort deterministically by it.
 *
 * @param key the raws id; lower-case, non-empty
 */
public record ActorTypeId(String key) implements Comparable<ActorTypeId> {

    public ActorTypeId {
        Objects.requireNonNull(key, "key");
        if (key.isEmpty()) {
            throw new IllegalArgumentException("actor type id must be non-empty");
        }
    }

    /** Creates a type id from its raws string. */
    public static ActorTypeId of(String key) {
        return new ActorTypeId(key);
    }

    @Override
    public int compareTo(ActorTypeId other) {
        return key.compareTo(other.key);
    }

    @Override
    public String toString() {
        return key;
    }
}
