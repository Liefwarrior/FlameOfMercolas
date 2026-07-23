package com.trojia.sim.actor.quest;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.RelationshipKind;

/**
 * The immutable, bake-bound quest table (Sprint 3 "The Vanished Clerk"): the authored
 * {@link QuestRaws} compiled against the scenario bake's {@link QuestBindings} — every
 * symbolic reference resolved to primitives (TRUE actor ids, item-kind shorts, zone
 * indices, packed cells, skill raws, faction columns) so the {@link QuestEngine} evaluates
 * draw-free integer state only. Constructed once at bake; never mutated; rides no save
 * (the {@code RestrictedZoneTable} convention — the same raws must boot before a load, the
 * {@code FactionStandings} frame-guard contract).
 *
 * <p>{@link #EMPTY} is the degradation every pre-quest {@code ActorsSystem} constructor
 * chains with: zero quests, the engine no-ops, and the {@link QuestLog} triad writes an
 * empty frame.
 */
public final class QuestRegistry {

    /** The degraded zero-quest registry (world-less bootstrap, pre-quest constructors). */
    public static final QuestRegistry EMPTY = new QuestRegistry(new CompiledQuest[0]);

    // ---- compiled shapes (package-private: the engine reads them directly) ----

    static final class CompiledQuest {
        final String id;
        final String title;
        final QuestRaws.Binding binding;
        final String[] partySymbols;
        final int[] partyActorIds; // TRUE ids, party order
        final CompiledStage[] stages;

        CompiledQuest(String id, String title, QuestRaws.Binding binding,
                String[] partySymbols, int[] partyActorIds, CompiledStage[] stages) {
            this.id = id;
            this.title = title;
            this.binding = binding;
            this.partySymbols = partySymbols;
            this.partyActorIds = partyActorIds;
            this.stages = stages;
        }
    }

    static final class CompiledStage {
        final String key;
        final String objective;
        final String log;
        final boolean terminal;
        final short[] liftItemKinds;
        final int[] liftFromPartyIds; // TRUE ids, parallel to liftItemKinds
        final CompiledTrigger[] advance;
        final CompiledEffect[] effects;

        CompiledStage(String key, String objective, String log, boolean terminal,
                short[] liftItemKinds, int[] liftFromPartyIds, CompiledTrigger[] advance,
                CompiledEffect[] effects) {
            this.key = key;
            this.objective = objective;
            this.log = log;
            this.terminal = terminal;
            this.liftItemKinds = liftItemKinds;
            this.liftFromPartyIds = liftFromPartyIds;
            this.advance = advance;
            this.effects = effects;
        }
    }

    static final class CompiledTrigger {
        final QuestRaws.TriggerKind kind;
        final int partyActorId;      // TALK
        final short requireItemKind; // TALK (optional; -1 none)
        final int zoneId;            // ENTER_ZONE
        final short itemKind;        // ITEM / SEARCH (the yielded item)
        final int cell;              // SEARCH
        final int skillRaw;          // SEARCH
        final int resist;            // SEARCH
        final short keyItemKind;     // SEARCH (optional; -1 none)
        final int retryTicks;        // SEARCH
        final int faction;           // STANDING_*
        final int value;             // STANDING_*
        final long ticks;            // AFTER_TICKS
        final int toStage;

        CompiledTrigger(QuestRaws.TriggerKind kind, int partyActorId, short requireItemKind,
                int zoneId, short itemKind, int cell, int skillRaw, int resist,
                short keyItemKind, int retryTicks, int faction, int value, long ticks,
                int toStage) {
            this.kind = kind;
            this.partyActorId = partyActorId;
            this.requireItemKind = requireItemKind;
            this.zoneId = zoneId;
            this.itemKind = itemKind;
            this.cell = cell;
            this.skillRaw = skillRaw;
            this.resist = resist;
            this.keyItemKind = keyItemKind;
            this.retryTicks = retryTicks;
            this.faction = faction;
            this.value = value;
            this.ticks = ticks;
            this.toStage = toStage;
        }
    }

    static final class CompiledEffect {
        final QuestRaws.EffectKind kind;
        final short itemKind;         // GIVE_ITEM
        final int toPartyId;          // GIVE_ITEM
        final int fromPartyId;        // PAY
        final int coins;              // PAY
        final int faction;            // STANDING
        final int delta;              // STANDING
        final RelationshipKind edgeKind; // EDGE
        final int partyId;            // EDGE
        final boolean mutual;         // EDGE
        final int skillRaw;           // AWARD_XP
        final int cp;                 // AWARD_XP
        final int contextCell;        // AWARD_XP

        CompiledEffect(QuestRaws.EffectKind kind, short itemKind, int toPartyId,
                int fromPartyId, int coins, int faction, int delta,
                RelationshipKind edgeKind, int partyId, boolean mutual, int skillRaw, int cp,
                int contextCell) {
            this.kind = kind;
            this.itemKind = itemKind;
            this.toPartyId = toPartyId;
            this.fromPartyId = fromPartyId;
            this.coins = coins;
            this.faction = faction;
            this.delta = delta;
            this.edgeKind = edgeKind;
            this.partyId = partyId;
            this.mutual = mutual;
            this.skillRaw = skillRaw;
            this.cp = cp;
            this.contextCell = contextCell;
        }
    }

    private final CompiledQuest[] quests;

    private QuestRegistry(CompiledQuest[] quests) {
        this.quests = quests;
    }

    /**
     * Compiles {@code raws} against the bake's {@code bindings}. Every unresolved symbol
     * (a negative binding) fails loudly — a quest never boots half-bound.
     */
    public static QuestRegistry bind(QuestRaws raws, QuestBindings bindings) {
        CompiledQuest[] compiled = new CompiledQuest[raws.quests().size()];
        for (int q = 0; q < compiled.length; q++) {
            compiled[q] = compileQuest(raws.quests().get(q), bindings);
        }
        return new QuestRegistry(compiled);
    }

    private static CompiledQuest compileQuest(QuestRaws.Quest quest, QuestBindings bindings) {
        String[] partySymbols = quest.parties().toArray(new String[0]);
        int[] partyIds = new int[partySymbols.length];
        for (int i = 0; i < partySymbols.length; i++) {
            partyIds[i] = required(bindings.partyActorId(quest.id(), partySymbols[i]),
                    quest.id(), "party", partySymbols[i]);
        }
        CompiledStage[] stages = new CompiledStage[quest.stages().size()];
        for (int s = 0; s < stages.length; s++) {
            stages[s] = compileStage(quest, quest.stages().get(s), bindings);
        }
        return new CompiledQuest(quest.id(), quest.title(), quest.binding(), partySymbols,
                partyIds, stages);
    }

    private static CompiledStage compileStage(QuestRaws.Quest quest, QuestRaws.Stage stage,
            QuestBindings bindings) {
        String qid = quest.id();
        short[] liftKinds = new short[stage.liftItems().size()];
        int[] liftFrom = new int[liftKinds.length];
        for (int i = 0; i < liftKinds.length; i++) {
            QuestRaws.Lift lift = stage.liftItems().get(i);
            liftKinds[i] = requiredKind(bindings.itemKind(qid, lift.item()), qid, lift.item());
            liftFrom[i] = required(bindings.partyActorId(qid, lift.fromParty()), qid,
                    "party", lift.fromParty());
        }
        CompiledTrigger[] advance = new CompiledTrigger[stage.advance().size()];
        for (int i = 0; i < advance.length; i++) {
            advance[i] = compileTrigger(quest, stage.advance().get(i), bindings);
        }
        CompiledEffect[] effects = new CompiledEffect[stage.effects().size()];
        for (int i = 0; i < effects.length; i++) {
            effects[i] = compileEffect(quest, stage.effects().get(i), bindings);
        }
        return new CompiledStage(stage.key(), stage.objective(), stage.log(), stage.terminal(),
                liftKinds, liftFrom, advance, effects);
    }

    private static CompiledTrigger compileTrigger(QuestRaws.Quest quest,
            QuestRaws.Trigger t, QuestBindings bindings) {
        String qid = quest.id();
        int party = Actor.NONE;
        short requireItem = -1;
        int zone = Actor.NONE;
        short item = -1;
        int cell = Actor.NONE;
        int skillRaw = Actor.NONE;
        short keyItem = -1;
        int faction = Actor.NONE;
        switch (t.kind()) {
            case TALK -> {
                party = required(bindings.partyActorId(qid, t.party()), qid, "party", t.party());
                if (t.requireItem() != null) {
                    requireItem = requiredKind(bindings.itemKind(qid, t.requireItem()), qid,
                            t.requireItem());
                }
            }
            case ENTER_ZONE -> zone =
                    required(bindings.zoneId(qid, t.zone()), qid, "zone", t.zone());
            case ITEM -> item = requiredKind(bindings.itemKind(qid, t.item()), qid, t.item());
            case SEARCH -> {
                cell = required(bindings.cell(qid, t.cell()), qid, "cell", t.cell());
                item = requiredKind(bindings.itemKind(qid, t.item()), qid, t.item());
                skillRaw = required(bindings.skillRaw(t.skill()), qid, "skill", t.skill());
                if (t.keyItem() != null) {
                    keyItem = requiredKind(bindings.itemKind(qid, t.keyItem()), qid, t.keyItem());
                }
            }
            case STANDING_AT_LEAST, STANDING_AT_MOST -> faction =
                    required(bindings.factionId(t.faction()), qid, "faction", t.faction());
            case AFTER_TICKS -> {
                // no symbols to bind
            }
        }
        int toStage = stageOrdinal(quest, t.to());
        return new CompiledTrigger(t.kind(), party, requireItem, zone, item, cell, skillRaw,
                t.resist(), keyItem, t.retryTicks(), faction, t.value(), t.ticks(), toStage);
    }

    private static CompiledEffect compileEffect(QuestRaws.Quest quest, QuestRaws.Effect e,
            QuestBindings bindings) {
        String qid = quest.id();
        short item = -1;
        int toParty = Actor.NONE;
        int fromParty = Actor.NONE;
        int faction = Actor.NONE;
        RelationshipKind edgeKind = null;
        int party = Actor.NONE;
        boolean mutual = false;
        int skillRaw = Actor.NONE;
        int contextCell = Actor.NONE;
        switch (e.kind()) {
            case GIVE_ITEM -> {
                item = requiredKind(bindings.itemKind(qid, e.item()), qid, e.item());
                toParty = required(bindings.partyActorId(qid, e.toParty()), qid, "party",
                        e.toParty());
            }
            case PAY -> fromParty = required(bindings.partyActorId(qid, e.fromParty()), qid,
                    "party", e.fromParty());
            case STANDING -> faction =
                    required(bindings.factionId(e.faction()), qid, "faction", e.faction());
            case EDGE -> {
                edgeKind = RelationshipKind.valueOf(e.edge());
                party = required(bindings.partyActorId(qid, e.party()), qid, "party", e.party());
                mutual = e.direction() == QuestRaws.EdgeDirection.MUTUAL;
            }
            case AWARD_XP -> {
                skillRaw = required(bindings.skillRaw(e.skill()), qid, "skill", e.skill());
                contextCell = required(bindings.cell(qid, e.contextCell()), qid, "cell",
                        e.contextCell());
            }
        }
        return new CompiledEffect(e.kind(), item, toParty, fromParty, e.coins(), faction,
                e.delta(), edgeKind, party, mutual, skillRaw, e.cp(), contextCell);
    }

    private static int stageOrdinal(QuestRaws.Quest quest, String key) {
        for (int s = 0; s < quest.stages().size(); s++) {
            if (quest.stages().get(s).key().equals(key)) {
                return s;
            }
        }
        throw new IllegalStateException("quest '" + quest.id() + "': unknown stage key '"
                + key + "' (the loader should have rejected this)");
    }

    private static int required(int bound, String questId, String what, String symbol) {
        if (bound < 0) {
            throw new IllegalStateException("quest '" + questId + "': " + what + " symbol '"
                    + symbol + "' did not bind (bake returned " + bound + ")");
        }
        return bound;
    }

    private static short requiredKind(short bound, String questId, String symbol) {
        if (bound < 0) {
            throw new IllegalStateException("quest '" + questId + "': item symbol '" + symbol
                    + "' did not bind (bake returned " + bound + ")");
        }
        return bound;
    }

    // ---------------------------------------------------------------- package engine access

    CompiledQuest quest(int q) {
        return quests[q];
    }

    // ---------------------------------------------------------------- public reads (client)

    public int questCount() {
        return quests.length;
    }

    public String questId(int q) {
        return quests[q].id;
    }

    public String questTitle(int q) {
        return quests[q].title;
    }

    public QuestRaws.Binding binding(int q) {
        return quests[q].binding;
    }

    public int stageCount(int q) {
        return quests[q].stages.length;
    }

    public String stageKey(int q, int stage) {
        return quests[q].stages[stage].key;
    }

    public String objective(int q, int stage) {
        return quests[q].stages[stage].objective;
    }

    /** The journal's "story so far" line for a completed stage. */
    public String logLine(int q, int stage) {
        return quests[q].stages[stage].log;
    }

    public boolean terminal(int q, int stage) {
        return quests[q].stages[stage].terminal;
    }

    /**
     * The party symbol quest {@code q} binds to TRUE actor id {@code actorId}, or
     * {@code null} — the client's talk-surface key builder
     * ({@code quest.<questId>.<stageKey>.<partySymbol>}).
     */
    public String partySymbol(int q, int actorId) {
        CompiledQuest quest = quests[q];
        for (int i = 0; i < quest.partyActorIds.length; i++) {
            if (quest.partyActorIds[i] == actorId) {
                return quest.partySymbols[i];
            }
        }
        return null;
    }

    /**
     * Whether TRUE actor id {@code actorId} is the party of a TALK trigger on stage
     * {@code stage} of quest {@code q} — the client's ◆ quest-marker read.
     */
    public boolean isTalkPartyOnStage(int q, int stage, int actorId) {
        CompiledStage s = quests[q].stages[stage];
        for (CompiledTrigger t : s.advance) {
            if (t.kind == QuestRaws.TriggerKind.TALK && t.partyActorId == actorId) {
                return true;
            }
        }
        return false;
    }
}
