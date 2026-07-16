package com.trojia.client.scenario;

import com.trojia.sim.actor.RestrictedZone;
import com.trojia.sim.actor.RestrictedZoneTable;
import com.trojia.sim.actor.job.Job;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Living-docks Pass 4 (F3 data): the baked {@link RestrictedZoneTable} the Docks injects into the
 * {@code ActorsSystem} carries the four expected zones — shipyard/ships gated on the Sailor job,
 * the guardhouse interior + holding cells on the Guard (watch.patrol) job, the bank vault chest on
 * the Guard job, and the shops' special-inventory counters on the Trader job — each keyed to the
 * real map anchors. Data + accessors only this pass; no live enforcement reads it yet.
 */
final class DocksRestrictedZoneBakeTest {

    @Test
    void theBakedTableHasTheFourExpectedGatedZones() {
        RestrictedZoneTable table = DocksPopulation.restrictedZoneTable();
        assertEquals(4, table.size(), "shipyard + guardhouse + vault + shops");

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
