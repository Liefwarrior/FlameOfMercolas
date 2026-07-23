package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * {@link ZRouter}: the pure cross-z hop planner — near-endpoint walk targets, the
 * on-connector vertical commit, the greedy cost choice with the lowest-baked-index
 * tie-break, descent symmetry, and the band-gap unroutable verdict.
 */
final class ZRouterTest {

    private static int cell(int x, int y, int z) {
        return PackedPos.pack(x, y, z);
    }

    /** Two z9->z10 stair links: A at (40,40), B at (50,50) (baked order A then B). */
    private static ZLinkTable twoStairs() {
        return new ZLinkTable(
                new int[] {cell(40, 40, 9), cell(50, 50, 9)},
                new int[] {cell(40, 40, 10), cell(50, 50, 10)});
    }

    @Test
    void offConnectorTheHopIsTheNearEndpointOfTheCheapestLink() {
        // From (55,55,9) to (60,60,10): A costs 15+20=35, B costs 5+10=15 -> B's low.
        assertEquals(cell(50, 50, 9),
                ZRouter.nextHop(cell(55, 55, 9), cell(60, 60, 10), twoStairs()));
    }

    @Test
    void aCostTieBreaksToTheLowestBakedIndex() {
        // From (35,35,9) to (60,60,10): A costs 5+20=25, B costs 15+10=25 -> tie -> A.
        assertEquals(cell(40, 40, 9),
                ZRouter.nextHop(cell(35, 35, 9), cell(60, 60, 10), twoStairs()));
    }

    @Test
    void standingOnTheNearEndpointReturnsTheFarEndpoint() {
        assertEquals(cell(50, 50, 10),
                ZRouter.nextHop(cell(50, 50, 9), cell(60, 60, 10), twoStairs()),
                "on the stair foot, the hop is the vertical commit");
    }

    @Test
    void descentMirrorsTheClimb() {
        // From z10 down to z9: the near endpoint is the HIGH cell.
        assertEquals(cell(50, 50, 10),
                ZRouter.nextHop(cell(55, 55, 10), cell(60, 60, 9), twoStairs()));
        assertEquals(cell(50, 50, 9),
                ZRouter.nextHop(cell(50, 50, 10), cell(60, 60, 9), twoStairs()),
                "on the stair head, the hop is the downward vertical commit");
    }

    @Test
    void aMultiBandClimbGoesOneBandAtATime() {
        // z9 -> z11 via a z9/z10 stair at (40,40) and a z10/z11 stair at (44,40).
        ZLinkTable table = new ZLinkTable(
                new int[] {cell(40, 40, 9), cell(44, 40, 10)},
                new int[] {cell(40, 40, 10), cell(44, 40, 11)});
        int target = cell(60, 40, 11);
        assertEquals(cell(40, 40, 9), ZRouter.nextHop(cell(36, 40, 9), target, table));
        assertEquals(cell(40, 40, 10), ZRouter.nextHop(cell(40, 40, 9), target, table));
        assertEquals(cell(44, 40, 10), ZRouter.nextHop(cell(40, 40, 10), target, table));
        assertEquals(cell(44, 40, 11), ZRouter.nextHop(cell(44, 40, 10), target, table));
    }

    @Test
    void aBandGapWithNoConnectorIsUnroutable() {
        // Only the z9/z10 crossing has links; a z11 target needs a z10/z11 link too.
        assertEquals(Actor.NONE,
                ZRouter.nextHop(cell(35, 35, 9), cell(60, 60, 11), twoStairs()));
        // ... and the verdict holds even standing on a connector of the first crossing.
        assertEquals(Actor.NONE,
                ZRouter.nextHop(cell(40, 40, 9), cell(60, 60, 11), twoStairs()));
    }

    @Test
    void sameZAndEmptyTablesResolveToNone() {
        assertEquals(Actor.NONE,
                ZRouter.nextHop(cell(35, 35, 9), cell(60, 60, 9), twoStairs()),
                "same-z routing belongs to the ordinary planner");
        assertEquals(Actor.NONE,
                ZRouter.nextHop(cell(35, 35, 9), cell(60, 60, 10), ZLinkTable.EMPTY));
    }
}
