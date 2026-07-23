package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.BarkSelector;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.RelationshipRegistry;
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

    /** One plate row: the label plus its standing-attitude token ({@code null} untinted). */
    public record Plate(String label, String attitude) {
    }

    /**
     * {@link #labelsAt} plus the per-actor standing attitude toward {@code viewerId} (the
     * PLAYED actor; pass {@link Actor#NONE} outside Play mode for plain plates) — the
     * Sprint 2 "subtle standing tint": the renderer maps each attitude token to a text
     * color, so a hover tells the player at a glance how that soul regards the face the
     * player currently wears.
     */
    public static List<Plate> platesAt(int tileX, int tileY, int z, ActorRegistry registry,
            JobRegistry jobs, IdentityRegistry identity, FactionStandings standings,
            RelationshipRegistry relationships, int viewerId) {
        List<Plate> plates = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            int cell = registry.get(i).cell();
            if (PackedPos.z(cell) == z && PackedPos.x(cell) == tileX
                    && PackedPos.y(cell) == tileY) {
                plates.add(new Plate(labelFor(i, registry, jobs, identity),
                        attitudeToward(i, viewerId, registry, jobs, standings, relationships)));
            }
        }
        return plates;
    }

    /**
     * How {@code actorId} regards {@code viewerId}'s PRESENTED face — the attitude token
     * ({@code kin}/{@code friend}/{@code warm}/{@code neutral}/{@code cold}/{@code hostile})
     * parsed off the key the sim's own {@link BarkSelector#select} produces for the pair, so
     * plate tint, greeting text and counter prices can never disagree. {@code null} (no
     * tint) with no viewer, for the viewer's own body, or when a mood override (held,
     * panicked...) preempts the greeting — a mood is not a standing.
     *
     * <p>Seed/tick are pinned to 0: they feed only the ROW draw, never the table key, and
     * the selector draws on its own presentation lane — asking can never perturb the sim.
     */
    public static String attitudeToward(int actorId, int viewerId, ActorRegistry registry,
            JobRegistry jobs, FactionStandings standings, RelationshipRegistry relationships) {
        if (viewerId == Actor.NONE || viewerId == actorId) {
            return null;
        }
        Actor speaker = registry.get(actorId);
        Actor presented = registry.get(speaker.identity().presentedId());
        Job presentedJob = presented.jobOrdinal() >= 0 ? jobs.get(presented.jobOrdinal()) : null;
        int viewerPresentedId = registry.get(viewerId).identity().presentedId();
        String key = BarkSelector.select(0L, 0L, speaker, presentedJob, viewerPresentedId,
                standings, relationships).tableKey();
        if (!key.startsWith("greet.")) {
            return null; // a mood override (held, downed, panicked...) is not a standing
        }
        String[] parts = key.split("\\.");
        return parts.length >= 3 ? parts[2] : null;
    }
}
