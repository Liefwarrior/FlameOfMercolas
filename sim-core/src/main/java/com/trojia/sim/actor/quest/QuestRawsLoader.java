package com.trojia.sim.actor.quest;

import com.trojia.sim.actor.ActorRawsValidationException;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.json.JsonArray;
import com.trojia.sim.json.JsonBool;
import com.trojia.sim.json.JsonNumber;
import com.trojia.sim.json.JsonNumberMode;
import com.trojia.sim.json.JsonObject;
import com.trojia.sim.json.JsonParseException;
import com.trojia.sim.json.JsonString;
import com.trojia.sim.json.JsonValue;
import com.trojia.sim.json.MiniJson;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Loader for {@code content/raws/quests/quests.json} (Sprint 3 "The Vanished Clerk") — THE
 * SCHEMA CONTRACT between the sim's quest engine and the World team's authored quests:
 *
 * <pre>{@code
 * {
 *   "id": "quests",
 *   "quests": [
 *     { "id": "vanished-clerk", "title": "The Vanished Clerk",
 *       "binding": "first_talker",
 *       "parties": ["crell", "gilt"], "items": ["vault_key"],
 *       "zones": ["bank_hall"], "cells": ["clerks_desk"],
 *       "stages": [
 *         { "key": "rumor", "objective": "...", "log": "...",
 *           "liftItems": [ {"item": "vault_key", "fromParty": "gilt"} ],
 *           "advance": [ {"kind": "talk", "party": "crell", "to": "next"} ],
 *           "effects": [ {"kind": "standing", "faction": "watch", "delta": 25} ],
 *           "terminal": false } ] } ]
 * }
 * }</pre>
 *
 * <p><b>Vocabularies.</b> {@code binding} is {@code first_talker | fixed}. Trigger kinds:
 * {@code talk} (party, optional requireItem), {@code enter_zone} (zone), {@code item}
 * (item), {@code search} (cell, item, skill, resist, optional keyItem, retryTicks),
 * {@code standing_at_least}/{@code standing_at_most} (faction, value), {@code after_ticks}
 * (ticks) — every trigger names {@code to}. Effect kinds: {@code give_item} (item,
 * toParty), {@code pay} (fromParty, coins), {@code standing} (faction, delta), {@code edge}
 * (edge, party, direction {@code mutual|from_party}), {@code award_xp} (skill, cp,
 * contextCell). Every party/item/zone/cell symbol referenced anywhere must be declared in
 * the quest's own vocabulary lists; skills/factions resolve against their raws registries
 * at BAKE (the {@link QuestRegistry#bind} step), not here.
 *
 * <p><b>Degraded mode:</b> a missing {@code quests/} dir or {@code quests.json} loads
 * {@link QuestRaws#EMPTY} — but a PRESENT file validates strictly (the fail-fast raws
 * contract, {@code BarkRawsLoader} precedent). MiniJson INTEGER_ONLY.
 */
public final class QuestRawsLoader {

    private static final String QUESTS_FILE = "quests/quests.json";

    private static final List<String> TOP_FIELDS = List.of("id", "quests", "notes", "provenance");
    private static final List<String> QUEST_FIELDS = List.of(
            "id", "title", "binding", "parties", "items", "zones", "cells", "stages", "notes");
    private static final List<String> STAGE_FIELDS = List.of(
            "key", "objective", "log", "liftItems", "advance", "effects", "terminal", "notes");
    private static final List<String> LIFT_FIELDS = List.of("item", "fromParty");
    private static final List<String> TRIGGER_FIELDS = List.of("kind", "party", "requireItem",
            "zone", "item", "cell", "skill", "resist", "keyItem", "retryTicks", "faction",
            "value", "ticks", "to");
    private static final List<String> EFFECT_FIELDS = List.of("kind", "item", "toParty",
            "fromParty", "coins", "faction", "delta", "edge", "party", "direction", "skill",
            "cp", "contextCell");

    private QuestRawsLoader() {
    }

    /**
     * Loads {@code quests/quests.json} under a raws root, or {@link QuestRaws#EMPTY} when
     * the file does not exist yet (the sparse-authoring contract).
     *
     * @throws ActorRawsValidationException on the first validation failure of a PRESENT file
     * @throws UncheckedIOException         if a present file cannot be read
     */
    public static QuestRaws load(Path rawsRoot) {
        java.util.Objects.requireNonNull(rawsRoot, "rawsRoot");
        Path file = rawsRoot.resolve("quests").resolve("quests.json");
        if (!Files.isRegularFile(file)) {
            return QuestRaws.EMPTY; // unauthored yet: the wired-degraded mode
        }
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(file);
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading raws file " + file, e);
        }
        return parse(bytes);
    }

    /** Parses raws bytes directly (the loader's pure core; the path overload reads then calls this). */
    public static QuestRaws parse(byte[] bytes) {
        JsonValue tree;
        try {
            tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);
        } catch (JsonParseException e) {
            throw new ActorRawsValidationException(QUESTS_FILE,
                    ActorRawsValidationException.NO_FIELD, "malformed JSON: " + e.getMessage());
        }
        if (!(tree instanceof JsonObject root)) {
            throw new ActorRawsValidationException(QUESTS_FILE,
                    ActorRawsValidationException.NO_FIELD, "top-level value must be an object");
        }
        for (JsonObject.Member member : root.members()) {
            if (!TOP_FIELDS.contains(member.name())) {
                throw new ActorRawsValidationException(QUESTS_FILE, member.name(), "unknown field");
            }
        }
        if (!(root.get("id") instanceof JsonString)) {
            throw new ActorRawsValidationException(QUESTS_FILE, "id",
                    "required string field is missing");
        }
        if (!(root.get("quests") instanceof JsonArray quests)) {
            throw new ActorRawsValidationException(QUESTS_FILE, "quests",
                    "required array is missing");
        }
        List<QuestRaws.Quest> parsed = new ArrayList<>(quests.size());
        List<String> questIds = new ArrayList<>();
        for (int i = 0; i < quests.size(); i++) {
            QuestRaws.Quest quest = parseQuest(i, quests.get(i));
            if (questIds.contains(quest.id())) {
                throw new ActorRawsValidationException(QUESTS_FILE,
                        "quests[" + i + "].id", "duplicate quest id '" + quest.id() + "'");
            }
            questIds.add(quest.id());
            parsed.add(quest);
        }
        return new QuestRaws(parsed);
    }

    // ---------------------------------------------------------------- quest

    private static QuestRaws.Quest parseQuest(int index, JsonValue value) {
        String where = "quests[" + index + "]";
        JsonObject quest = requireObject(value, where);
        rejectUnknown(quest, QUEST_FIELDS, where);
        String id = requireString(quest, "id", where);
        String title = requireString(quest, "title", where);
        String bindingText = requireString(quest, "binding", where);
        QuestRaws.Binding binding = switch (bindingText) {
            case "first_talker" -> QuestRaws.Binding.FIRST_TALKER;
            case "fixed" -> QuestRaws.Binding.FIXED;
            default -> throw new ActorRawsValidationException(QUESTS_FILE, where + ".binding",
                    "must be 'first_talker' or 'fixed', not '" + bindingText + "'");
        };
        List<String> parties = stringList(quest, "parties", where);
        List<String> items = stringList(quest, "items", where);
        List<String> zones = stringList(quest, "zones", where);
        List<String> cells = stringList(quest, "cells", where);

        if (!(quest.get("stages") instanceof JsonArray stagesJson) || stagesJson.size() == 0) {
            throw new ActorRawsValidationException(QUESTS_FILE, where + ".stages",
                    "required non-empty array is missing");
        }
        // Pass 1: stage keys (unique) — so `to` references validate against the full set.
        List<String> stageKeys = new ArrayList<>(stagesJson.size());
        for (int s = 0; s < stagesJson.size(); s++) {
            JsonObject stage = requireObject(stagesJson.get(s), where + ".stages[" + s + "]");
            String key = requireString(stage, "key", where + ".stages[" + s + "]");
            if (stageKeys.contains(key)) {
                throw new ActorRawsValidationException(QUESTS_FILE,
                        where + ".stages[" + s + "].key", "duplicate stage key '" + key + "'");
            }
            stageKeys.add(key);
        }
        // Pass 2: full stage parse + symbol/reference validation.
        List<QuestRaws.Stage> stages = new ArrayList<>(stagesJson.size());
        for (int s = 0; s < stagesJson.size(); s++) {
            stages.add(parseStage(where + ".stages[" + s + "]", (JsonObject) stagesJson.get(s),
                    stageKeys, parties, items, zones, cells));
        }
        return new QuestRaws.Quest(id, title, binding, parties, items, zones, cells, stages);
    }

    // ---------------------------------------------------------------- stage

    private static QuestRaws.Stage parseStage(String where, JsonObject stage,
            List<String> stageKeys, List<String> parties, List<String> items,
            List<String> zones, List<String> cells) {
        rejectUnknown(stage, STAGE_FIELDS, where);
        String key = requireString(stage, "key", where);
        String objective = requireString(stage, "objective", where);
        String log = requireString(stage, "log", where);
        boolean terminal = stage.get("terminal") instanceof JsonBool b && b.value();

        List<QuestRaws.Lift> lifts = new ArrayList<>();
        if (stage.has("liftItems")) {
            JsonArray liftsJson = requireArray(stage.get("liftItems"), where + ".liftItems");
            for (int i = 0; i < liftsJson.size(); i++) {
                String liftWhere = where + ".liftItems[" + i + "]";
                JsonObject lift = requireObject(liftsJson.get(i), liftWhere);
                rejectUnknown(lift, LIFT_FIELDS, liftWhere);
                String item = requireSymbol(lift, "item", liftWhere, items, "items");
                String fromParty = requireSymbol(lift, "fromParty", liftWhere, parties, "parties");
                lifts.add(new QuestRaws.Lift(item, fromParty));
            }
        }

        List<QuestRaws.Trigger> advance = new ArrayList<>();
        if (stage.has("advance")) {
            JsonArray advanceJson = requireArray(stage.get("advance"), where + ".advance");
            for (int i = 0; i < advanceJson.size(); i++) {
                advance.add(parseTrigger(where + ".advance[" + i + "]",
                        requireObject(advanceJson.get(i), where + ".advance[" + i + "]"),
                        stageKeys, parties, items, zones, cells));
            }
        }
        if (terminal && !advance.isEmpty()) {
            throw new ActorRawsValidationException(QUESTS_FILE, where + ".advance",
                    "a terminal stage must not declare advance triggers");
        }
        if (!terminal && advance.isEmpty()) {
            throw new ActorRawsValidationException(QUESTS_FILE, where + ".advance",
                    "a non-terminal stage must declare at least one advance trigger "
                            + "(it would be a dead end)");
        }

        List<QuestRaws.Effect> effects = new ArrayList<>();
        if (stage.has("effects")) {
            JsonArray effectsJson = requireArray(stage.get("effects"), where + ".effects");
            for (int i = 0; i < effectsJson.size(); i++) {
                effects.add(parseEffect(where + ".effects[" + i + "]",
                        requireObject(effectsJson.get(i), where + ".effects[" + i + "]"),
                        parties, items, cells));
            }
        }
        return new QuestRaws.Stage(key, objective, log, lifts, advance, effects, terminal);
    }

    // ---------------------------------------------------------------- trigger

    private static QuestRaws.Trigger parseTrigger(String where, JsonObject trigger,
            List<String> stageKeys, List<String> parties, List<String> items,
            List<String> zones, List<String> cells) {
        rejectUnknown(trigger, TRIGGER_FIELDS, where);
        String kindText = requireString(trigger, "kind", where);
        String to = requireString(trigger, "to", where);
        if (!stageKeys.contains(to)) {
            throw new ActorRawsValidationException(QUESTS_FILE, where + ".to",
                    "'" + to + "' names no stage in this quest");
        }
        String party = null;
        String requireItem = null;
        String zone = null;
        String item = null;
        String cell = null;
        String skill = null;
        int resist = 0;
        String keyItem = null;
        int retryTicks = 0;
        String faction = null;
        int value = 0;
        long ticks = 0;
        QuestRaws.TriggerKind kind;
        switch (kindText) {
            case "talk" -> {
                kind = QuestRaws.TriggerKind.TALK;
                party = requireSymbol(trigger, "party", where, parties, "parties");
                if (trigger.has("requireItem")) {
                    requireItem = requireSymbol(trigger, "requireItem", where, items, "items");
                }
            }
            case "enter_zone" -> {
                kind = QuestRaws.TriggerKind.ENTER_ZONE;
                zone = requireSymbol(trigger, "zone", where, zones, "zones");
            }
            case "item" -> {
                kind = QuestRaws.TriggerKind.ITEM;
                item = requireSymbol(trigger, "item", where, items, "items");
            }
            case "search" -> {
                kind = QuestRaws.TriggerKind.SEARCH;
                cell = requireSymbol(trigger, "cell", where, cells, "cells");
                item = requireSymbol(trigger, "item", where, items, "items");
                skill = requireString(trigger, "skill", where);
                resist = requireInt(trigger, "resist", where, 0, Integer.MAX_VALUE);
                retryTicks = requireInt(trigger, "retryTicks", where, 1, Integer.MAX_VALUE);
                if (trigger.has("keyItem")) {
                    keyItem = requireSymbol(trigger, "keyItem", where, items, "items");
                }
            }
            case "standing_at_least" -> {
                kind = QuestRaws.TriggerKind.STANDING_AT_LEAST;
                faction = requireString(trigger, "faction", where);
                value = requireInt(trigger, "value", where, Integer.MIN_VALUE, Integer.MAX_VALUE);
            }
            case "standing_at_most" -> {
                kind = QuestRaws.TriggerKind.STANDING_AT_MOST;
                faction = requireString(trigger, "faction", where);
                value = requireInt(trigger, "value", where, Integer.MIN_VALUE, Integer.MAX_VALUE);
            }
            case "after_ticks" -> {
                kind = QuestRaws.TriggerKind.AFTER_TICKS;
                ticks = requireInt(trigger, "ticks", where, 1, Integer.MAX_VALUE);
            }
            default -> throw new ActorRawsValidationException(QUESTS_FILE, where + ".kind",
                    "unknown trigger kind '" + kindText + "'");
        }
        return new QuestRaws.Trigger(kind, party, requireItem, zone, item, cell, skill, resist,
                keyItem, retryTicks, faction, value, ticks, to);
    }

    // ---------------------------------------------------------------- effect

    private static QuestRaws.Effect parseEffect(String where, JsonObject effect,
            List<String> parties, List<String> items, List<String> cells) {
        rejectUnknown(effect, EFFECT_FIELDS, where);
        String kindText = requireString(effect, "kind", where);
        String item = null;
        String toParty = null;
        String fromParty = null;
        int coins = 0;
        String faction = null;
        int delta = 0;
        String edge = null;
        String party = null;
        QuestRaws.EdgeDirection direction = null;
        String skill = null;
        int cp = 0;
        String contextCell = null;
        QuestRaws.EffectKind kind;
        switch (kindText) {
            case "give_item" -> {
                kind = QuestRaws.EffectKind.GIVE_ITEM;
                item = requireSymbol(effect, "item", where, items, "items");
                toParty = requireSymbol(effect, "toParty", where, parties, "parties");
            }
            case "pay" -> {
                kind = QuestRaws.EffectKind.PAY;
                fromParty = requireSymbol(effect, "fromParty", where, parties, "parties");
                coins = requireInt(effect, "coins", where, 1, Integer.MAX_VALUE);
            }
            case "standing" -> {
                kind = QuestRaws.EffectKind.STANDING;
                faction = requireString(effect, "faction", where);
                delta = requireInt(effect, "delta", where, Integer.MIN_VALUE, Integer.MAX_VALUE);
            }
            case "edge" -> {
                kind = QuestRaws.EffectKind.EDGE;
                String edgeText = requireString(effect, "edge", where);
                try {
                    RelationshipKind.valueOf(edgeText);
                } catch (IllegalArgumentException e) {
                    throw new ActorRawsValidationException(QUESTS_FILE, where + ".edge",
                            "'" + edgeText + "' is not a RelationshipKind");
                }
                edge = edgeText;
                party = requireSymbol(effect, "party", where, parties, "parties");
                String directionText = requireString(effect, "direction", where);
                direction = switch (directionText) {
                    case "mutual" -> QuestRaws.EdgeDirection.MUTUAL;
                    case "from_party" -> QuestRaws.EdgeDirection.FROM_PARTY;
                    default -> throw new ActorRawsValidationException(QUESTS_FILE,
                            where + ".direction",
                            "must be 'mutual' or 'from_party', not '" + directionText + "'");
                };
            }
            case "award_xp" -> {
                kind = QuestRaws.EffectKind.AWARD_XP;
                skill = requireString(effect, "skill", where);
                cp = requireInt(effect, "cp", where, 1, Integer.MAX_VALUE);
                contextCell = requireSymbol(effect, "contextCell", where, cells, "cells");
            }
            default -> throw new ActorRawsValidationException(QUESTS_FILE, where + ".kind",
                    "unknown effect kind '" + kindText + "'");
        }
        return new QuestRaws.Effect(kind, item, toParty, fromParty, coins, faction, delta,
                edge, party, direction, skill, cp, contextCell);
    }

    // ---------------------------------------------------------------- shared field helpers

    private static JsonObject requireObject(JsonValue value, String where) {
        if (!(value instanceof JsonObject obj)) {
            throw new ActorRawsValidationException(QUESTS_FILE, where, "must be an object");
        }
        return obj;
    }

    private static JsonArray requireArray(JsonValue value, String where) {
        if (!(value instanceof JsonArray arr)) {
            throw new ActorRawsValidationException(QUESTS_FILE, where, "must be an array");
        }
        return arr;
    }

    private static void rejectUnknown(JsonObject obj, List<String> allowed, String where) {
        for (JsonObject.Member member : obj.members()) {
            if (!allowed.contains(member.name())) {
                throw new ActorRawsValidationException(QUESTS_FILE,
                        where + "." + member.name(), "unknown field");
            }
        }
    }

    private static String requireString(JsonObject obj, String field, String where) {
        if (!(obj.get(field) instanceof JsonString s) || s.value().isEmpty()) {
            throw new ActorRawsValidationException(QUESTS_FILE, where + "." + field,
                    "required non-empty string is missing");
        }
        return s.value();
    }

    private static int requireInt(JsonObject obj, String field, String where, int min, int max) {
        if (!(obj.get(field) instanceof JsonNumber n)) {
            throw new ActorRawsValidationException(QUESTS_FILE, where + "." + field,
                    "required integer is missing");
        }
        int value;
        try {
            value = n.asInt();
        } catch (RuntimeException e) {
            throw new ActorRawsValidationException(QUESTS_FILE, where + "." + field,
                    "not a valid int: " + n.literal());
        }
        if (value < min || value > max) {
            throw new ActorRawsValidationException(QUESTS_FILE, where + "." + field,
                    "out of range [" + min + ", " + max + "]: " + value);
        }
        return value;
    }

    /** A required string that must additionally be DECLARED in the named vocabulary list. */
    private static String requireSymbol(JsonObject obj, String field, String where,
            List<String> vocabulary, String vocabularyName) {
        String symbol = requireString(obj, field, where);
        if (!vocabulary.contains(symbol)) {
            throw new ActorRawsValidationException(QUESTS_FILE, where + "." + field,
                    "'" + symbol + "' is not declared in this quest's " + vocabularyName);
        }
        return symbol;
    }

    private static List<String> stringList(JsonObject quest, String field, String where) {
        if (!quest.has(field)) {
            return List.of();
        }
        JsonArray arr = requireArray(quest.get(field), where + "." + field);
        List<String> out = new ArrayList<>(arr.size());
        for (int i = 0; i < arr.size(); i++) {
            if (!(arr.get(i) instanceof JsonString s) || s.value().isEmpty()) {
                throw new ActorRawsValidationException(QUESTS_FILE,
                        where + "." + field + "[" + i + "]", "must be a non-empty string");
            }
            if (out.contains(s.value())) {
                throw new ActorRawsValidationException(QUESTS_FILE,
                        where + "." + field + "[" + i + "]",
                        "duplicate symbol '" + s.value() + "'");
            }
            out.add(s.value());
        }
        return out;
    }
}
