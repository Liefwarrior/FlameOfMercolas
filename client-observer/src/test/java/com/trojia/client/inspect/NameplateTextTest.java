package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobId;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link NameplateText} label contract (Sprint 1 item 2) against the real compound
 * population: PRESENTED identity always — the §1.1 social read — plus the un-forged name
 * fallback and the co-located stack cascade. Named labels are pinned by
 * {@code DocksCharacterSheetTest} against the forged docks bake. Headless, no GL.
 */
class NameplateTextTest {

    private static CompoundBlockPopulation build() {
        return CompoundBlockPopulation.build(1234L);
    }

    @Test
    void unforgedLabelFallsBackToTypeAndJob() {
        CompoundBlockPopulation p = build();
        String label = NameplateText.labelFor(2, p.registry(), p.jobs(), IdentityRegistry.EMPTY);
        assertEquals("Serf #2 -- serf.laborer", label);
    }

    @Test
    void villainCoverLabelShowsThePresentedJobNeverTheTrueOne() {
        CompoundBlockPopulation p = build();
        int skyrunnerId = firstWithTrueJob(p, Job.Villain.Skyrunner.ID);
        String label = NameplateText.labelFor(skyrunnerId, p.registry(), p.jobs(),
                IdentityRegistry.EMPTY);
        assertTrue(label.endsWith(" -- wastrel.streetlife"),
                "a cover must read as the cover: " + label);
        assertFalse(label.contains("skyrunner"), "the true job must never leak: " + label);
    }

    @Test
    void disguisedActorHoversAsThePresentedActor() {
        // The whole persona north star, made hoverable: after setActAs, the disguised
        // actor's label is byte-identical to the impersonated actor's own label.
        CompoundBlockPopulation p = build();
        Actor serf = p.registry().get(2);
        Actor watch = firstOfType(p, "militia_watch");

        serf.setActAs(watch.id());
        assertEquals(
                NameplateText.labelFor(watch.id(), p.registry(), p.jobs(), IdentityRegistry.EMPTY),
                NameplateText.labelFor(2, p.registry(), p.jobs(), IdentityRegistry.EMPTY));

        serf.setActAs(serf.id());
        assertTrue(NameplateText.labelFor(2, p.registry(), p.jobs(), IdentityRegistry.EMPTY)
                .startsWith("Serf #2"));
    }

    @Test
    void coLocatedActorsStackLabelsInAscendingIdOrder() {
        // Household members spawn co-located (the ActorPicker javadoc's own convention);
        // the plate lists the whole stack in ActorRenderer's cascade order — ascending id.
        CompoundBlockPopulation p = build();
        int[] pair = firstSharedCellPair(p);
        int cell = p.registry().get(pair[0]).cell();

        List<String> labels = NameplateText.labelsAt(PackedPos.x(cell), PackedPos.y(cell),
                PackedPos.z(cell), p.registry(), p.jobs(), IdentityRegistry.EMPTY);

        assertTrue(labels.size() >= 2, "expected a stacked cell, got " + labels);
        assertEquals(NameplateText.labelFor(pair[0], p.registry(), p.jobs(),
                IdentityRegistry.EMPTY), labels.get(0), "lowest id leads the cascade");
        assertTrue(labels.contains(NameplateText.labelFor(pair[1], p.registry(), p.jobs(),
                IdentityRegistry.EMPTY)), labels.toString());
    }

    @Test
    void emptyTileYieldsNoLabels() {
        CompoundBlockPopulation p = build();
        // z-level 60 is far above the compound block — guaranteed empty.
        assertTrue(NameplateText.labelsAt(0, 0, 60, p.registry(), p.jobs(),
                IdentityRegistry.EMPTY).isEmpty());
    }

    /** The two lowest ids sharing a spawn cell (ascending scan — deterministic). */
    private static int[] firstSharedCellPair(CompoundBlockPopulation p) {
        Map<Integer, Integer> firstOnCell = new HashMap<>();
        for (int i = 0; i < p.registry().size(); i++) {
            Integer prior = firstOnCell.putIfAbsent(p.registry().get(i).cell(), i);
            if (prior != null) {
                return new int[] {prior, i};
            }
        }
        throw new AssertionError("no two actors share a spawn cell");
    }

    private static Actor firstOfType(CompoundBlockPopulation p, String typeKey) {
        for (int i = 0; i < p.registry().size(); i++) {
            if (p.registry().get(i).typeId().key().equals(typeKey)) {
                return p.registry().get(i);
            }
        }
        throw new AssertionError("no actor of type " + typeKey + " in the population");
    }

    /** The lowest-id actor whose TRUE (not presented) job is {@code jobId}. */
    private static int firstWithTrueJob(CompoundBlockPopulation p, JobId jobId) {
        for (int i = 0; i < p.registry().size(); i++) {
            Actor a = p.registry().get(i);
            if (a.jobOrdinal() >= 0 && p.jobs().get(a.jobOrdinal()).id().equals(jobId)) {
                return a.id();
            }
        }
        throw new AssertionError("no actor with true job " + jobId + " in the population");
    }
}
