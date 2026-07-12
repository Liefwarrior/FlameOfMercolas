package com.trojia.sim.world.site;

import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.WorldConfig;

import java.util.Arrays;
import java.util.List;

/**
 * Spatial site lookup (ARCHITECTURE.md §1.1 #25). Resolution: smallest-volume
 * containing site wins, tie by ascending siteId; macro attribution goes to the
 * innermost non-DISTRICT site, else the containing district.
 *
 * <p>Storage: parallel primitive arrays sorted by {@code (volume asc, siteId
 * asc)} — a query scans in resolution-priority order and the first containing
 * site is the answer. v0 site counts are small (hundreds); the per-column
 * z-range refinement of §1.1 #25 is a drop-in replacement behind this same
 * API if scans ever show up in profiles.
 *
 * <p>Immutable after build; queries are allocation-free.
 */
public final class SiteIndex {

    /** The "no site here" answer of both queries. */
    public static final int NO_SITE = 0;

    private final int[] siteIds;
    private final boolean[] district;
    private final int[] minX;
    private final int[] minY;
    private final int[] minZ;
    private final int[] maxX;
    private final int[] maxY;
    private final int[] maxZ;

    private SiteIndex(SiteDef[] ordered) {
        int count = ordered.length;
        this.siteIds = new int[count];
        this.district = new boolean[count];
        this.minX = new int[count];
        this.minY = new int[count];
        this.minZ = new int[count];
        this.maxX = new int[count];
        this.maxY = new int[count];
        this.maxZ = new int[count];
        for (int i = 0; i < count; i++) {
            SiteDef site = ordered[i];
            siteIds[i] = site.siteId();
            district[i] = site.district();
            minX[i] = site.minX();
            minY[i] = site.minY();
            minZ[i] = site.minZ();
            maxX[i] = site.maxX();
            maxY[i] = site.maxY();
            maxZ[i] = site.maxZ();
        }
    }

    /**
     * Builds the index for {@code config}'s world from {@code sites}.
     * Validates bounds against the paintable interior and siteId uniqueness.
     */
    public static SiteIndex build(WorldConfig config, List<SiteDef> sites) {
        if (config == null || sites == null) {
            throw new IllegalArgumentException("config and sites must be non-null");
        }
        SiteDef[] ordered = sites.toArray(new SiteDef[0]);
        validate(config, ordered);
        Arrays.sort(ordered, (a, b) -> {
            int byVolume = Long.compare(a.volume(), b.volume());
            return byVolume != 0 ? byVolume : Integer.compare(a.siteId(), b.siteId());
        });
        return new SiteIndex(ordered);
    }

    /**
     * The innermost site containing {@code packedPos} (smallest volume, tie by
     * ascending siteId), or {@link #NO_SITE}.
     */
    public int siteAt(int packedPos) {
        int x = PackedPos.x(packedPos);
        int y = PackedPos.y(packedPos);
        int z = PackedPos.z(packedPos);
        for (int i = 0; i < siteIds.length; i++) {
            if (contains(i, x, y, z)) {
                return siteIds[i];
            }
        }
        return NO_SITE;
    }

    /**
     * The macro-attribution site of {@code packedPos}: the innermost
     * non-DISTRICT site, else the containing district, else {@link #NO_SITE}.
     */
    public int attributionSiteAt(int packedPos) {
        int x = PackedPos.x(packedPos);
        int y = PackedPos.y(packedPos);
        int z = PackedPos.z(packedPos);
        int districtHit = NO_SITE;
        for (int i = 0; i < siteIds.length; i++) {
            if (contains(i, x, y, z)) {
                if (!district[i]) {
                    return siteIds[i];
                }
                if (districtHit == NO_SITE) {
                    districtHit = siteIds[i];
                }
            }
        }
        return districtHit;
    }

    private boolean contains(int i, int x, int y, int z) {
        return x >= minX[i] && x <= maxX[i]
                && y >= minY[i] && y <= maxY[i]
                && z >= minZ[i] && z <= maxZ[i];
    }

    /** Boot validation: unique siteIds, bounds inside the paintable interior (border excluded). */
    private static void validate(WorldConfig config, SiteDef[] sites) {
        int loX = Coords.CHUNK_SIZE_X;
        int loY = Coords.CHUNK_SIZE_Y;
        int loZ = Coords.CHUNK_SIZE_Z;
        int hiX = (config.chunksX() - 1) * Coords.CHUNK_SIZE_X - 1;
        int hiY = (config.chunksY() - 1) * Coords.CHUNK_SIZE_Y - 1;
        int hiZ = (config.chunksZ() - 1) * Coords.CHUNK_SIZE_Z - 1;
        int[] ids = new int[sites.length];
        for (int i = 0; i < sites.length; i++) {
            SiteDef site = sites[i];
            ids[i] = site.siteId();
            if (site.minX() < loX || site.maxX() > hiX
                    || site.minY() < loY || site.maxY() > hiY
                    || site.minZ() < loZ || site.maxZ() > hiZ) {
                throw new IllegalArgumentException("site " + site.siteId()
                        + " exceeds the paintable interior ["
                        + loX + ".." + hiX + ", " + loY + ".." + hiY + ", " + loZ + ".." + hiZ
                        + "]: " + site);
            }
        }
        Arrays.sort(ids);
        for (int i = 1; i < ids.length; i++) {
            if (ids[i] == ids[i - 1]) {
                throw new IllegalArgumentException("duplicate siteId: " + ids[i]);
            }
        }
    }
}
