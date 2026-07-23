package com.trojia.sim.actor.faction;

import java.util.List;
import java.util.Objects;

/**
 * One immutable faction definition (Sprint 1 faction registry): the raws row of
 * {@code content/raws/factions/factions.json} in loader-normalized form — the
 * {@code SkillDefinition}/{@code Material} content-record shape.
 *
 * <p>The record deliberately carries no numeric id: identity is assigned by
 * {@link FactionRegistry} from the sorted key order (the platform-stable convention every
 * raws-driven registry in this project shares).</p>
 *
 * @param key         unique string id from the raw ({@code "id"} field), e.g. {@code "watch"}
 * @param displayName human-readable name, e.g. {@code "The Watch"}
 * @param memberJobs  the job raws keys whose holders are members (jobs.json ids, e.g.
 *                    {@code "watch.patrol"}); may be empty (a faction of standing only)
 */
public record FactionDefinition(String key, String displayName, List<String> memberJobs) {

    /**
     * @throws NullPointerException     if a required reference is {@code null}
     * @throws IllegalArgumentException if {@code key} is empty
     */
    public FactionDefinition {
        Objects.requireNonNull(key, "key");
        Objects.requireNonNull(displayName, "displayName");
        memberJobs = List.copyOf(Objects.requireNonNull(memberJobs, "memberJobs"));
        if (key.isEmpty()) {
            throw new IllegalArgumentException("faction key must be non-empty");
        }
    }
}
