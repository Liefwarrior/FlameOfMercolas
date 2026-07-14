package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The specific, previously-failing cross-band scenario found by an
 * adversarial verify pass on the needs-hierarchy pass: HUNGER merely LOW
 * (not CRITICAL) while REST is CRITICAL. {@code ActorRawsLoaderTest}
 * exhaustively proves the raws-level score invariant across all 4 band
 * combinations for every committed type; this test additionally drives one
 * real actor (a Serf, real committed raws) through that exact cross-band
 * state end-to-end — HUNGER and RETURN — to prove SEEK_FOOD actually wins the
 * whole-stack decision at runtime, not just that the loader's arithmetic
 * checks out. Doesn't rely on the real sim happening to reach this state
 * (per the diagnosis notes, it currently doesn't, since HUNGER decays faster
 * and recovers faster than REST for every type) — it's manufactured directly
 * via {@link Actor#applyNeedDelta}.
 */
final class CrossBandNeedsHierarchyTest {

    private static Path committedActorsRawsDir() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws").resolve("actors");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws/actors not found above "
                + Path.of("").toAbsolutePath());
    }

    @Test
    void seekFoodOutscoresReturnHomeWhenHungerIsLowButRestIsCritical() {
        ActorTypeStatsTable table = ActorRawsLoader.load(committedActorsRawsDir());
        ActorTypeStats serfStats = table.get(Serf.TYPE);

        ActorRegistry registry = new ActorRegistry();
        Actor actor = registry.spawn(Serf.TYPE, serfStats, PackedPos.pack(10, 0, 1));
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(1_000L); // daytime — no night-rhythm term to muddy the comparison
        int homeId = ctx.homes().addHome(PackedPos.pack(0, 0, 1));
        actor.setHomeId(homeId);

        // Drive HUNGER to LOW-not-CRITICAL (just under 3000) and REST to CRITICAL (just under
        // 1000) — the exact cross-band combination the pre-fix loader let through unchecked.
        // (Deltas are relative to each need's own raws-authored start reserve, not MAX.)
        actor.applyNeedDelta(Need.HUNGER, (NeedThresholds.LOW - 100) - actor.need(Need.HUNGER));
        actor.applyNeedDelta(Need.REST, (NeedThresholds.CRITICAL - 100) - actor.need(Need.REST));
        assertTrue(NeedThresholds.isLow(actor.need(Need.HUNGER))
                        && !NeedThresholds.isCritical(actor.need(Need.HUNGER)),
                "sanity: HUNGER must be low-not-critical, was " + actor.need(Need.HUNGER));
        assertTrue(NeedThresholds.isCritical(actor.need(Need.REST)),
                "sanity: REST must be critical, was " + actor.need(Need.REST));

        int seekFoodScore = Policies.SEEK_FOOD.score(actor, ctx);
        int returnHomeScore = Policies.RETURN_HOME.score(actor, ctx);
        assertTrue(seekFoodScore >= returnHomeScore,
                "SEEK_FOOD (" + seekFoodScore + ") must be >= RETURN_HOME (" + returnHomeScore
                        + ") even with HUNGER only low and REST critical");

        // speedTicksPerStep (2 for a real Serf) gates movement by an internal tick
        // accumulator (Actor#stepToward), so a single tick() call isn't guaranteed to move —
        // tick until it does (or fail fast well short of that).
        for (int i = 0; i < 10 && actor.cell() == PackedPos.pack(10, 0, 1); i++) {
            actor.tick(ctx);
        }
        assertTrue(actor.cell() != PackedPos.pack(10, 0, 1), "the actor must have stepped");
        assertEqualsReasonCode(actor);
    }

    private static void assertEqualsReasonCode(Actor actor) {
        org.junit.jupiter.api.Assertions.assertEquals(ReasonCode.NEED_HUNGER_LOW, actor.lastReasonCode(),
                "SEEK_FOOD must win the whole-stack decision in the cross-band case");
    }
}
