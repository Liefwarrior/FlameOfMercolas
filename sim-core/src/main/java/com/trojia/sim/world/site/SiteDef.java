package com.trojia.sim.world.site;

/**
 * One named axis-aligned site volume in world-tile coordinates (inclusive
 * bounds), authored by the Tiled importer's annotations. Sites nest; the
 * innermost (smallest-volume) site wins spatial queries, tie broken by
 * ascending siteId (ARCHITECTURE.md §1.1 #25).
 *
 * @param siteId     positive, unique, stable site id (0 is reserved for "no site")
 * @param name       display name from the map annotation
 * @param kindId     id into the macro {@code SiteKind} registry
 * @param district   whether this site is a DISTRICT (macro attribution fallback)
 * @param minX     inclusive lower x bound
 * @param minY     inclusive lower y bound
 * @param minZ     inclusive lower z bound
 * @param maxX     inclusive upper x bound
 * @param maxY     inclusive upper y bound
 * @param maxZ     inclusive upper z bound
 */
public record SiteDef(int siteId, String name, int kindId, boolean district,
        int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {

    public SiteDef {
        if (siteId <= 0) {
            throw new IllegalArgumentException("siteId must be positive: " + siteId);
        }
        if (name == null || name.isEmpty()) {
            throw new IllegalArgumentException("site name must be non-empty");
        }
        if (maxX < minX || maxY < minY || maxZ < minZ) {
            throw new IllegalArgumentException("degenerate site bounds for siteId " + siteId);
        }
    }

    /** Tile volume of this site (used for innermost-wins resolution). */
    public long volume() {
        return (long) (maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
    }
}
