package com.trojia.sim.actor.spell;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

/**
 * The immutable spell universe, keyed by raw index exactly like {@code SkillRegistry} — a
 * spell's "raw" is its position here, and that is the only handle any intent, toast or button
 * ever carries. Built once from raws at boot; never mutated.
 *
 * <p>Ordering is the raws loader's (alphabetical by key, the {@code SkillRawsLoader}
 * convention), so identical raws bytes give identical raw indices on every machine — which is
 * what lets a spell raw ride an intent without ever touching the save.
 */
public final class SpellRegistry {

    /** The degraded no-spell universe: nothing is castable, every lookup misses. */
    public static final SpellRegistry EMPTY = new SpellRegistry(List.of());

    private final List<SpellDefinition> spells;

    private SpellRegistry(List<SpellDefinition> spells) {
        this.spells = List.copyOf(spells);
    }

    /** Builds a registry over {@code spells} in the given order; duplicate keys are rejected. */
    public static SpellRegistry of(Collection<SpellDefinition> spells) {
        List<SpellDefinition> copy = new ArrayList<>(spells);
        for (int i = 0; i < copy.size(); i++) {
            for (int j = i + 1; j < copy.size(); j++) {
                if (copy.get(i).key().equals(copy.get(j).key())) {
                    throw new IllegalArgumentException("duplicate spell id " + copy.get(i).key());
                }
            }
        }
        return new SpellRegistry(copy);
    }

    public int size() {
        return spells.size();
    }

    /** The spell at a raw index. */
    public SpellDefinition get(int raw) {
        return spells.get(raw);
    }

    /** The raw index of {@code key}, or {@code -1} when this universe has no such spell. */
    public int rawOf(String key) {
        for (int i = 0; i < spells.size(); i++) {
            if (spells.get(i).key().equals(key)) {
                return i;
            }
        }
        return -1;
    }

    /** Whether {@code raw} addresses a spell in this universe. */
    public boolean isValidRaw(int raw) {
        return raw >= 0 && raw < spells.size();
    }

    /** Every spell, in raw-index order. */
    public List<SpellDefinition> all() {
        return spells;
    }
}
