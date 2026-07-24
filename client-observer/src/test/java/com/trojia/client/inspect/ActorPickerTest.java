package com.trojia.client.inspect;

import com.trojia.client.render.DepthSight;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;

/**
 * {@link ActorPicker} tile&rarr;actor resolution against the real compound population — the
 * screen-independent half of click-to-select (the click&rarr;tile half is
 * {@code MapCamera.screenToTileX/Y}, tested there). Headless: reads committed raws, no GL.
 */
class ActorPickerTest {

    private static ActorRegistry population() {
        return CompoundBlockPopulation.build(1234L).registry();
    }

    @Test
    void picksAnActorStandingOnItsOwnTile() {
        ActorRegistry registry = population();
        for (int i = 0; i < registry.size(); i++) {
            int id = registry.get(i).id();
            int cell = registry.get(i).cell();
            int picked = ActorPicker.pickAt(registry,
                    PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell));
            assertNotEquals(Actor.NONE, picked, () -> "actor " + id + " unpicked");
            // The picked actor is genuinely on that exact tile (lowest id wins when co-located).
            assertEquals(cell, registry.get(picked).cell());
        }
    }

    @Test
    void emptyTileReturnsNone() {
        ActorRegistry registry = population();
        // No actor lives at the far corner of the ground floor.
        assertEquals(Actor.NONE, ActorPicker.pickAt(registry, 3, 3, 8));
    }

    @Test
    void pickingIsScopedToTheGivenZLevel() {
        ActorRegistry registry = population();
        // The animal keeper spawns alone on its ground tile; the tile directly "above" it
        // (same x/y, next z) holds no actor, so a picker scoped to that z finds nothing.
        Actor keeper = firstOfType(registry, "animal_keeper");
        int cell = keeper.cell();
        int here = ActorPicker.pickAt(registry, PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell));
        assertEquals(keeper.id(), here, "keeper should be pickable on its own z");
        int above = ActorPicker.pickAt(registry, PackedPos.x(cell), PackedPos.y(cell),
                PackedPos.z(cell) + 1);
        assertEquals(Actor.NONE, above, "same x/y on a different z must not match");
    }

    // ------------------------------------------------- pickThroughDepth (S4 EPIC)

    @Test
    void depthFallbackFindsTheActorTheLookdownShows() {
        ActorRegistry registry = population();
        Actor keeper = firstOfType(registry, "animal_keeper");
        int cell = keeper.cell();
        int keeperZ = PackedPos.z(cell);
        int viewZ = keeperZ + 2;
        // A synthetic open column: from viewZ the look-down resolves to the keeper's z.
        DepthSight sight = (vz, x, y) -> keeperZ;
        int picked = ActorPicker.pickThroughDepth(registry,
                PackedPos.x(cell), PackedPos.y(cell), viewZ, sight);
        assertEquals(keeper.id(), picked,
                "the below-z actor the depth pass renders must be clickable");
    }

    @Test
    void samePlaneActorAlwaysWinsOverTheDepthFallback() {
        ActorRegistry registry = population();
        Actor keeper = firstOfType(registry, "animal_keeper");
        int cell = keeper.cell();
        // A sight that would resolve somewhere else entirely: it must never be consulted,
        // because an actor stands on the asked tile at the viewed z itself.
        DepthSight neverThis = (vz, x, y) -> {
            throw new AssertionError("depth fallback consulted despite a same-z actor");
        };
        int picked = ActorPicker.pickThroughDepth(registry,
                PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell), neverThis);
        assertEquals(keeper.id(), picked);
    }

    @Test
    void occludedOrBottomlessColumnPicksNothing() {
        ActorRegistry registry = population();
        Actor keeper = firstOfType(registry, "animal_keeper");
        int cell = keeper.cell();
        DepthSight occluded = (vz, x, y) -> DepthSight.NONE;
        assertEquals(Actor.NONE, ActorPicker.pickThroughDepth(registry,
                PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell) + 2, occluded));
    }

    @Test
    void lookdownStoppingShortOfTheActorPicksNothing() {
        ActorRegistry registry = population();
        Actor keeper = firstOfType(registry, "animal_keeper");
        int cell = keeper.cell();
        // The column resolves to a floor ABOVE the keeper (an intervening storey): the
        // keeper is hidden under it and must not be pickable through it.
        DepthSight floorAbove = (vz, x, y) -> PackedPos.z(cell) + 1;
        assertEquals(Actor.NONE, ActorPicker.pickThroughDepth(registry,
                PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell) + 2, floorAbove));
    }

    @Test
    void nullSightDegradesToThePlainSameZPick() {
        ActorRegistry registry = population();
        Actor keeper = firstOfType(registry, "animal_keeper");
        int cell = keeper.cell();
        assertEquals(Actor.NONE, ActorPicker.pickThroughDepth(registry,
                PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell) + 1, null));
        assertEquals(keeper.id(), ActorPicker.pickThroughDepth(registry,
                PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell), null));
    }

    private static Actor firstOfType(ActorRegistry registry, String typeKey) {
        for (int i = 0; i < registry.size(); i++) {
            if (registry.get(i).typeId().key().equals(typeKey)) {
                return registry.get(i);
            }
        }
        throw new AssertionError("no actor of type " + typeKey + " in the population");
    }
}
