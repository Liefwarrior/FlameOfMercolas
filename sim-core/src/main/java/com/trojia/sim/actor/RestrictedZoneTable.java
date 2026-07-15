package com.trojia.sim.actor;

import java.util.ArrayList;
import java.util.List;

/**
 * The baked restricted-zone side-table (Phase-0 job/access F3): a dense,
 * bake-order list of {@link RestrictedZone}s injected through the {@link
 * ActorsSystem} constructor exactly like {@code arrestHoldCell}. A dense
 * {@code ArrayList}, never a {@code Map}/{@code Set} — iteration order is bake
 * order, so lookups are deterministic and the actor-package purity gate holds.
 *
 * <p>No zones are wired into the live district in Phase 0 ({@link #EMPTY} is
 * what the bake injects); the resolver and gate are exercised by unit tests with
 * synthetic zones. A live enforcement pass (a later phase) reads {@link
 * #zoneAt(int)} to find the zone a cell belongs to.
 */
public final class RestrictedZoneTable {

    /** The degraded empty table the world-less/no-zones bake injects. */
    public static final RestrictedZoneTable EMPTY = new RestrictedZoneTable(List.of());

    private final List<RestrictedZone> zones;

    public RestrictedZoneTable(List<RestrictedZone> zones) {
        this.zones = new ArrayList<>(zones);
    }

    public int size() {
        return zones.size();
    }

    /** The zone at dense index {@code zoneId} (bake order). */
    public RestrictedZone get(int zoneId) {
        return zones.get(zoneId);
    }

    /**
     * The first (lowest zoneId) zone whose tagged cells include {@code cell}, or {@code null} if
     * {@code cell} is unrestricted. Ascending bake-order scan (deterministic).
     */
    public RestrictedZone zoneAt(int cell) {
        for (RestrictedZone zone : zones) {
            if (zone.contains(cell)) {
                return zone;
            }
        }
        return null;
    }
}
