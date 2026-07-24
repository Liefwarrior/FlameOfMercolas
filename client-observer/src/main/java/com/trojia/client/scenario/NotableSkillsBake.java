package com.trojia.client.scenario;

import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.progression.SkillRegistry;

import java.util.List;
import java.util.Map;

/**
 * The Sprint-5 authored-mastery bake ("the masters of the ward"): seeds each notable's
 * authored starting skills ({@code notables.json} {@code "skills"} maps) onto its
 * spawn-site-bound actor's track via {@link SkillTrackRegistry#seedLevel} — BEFORE the
 * first tick, so the levels are ordinary bake-immutable state inside the persisted triad
 * and the twin-run identity gates (the {@link MicroHistoryBake} discipline exactly).
 *
 * <p>Draw-free and file-ordered: a pure function of (bound notable map, committed raws),
 * so twin bakes seed identical sheets. An authored key that does not resolve against the
 * committed skill raws fails the bake loudly — the frame-guard discipline, never a silent
 * zero.
 */
final class NotableSkillsBake {

    private NotableSkillsBake() {
    }

    /**
     * Seeds every authored grant. Returns the number of grants applied (test/report seam).
     *
     * @throws IllegalStateException when a notable with grants is unbound, or a grant names
     *                               an unknown skill key
     */
    static int apply(List<NotableRaws.Notable> notables, Map<String, Integer> notableActors,
            SkillTrackRegistry tracks) {
        SkillRegistry skills = tracks.skills();
        int applied = 0;
        for (NotableRaws.Notable notable : notables) {
            if (notable.skills().isEmpty()) {
                continue;
            }
            Integer actorId = notableActors.get(notable.id());
            if (actorId == null) {
                throw new IllegalStateException("notable \"" + notable.id()
                        + "\" has authored skills but no bound actor");
            }
            for (NotableRaws.SkillGrant grant : notable.skills()) {
                if (skills == null || !skills.contains(grant.skillKey())) {
                    throw new IllegalStateException("notable \"" + notable.id()
                            + "\" grants unknown skill \"" + grant.skillKey()
                            + "\" (skills.json is the vocabulary)");
                }
                tracks.seedLevel(actorId, skills.id(grant.skillKey()).raw(), grant.level());
                applied++;
            }
        }
        return applied;
    }
}
