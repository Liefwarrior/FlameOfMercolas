package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.WorldBuilder;
import com.trojia.sim.world.WorldConfig;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link ZReachability}: the bake-time 3D flood audit — same-z spread, connector
 * crossings, and the walkable-but-unreachable pocket verdict the audit exists to expose.
 */
final class ZReachabilityTest {

    private static final short OAK = 1;

    @Test
    void theFloodCrossesConnectorsAndExposesUnreachablePockets() {
        TickableWorld world = WorldBuilder.create(new WorldConfig(3, 3, 3)).build();
        var writer = world.writer();
        // A 3-cell z9 street, a stair pair at its end, a 2-cell z10 landing...
        for (int x = 40; x <= 42; x++) {
            writer.setMaterialAndForm(PackedPos.pack(x, 40, 9), OAK, TileForm.FLOOR);
        }
        writer.setMaterialAndForm(PackedPos.pack(43, 40, 9), OAK, TileForm.STAIR);
        writer.setMaterialAndForm(PackedPos.pack(43, 40, 10), OAK, TileForm.STAIR);
        writer.setMaterialAndForm(PackedPos.pack(44, 40, 10), OAK, TileForm.FLOOR);
        // ...and a walkable z10 pocket nothing connects to (the punch-list case).
        writer.setMaterialAndForm(PackedPos.pack(60, 60, 10), OAK, TileForm.FLOOR);

        ZLinkTable links = ZLinkTable.extract(world);
        assertEquals(1, links.linkCount());
        ZReachability audit = ZReachability.flood(world, links, PackedPos.pack(40, 40, 9));

        assertTrue(audit.reachable(PackedPos.pack(42, 40, 9)), "the street floods");
        assertTrue(audit.reachable(PackedPos.pack(44, 40, 10)),
                "the flood crosses the stair onto the landing");
        assertFalse(audit.reachable(PackedPos.pack(60, 60, 10)),
                "the isolated pocket is walkable but unreachable");
        assertTrue(audit.walkable(PackedPos.pack(60, 60, 10)));
        assertEquals(4, audit.walkableAtZ(9), "street + stair foot");
        assertEquals(4, audit.reachableAtZ(9));
        assertEquals(3, audit.walkableAtZ(10), "stair head + landing + pocket");
        assertEquals(2, audit.reachableAtZ(10), "the pocket stays out");
    }
}
