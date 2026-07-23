package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.WorldBuilder;
import com.trojia.sim.world.WorldConfig;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link ZLinkTable}: the baked cross-z connector table — extraction from a real world's
 * FORM/FLUID lanes (same-column STAIR pairs, walkable ramp exits in fixed W/E/N/S order,
 * fluid-drowned passages skipped), the constructor's link-shape validation, and the
 * {@code linked}/{@code anyLinkAtZ} movement guards.
 */
final class ZLinkTableTest {

    private static final short OAK = 1;

    /** A minimal 3x3x3-chunk world (interior x/y 32..63, z 8..15). */
    private static TickableWorld world() {
        return WorldBuilder.create(new WorldConfig(3, 3, 3)).build();
    }

    @Test
    void extractFindsStairPairsRampExitsAndSkipsTheDrowned() {
        TickableWorld world = world();
        var writer = world.writer();

        // A same-column STAIR pair (the baked shape of authored STAIR_UP/STAIR_DOWN).
        writer.setMaterialAndForm(PackedPos.pack(40, 40, 9), OAK, TileForm.STAIR);
        writer.setMaterialAndForm(PackedPos.pack(40, 40, 10), OAK, TileForm.STAIR);
        // A lone STAIR with nothing above: no passage.
        writer.setMaterialAndForm(PackedPos.pack(45, 40, 9), OAK, TileForm.STAIR);
        // A RAMP with two walkable z+1 exits (west and south neighbor columns).
        writer.setMaterialAndForm(PackedPos.pack(50, 40, 9), OAK, TileForm.RAMP);
        writer.setMaterialAndForm(PackedPos.pack(49, 40, 10), OAK, TileForm.FLOOR);
        writer.setMaterialAndForm(PackedPos.pack(50, 41, 10), OAK, TileForm.FLOOR);
        // A drowned STAIR pair (blocking fluid on the lower half): no passage.
        writer.setMaterialAndForm(PackedPos.pack(55, 40, 9), OAK, TileForm.STAIR);
        writer.setMaterialAndForm(PackedPos.pack(55, 40, 10), OAK, TileForm.STAIR);
        writer.setFluidBits(PackedPos.pack(55, 40, 9), 7); // depth 7 >= BLOCKING_FLUID_DEPTH

        ZLinkTable table = ZLinkTable.extract(world);

        assertEquals(3, table.linkCount(), "stair pair + two ramp exits, drowned pair skipped");
        // Scan order: ascending (z, y, x) — the stair column (x40) precedes the ramp (x50);
        // ramp exits probe in the fixed W, E, N, S order (west exit before south exit).
        assertEquals(PackedPos.pack(40, 40, 9), table.low(0));
        assertEquals(PackedPos.pack(40, 40, 10), table.high(0));
        assertEquals(PackedPos.pack(50, 40, 9), table.low(1));
        assertEquals(PackedPos.pack(49, 40, 10), table.high(1));
        assertEquals(PackedPos.pack(50, 40, 9), table.low(2));
        assertEquals(PackedPos.pack(50, 41, 10), table.high(2));

        assertTrue(table.linked(PackedPos.pack(40, 40, 9), PackedPos.pack(40, 40, 10)));
        assertTrue(table.linked(PackedPos.pack(40, 40, 10), PackedPos.pack(40, 40, 9)),
                "linked() is direction-agnostic");
        assertFalse(table.linked(PackedPos.pack(45, 40, 9), PackedPos.pack(45, 40, 10)),
                "a lone stair is no passage");
        assertFalse(table.linked(PackedPos.pack(55, 40, 9), PackedPos.pack(55, 40, 10)),
                "a drowned stair is no passage");
        assertTrue(table.anyLinkAtZ(9));
        assertFalse(table.anyLinkAtZ(10));
    }

    @Test
    void extractionIsDeterministic() {
        TickableWorld world = world();
        var writer = world.writer();
        writer.setMaterialAndForm(PackedPos.pack(40, 40, 9), OAK, TileForm.STAIR);
        writer.setMaterialAndForm(PackedPos.pack(40, 40, 10), OAK, TileForm.STAIR);
        writer.setMaterialAndForm(PackedPos.pack(50, 40, 9), OAK, TileForm.RAMP);
        writer.setMaterialAndForm(PackedPos.pack(51, 40, 10), OAK, TileForm.FLOOR);

        ZLinkTable first = ZLinkTable.extract(world);
        ZLinkTable second = ZLinkTable.extract(world);
        assertEquals(first.linkCount(), second.linkCount());
        for (int i = 0; i < first.linkCount(); i++) {
            assertEquals(first.low(i), second.low(i));
            assertEquals(first.high(i), second.high(i));
        }
    }

    @Test
    void theConstructorRejectsMalformedLinks() {
        // Length mismatch.
        assertThrows(IllegalArgumentException.class,
                () -> new ZLinkTable(new int[] {PackedPos.pack(1, 1, 1)}, new int[0]));
        // Not one z apart.
        assertThrows(IllegalArgumentException.class, () -> new ZLinkTable(
                new int[] {PackedPos.pack(1, 1, 1)}, new int[] {PackedPos.pack(1, 1, 3)}));
        // Diagonal (neither same column nor orthogonally adjacent).
        assertThrows(IllegalArgumentException.class, () -> new ZLinkTable(
                new int[] {PackedPos.pack(1, 1, 1)}, new int[] {PackedPos.pack(2, 2, 2)}));
        // The empty table and a well-formed stair + ramp link construct cleanly.
        assertTrue(ZLinkTable.EMPTY.isEmpty());
        ZLinkTable ok = new ZLinkTable(
                new int[] {PackedPos.pack(1, 1, 1), PackedPos.pack(5, 5, 1)},
                new int[] {PackedPos.pack(1, 1, 2), PackedPos.pack(5, 6, 2)});
        assertEquals(2, ok.linkCount());
        assertFalse(ok.isEmpty());
    }
}
