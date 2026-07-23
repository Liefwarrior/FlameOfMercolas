package com.trojia.sim.actor.quest;

import java.util.List;

/**
 * The parsed quest raws model (Sprint 3 "The Vanished Clerk"): the immutable, symbol-level
 * shape of {@code content/raws/quests/quests.json} exactly as authored — parties/items/
 * zones/cells are SYMBOLIC vocabularies here (strings the World team authors); the scenario
 * BAKE binds them to actor ids / item kinds / zone indices / packed cells when it builds the
 * {@link QuestRegistry}. Quest order in the file is dense ordinal order and is append-only
 * (the restricted-zone-table rule); stage order is dense stage-ordinal order with stage 0
 * the initial stage.
 *
 * <p>Plain records over {@code List}s (the {@link com.trojia.sim.actor.RestrictedZoneTable}
 * allowance: registry/raws construction may hold ordered collections — iteration order is
 * authored order, never hash order).
 */
public record QuestRaws(List<Quest> quests) {

    /** The degraded no-quests raws (missing {@code quests/quests.json} — pre-quest worlds). */
    public static final QuestRaws EMPTY = new QuestRaws(List.of());

    public QuestRaws {
        quests = List.copyOf(quests);
    }

    /** Owner binding modes ({@code binding} field vocabulary). */
    public enum Binding {
        /** The first actor whose TALK matches a stage-0 trigger becomes (and stays) the owner. */
        FIRST_TALKER,
        /** The owner is fixed at bake (NPC-scoped quests — shape only this sprint). */
        FIXED
    }

    /** One authored quest: symbolic vocabularies + the dense stage list. */
    public record Quest(String id, String title, Binding binding, List<String> parties,
            List<String> items, List<String> zones, List<String> cells, List<Stage> stages) {
        public Quest {
            parties = List.copyOf(parties);
            items = List.copyOf(items);
            zones = List.copyOf(zones);
            cells = List.copyOf(cells);
            stages = List.copyOf(stages);
        }
    }

    /**
     * One authored stage. {@code objective}/{@code log} are the client journal's text.
     * {@code liftItems} arms the key-lift watcher while this stage is current; {@code advance}
     * is evaluated in authored order (first match wins); {@code effects} apply once ON
     * ENTERING this stage, in authored order. A terminal stage has an empty {@code advance}.
     */
    public record Stage(String key, String objective, String log, List<Lift> liftItems,
            List<Trigger> advance, List<Effect> effects, boolean terminal) {
        public Stage {
            liftItems = List.copyOf(liftItems);
            advance = List.copyOf(advance);
            effects = List.copyOf(effects);
        }
    }

    /** A key-lift declaration: a successful owner-pickpocket of {@code fromParty} also yields {@code item}. */
    public record Lift(String item, String fromParty) {
    }

    /** Trigger kinds ({@code advance[].kind} vocabulary). */
    public enum TriggerKind {
        TALK, ENTER_ZONE, ITEM, SEARCH, STANDING_AT_LEAST, STANDING_AT_MOST, AFTER_TICKS
    }

    /**
     * One advance trigger. Only the fields its {@link TriggerKind} names are meaningful;
     * unused symbol fields are {@code null} and unused numeric fields are 0 ({@code resist}/
     * {@code retryTicks}/{@code value}/{@code ticks} — the loader validates presence per kind).
     */
    public record Trigger(TriggerKind kind, String party, String requireItem, String zone,
            String item, String cell, String skill, int resist, String keyItem, int retryTicks,
            String faction, int value, long ticks, String to) {
    }

    /** Effect kinds ({@code effects[].kind} vocabulary). */
    public enum EffectKind {
        GIVE_ITEM, PAY, STANDING, EDGE, AWARD_XP
    }

    /** Edge direction vocabulary for {@link EffectKind#EDGE}. */
    public enum EdgeDirection {
        MUTUAL, FROM_PARTY
    }

    /**
     * One stage-entry effect. Only the fields its {@link EffectKind} names are meaningful
     * (the {@link Trigger} convention).
     */
    public record Effect(EffectKind kind, String item, String toParty, String fromParty,
            int coins, String faction, int delta, String edge, String party,
            EdgeDirection direction, String skill, int cp, String contextCell) {
    }
}
