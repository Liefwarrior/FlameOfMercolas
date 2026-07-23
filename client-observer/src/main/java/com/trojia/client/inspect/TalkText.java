package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.BarkSelector;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.quest.QuestLog;
import com.trojia.sim.actor.quest.QuestRegistry;
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

    /**
     * The quest-beat marker on the name line (Sprint 3, the design's {@code ◆} rendered
     * ASCII — the in-game glyph set covers printable ASCII only, the quests.json
     * normalization ruling).
     */
    public static final String QUEST_MARK = "*";

    /** The quest-beat context tag (in place of the disposition tag while a beat is served). */
    public static final String QUEST_TAG = "[*]";

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
     *
     * <p>The quest-less overload (pre-quest fixtures and callers): delegates with the empty
     * quest surface, so the stock greet path is byte-identical to Sprint 2.
     */
    public static Exchange greet(long worldSeed, long tick, int speakerId, int listenerId,
            ActorRegistry registry, JobRegistry jobs, IdentityRegistry identity,
            FactionStandings standings, RelationshipRegistry relationships,
            BarkTableRegistry barks) {
        return greet(worldSeed, tick, speakerId, listenerId, registry, jobs, identity,
                standings, relationships, barks, QuestRegistry.EMPTY, QuestLog.UNWIRED);
    }

    /**
     * The quest-aware greet (Sprint 3 "The Vanished Clerk", §3.2): when the speaker's TRUE
     * id is a declared party of a live quest entry the listener could advance (unbound, or
     * bound to the listener's TRUE body; current stage non-terminal), the table key becomes
     * {@code quest.<questId>.<stageKey>.<partySymbol>} — resolved through the same stock
     * fallback chain on the SAME presentation-lane row draw (no extra draw, sim-silent as
     * ever) — the name line gains the {@link #QUEST_MARK} and the context tag renders
     * {@link #QUEST_TAG}. Otherwise the stock greet path, unchanged.
     *
     * <p>Beat vs engine alignment: an {@code EXECUTED} speaker never serves a beat (the
     * engine's own talk-trigger skip), and a {@code mood.dead} exchange keeps the mood
     * (the gibbet/grave outranks the errand); every other mood — downed, held, confined —
     * still serves the beat, because a held party can still mutter the truth (the S3 edge
     * ruling). Quest matching is on TRUE ids for both sides (bodies talk to bodies, the
     * engine's own rule), so a disguised listener still sees — and can still advance — its
     * own quest.
     */
    public static Exchange greet(long worldSeed, long tick, int speakerId, int listenerId,
            ActorRegistry registry, JobRegistry jobs, IdentityRegistry identity,
            FactionStandings standings, RelationshipRegistry relationships,
            BarkTableRegistry barks, QuestRegistry quests, QuestLog questLog) {
        Actor speaker = registry.get(speakerId);
        int speakerPresentedId = speaker.identity().presentedId();
        Actor presented = registry.get(speakerPresentedId);
        Job presentedJob = presented.jobOrdinal() >= 0 ? jobs.get(presented.jobOrdinal()) : null;
        int listenerPresentedId = registry.get(listenerId).identity().presentedId();

        BarkSelector.BarkChoice choice = BarkSelector.select(worldSeed, tick, speaker,
                presentedJob, listenerPresentedId, standings, relationships);

        String name = PersonNames.nameWithEpithet(speakerPresentedId, registry, identity);
        String jobId = JobDisplay.presentedJobId(presentedJob);
        String jobLine = JobDisplay.NONE_LABEL.equals(jobId) ? "" : jobId;

        int entry = questBeatEntry(quests, questLog, speaker, listenerId);
        if (entry >= 0 && !"mood.dead".equals(choice.tableKey())) {
            int q = questLog.questOrdinalOf(entry);
            String key = "quest." + quests.questId(q) + "."
                    + quests.stageKey(q, questLog.stageOf(entry)) + "."
                    + quests.partySymbol(q, speakerId);
            String bark = new BarkSelector.BarkChoice(key, choice.rowDraw()).resolve(barks);
            return new Exchange(speakerId, speakerPresentedId, tick,
                    QUEST_MARK + " " + name, jobLine, QUEST_TAG,
                    bark == null ? SAYS_NOTHING : bark);
        }

        String bark = choice.resolve(barks);
        return new Exchange(speakerId, speakerPresentedId, tick, name, jobLine,
                dispositionTag(choice.tableKey()), bark == null ? SAYS_NOTHING : bark);
    }

    /**
     * The quest entry {@code speaker} serves a beat for toward {@code listenerTrueId}, or
     * {@code -1}: ascending entry order, first live entry wins — current stage non-terminal,
     * owner unbound or the listener's TRUE body, the speaker's TRUE id a declared party
     * (the {@link QuestRegistry#partySymbol} vocabulary — exactly the key set the World
     * bark lint calls selectable), and the speaker not {@code EXECUTED}.
     */
    static int questBeatEntry(QuestRegistry quests, QuestLog questLog, Actor speaker,
            int listenerTrueId) {
        if (speaker.hasStatus(StatusBit.EXECUTED)) {
            return -1;
        }
        for (int e = 0; e < questLog.entryCount(); e++) {
            int q = questLog.questOrdinalOf(e);
            if (quests.terminal(q, questLog.stageOf(e))) {
                continue;
            }
            int owner = questLog.ownerOf(e);
            if (owner != Actor.NONE && owner != listenerTrueId) {
                continue;
            }
            if (quests.partySymbol(q, speaker.id()) != null) {
                return e;
            }
        }
        return -1;
    }

    /**
     * The asked exchange (Sprint 4 item 2, the topic rows): recomputes the panel content
     * for a PLAYER-CHOSEN topic against the same speaker, at the asking tick.
     *
     * <ul>
     *   <li>{@link TalkTopics.Kind#QUEST} — delegates to the quest-aware {@link #greet}
     *       verbatim (the beat exchange, marker and all): choosing the marked row re-serves
     *       the beat line the S3 convention already renders.</li>
     *   <li>{@link TalkTopics.Kind#PERSONAL} / {@link TalkTopics.Kind#GOSSIP} — the sim's
     *       own {@link BarkSelector#selectAsk} with the topic list NARROWED to the chosen
     *       symbol (the frozen S4 seam, exactly): the mood override still wins — a held
     *       soul mutters its mood instead of gossiping — and the resolved row rides the
     *       pinned presentation lane, sim-silent as ever.</li>
     * </ul>
     */
    public static Exchange ask(long worldSeed, long tick, int speakerId, int listenerId,
            ActorRegistry registry, JobRegistry jobs, IdentityRegistry identity,
            FactionStandings standings, RelationshipRegistry relationships,
            BarkTableRegistry barks, QuestRegistry quests, QuestLog questLog,
            TalkTopics.Topic topic) {
        if (topic.kind() == TalkTopics.Kind.QUEST) {
            return greet(worldSeed, tick, speakerId, listenerId, registry, jobs, identity,
                    standings, relationships, barks, quests, questLog);
        }
        Actor speaker = registry.get(speakerId);
        int speakerPresentedId = speaker.identity().presentedId();
        Actor presented = registry.get(speakerPresentedId);
        Job presentedJob = presented.jobOrdinal() >= 0 ? jobs.get(presented.jobOrdinal()) : null;

        BarkSelector.BarkChoice choice = topic.kind() == TalkTopics.Kind.PERSONAL
                ? BarkSelector.selectAsk(worldSeed, tick, speaker, topic.symbol(), List.of())
                : BarkSelector.selectAsk(worldSeed, tick, speaker, null,
                        List.of(topic.symbol()));

        String name = PersonNames.nameWithEpithet(speakerPresentedId, registry, identity);
        String jobId = JobDisplay.presentedJobId(presentedJob);
        String jobLine = JobDisplay.NONE_LABEL.equals(jobId) ? "" : jobId;
        String bark = choice == null ? null : choice.resolve(barks);
        String tag = choice == null ? "[?]" : dispositionTag(choice.tableKey());
        return new Exchange(speakerId, speakerPresentedId, tick, name, jobLine, tag,
                bark == null ? SAYS_NOTHING : bark);
    }

    /**
     * The disposition tag a bark table key encodes, rendered {@code [word]}:
     * {@code mood.held} &rarr; {@code [held]}; {@code greet.watch.cold.night} &rarr;
     * {@code [cold]}; the S4 ask families read {@code personal.*} &rarr; {@code [personal]}
     * and {@code gossip.*} &rarr; {@code [rumor]}. The attitude/mood segment is the
     * SELECTOR's own computation — parsing the key it produced is reuse, not re-derivation
     * (no thresholds duplicated here).
     */
    public static String dispositionTag(String tableKey) {
        String[] parts = tableKey.split("\\.");
        if (parts.length >= 2 && "mood".equals(parts[0])) {
            return "[" + parts[1] + "]";
        }
        if (parts.length >= 3 && "greet".equals(parts[0])) {
            return "[" + parts[2] + "]";
        }
        if (parts.length >= 2 && "personal".equals(parts[0])) {
            return "[personal]";
        }
        if (parts.length >= 2 && "gossip".equals(parts[0])) {
            return "[rumor]";
        }
        return "[?]"; // defensive: an unrecognized key shape still renders something
    }
}
