package com.trojia.sim.actor.faction;

import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Objects;

/**
 * The immutable, boot-built faction universe (Sprint 1): mirrors
 * {@code com.trojia.sim.progression.SkillRegistry}'s conventions exactly —
 * deterministic id assignment by sorted string key ({@code 0..n-1}), deeply immutable,
 * safe to share.
 *
 * <p>Also owns the job→faction membership resolution ({@link #factionOfJob}): the raws
 * declare membership as job-key lists per faction, flattened here to a sorted parallel
 * lookup at build time. A job may belong to at most ONE faction (validated); jobs listed
 * nowhere are unaffiliated ({@code -1}) — wastrels, the Wielder, beasts.</p>
 */
public final class FactionRegistry {

    private final FactionDefinition[] byId;
    private final String[] sortedKeys;
    /** Sorted job keys with parallel faction raw ids — the flattened membership map. */
    private final String[] memberJobKeys;
    private final int[] memberJobFaction;

    private FactionRegistry(FactionDefinition[] byId) {
        this.byId = byId;
        this.sortedKeys = new String[byId.length];
        for (int i = 0; i < byId.length; i++) {
            sortedKeys[i] = byId[i].key();
        }
        int jobCount = 0;
        for (FactionDefinition def : byId) {
            jobCount += def.memberJobs().size();
        }
        String[] jobs = new String[jobCount];
        int k = 0;
        for (FactionDefinition def : byId) {
            for (String job : def.memberJobs()) {
                jobs[k++] = job;
            }
        }
        Arrays.sort(jobs);
        for (int i = 1; i < jobs.length; i++) {
            if (jobs[i].equals(jobs[i - 1])) {
                throw new IllegalArgumentException(
                        "job belongs to more than one faction: " + jobs[i]);
            }
        }
        int[] factionOf = new int[jobs.length];
        for (int f = 0; f < byId.length; f++) {
            for (String job : byId[f].memberJobs()) {
                factionOf[Arrays.binarySearch(jobs, job)] = f;
            }
        }
        this.memberJobKeys = jobs;
        this.memberJobFaction = factionOf;
    }

    /**
     * Builds a registry from the given definitions. Input order is irrelevant: ids are
     * assigned from the sorted key order.
     *
     * @throws IllegalArgumentException if a faction key is duplicated or a job key is
     *                                  claimed by more than one faction
     */
    public static FactionRegistry of(Collection<FactionDefinition> factions) {
        Objects.requireNonNull(factions, "factions");
        FactionDefinition[] byId = factions.toArray(new FactionDefinition[0]);
        Arrays.sort(byId, (a, b) -> a.key().compareTo(b.key()));
        for (int i = 1; i < byId.length; i++) {
            if (byId[i].key().equals(byId[i - 1].key())) {
                throw new IllegalArgumentException("duplicate faction key: " + byId[i].key());
            }
        }
        return new FactionRegistry(byId);
    }

    /** Returns the number of factions. */
    public int size() {
        return byId.length;
    }

    /** Whether a faction with the given string id exists. */
    public boolean contains(String key) {
        return Arrays.binarySearch(sortedKeys, Objects.requireNonNull(key, "key")) >= 0;
    }

    /**
     * Resolves a string id to its assigned raw id.
     *
     * @throws IllegalArgumentException if no such faction exists
     */
    public int rawId(String key) {
        int index = Arrays.binarySearch(sortedKeys, Objects.requireNonNull(key, "key"));
        if (index < 0) {
            throw new IllegalArgumentException("unknown faction id: " + key);
        }
        return index;
    }

    /**
     * Returns the definition at a raw id index.
     *
     * @throws IllegalArgumentException if {@code index} is out of range
     */
    public FactionDefinition get(int index) {
        if (index < 0 || index >= byId.length) {
            throw new IllegalArgumentException("faction id out of range: " + index);
        }
        return byId[index];
    }

    /** All factions in id order (index == raw id); immutable. */
    public List<FactionDefinition> all() {
        return List.of(byId);
    }

    /**
     * The faction a job's holders belong to (jobs.json key → faction raw id), or {@code -1}
     * for an unaffiliated job. Pure sorted-array lookup.
     */
    public int factionOfJob(String jobKey) {
        int index = Arrays.binarySearch(memberJobKeys, Objects.requireNonNull(jobKey, "jobKey"));
        return index < 0 ? -1 : memberJobFaction[index];
    }
}
