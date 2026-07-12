package com.trojia.sim.world.site;

import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.WorldConfig;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/**
 * Site resolution contract (ARCHITECTURE.md §1.1 #25): smallest-volume
 * containing site wins with siteId as the tiebreak, macro attribution prefers
 * the innermost non-DISTRICT site, and build-time validation rejects
 * out-of-interior bounds and duplicate ids.
 */
final class SiteIndexTest {

    /** 4×4×3 chunks: paintable interior x,y ∈ [32,95], z ∈ [8,15]. */
    private static final WorldConfig CONFIG = new WorldConfig(4, 4, 3);

    private static SiteDef site(int id, boolean district, int minX, int minY, int minZ,
            int maxX, int maxY, int maxZ) {
        return new SiteDef(id, "site-" + id, 1, district, minX, minY, minZ, maxX, maxY, maxZ);
    }

    @Test
    void emptyIndexAnswersNoSite() {
        SiteIndex index = SiteIndex.build(CONFIG, List.of());
        assertEquals(SiteIndex.NO_SITE, index.siteAt(PackedPos.pack(40, 40, 10)));
        assertEquals(SiteIndex.NO_SITE, index.attributionSiteAt(PackedPos.pack(40, 40, 10)));
    }

    @Test
    void innermostSmallestVolumeSiteWins() {
        SiteDef outer = site(1, false, 32, 32, 8, 63, 63, 15);
        SiteDef inner = site(2, false, 40, 40, 9, 47, 47, 12);
        SiteIndex index = SiteIndex.build(CONFIG, List.of(outer, inner));
        assertEquals(2, index.siteAt(PackedPos.pack(42, 41, 10)));
        assertEquals(1, index.siteAt(PackedPos.pack(33, 33, 8)));
        assertEquals(SiteIndex.NO_SITE, index.siteAt(PackedPos.pack(80, 80, 10)));
        // Inclusive bounds on every face.
        assertEquals(2, index.siteAt(PackedPos.pack(40, 40, 9)));
        assertEquals(2, index.siteAt(PackedPos.pack(47, 47, 12)));
        assertEquals(1, index.siteAt(PackedPos.pack(47, 47, 13)));
    }

    @Test
    void equalVolumeTieBreaksByAscendingSiteId() {
        SiteDef late = site(9, false, 40, 40, 10, 49, 49, 11);
        SiteDef early = site(3, false, 45, 40, 10, 54, 49, 11);
        SiteIndex index = SiteIndex.build(CONFIG, List.of(late, early));
        // Overlap region: both contain (46, 44, 10); volumes identical.
        assertEquals(3, index.siteAt(PackedPos.pack(46, 44, 10)));
        assertEquals(9, index.siteAt(PackedPos.pack(41, 44, 10)));
        assertEquals(3, index.siteAt(PackedPos.pack(52, 44, 10)));
    }

    @Test
    void attributionPrefersInnermostNonDistrictThenDistrict() {
        SiteDef district = site(1, true, 32, 32, 8, 71, 71, 15);
        SiteDef workshop = site(2, false, 40, 40, 9, 45, 45, 11);
        SiteDef cellar = site(3, false, 41, 41, 9, 43, 43, 10); // nested inside workshop
        SiteIndex index = SiteIndex.build(CONFIG, List.of(district, workshop, cellar));
        assertEquals(3, index.attributionSiteAt(PackedPos.pack(42, 42, 10)));
        assertEquals(2, index.attributionSiteAt(PackedPos.pack(44, 44, 10)));
        assertEquals(1, index.attributionSiteAt(PackedPos.pack(60, 60, 12)));
        assertEquals(SiteIndex.NO_SITE, index.attributionSiteAt(PackedPos.pack(90, 90, 12)));
        // siteAt still answers the district where only it contains the tile.
        assertEquals(1, index.siteAt(PackedPos.pack(60, 60, 12)));
    }

    @Test
    void nestedDistrictsAttributeToTheInnermost() {
        SiteDef city = site(1, true, 32, 32, 8, 95, 95, 15);
        SiteDef quarter = site(2, true, 40, 40, 8, 63, 63, 15);
        SiteIndex index = SiteIndex.build(CONFIG, List.of(city, quarter));
        assertEquals(2, index.attributionSiteAt(PackedPos.pack(50, 50, 10)));
        assertEquals(1, index.attributionSiteAt(PackedPos.pack(80, 80, 10)));
    }

    @Test
    void boundsOutsideThePaintableInteriorAreRejected() {
        // Touching the VOID border ring on any face fails the build.
        assertThrows(IllegalArgumentException.class, () -> SiteIndex.build(CONFIG,
                List.of(site(1, false, 31, 40, 10, 40, 41, 11))));
        assertThrows(IllegalArgumentException.class, () -> SiteIndex.build(CONFIG,
                List.of(site(1, false, 40, 40, 10, 96, 41, 11))));
        assertThrows(IllegalArgumentException.class, () -> SiteIndex.build(CONFIG,
                List.of(site(1, false, 40, 40, 7, 41, 41, 11))));
        assertThrows(IllegalArgumentException.class, () -> SiteIndex.build(CONFIG,
                List.of(site(1, false, 40, 40, 10, 41, 41, 16))));
        // The full interior is legal.
        SiteIndex.build(CONFIG, List.of(site(1, false, 32, 32, 8, 95, 95, 15)));
    }

    @Test
    void duplicateSiteIdsAreRejected() {
        assertThrows(IllegalArgumentException.class, () -> SiteIndex.build(CONFIG, List.of(
                site(7, false, 40, 40, 10, 41, 41, 11),
                site(7, false, 50, 50, 10, 51, 51, 11))));
    }

    @Test
    void nullArgumentsAreRejected() {
        assertThrows(IllegalArgumentException.class, () -> SiteIndex.build(null, List.of()));
        assertThrows(IllegalArgumentException.class, () -> SiteIndex.build(CONFIG, null));
    }
}
