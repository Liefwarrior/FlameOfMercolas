package com.trojia.client.scenario;

import com.trojia.sim.actor.RestrictedZone;
import com.trojia.sim.actor.RestrictedZoneTable;
import com.trojia.sim.actor.job.Job;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Living-docks Pass 4 (F3 data) + the law &amp; order pass (Passes 11-12): the baked {@link
 * RestrictedZoneTable} the Docks injects into the {@code ActorsSystem} carries the four Pass-4
 * access-gate zones — shipyard/ships gated on the Sailor job, the guardhouse interior + holding
 * cells on the Guard (watch.patrol) job, the bank vault chest on the Guard job, and the shops'
 * special-inventory counters on the Trader job — PLUS the seven policed retail-shop interiors
 * and the bank hall the Watch's APPREHEND now enforces live. Zone order is append-only, so the
 * original four keep their indices and win their own cells under {@code zoneAt}'s
 * lowest-index-wins rule.
 */
final class DocksRestrictedZoneBakeTest {

    @Test
    void theBakedTableHasTheFourExpectedGatedZones() {
        RestrictedZoneTable table = DocksPopulation.restrictedZoneTable();
        assertEquals(12, table.size(),
                "shipyard + guardhouse + vault + shops + 7 policed shop interiors + bank hall");

        int shipCell = DocksPopulation.maritimeTradeAnchors()[0];
        RestrictedZone shipyard = table.zoneAt(shipCell);
        assertNotNull(shipyard, "a ship/shipyard anchor is a restricted cell");
        assertEquals(Job.Maritime.Sailor.ID, shipyard.requiredJob(), "shipyard is Sailor-gated");

        int shopCell = DocksPopulation.traderShopAnchors()[0];
        RestrictedZone shop = table.zoneAt(shopCell);
        assertNotNull(shop, "a shop special-inventory anchor is a restricted cell");
        assertEquals(Job.Trade.Trader.ID, shop.requiredJob(), "the shop back-room is Trader-gated");

        RestrictedZone vault = table.zoneAt(DocksPopulation.bankVaultChestCell());
        assertNotNull(vault, "the vault chest cell is restricted");
        assertEquals(Job.Watch.Patrol.ID, vault.requiredJob(),
                "the vault is Guard-gated (no distinct banker job this pass)");

        int guardhouseCell = DocksPopulation.prisonCellsK34().get(0);
        RestrictedZone guardhouse = table.zoneAt(guardhouseCell);
        assertNotNull(guardhouse, "a K34 holding cell is inside the guard-only zone");
        assertEquals(Job.Watch.Patrol.ID, guardhouse.requiredJob(), "the guardhouse is Guard-gated");

        assertTrue(shipCell != shopCell, "zones are keyed to distinct real anchors");
    }
}
