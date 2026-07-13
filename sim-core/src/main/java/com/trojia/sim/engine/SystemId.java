package com.trojia.sim.engine;

import com.trojia.sim.random.RandomSource;

/**
 * Stable identity of a {@link SimulationSystem}: a human-readable name, a
 * 64-bit salt that seeds the system's counter-based RNG derivation and names
 * its TROJSAV section hash, and the 4-character TROJSAV section id its state
 * is saved under. Salts and section ids are collision-checked at engine boot;
 * a collision is a boot failure, never a silent reseed.
 *
 * @param name      stable, unique, lower-case system name (e.g. {@code "fire"})
 * @param salt      64-bit RNG/hash salt, unique across all registered systems
 * @param sectionId 4-character TROJSAV section id (ARCHITECTURE.md §9), unique
 *                  across all registered systems and the reserved container ids
 */
public record SystemId(String name, long salt, String sectionId) {

    public SystemId {
        if (name == null || name.isEmpty()) {
            throw new IllegalArgumentException("system name must be non-empty");
        }
        requireValidSectionId(sectionId);
    }

    /**
     * Derives a {@code SystemId} whose salt is a pure, platform-independent
     * function of {@code name} (folded through {@link RandomSource#mix64(long)})
     * and whose section id is derived from the name (see
     * {@link #of(String, String)} for the explicit-override form). Equal names
     * always produce equal salts; the engine's boot-time collision check
     * therefore also rejects duplicate names.
     */
    public static SystemId of(String name) {
        return of(name, deriveSectionId(name));
    }

    /**
     * Derives a {@code SystemId} as {@link #of(String)} but saving under the
     * explicit {@code sectionId} instead of the name-derived one — the way the
     * §9 pinned ids ({@code FLUD}, {@code THRM}, {@code LGHT}, …) are reachable
     * for systems whose natural names derive differently (e.g.
     * {@code of("fluids", TrojSav.FLUD)}). {@code sectionId} must be exactly 4
     * printable-ASCII characters; collisions remain a boot failure.
     */
    public static SystemId of(String name, String sectionId) {
        if (name == null || name.isEmpty()) {
            throw new IllegalArgumentException("system name must be non-empty");
        }
        long h = 0x5CA1AB1E_0DDBA11L;
        for (int i = 0; i < name.length(); i++) {
            h = RandomSource.mix64(h ^ name.charAt(i));
        }
        return new SystemId(name, h, sectionId);
    }

    /**
     * The default section-id derivation (ARCHITECTURE.md §9): the first four
     * letters/digits of {@code name} upper-cased, padded with {@code '_'} —
     * e.g. {@code "fire"} → {@code "FIRE"}, {@code "fluids"} → {@code "FLUI"}.
     * A pure function of the name, so the save layout is stable.
     */
    private static String deriveSectionId(String name) {
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

    private static void requireValidSectionId(String sectionId) {
        if (sectionId == null || sectionId.length() != 4) {
            throw new IllegalArgumentException(
                    "section id must be exactly 4 chars: " + sectionId);
        }
        for (int i = 0; i < 4; i++) {
            char c = sectionId.charAt(i);
            if (c < 0x20 || c > 0x7E) {
                throw new IllegalArgumentException(
                        "section id must be printable ASCII: " + sectionId);
            }
        }
    }
}
