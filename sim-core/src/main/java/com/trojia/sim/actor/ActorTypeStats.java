package com.trojia.sim.actor;

import java.util.Objects;

/**
 * The complete raws-bound stat block for one actor type (ACTORS-SPEC.md §6).
 * One instance per type, shared by every actor of that type; injected into
 * the {@link Actor} base at construction (subclasses stay thin, §1.1 — the
 * base carries this, not the subclass).
 *
 * <p>Scoped to what this foundation milestone's starter policy library
 * ({@code DEFER_WIELDER}, {@code FLEE}, {@code GOAL_PURSUE},
 * {@code RETURN_HOME}, {@code SEEK_FOOD}, {@code LOITER}) actually reads; the
 * full §6 schema (scuffle stats, sightRadiusByLight, per-policy param blobs
 * for the entire v1 library) is a later extension of this same record.
 */
public record ActorTypeStats(
        ActorTypeId typeId,
        String displayName,
        char glyph,
        int tint,
        String factionId,
        short hp,
        int speedTicksPerStep,
        int leashRadius,
        int inventoryCap,
        NeedConfig[] needs,
        boolean hasDeferWielder,
        int deferWielderPriority,
        int deferWielderRadius,
        int fleeEmergencyPriority,
        int seekFoodPriority,
        int returnHomePriority,
        int returnHomeRhythmBonus,
        int nightWindowStart,
        int nightWindowEnd,
        int loiterPriority) {

    public ActorTypeStats {
        Objects.requireNonNull(typeId, "typeId");
        Objects.requireNonNull(displayName, "displayName");
        Objects.requireNonNull(factionId, "factionId");
        Objects.requireNonNull(needs, "needs");
        if (needs.length != Need.COUNT) {
            throw new IllegalArgumentException(
                    "needs must have exactly " + Need.COUNT + " entries, got " + needs.length);
        }
        needs = needs.clone();
        if (glyph < 0x20 || glyph > 0x7E) {
            throw new IllegalArgumentException("glyph must be printable ASCII: " + (int) glyph);
        }
        if (hp <= 0) {
            throw new IllegalArgumentException("hp must be > 0: " + hp);
        }
        if (speedTicksPerStep < 1) {
            throw new IllegalArgumentException("speedTicksPerStep must be >= 1: " + speedTicksPerStep);
        }
        if (leashRadius < 0) {
            throw new IllegalArgumentException("leashRadius must be >= 0: " + leashRadius);
        }
        if (inventoryCap < 0) {
            throw new IllegalArgumentException("inventoryCap must be >= 0: " + inventoryCap);
        }
        if (nightWindowStart < 0 || nightWindowEnd < nightWindowStart) {
            throw new IllegalArgumentException("invalid night window ["
                    + nightWindowStart + ", " + nightWindowEnd + "]");
        }
    }

    /** The raws config for one {@link Need}. */
    public NeedConfig need(Need need) {
        return needs[need.ordinal()];
    }

    /** Defensive copy — callers must not be able to mutate the shared per-type array. */
    @Override
    public NeedConfig[] needs() {
        return needs.clone();
    }
}
