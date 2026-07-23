package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.inspect.AdjacentTargets;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.TalkState;
import com.trojia.client.inspect.TalkText;
import com.trojia.client.inspect.TalkTopics;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.scenario.AskTopicsBake;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.client.scenario.TopicCatalog;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.quest.QuestLog;
import com.trojia.sim.actor.quest.QuestRegistry;
import com.trojia.sim.bark.BarkTableRegistry;

import java.util.function.IntFunction;

/**
 * Polls the TALK verb (Sprint 2 item 1, PLAY-MODE adjacency interaction): while driving an
 * actor, {@code T} opens (or re-greets) the speech panel against the adjacent soul —
 * {@link AdjacentTargets}' lowest-id rule — and {@code ESC} closes it. Mirrors
 * {@link PlayModeInput}'s shape: a thin {@code Gdx.input} wrapper around a deterministic
 * {@link #applyTalk} seam a debug/verification caller can drive without a keyboard.
 *
 * <p><b>ESC ownership:</b> {@link #poll} returns {@code true} when it consumed this frame's
 * {@code ESC} to close the panel — the caller must then skip its own quit-on-ESC branch for
 * the frame, so closing a conversation never also closes the observer.
 */
public final class TalkInput {

    private TalkInput() {
    }

    /**
     * The pre-topics poll (kept so existing call sites/tests compile): no ask surface —
     * the panel opens greet-only with an empty topic list.
     */
    public static boolean poll(TalkState talk, PlayModeState playMode, ActorRegistry registry,
            JobRegistry jobs, IdentityRegistry identity, FactionStandings standings,
            RelationshipRegistry relationships, BarkTableRegistry barks, ToastQueue toasts,
            QuestRegistry quests, QuestLog questLog, long worldSeed, long tick) {
        return poll(talk, playMode, registry, jobs, identity, standings, relationships,
                barks, toasts, quests, questLog, worldSeed, tick, id -> null,
                TopicCatalog.EMPTY);
    }

    /**
     * Applies one frame's talk input (Sprint 4: {@code T} greet + the {@code 1-9}/{@code 0}
     * topic keys). Returns whether this call consumed the frame's {@code ESC} (panel
     * closed — the caller skips app-exit for this frame).
     */
    public static boolean poll(TalkState talk, PlayModeState playMode, ActorRegistry registry,
            JobRegistry jobs, IdentityRegistry identity, FactionStandings standings,
            RelationshipRegistry relationships, BarkTableRegistry barks, ToastQueue toasts,
            QuestRegistry quests, QuestLog questLog, long worldSeed, long tick,
            IntFunction<AskTopicsBake.Topics> askTopicsOf, TopicCatalog catalog) {
        // Leaving Play mode always drops the conversation (no floating panel over free-cam).
        if (talk.open() && !playMode.active()) {
            talk.close();
        }
        if (talk.open() && Gdx.input.isKeyJustPressed(Input.Keys.ESCAPE)) {
            talk.close();
            return true;
        }
        if (Gdx.input.isKeyJustPressed(Input.Keys.T)) {
            applyTalk(talk, playMode, registry, jobs, identity, standings, relationships,
                    barks, toasts, quests, questLog, worldSeed, tick, askTopicsOf, catalog);
        }
        if (talk.open()) {
            // The number row picks a topic (1-9 then 0 — TalkTopics' key order).
            for (int key = 0; key <= 9; key++) {
                if (Gdx.input.isKeyJustPressed(Input.Keys.NUM_0 + key)) {
                    applyAsk(talk, playMode, registry, jobs, identity, standings,
                            relationships, barks, quests, questLog, worldSeed, tick,
                            TalkTopics.indexOfKeyNumber(key));
                }
            }
        }
        return false;
    }

    /** The pre-topics application (kept for existing tests): empty ask surface. */
    public static void applyTalk(TalkState talk, PlayModeState playMode, ActorRegistry registry,
            JobRegistry jobs, IdentityRegistry identity, FactionStandings standings,
            RelationshipRegistry relationships, BarkTableRegistry barks, ToastQueue toasts,
            QuestRegistry quests, QuestLog questLog, long worldSeed, long tick) {
        applyTalk(talk, playMode, registry, jobs, identity, standings, relationships, barks,
                toasts, quests, questLog, worldSeed, tick, id -> null, TopicCatalog.EMPTY);
    }

    /**
     * The deterministic talk application (split from the live {@code Gdx.input} read above,
     * the {@code PlayModeInput.applyMovement} convention): resolves the adjacent target,
     * arms the sim's play-mode talk intent on the played body ({@code Actor
     * .setPlayerTalkTarget} — Sprint 3's §1.1 seam promotion: the FACT of talking enters
     * the sim so quest TALK triggers can fire; the greet itself stays sim-silent), and
     * opens the panel on a fresh quest-aware {@link TalkText#greet} exchange WITH its
     * numbered topic rows (S4 item 2, {@link TalkTopics#topicsFor}); toasts when nobody is
     * in reach. A no-op while Play mode is inactive.
     */
    public static void applyTalk(TalkState talk, PlayModeState playMode, ActorRegistry registry,
            JobRegistry jobs, IdentityRegistry identity, FactionStandings standings,
            RelationshipRegistry relationships, BarkTableRegistry barks, ToastQueue toasts,
            QuestRegistry quests, QuestLog questLog, long worldSeed, long tick,
            IntFunction<AskTopicsBake.Topics> askTopicsOf, TopicCatalog catalog) {
        if (!playMode.active()) {
            return;
        }
        int played = playMode.playedActorId();
        int target = AdjacentTargets.lowestIdAdjacent(registry, played, true);
        if (target == Actor.NONE) {
            toasts.add("No one within reach to talk to.");
            return;
        }
        registry.get(played).setPlayerTalkTarget(target);
        talk.open(TalkText.greet(worldSeed, tick, target, played, registry, jobs, identity,
                        standings, relationships, barks, quests, questLog),
                TalkTopics.topicsFor(registry.get(target), played, askTopicsOf.apply(target),
                        catalog, quests, questLog, registry, identity));
    }

    /**
     * The deterministic topic application (S4 item 2): replaces the open panel's exchange
     * with the chosen topic's line via {@link TalkText#ask} at the asking tick, against
     * the SAME speaker the panel opened on. Presentation-only and sim-silent — no intent
     * is armed, no adjacency is re-checked (the panel is frozen; a walked-away speaker
     * still finishes its story). A no-op while closed, outside Play mode, or for a number
     * with no row.
     */
    public static void applyAsk(TalkState talk, PlayModeState playMode, ActorRegistry registry,
            JobRegistry jobs, IdentityRegistry identity, FactionStandings standings,
            RelationshipRegistry relationships, BarkTableRegistry barks,
            QuestRegistry quests, QuestLog questLog, long worldSeed, long tick, int index) {
        if (!talk.open() || !playMode.active() || index < 0 || index >= talk.topics().size()) {
            return;
        }
        TalkTopics.Topic topic = talk.topics().get(index);
        talk.setExchange(TalkText.ask(worldSeed, tick, talk.exchange().speakerId(),
                playMode.playedActorId(), registry, jobs, identity, standings, relationships,
                barks, quests, questLog, topic));
    }
}
