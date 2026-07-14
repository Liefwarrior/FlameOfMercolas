package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Direct coverage of {@link Actor#stepToward}, previously exercised only
 * incidentally by {@link ReturnHomePolicyTest}'s home-walk scenario (which
 * never approaches the leash boundary, never targets a different z-level,
 * and never uses a {@code speedTicksPerStep > 1} actor). Covers:
 * <ul>
 *   <li>the leash boundary (an absolute-vs-relative distinction, §2.5): a
 *       step that would increase the distance from anchor past leashRadius
 *       is refused, but {@code ignoresLeash} still allows it;</li>
 *   <li>the relative/monotonic-improvement leash check specifically: an
 *       actor already beyond its leash can still walk itself back in one
 *       cell at a time instead of being permanently frozen;</li>
 *   <li>the z-level mismatch no-op (actors move on one z-level only);</li>
 *   <li>the {@code speedTicksPerStep} accumulator (a slow actor only moves
 *       on the Nth call to {@code stepToward}).</li>
 * </ul>
 */
final class ActorTest {

    private static Actor serfAt(ActorRegistry registry, int x, int y, int z,
            int speedTicksPerStep, int leashRadius) {
        ActorTypeStats stats = ActorTestFixtures.statsWithSpeedAndLeash(
                Serf.TYPE, true, speedTicksPerStep, leashRadius);
        return registry.spawn(Serf.TYPE, stats, PackedPos.pack(x, y, z));
    }

    @Test
    void leashRespectingStepIsRefusedWhenItWouldExceedTheRadius() {
        ActorRegistry registry = new ActorRegistry();
        // anchor defaults to the spawn cell; leashRadius 24 (fixture default).
        Actor actor = serfAt(registry, 0, 0, 1, 1, 24);
        actor.setCell(PackedPos.pack(24, 0, 1)); // exactly at the leash boundary

        actor.stepToward(PackedPos.pack(30, 0, 1)); // one more step would read 25 > 24

        assertEquals(PackedPos.pack(24, 0, 1), actor.cell(),
                "a step that would exceed the leash radius (and doesn't improve on the "
                        + "current distance) must be refused");
    }

    @Test
    void ignoresLeashOverrideStillMovesPastTheBoundary() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0, 1, 1, 24);
        actor.setCell(PackedPos.pack(24, 0, 1));

        actor.stepToward(PackedPos.pack(30, 0, 1), true);

        assertEquals(PackedPos.pack(25, 0, 1), actor.cell(),
                "ignoresLeash=true (FLEE/RETURN_HOME/APPREHEND-style) must bypass the leash");
    }

    @Test
    void anActorAlreadyBeyondItsLeashCanStillWalkItselfBackIn() {
        ActorRegistry registry = new ActorRegistry();
        // anchor = spawn cell (0,0,1); setCell (the documented spawn-bake/test-only direct
        // placement) simulates a post-spawn setAnchorCell leaving the actor's current cell
        // already outside its own leash radius (a real, shipped content pattern) without
        // routing through the leash-enforcing stepToward itself.
        Actor actor = serfAt(registry, 0, 0, 1, 1, 24);
        actor.setCell(PackedPos.pack(30, 0, 1)); // 30 > leashRadius(24): already 6 over

        actor.stepToward(PackedPos.pack(0, 0, 1)); // walking back toward the anchor itself

        assertEquals(PackedPos.pack(29, 0, 1), actor.cell(),
                "a step that reduces the distance to the anchor must be allowed even though "
                        + "the destination (29 > 24) is still outside the leash radius — "
                        + "otherwise the actor would be frozen forever (the bug this guards)");
    }

    @Test
    void aStepThatWouldIncreaseAnAlreadyExcessiveDistanceIsStillRefused() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0, 1, 1, 24); // anchor = (0,0,1)
        actor.setCell(PackedPos.pack(30, 0, 1)); // already 6 over the leash radius

        actor.stepToward(PackedPos.pack(60, 0, 1)); // moving further away, not back

        assertEquals(PackedPos.pack(30, 0, 1), actor.cell(),
                "even when already beyond the leash, a step that makes things worse must "
                        + "still be refused — only improving steps are let through");
    }

    @Test
    void targetOnADifferentZLevelIsANoOp() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5, 1, 1, 24);

        actor.stepToward(PackedPos.pack(5, 5, 2));

        assertEquals(PackedPos.pack(5, 5, 1), actor.cell(),
                "actors move on one z-level only; a cross-z target must be a no-op");
    }

    @Test
    void speedTicksPerStepGatesMovementToEveryNthCall() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0, 1, 3, 24);
        int target = PackedPos.pack(10, 0, 1);

        actor.stepToward(target);
        assertEquals(PackedPos.pack(0, 0, 1), actor.cell(), "call 1/3: must not move yet");

        actor.stepToward(target);
        assertEquals(PackedPos.pack(0, 0, 1), actor.cell(), "call 2/3: must not move yet");

        actor.stepToward(target);
        assertEquals(PackedPos.pack(1, 0, 1), actor.cell(), "call 3/3: must move exactly one cell");

        actor.stepToward(target);
        assertEquals(PackedPos.pack(1, 0, 1), actor.cell(),
                "call 4 (1/3 of the next cycle): must not have moved again yet");
    }
}
