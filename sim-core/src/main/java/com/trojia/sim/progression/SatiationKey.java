package com.trojia.sim.progression;

/**
 * The per-{@code (skillId, contextKey)} satiation lookup key
 * (PROGRESSION-SPEC.md &sect;3.3). {@code contextKey} is an opaque long
 * (target species id, attacker entity id, tile-region id, lock/trap instance
 * id, item id, informant id, or a constant &mdash; the skill-specific meaning
 * is the caller's concern, per &sect;3.3's per-skill contextKey table).
 *
 * <p>Used as a {@link java.util.TreeMap} key (never a hash container, per the
 * project's determinism rule) &mdash; {@link Comparable} gives a stable
 * iteration order without ever needing one.</p>
 *
 * @param skillRaw   the skill's raw registry index
 * @param contextKey the skill-specific context discriminator
 */
public record SatiationKey(int skillRaw, long contextKey) implements Comparable<SatiationKey> {

    @Override
    public int compareTo(SatiationKey other) {
        int bySkill = Integer.compare(skillRaw, other.skillRaw);
        return bySkill != 0 ? bySkill : Long.compare(contextKey, other.contextKey);
    }
}
