package com.trojia.client.input;

import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * {@link PlayModeInput#applyMovement} — the GL-free half of Play-mode movement (the real
 * {@code Gdx.input} read is a thin, untestable wrapper around this), against the real compound
 * population. Headless: reads committed raws, no GL.
 */
class PlayModeInputTest {

    private static ActorRegistry population() {
        return CompoundBlockPopulation.build(1234L).registry();
    }

    @Test
    void applyMovementSetsThePendingTargetOneCellInTheGivenDirection() {
        ActorRegistry registry = population();
        Actor actor = registry.get(2);
        int cell = actor.cell();
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());

        PlayModeInput.applyMovement(playMode, registry, 1, 0);

        assertEquals(PackedPos.pack(PackedPos.x(cell) + 1, PackedPos.y(cell), PackedPos.z(cell)),
                actor.playerMoveTargetCell());
    }

    @Test
    void applyMovementIsANoOpWhenPlayModeIsInactive() {
        ActorRegistry registry = population();
        Actor actor = registry.get(2);
        PlayModeState playMode = new PlayModeState(); // never enabled

        PlayModeInput.applyMovement(playMode, registry, 1, 0);

        assertEquals(Actor.NONE, actor.playerMoveTargetCell());
    }

    @Test
    void applyMovementIsANoOpWithNoDirection() {
        ActorRegistry registry = population();
        Actor actor = registry.get(2);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());

        PlayModeInput.applyMovement(playMode, registry, 0, 0);

        assertEquals(Actor.NONE, actor.playerMoveTargetCell());
    }
}
