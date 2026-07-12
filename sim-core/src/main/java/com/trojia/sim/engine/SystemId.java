package com.trojia.sim.engine;

import com.trojia.sim.random.RandomSource;

/**
 * Stable identity of a {@link SimulationSystem}: a human-readable name plus a
 * 64-bit salt that seeds the system's counter-based RNG derivation and names
 * its TROJSAV section hash. Salts are collision-checked at engine boot; a
 * collision is a boot failure, never a silent reseed.
 *
 * @param name stable, unique, lower-case system name (e.g. {@code "fire"})
 * @param salt 64-bit RNG/hash salt, unique across all registered systems
 */
public record SystemId(String name, long salt) {

    public SystemId {
        if (name == null || name.isEmpty()) {
            throw new IllegalArgumentException("system name must be non-empty");
        }
    }

    /**
     * Derives a {@code SystemId} whose salt is a pure, platform-independent
     * function of {@code name} (folded through {@link RandomSource#mix64(long)}).
     * Equal names always produce equal salts; the engine's boot-time collision
     * check therefore also rejects duplicate names.
     */
    public static SystemId of(String name) {
        if (name == null || name.isEmpty()) {
            throw new IllegalArgumentException("system name must be non-empty");
        }
        long h = 0x5CA1AB1E_0DDBA11L;
        for (int i = 0; i < name.length(); i++) {
            h = RandomSource.mix64(h ^ name.charAt(i));
        }
        return new SystemId(name, h);
    }

    /**
     * The 4-character TROJSAV section id this system's state is saved under
     * (ARCHITECTURE.md §9): the first four letters/digits of {@link #name()}
     * upper-cased, padded with {@code '_'} — e.g. {@code "fluids"} →
     * {@code "FLUI"}, {@code "fire"} → {@code "FIRE"}. A pure function of the
     * name, so the save layout is stable; collisions across the registration
     * list (or with a reserved container id) are a boot failure.
     */
    public String sectionId() {
        StringBuilder id = new StringBuilder(4);
        for (int i = 0; i < name.length() && id.length() < 4; i++) {
            char c = name.charAt(i);
            if (c >= 'a' && c <= 'z') {
                id.append((char) (c - ('a' - 'A')));
            } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                id.append(c);
            }
        }
        while (id.length() < 4) {
            id.append('_');
        }
        return id.toString();
    }
}
