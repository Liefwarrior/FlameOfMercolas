package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.HashSet;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Pass 10 (Feature 2 substrate) — multi-cell holding: the scalar {@code arrestHoldCell} is
 * generalized into a {@link PrisonCellRegistry}, and each arrest is assigned the lowest-free cell
 * by a deterministic ascending scan (respecting {@link Actor#MAX_OCCUPANTS_PER_CELL}). Six
 * simultaneous prisoners occupy six distinct cells; a release frees the slot for reuse.
 */
final class MultiCellHoldingTest {

    private static final int Z = 11;

    /** A hold context: a prison registry + a nearby Watch + pinned "arrest succeeds, floor sentence". */
    private static final class HoldContext extends NoOpActorContext {
        private final PrisonCellRegistry cells;

        HoldContext(ActorRegistry registry, PrisonCellRegistry cells) {
            super(registry);
            this.cells = cells;
        }

        @Override
        public PrisonCellRegistry prisonCells() {
            return cells;
        }

        @Override
        public long draw(ActorRngStream stream, int actorId, int drawIndex) {
            if (stream == ActorRngStream.WATCH_ARREST_CHECK) {
                return 0L; // always caught
            }
            if (stream == ActorRngStream.WATCH_SENTENCE_LENGTH) {
                return 0L; // 1-day floor
            }
            return super.draw(stream, actorId, drawIndex);
        }
    }

    private static PrisonCellRegistry cells(int n) {
        int[] c = new int[n];
        for (int i = 0; i < n; i++) {
            c[i] = PackedPos.pack(100 + 2 * i, 90, Z); // six distinct K34-style cells
        }
        return new PrisonCellRegistry(c);
    }

    private static Actor villain(ActorRegistry registry, int x) {
        return registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                PackedPos.pack(x, 50, Z));
    }

    private static void spawnWatch(ActorRegistry registry, ActorContext ctx, int x) {
        Actor watch = registry.spawn(MilitiaWatch.TYPE, ActorTestFixtures.stats(MilitiaWatch.TYPE),
                PackedPos.pack(x, 50, Z));
        watch.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
    }

    private static Job.Villain cutpurse(ActorContext ctx) {
        return (Job.Villain) ctx.jobs().get(ctx.jobs().ordinalOf(Job.Villain.Cutpurse.ID));
    }

    @Test
    void sixSimultaneousPrisonersOccupySixDistinctCells() {
        ActorRegistry registry = new ActorRegistry();
        // Six villains clustered next to one Watch, all within the detection radius.
        Actor[] villains = new Actor[6];
        for (int i = 0; i < 6; i++) {
            villains[i] = villain(registry, 48 + i);
        }
        HoldContext ctx = new HoldContext(registry, cells(6));
        spawnWatch(registry, ctx, 50);
        Job.Villain job = cutpurse(ctx);

        Set<Integer> assigned = new HashSet<>();
        for (Actor v : villains) {
            assertTrue(JobBehaviors.checkArrestExposure(v, ctx, job), "each is caught (pinned draw)");
            assertTrue(v.hasStatus(StatusBit.HELD));
            assertNotEquals(Actor.NONE, v.assignedHoldCell(), "each gets a real cell");
            assigned.add(v.assignedHoldCell());
        }
        assertEquals(6, assigned.size(), "six prisoners -> six DISTINCT cells, none piled in the street");
        // The lowest-free ascending scan hands out cells 0..5 in arrest order.
        for (int i = 0; i < 6; i++) {
            assertEquals(cells(6).cellAt(i), villains[i].assignedHoldCell(),
                    "arrest i takes prison cell i (ascending lowest-free)");
        }
    }

    @Test
    void aSeventhPrisonerSharesOnlyOnceEveryCellHasOneAndTheCapIsRespected() {
        ActorRegistry registry = new ActorRegistry();
        Actor[] villains = new Actor[8];
        for (int i = 0; i < 8; i++) {
            villains[i] = villain(registry, 46 + i);
        }
        HoldContext ctx = new HoldContext(registry, cells(6));
        spawnWatch(registry, ctx, 50);
        Job.Villain job = cutpurse(ctx);
        for (Actor v : villains) {
            JobBehaviors.checkArrestExposure(v, ctx, job);
        }
        // First six fan out to distinct cells. Density revisit (cap 2 -> 1): no cell ever
        // doubles up any more — the 7th and 8th find every cell at the 1-occupant cap and get
        // NO cell (HeldPolicy falls back to the scalar hold cell / hold-in-place). This is why
        // the map pass grew K34 from 6 to 9 cells: capacity is now exactly the cell count.
        assertEquals(Actor.NONE, villains[6].assignedHoldCell(),
                "the 7th finds every cell at the 1-occupant cap -> no cell assigned");
        assertEquals(Actor.NONE, villains[7].assignedHoldCell(),
                "the 8th likewise -- a cell is never overfilled");
        // Cell 0 must hold exactly MAX_OCCUPANTS_PER_CELL (= 1) prisoners, no more.
        int inCell0 = 0;
        for (Actor v : villains) {
            if (v.assignedHoldCell() == cells(6).cellAt(0)) {
                inCell0++;
            }
        }
        assertEquals(Actor.MAX_OCCUPANTS_PER_CELL, inCell0);
    }

    @Test
    void releaseFreesTheSlotForTheNextArrest() {
        ActorRegistry registry = new ActorRegistry();
        // A single-cell prison (capacity 1 since the density revisit). One arrest fills it; the
        // next finds no free cell. All actors (incl. the later-arrested v3) are spawned before
        // the context so its per-actor draw-counter array is sized to the full registry.
        Actor v0 = villain(registry, 48);
        Actor v1 = villain(registry, 49);
        Actor v2 = villain(registry, 51);
        Actor v3 = villain(registry, 52);
        HoldContext ctx = new HoldContext(registry, cells(1));
        spawnWatch(registry, ctx, 50);
        Job.Villain job = cutpurse(ctx);

        JobBehaviors.checkArrestExposure(v0, ctx, job);
        JobBehaviors.checkArrestExposure(v1, ctx, job);
        assertEquals(cells(1).cellAt(0), v0.assignedHoldCell());
        assertEquals(Actor.NONE, v1.assignedHoldCell(),
                "the only cell is at the 1-occupant cap -> no cell assigned (falls back to hold-in-place)");

        JobBehaviors.checkArrestExposure(v2, ctx, job);
        assertEquals(Actor.NONE, v2.assignedHoldCell(),
                "likewise for every later arrest while the cell is taken");

        // Release v0 (sentence elapsed): HeldPolicy clears HELD and frees the assigned cell.
        v0.setHeldUntilTick(100L);
        ctx.setTick(100L);
        Policies.HELD.act(v0, ctx);
        assertFalse(v0.hasStatus(StatusBit.HELD));
        assertEquals(Actor.NONE, v0.assignedHoldCell(), "release frees the slot");

        // A fresh arrest now finds the freed slot again (the cell is empty, under the cap).
        JobBehaviors.checkArrestExposure(v3, ctx, job);
        assertEquals(cells(1).cellAt(0), v3.assignedHoldCell(), "the freed slot is reused");
    }
}
