package com.trojia.sim.actor;

import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;

/**
 * {@link ActorRegistry} determinism (ACTORS-SPEC.md §2.2, test A2 shape):
 * ids are assigned monotonically in spawn (== registration) order, and
 * {@code array index == ActorId}, so ascending-id iteration is simply array
 * order — this test pins that equivalence directly.
 */
final class ActorRegistryTest {

    @Test
    void idsAreAssignedInAscendingSpawnOrder() {
        ActorRegistry registry = new ActorRegistry();
        Actor a = registry.spawn(MilitiaWatch.TYPE, ActorTestFixtures.stats(MilitiaWatch.TYPE),
                PackedPos.pack(1, 1, 1));
        Actor b = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                PackedPos.pack(2, 2, 1));
        Actor c = registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                PackedPos.pack(3, 3, 1));

        assertEquals(0, a.id());
        assertEquals(1, b.id());
        assertEquals(2, c.id());
        assertSame(a, registry.get(0));
        assertSame(b, registry.get(1));
        assertSame(c, registry.get(2));
        assertEquals(3, registry.size());
    }

    @Test
    void tickAllVisitsActorsInAscendingIdOrder() {
        ActorRegistry registry = new ActorRegistry();
        for (int i = 0; i < 10; i++) {
            registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                    PackedPos.pack(i, i, 1));
        }
        List<Integer> visited = new ArrayList<>();
        ActorContext probe = new NoOpActorContext(registry) {
            @Override
            public int nextDrawIndex(int actorId) {
                visited.add(actorId);
                return super.nextDrawIndex(actorId);
            }
        };
        registry.tickAll(probe);
        // Every actor draws at least once per tick (LOITER always runs unless it
        // wins over nothing — here it's the only applicable policy for a bare
        // stats block with no job bound), so ascending order is directly observable.
        List<Integer> sorted = new ArrayList<>(visited);
        sorted.sort(Integer::compareTo);
        assertEquals(sorted, visited, "tick order must be ascending ActorId");
    }

    @Test
    void allReturnsAscendingIdSnapshot() {
        ActorRegistry registry = new ActorRegistry();
        registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), PackedPos.pack(0, 0, 1));
        registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE), PackedPos.pack(1, 1, 1));
        List<Actor> all = registry.all();
        assertEquals(2, all.size());
        assertEquals(0, all.get(0).id());
        assertEquals(1, all.get(1).id());
    }
}
