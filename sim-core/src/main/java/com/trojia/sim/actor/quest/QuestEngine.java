package com.trojia.sim.actor.quest;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorContext;
import com.trojia.sim.actor.ActorGeometry;
import com.trojia.sim.actor.ActorRngStream;
import com.trojia.sim.actor.CrimeLog;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.RestrictedZone;
import com.trojia.sim.actor.SkillChecks;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.world.PackedPos;

/**
 * The quest advancement engine (Sprint 3 "The Vanished Clerk"): static verbs (the
 * {@code TheftMechanics} shape) invoked once per tick by {@code ActorsSystem.tick} AFTER
 * {@code registry.tickAll} on the SAME per-tick {@link ActorContext} — the per-actor draw
 * counters are still live, so the one search draw rides the pinned §2.2 attribution.
 *
 * <p><b>Evaluation rules (binding, per tick, ascending entry order):</b>
 * <ol>
 *   <li><b>Key-lift watcher:</b> fresh {@link CrimeLog} rows are harvested off the
 *       monotonic {@code crimeCursor} (the {@code CrimeFeedTracker} arithmetic); every
 *       SUCCESSFUL (unwitnessed) row whose thief is a bound owner and whose victim is a
 *       declared {@code liftItems} party of that owner's CURRENT stage also moves the
 *       declared token party→owner — the ordinary COIN pickpocket (check, XP, crime row,
 *       justice exposure) stays stock; the quest engine just adds the token to the same
 *       successful dip. Ambient thieves can never take the token (their lifts move COIN
 *       only, and the watcher moves it only for {@code thief == owner}). The cursor
 *       advances unconditionally each tick.</li>
 *   <li><b>Trigger scan:</b> the current stage's {@code advance} list in authored order;
 *       first satisfied trigger wins; at most ONE advance per entry per tick (chained
 *       stages settle on subsequent ticks — bounded, deterministic).</li>
 *   <li><b>On advance:</b> stamp the left stage's {@code completedTick}, enter the new
 *       stage, apply its effects in authored order, bump {@code totalAdvances}, and stamp
 *       {@link ReasonCode#QUEST_ADVANCED} on the owner (hash-carried, so the trail is
 *       twin-run-proof).</li>
 * </ol>
 *
 * <p><b>Determinism.</b> Everything here is draw-free state evaluation in fixed order
 * except the ONE search draw ({@link ActorRngStream#CHECK_SEARCH}, cooldown-gated, owner
 * as spatialKey through the shared counter). Every trigger except the {@code first_talker}
 * binding TALK requires a bound owner, so an inputless run (no talk intent ever fires)
 * leaves the engine state-idle at stage 0 with zero draws and zero writes.
 *
 * <p><b>Persona conformance.</b> Triggers match TRUE ids (bodies talk to bodies; the body
 * drives the quest); standing effects and ending edges land on the owner's PRESENTED id at
 * the firing tick (the ward rewards the FACE that did it — the disguise loop stays
 * priced); zone access is a plain cell-membership read (legitimacy stays
 * {@code ApprehendPolicy}'s business, untouched).
 */
public final class QuestEngine {

    /** Chebyshev reach of a search: adjacency to the declared cell (same z). */
    public static final int SEARCH_REACH = 1;

    private QuestEngine() {
    }

    /** One engine pass over every entry (called once per tick, after {@code tickAll}). */
    public static void tick(QuestRegistry quests, QuestLog log, ActorContext ctx) {
        if (log.entryCount() == 0) {
            return; // pre-quest worlds: nothing to evaluate, nothing to write
        }
        harvestKeyLifts(quests, log, ctx);
        for (int e = 0; e < log.entryCount(); e++) {
            evaluateEntry(quests, log, e, ctx);
        }
    }

    // ---------------------------------------------------------------- 1. key-lift watcher

    private static void harvestKeyLifts(QuestRegistry quests, QuestLog log, ActorContext ctx) {
        CrimeLog crimes = ctx.crimeLog();
        long total = crimes.totalRecorded();
        int fresh = (int) Math.min(total - log.crimeCursor(), crimes.size());
        for (int r = crimes.size() - fresh; r < crimes.size(); r++) {
            if (crimes.witnessedAt(r)) {
                continue; // only a SUCCESSFUL dip also lifts the token
            }
            int thief = crimes.thiefIdAt(r);
            int victim = crimes.victimIdAt(r);
            for (int e = 0; e < log.entryCount(); e++) {
                int owner = log.ownerOf(e);
                if (owner == Actor.NONE || thief != owner) {
                    continue;
                }
                QuestRegistry.CompiledStage stage = quests.quest(log.questOrdinalOf(e))
                        .stages[log.stageOf(e)];
                for (int li = 0; li < stage.liftItemKinds.length; li++) {
                    if (stage.liftFromPartyIds[li] == victim) {
                        ctx.items().moveCarried(victim, owner, stage.liftItemKinds[li], 1);
                    }
                }
            }
        }
        log.setCrimeCursor(total); // unconditional — consumed rows never re-harvest
    }

    // ---------------------------------------------------------------- 2. trigger scan

    private static void evaluateEntry(QuestRegistry quests, QuestLog log, int e,
            ActorContext ctx) {
        QuestRegistry.CompiledQuest quest = quests.quest(log.questOrdinalOf(e));
        QuestRegistry.CompiledStage stage = quest.stages[log.stageOf(e)];
        if (stage.terminal) {
            return;
        }
        int owner = log.ownerOf(e);
        for (QuestRegistry.CompiledTrigger t : stage.advance) {
            if (!fires(t, quest, log, e, owner, ctx)) {
                continue;
            }
            if (owner == Actor.NONE) {
                // A first_talker TALK just matched with no owner: the talker's TRUE body
                // takes (and keeps) the quest — fires() only passes this state for that
                // exact combination.
                log.bindOwner(e, log.latchTalkerId());
            }
            advance(quests, log, e, t.toStage, ctx);
            return; // at most one advance per entry per tick (first match wins)
        }
    }

    private static boolean fires(QuestRegistry.CompiledTrigger t,
            QuestRegistry.CompiledQuest quest, QuestLog log, int e, int owner,
            ActorContext ctx) {
        switch (t.kind) {
            case TALK -> {
                if (log.latchTick() != ctx.tick()) {
                    return false; // the latch matches THIS tick only (stale latches are inert)
                }
                if (log.latchTargetId() != t.partyActorId) {
                    return false; // bodies talk to bodies: target TRUE id == bound party
                }
                Actor party = ctx.registry().get(t.partyActorId);
                if (party.hasStatus(StatusBit.EXECUTED)) {
                    return false; // the gibbet closes only its own thread (downed/held still mutter)
                }
                int talker = log.latchTalkerId();
                if (owner == Actor.NONE) {
                    if (quest.binding != QuestRaws.Binding.FIRST_TALKER) {
                        return false; // a fixed quest with no owner never binds by talking
                    }
                } else if (talker != owner) {
                    return false; // the quest stays with the body that bound it
                }
                int carrier = owner == Actor.NONE ? talker : owner;
                return t.requireItemKind < 0
                        || ctx.items().countCarriedOfKind(carrier, t.requireItemKind) > 0;
            }
            case ENTER_ZONE -> {
                if (owner == Actor.NONE) {
                    return false;
                }
                RestrictedZone zone = ctx.restrictedZones().get(t.zoneId);
                return zone.contains(ctx.registry().get(owner).cell());
            }
            case ITEM -> {
                return owner != Actor.NONE
                        && ctx.items().countCarriedOfKind(owner, t.itemKind) > 0;
            }
            case SEARCH -> {
                return searchFires(t, log, e, owner, ctx);
            }
            case STANDING_AT_LEAST -> {
                return owner != Actor.NONE
                        && ctx.factionStandings().standingOf(presentedOf(owner, ctx),
                                t.faction) >= t.value;
            }
            case STANDING_AT_MOST -> {
                return owner != Actor.NONE
                        && ctx.factionStandings().standingOf(presentedOf(owner, ctx),
                                t.faction) <= t.value;
            }
            case AFTER_TICKS -> {
                return owner != Actor.NONE
                        && ctx.tick() - log.stageEnteredTickOf(e) >= t.ticks;
            }
            default -> {
                return false; // unreachable: the loader admits no other kind
            }
        }
    }

    /**
     * The search trigger — the system's ONE draw. Eligible when the owner stands within
     * {@link #SEARCH_REACH} of the declared cell on its z. Carrying the declared key item
     * opens draw-free; otherwise a cooldown-gated {@code check.search} pry at
     * {@code skill + WIT} vs the authored resist. Success (either way) moves the declared
     * item cell→owner and fires the advance.
     */
    private static boolean searchFires(QuestRegistry.CompiledTrigger t, QuestLog log, int e,
            int owner, ActorContext ctx) {
        if (owner == Actor.NONE) {
            return false;
        }
        Actor body = ctx.registry().get(owner);
        if (PackedPos.z(body.cell()) != PackedPos.z(t.cell)
                || ActorGeometry.chebyshev(body.cell(), t.cell) > SEARCH_REACH) {
            return false;
        }
        if (t.keyItemKind >= 0 && ctx.items().countCarriedOfKind(owner, t.keyItemKind) > 0) {
            ctx.items().moveCellToCarried(t.cell, owner, t.itemKind, 1);
            return true; // the key opens the drawer draw-free
        }
        if (ctx.tick() - log.lastCheckTickOf(e) < t.retryTicks) {
            return false; // cooling down between pries
        }
        log.noteSearchAttempt(e, ctx.tick());
        int permille = SkillChecks.searchPermille(ctx.skillTracks(), owner, t.skillRaw,
                t.resist);
        long draw = ctx.draw(ActorRngStream.CHECK_SEARCH, owner, ctx.nextDrawIndex(owner));
        if (!SkillChecks.passes(draw, permille)) {
            return false; // the drawer holds; the attempt counter is the client's toast cursor
        }
        ctx.items().moveCellToCarried(t.cell, owner, t.itemKind, 1);
        return true;
    }

    // ---------------------------------------------------------------- 3. the advance

    private static void advance(QuestRegistry quests, QuestLog log, int e, int toStage,
            ActorContext ctx) {
        log.advanceStage(e, toStage, ctx.tick());
        int owner = log.ownerOf(e);
        QuestRegistry.CompiledStage entered =
                quests.quest(log.questOrdinalOf(e)).stages[toStage];
        for (QuestRegistry.CompiledEffect effect : entered.effects) {
            apply(effect, owner, ctx);
        }
        ctx.registry().get(owner).setLastReasonCode(ReasonCode.QUEST_ADVANCED);
    }

    /** Applies one stage-entry effect (deterministic, draw-free, authored order). */
    private static void apply(QuestRegistry.CompiledEffect effect, int owner,
            ActorContext ctx) {
        switch (effect.kind) {
            case GIVE_ITEM ->
                    ctx.items().moveCarried(owner, effect.toPartyId, effect.itemKind, 1);
            case PAY ->
                    // Seize-what-exists (the fine precedent): moveCarried moves UP TO the
                    // authored coins — a short pocket pays what it has; a move, never a mint.
                    ctx.items().moveCarried(effect.fromPartyId, owner, ItemKinds.COIN,
                            effect.coins);
            case STANDING ->
                    // The Persona rule: the ward rewards (or blames) the FACE that did it.
                    ctx.factionStandings().adjust(presentedOf(owner, ctx), effect.faction,
                            effect.delta);
            case EDGE -> {
                // Edges land between the party's TRUE body and the face the owner wore at
                // the ending (the BarkSelector tie convention).
                int face = presentedOf(owner, ctx);
                if (face == effect.partyId) {
                    return; // an owner wearing the party's own face mints no self-edge
                }
                if (effect.mutual) {
                    ctx.relationships().addSymmetric(effect.partyId, face, effect.edgeKind);
                } else {
                    ctx.relationships().addDirected(effect.partyId, face, effect.edgeKind);
                }
            }
            case AWARD_XP ->
                    // XP lands on the TRUE doer (the body that did the deed), context-keyed
                    // on the declared cell (§3.3 satiation — farming one drawer decays).
                    ctx.skillTracks().award(owner, effect.skillRaw, effect.cp,
                            effect.contextCell, ctx.tick());
        }
    }

    private static int presentedOf(int actorId, ActorContext ctx) {
        return ctx.registry().get(actorId).identity().presentedId();
    }
}
