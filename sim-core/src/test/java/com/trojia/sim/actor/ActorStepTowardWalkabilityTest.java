package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.HashSet;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Direct coverage of the world-aware {@link Actor#stepToward(int, boolean, Actor.WalkabilityQuery)}
 * overload: the wall-slide algorithm (a blocked diagonal primary step retries the two orthogonal
 * component steps before giving up) and its per-candidate leash re-check. The legacy 2-arg/1-arg
 * overloads (exercised by {@link ActorTest}) stay untouched — this file only exercises the new
 * 3-arg path.
 */
final class ActorStepTowardWalkabilityTest {

    private static Actor serfAt(ActorRegistry registry, int x, int y, int z, int leashRadius) {
        ActorTypeStats stats = ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, leashRadius);
        return registry.spawn(Serf.TYPE, stats, PackedPos.pack(x, y, z));
    }

    /** A query that reports every cell walkable except an explicit blocked set. */
    private static Actor.WalkabilityQuery blocking(int... blockedCells) {
        Set<Integer> blocked = new HashSet<>();
        for (int c : blockedCells) {
            blocked.add(c);
        }
        return cell -> !blocked.contains(cell);
    }

    @Test
    void diagonalBlockedSlidesToTheHorizontalOrthogonalStep() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0, 1, 24);
        int diagonal = PackedPos.pack(1, 1, 1);
        int target = PackedPos.pack(5, 5, 1);

        actor.stepToward(target, false, blocking(diagonal));

        assertEquals(PackedPos.pack(1, 0, 1), actor.cell(),
                "diagonal blocked -> the horizontal component (dx,0) is tried first");
    }

    @Test
    void diagonalAndHorizontalBlockedSlidesToTheVerticalOrthogonalStep() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0, 1, 24);
        int diagonal = PackedPos.pack(1, 1, 1);
        int horizontal = PackedPos.pack(1, 0, 1);
        int target = PackedPos.pack(5, 5, 1);

        actor.stepToward(target, false, blocking(diagonal, horizontal));

        assertEquals(PackedPos.pack(0, 1, 1), actor.cell(),
                "diagonal and horizontal blocked -> the vertical component (0,dy) is tried next");
    }

    @Test
    void everyCandidateBlockedIsADeterministicNoOp() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0, 1, 24);
        int diagonal = PackedPos.pack(1, 1, 1);
        int horizontal = PackedPos.pack(1, 0, 1);
        int vertical = PackedPos.pack(0, 1, 1);
        int target = PackedPos.pack(5, 5, 1);

        actor.stepToward(target, false, blocking(diagonal, horizontal, vertical));

        assertEquals(PackedPos.pack(0, 0, 1), actor.cell(),
                "when every candidate (primary + both slide alternatives) is blocked, the actor "
                        + "must not move at all");
    }

    @Test
    void aStraightPrimaryStepNeverSlides() {
        // dx == 0 (a purely vertical move): no diagonal branch applies, so a block on the
        // primary straight step must be a plain no-op, not a slide attempt.
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0, 1, 24);
        int primary = PackedPos.pack(0, 1, 1);
        int target = PackedPos.pack(0, 5, 1);

        actor.stepToward(target, false, blocking(primary));

        assertEquals(PackedPos.pack(0, 0, 1), actor.cell(),
                "a blocked straight (non-diagonal) step has no orthogonal alternative to slide to");
    }

    @Test
    void aWalkableSlideCandidateThatWouldExceedTheLeashIsStillRefused() {
        // anchor defaults to spawn (24,0,1); leashRadius 0 means ANY step away from anchor is
        // refused by the leash check even though the walkability query allows every cell.
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 24, 0, 1, 0);
        int target = PackedPos.pack(30, 5, 1);

        actor.stepToward(target, false, blocking()); // walkability allows everything

        assertEquals(PackedPos.pack(24, 0, 1), actor.cell(),
                "the leash check still applies independently to every wall-slide candidate");
    }
}
