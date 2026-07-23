package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.ActorRegistry;

/**
 * The one shared "how do we print a person" rule (Sprint 1 "Click a person, meet a person"):
 * resolves an ActorId to its display name off the bake-side {@link IdentityRegistry}, with a
 * deliberate degraded mode — a fixture without a NameForge pass (the compound block, any
 * test double passing {@link IdentityRegistry#EMPTY}) falls back to the pre-names
 * {@code "Serf #2"} style instead of failing. GL-free; used by the character sheet, the
 * hover nameplates and the skill-up feed so all three surfaces print people identically.
 *
 * <p><b>Callers pick the id.</b> This class never resolves disguises: the caller decides
 * whether it wants the TRUE body or the PRESENTED identity (the §1.1 social-read rule) and
 * passes that id.
 */
public final class PersonNames {

    private PersonNames() {
    }

    /**
     * The display name of {@code actorId}: the identity table's {@code fullName} when a
     * non-blank row exists (forged citizens, authored notables, kennel-named beasts, and the
     * deliberately nameless' descriptors like "a wharf mouse"), else the
     * {@code "Serf #2"}-style type fallback.
     */
    public static String fullNameOf(int actorId, ActorRegistry registry,
            IdentityRegistry identity) {
        if (actorId >= 0 && actorId < identity.size()) {
            String name = identity.get(actorId).fullName();
            if (!name.isBlank()) {
                return name;
            }
        }
        return registry.get(actorId).stats().displayName() + " #" + actorId;
    }

    /**
     * {@link #fullNameOf} plus the quoted epithet when the identity row carries one —
     * {@code Ditta Pilchard "Barrel-Back"}.
     */
    public static String nameWithEpithet(int actorId, ActorRegistry registry,
            IdentityRegistry identity) {
        String base = fullNameOf(actorId, registry, identity);
        if (actorId >= 0 && actorId < identity.size()) {
            String epithet = identity.get(actorId).epithet();
            if (!epithet.isBlank()) {
                return base + " \"" + epithet + "\"";
            }
        }
        return base;
    }
}
