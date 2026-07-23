package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.BarkSelector;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.bark.BarkTableRegistry;

import java.util.ArrayList;
import java.util.List;

/**
 * Pure content for the TALK surface (Sprint 2 item 1, "walk up and talk"): given a speaker,
 * a listener and the live side-tables, builds one {@link Exchange} — the speaker's
 * ward-facing identity plus the bark the sim's own {@link BarkSelector} chose for this
 * {@code (worldSeed, tick, speaker, listener)}. GL-free so the exact exchange is
 * unit-testable; {@code TalkPanelRenderer} only lays it out.
 *
 * <p><b>Every social read is PRESENTED (the Persona rule).</b> The speaker is named and
 * jobbed as the ward sees it; the listener is read by the face it presents — a disguised
 * played actor is greeted as WHO IT PRESENTS, which is the whole disguise loop made audible.
 *
 * <p><b>Sim-silent.</b> {@link BarkSelector#select} draws on the pinned presentation lane
 * (never the per-actor sim counter), so opening the panel can never perturb a running twin.
 * The exchange is computed ONCE at open (frozen at its tick) — the panel does not flicker
 * as ticks pass; press T again for a fresh greeting.
 */
public final class TalkText {

    /** Shown when the fallback chain finds no authored table at all (degraded fixtures). */
    public static final String SAYS_NOTHING = "(says nothing)";

    private TalkText() {
    }

    /**
     * One frozen exchange: the speaker's ward-facing header plus the selected bark.
     *
     * @param speakerId          the TRUE body spoken to (the portrait cache key is presented)
     * @param speakerPresentedId the identity the ward sees (name, portrait, job)
     * @param tick               the tick the exchange was computed at
     * @param nameLine           presented name + quoted epithet
     * @param jobLine            the presented job id, or {@code ""} for the jobless (beasts)
     * @param contextLine        the disposition tag the bark table key encodes, e.g.
     *                           {@code [cold]} / {@code [kin]} / {@code [held]}
     * @param barkLine           the resolved bark text (never blank; {@link #SAYS_NOTHING}
     *                           when nothing is authored)
     */
    public record Exchange(int speakerId, int speakerPresentedId, long tick, String nameLine,
            String jobLine, String contextLine, String barkLine) {

        /** The whole exchange as flat text lines — the headless/unit-test surface. */
        public List<String> panelLines() {
            List<String> lines = new ArrayList<>();
            lines.add(nameLine);
            if (!jobLine.isEmpty()) {
                lines.add(jobLine + "  " + contextLine);
            } else {
                lines.add(contextLine);
            }
            lines.add(barkLine);
            return lines;
        }
    }

    /**
     * Builds the exchange for {@code speakerId} addressing {@code listenerId} at
     * {@code tick}: the sim's own {@link BarkSelector#select} picks the table (mood override,
     * else presented-family x attitude x time), the World-authored registry resolves the row
     * through the documented fallback chain, and the disposition tag is parsed straight off
     * the selected key — the one derivation, never re-derived client-side.
     */
    public static Exchange greet(long worldSeed, long tick, int speakerId, int listenerId,
            ActorRegistry registry, JobRegistry jobs, IdentityRegistry identity,
            FactionStandings standings, RelationshipRegistry relationships,
            BarkTableRegistry barks) {
        Actor speaker = registry.get(speakerId);
        int speakerPresentedId = speaker.identity().presentedId();
        Actor presented = registry.get(speakerPresentedId);
        Job presentedJob = presented.jobOrdinal() >= 0 ? jobs.get(presented.jobOrdinal()) : null;
        int listenerPresentedId = registry.get(listenerId).identity().presentedId();

        BarkSelector.BarkChoice choice = BarkSelector.select(worldSeed, tick, speaker,
                presentedJob, listenerPresentedId, standings, relationships);
        String bark = choice.resolve(barks);

        String name = PersonNames.nameWithEpithet(speakerPresentedId, registry, identity);
        String jobId = JobDisplay.presentedJobId(presentedJob);
        String jobLine = JobDisplay.NONE_LABEL.equals(jobId) ? "" : jobId;
        return new Exchange(speakerId, speakerPresentedId, tick, name, jobLine,
                dispositionTag(choice.tableKey()), bark == null ? SAYS_NOTHING : bark);
    }

    /**
     * The disposition tag a bark table key encodes, rendered {@code [word]}:
     * {@code mood.held} &rarr; {@code [held]}; {@code greet.watch.cold.night} &rarr;
     * {@code [cold]}. The attitude/mood segment is the SELECTOR's own computation — parsing
     * the key it produced is reuse, not re-derivation (no thresholds duplicated here).
     */
    public static String dispositionTag(String tableKey) {
        String[] parts = tableKey.split("\\.");
        if (parts.length >= 2 && "mood".equals(parts[0])) {
            return "[" + parts[1] + "]";
        }
        if (parts.length >= 3 && "greet".equals(parts[0])) {
            return "[" + parts[2] + "]";
        }
        return "[?]"; // defensive: an unrecognized key shape still renders something
    }
}
