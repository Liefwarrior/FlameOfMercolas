package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.world.PackedPos;

import java.util.ArrayList;
import java.util.List;

/**
 * Pure label content for the hover nameplates (Sprint 1 item 2, the Morrowind hover-name):
 * one line per actor, {@code Ditta Pilchard "Barrel-Back" -- laborer.dock}. GL-free — the
 * {@code NameplateRenderer} draws these; this class is the unit-testable content.
 *
 * <p><b>PRESENTED identity, always (§1.1 social read made visible).</b> Every part of a
 * label — name, epithet, job — resolves through {@code Actor#identity().presentedId()}
 * first and then through the presented actor's own Job-cover: a disguised villain hovers
 * as the wastrel it presents, and a cover job reads as the cover. The nameplate is the
 * ward's eye view, never the omniscient one (that is the sheet's IDENTITY section).
 */
public final class NameplateText {

    private NameplateText() {
    }

    /**
     * The nameplate label for one actor: the PRESENTED identity's name + epithet, then its
     * presented job id when it has one ({@code "name -- job"}); just the name for the
     * jobless (beasts).
     */
    public static String labelFor(int actorId, ActorRegistry registry, JobRegistry jobs,
            IdentityRegistry identity) {
        int presentedId = registry.get(actorId).identity().presentedId();
        Actor presented = registry.get(presentedId);
        String name = PersonNames.nameWithEpithet(presentedId, registry, identity);
        Job job = presented.jobOrdinal() >= 0 ? jobs.get(presented.jobOrdinal()) : null;
        String jobId = JobDisplay.presentedJobId(job);
        return JobDisplay.NONE_LABEL.equals(jobId) ? name : name + " -- " + jobId;
    }

    /**
     * The labels of EVERY actor standing on {@code (tileX, tileY, z)}, ascending ActorId —
     * exactly {@code ActorRenderer}'s stack-cascade draw order, so a Keeper standing on its
     * Animal's tile yields both labels in the order their sprites cascade. Empty when the
     * tile is empty. Same linear-scan convention as {@code ActorPicker} (which returns only
     * the lowest id — the plate wants the whole stack).
     */
    public static List<String> labelsAt(int tileX, int tileY, int z, ActorRegistry registry,
            JobRegistry jobs, IdentityRegistry identity) {
        List<String> labels = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            int cell = registry.get(i).cell();
            if (PackedPos.z(cell) == z && PackedPos.x(cell) == tileX
                    && PackedPos.y(cell) == tileY) {
                labels.add(labelFor(i, registry, jobs, identity));
            }
        }
        return labels;
    }
}
