package com.trojia.sim.actor.quest;

import com.trojia.sim.actor.ItemKinds;

import java.nio.charset.StandardCharsets;

/**
 * Shared synthetic-quest fixture for the quest unit tests: a four-stage quest exercising
 * every trigger and effect kind, plus a plain array-backed {@link QuestBindings}.
 */
final class QuestTestFixtures {

    static final String QUEST_JSON = """
            {
              "id": "quests",
              "quests": [
                {
                  "id": "test-quest",
                  "title": "A Test Quest",
                  "binding": "first_talker",
                  "parties": ["alice", "bob"],
                  "items": ["token", "prize"],
                  "zones": ["yard"],
                  "cells": ["chest"],
                  "stages": [
                    { "key": "start", "objective": "o0", "log": "l0",
                      "advance": [ {"kind": "talk", "party": "alice", "to": "middle"} ] },
                    { "key": "middle", "objective": "o1", "log": "l1",
                      "liftItems": [ {"item": "token", "fromParty": "bob"} ],
                      "advance": [
                        {"kind": "item", "item": "token", "to": "search"},
                        {"kind": "enter_zone", "zone": "yard", "to": "search"} ] },
                    { "key": "search", "objective": "o2", "log": "l2",
                      "advance": [ {"kind": "search", "cell": "chest", "item": "prize",
                                    "skill": "streetwise", "resist": 12, "keyItem": "token",
                                    "retryTicks": 25, "to": "end"} ] },
                    { "key": "end", "objective": "o3", "log": "l3", "terminal": true,
                      "effects": [
                        {"kind": "give_item", "item": "prize", "toParty": "alice"},
                        {"kind": "pay", "fromParty": "bob", "coins": 40},
                        {"kind": "standing", "faction": "watch", "delta": 25},
                        {"kind": "edge", "edge": "FRIEND", "party": "alice",
                         "direction": "mutual"},
                        {"kind": "edge", "edge": "GRUDGE", "party": "bob",
                         "direction": "from_party"},
                        {"kind": "award_xp", "skill": "streetwise", "cp": 150,
                         "contextCell": "chest"} ] }
                  ]
                }
              ]
            }
            """;

    /** Item-kind ids the fixture binds its two item symbols to. */
    static final short TOKEN_KIND = ItemKinds.VAULT_KEY;
    static final short PRIZE_KIND = ItemKinds.LEDGER_LEAF;

    private QuestTestFixtures() {
    }

    static QuestRaws parseFixture() {
        return QuestRawsLoader.parse(QUEST_JSON.getBytes(StandardCharsets.UTF_8));
    }

    /**
     * Plain bindings: alice/bob to the given actor ids, token/prize to the Sprint-3 item
     * kinds, yard to zone index {@code yardZone}, chest to packed cell {@code chestCell},
     * streetwise to {@code streetwiseRaw}, watch to {@code watchFaction}.
     */
    static QuestBindings bindings(int alice, int bob, int yardZone, int chestCell,
            int streetwiseRaw, int watchFaction) {
        return new QuestBindings() {
            @Override
            public int partyActorId(String questId, String partySymbol) {
                return switch (partySymbol) {
                    case "alice" -> alice;
                    case "bob" -> bob;
                    default -> -1;
                };
            }

            @Override
            public short itemKind(String questId, String itemSymbol) {
                return switch (itemSymbol) {
                    case "token" -> TOKEN_KIND;
                    case "prize" -> PRIZE_KIND;
                    default -> -1;
                };
            }

            @Override
            public int zoneId(String questId, String zoneSymbol) {
                return zoneSymbol.equals("yard") ? yardZone : -1;
            }

            @Override
            public int cell(String questId, String cellSymbol) {
                return cellSymbol.equals("chest") ? chestCell : -1;
            }

            @Override
            public int skillRaw(String skillKey) {
                return skillKey.equals("streetwise") ? streetwiseRaw : -1;
            }

            @Override
            public int factionId(String factionKey) {
                return factionKey.equals("watch") ? watchFaction : -1;
            }
        };
    }
}
