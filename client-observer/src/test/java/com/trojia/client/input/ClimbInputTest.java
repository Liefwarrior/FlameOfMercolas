package com.trojia.client.input;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ZLinkTable;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link ClimbInput#applyClimb} — the GL-free half of the Sprint-4 CLIMB verb (the arrow
 * reads are thin wrappers, the {@code PlayModeInputTest} convention): standing on a baked
 * connector's near endpoint arms the sim's move intent with the linked far cell; standing
 * anywhere else toasts the refusal on press and stays silent on held frames. Plus the
 * docks-bake wiring proof: {@code ScenarioPopulation.zLinks()} exposes the extracted,
 * non-empty connector table the climb keys read.
 */
class ClimbInputTest {

    /** A synthetic one-stair table whose low endpoint is wherever the actor stands. */
    private static ZLinkTable stairUnder(Actor actor) {
        int cell = actor.cell();
        int above = PackedPos.pack(PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell) + 1);
        return new ZLinkTable(new int[] {cell}, new int[] {above});
    }

    @Test
    void applyClimbUpOnAConnectorArmsTheCrossZMoveIntent() {
        ActorRegistry registry = CompoundBlockPopulation.build(1234L).registry();
        Actor actor = registry.get(2);
        ZLinkTable links = stairUnder(actor);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());
        ToastQueue toasts = new ToastQueue();
        try {
            ClimbInput.applyClimb(playMode, registry, links, toasts, +1, true);

            assertEquals(links.high(0), actor.playerMoveTargetCell(),
                    "the intent must land on the sim's own cross-z move seam");
            assertEquals(PackedPos.z(actor.cell()) + 1,
                    PackedPos.z(actor.playerMoveTargetCell()));
            assertTrue(toasts.visible().isEmpty(), "a found connector never toasts");
        } finally {
            actor.setPlayerMoveTarget(Actor.NONE);
        }
    }

    @Test
    void applyClimbDownFromTheUpperEndpointArmsTheLowCell() {
        ActorRegistry registry = CompoundBlockPopulation.build(1234L).registry();
        Actor actor = registry.get(2);
        int cell = actor.cell();
        int below = PackedPos.pack(PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell) - 1);
        ZLinkTable links = new ZLinkTable(new int[] {below}, new int[] {cell});
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());
        try {
            ClimbInput.applyClimb(playMode, registry, links, new ToastQueue(), -1, true);
            assertEquals(below, actor.playerMoveTargetCell());
        } finally {
            actor.setPlayerMoveTarget(Actor.NONE);
        }
    }

    @Test
    void offConnectorClimbToastsOnPressAndStaysSilentHeld() {
        ActorRegistry registry = CompoundBlockPopulation.build(1234L).registry();
        Actor actor = registry.get(2);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());
        ToastQueue toasts = new ToastQueue();

        ClimbInput.applyClimb(playMode, registry, ZLinkTable.EMPTY, toasts, +1, true);
        assertEquals(ClimbInput.NO_WAY_UP, toasts.visible().get(0).text());
        assertEquals(Actor.NONE, actor.playerMoveTargetCell());

        ClimbInput.applyClimb(playMode, registry, ZLinkTable.EMPTY, toasts, -1, false);
        assertEquals(1, toasts.visible().size(), "held frames never re-toast");
    }

    @Test
    void applyClimbIsANoOpOutsidePlayMode() {
        ActorRegistry registry = CompoundBlockPopulation.build(1234L).registry();
        Actor actor = registry.get(2);
        ToastQueue toasts = new ToastQueue();
        ClimbInput.applyClimb(new PlayModeState(), registry, stairUnder(actor), toasts, +1,
                true);
        assertEquals(Actor.NONE, actor.playerMoveTargetCell());
        assertTrue(toasts.visible().isEmpty());
    }

    /**
     * The Sprint-4 stall fix: a ramp with several exits used to hand back the first baked
     * link (usually the WEST exit), dumping the climber a column off its road. The choice
     * now prefers the same-column stair, then the exit continuing the climber's facing,
     * then the baked order — all deterministic.
     */
    @Test
    void connectorChoicePrefersStraightThenFacingThenBakedOrder() {
        int ramp = PackedPos.pack(100, 100, 5);
        int westExit = PackedPos.pack(99, 100, 6);
        int southExit = PackedPos.pack(100, 101, 6);
        ZLinkTable twoExits = new ZLinkTable(new int[] {ramp, ramp},
                new int[] {westExit, southExit});
        // Facing south: the south exit wins over the earlier-baked west exit.
        assertEquals(southExit, ClimbInput.connectorFrom(twoExits, ramp, +1, 0, 1));
        // Facing east (no matching exit): fixed baked order falls through to west.
        assertEquals(westExit, ClimbInput.connectorFrom(twoExits, ramp, +1, 1, 0));
        // No facing preference: baked order.
        assertEquals(westExit, ClimbInput.connectorFrom(twoExits, ramp, +1));

        // A same-column stair beats everything, wherever it sits in the baked list.
        int stairTop = PackedPos.pack(100, 100, 6);
        ZLinkTable withStair = new ZLinkTable(new int[] {ramp, ramp, ramp},
                new int[] {westExit, southExit, stairTop});
        assertEquals(stairTop, ClimbInput.connectorFrom(withStair, ramp, +1, 0, 1));
        // And downward from an upper endpoint, facing preference works the same way.
        assertEquals(ramp, ClimbInput.connectorFrom(twoExits, southExit, -1, 0, -1));
    }

    /** The wiring proof: the docks bake exposes its extracted connector table. */
    @Test
    void docksPopulationExposesTheBakedConnectorTable() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        ZLinkTable links = population.zLinks();
        assertTrue(links.linkCount() > 0,
                "the docks authored stairs/ramps must reach the climb keys");
        // Every link is climbable both ways through the connector query the verb uses
        // (a shared endpoint may resolve an EARLIER link — fixed ascending order — so the
        // assertion is "some valid destination one band away", not "this exact link").
        for (int i = 0; i < links.linkCount(); i++) {
            int up = ClimbInput.connectorFrom(links, links.low(i), +1);
            assertTrue(up != Actor.NONE, "link " + i + ": no upward destination");
            assertEquals(PackedPos.z(links.low(i)) + 1, PackedPos.z(up));
            int down = ClimbInput.connectorFrom(links, links.high(i), -1);
            assertTrue(down != Actor.NONE, "link " + i + ": no downward destination");
            assertEquals(PackedPos.z(links.high(i)) - 1, PackedPos.z(down));
        }
        // World-less builds stay EMPTY (the degrade contract).
        assertTrue(DocksPopulation.build(loaded.worldSeed()).zLinks().isEmpty());
    }
}
