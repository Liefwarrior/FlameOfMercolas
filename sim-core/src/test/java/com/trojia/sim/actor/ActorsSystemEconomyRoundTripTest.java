package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobBinder;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.io.WorldHasher;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * ACTR round-trip over the Phase-0 persisted state added this milestone: item {@code accountId}
 * stamps, the recycling free-slot stack, and the Royals ledger all serialize/load byte-identically
 * and hash identically (ledger balances are now in {@code hashInto}, closing the money-state hash
 * gap — landmine F).
 */
final class ActorsSystemEconomyRoundTripTest {

    private static Path committedJobsJson() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws").resolve("jobs").resolve("jobs.json");
            if (Files.isRegularFile(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws/jobs/jobs.json not found");
    }

    private final ActorTypeStatsTable typeStats =
            ActorTypeStatsTable.of(List.of(ActorTestFixtures.stats(Serf.TYPE)));
    private final JobRegistry jobs = JobBinder.bind(committedJobsJson(), ActorTypes.allTypeIds());

    private ActorsSystem buildSource() {
        ActorRegistry registry = new ActorRegistry();
        HomeRegistry homes = new HomeRegistry();
        RelationshipRegistry relationships = new RelationshipRegistry();
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        BankLedger bank = new BankLedger();

        Actor a0 = registry.spawn(Serf.TYPE, typeStats.get(Serf.TYPE), PackedPos.pack(5, 5, 1));
        Actor a1 = registry.spawn(Serf.TYPE, typeStats.get(Serf.TYPE), PackedPos.pack(6, 5, 1));
        int home = homes.addHome(PackedPos.pack(5, 5, 1));
        a0.setHomeId(home);
        a1.setHomeId(home);
        a0.setJobOrdinal((short) jobs.ordinalOf(Job.Serf.Farmer.ID));
        a1.setJobOrdinal((short) jobs.ordinalOf(Job.Serf.Laborer.ID));
        a0.applyNeedDelta(Need.HUNGER, -1234);
        a0.setHeldUntilTick(42_000L);
        // Phase-2 STEP C: a held prisoner with an assigned cell — the new serialized scalar.
        a1.setStatus(StatusBit.HELD, true);
        a1.setHeldUntilTick(60_000L);
        a1.setAssignedHoldCell(PackedPos.pack(105, 90, 11));
        relationships.addSymmetric(a0.id(), a1.id(), RelationshipKind.HOUSEHOLD);

        // Items: a purse, a larder, a stamped ID_CARD, a vault stack, then a sunk stack (last, so
        // its vacated free-slot survives to round-trip rather than being recycled by a later mint).
        items.addCarried(a0.id(), ItemKinds.COIN, 5);       // itemId 0
        items.addCarried(a1.id(), ItemKinds.FOOD, 3);       // itemId 1
        items.mintIdCard(a0.id(), a0.id());                 // itemId 2
        // A vault-sized COIN stack past a short's ceiling — STEP A's int quantity must round-trip.
        int vaultCell = PackedPos.pack(152, 57, 11);
        items.addOnCell(vaultCell, ItemKinds.COIN, 2_000_000); // itemId 3
        int scrap = items.mintCarried(ItemKinds.FOOD, a1.id()); // itemId 4
        items.sink(scrap); // leaves free-slot 4 to round-trip

        // Ledger: two accounts with non-trivial balances.
        bank.openAccount();
        bank.openAccount();
        bank.credit(0, 500);
        bank.credit(1, 250);

        // Density revisit: the three new persisted per-actor scalars + a populated shove ring
        // buffer (behavior-carrying — riot detection reads it — so it must round-trip).
        a0.setLastPushTick(4_990L);
        a0.setHuntBackoffUntilTick(5_400L); // the hop-blocked-chase backoff deadline
        a1.setStatus(StatusBit.HOUSE_ARREST, true);
        a1.setHouseArrestUntilTick(70_000L);

        ActorsSystem system = new ActorsSystem(1L, typeStats, jobs, registry, homes,
                relationships, items, bank, null, Actor.NONE, RestrictedZoneTable.EMPTY);
        system.shoveLog().record(4_990L, PackedPos.pack(6, 5, 1), a0.id());
        system.shoveLog().record(4_995L, PackedPos.pack(7, 5, 1), a1.id());
        return system;
    }

    private static ActorsSystem freshLoadTarget(ActorTypeStatsTable typeStats, JobRegistry jobs) {
        return new ActorsSystem(1L, typeStats, jobs, new ActorRegistry(), new HomeRegistry(),
                new RelationshipRegistry(), new ItemsLiteRegistry(), new BankLedger(), null,
                Actor.NONE, RestrictedZoneTable.EMPTY);
    }

    private static byte[] serialize(ActorsSystem system) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        system.serialize(new DataOutputStream(bytes));
        return bytes.toByteArray();
    }

    private static long hash(ActorsSystem system) {
        WorldHasher hasher = new WorldHasher();
        system.hashInto(hasher.sectionSink(system.id()));
        return hasher.combinedHash();
    }

    @Test
    void serializeLoadSerializeIsByteIdenticalAndHashStable() throws IOException {
        ActorsSystem source = buildSource();
        byte[] first = serialize(source);

        ActorsSystem loaded = freshLoadTarget(typeStats, jobs);
        loaded.load(new DataInputStream(new ByteArrayInputStream(first)));
        byte[] second = serialize(loaded);

        assertArrayEquals(first, second, "ACTR round-trip must be byte-identical over the new state");
        assertEquals(hash(source), hash(loaded), "hashInto (now incl. the ledger) must match");
    }

    @Test
    void loadRestoresLedgerBalancesItemStampsAndFreeStack() throws IOException {
        ActorsSystem source = buildSource();
        byte[] bytes = serialize(source);

        ActorsSystem loaded = freshLoadTarget(typeStats, jobs);
        loaded.load(new DataInputStream(new ByteArrayInputStream(bytes)));

        assertEquals(2, loaded.registry().size(), "both actors round-trip");
        assertEquals(500, loaded.bankAccounts().balanceOf(0));
        assertEquals(250, loaded.bankAccounts().balanceOf(1));
        assertEquals(750, loaded.bankAccounts().totalRoyals());

        // Phase-2 STEP C: the assigned prison cell survived the round-trip (the new serialized scalar).
        assertEquals(PackedPos.pack(105, 90, 11), loaded.registry().get(1).assignedHoldCell());
        assertEquals(Actor.NONE, loaded.registry().get(0).assignedHoldCell(), "unheld actor: no cell");
        // STEP A: the vault-sized COIN stack (> Short.MAX_VALUE) round-tripped intact (int quantity).
        assertEquals(2_000_000,
                loaded.items().countOnCellOfKind(PackedPos.pack(152, 57, 11), ItemKinds.COIN));

        // The ID_CARD's stamped accountId survived the round-trip.
        int card = loaded.items().firstCarriedOfKind(0, ItemKinds.ID_CARD);
        assertEquals(0, loaded.items().get(card).accountId());

        // The sunk stack and its recycled free slot round-tripped: the next mint reuses that slot.
        assertEquals(1, loaded.items().freeSlotCount());
        int reused = loaded.items().mintCarried(ItemKinds.COIN, 1);
        assertEquals(4, reused, "the recycled slot (itemId 4, the sunk scrap) is reused before appending");
    }

    // ================================================================== Sprint 3: quests

    private static com.trojia.sim.actor.quest.QuestRegistry questRegistry() {
        com.trojia.sim.actor.quest.QuestRaws raws =
                com.trojia.sim.actor.quest.QuestRawsLoader.parse("""
                {
                  "id": "quests",
                  "quests": [
                    { "id": "roundtrip-quest", "title": "Round Trip",
                      "binding": "first_talker",
                      "parties": ["alice"], "items": [], "zones": [], "cells": [],
                      "stages": [
                        { "key": "start", "objective": "o0", "log": "l0",
                          "advance": [ {"kind": "talk", "party": "alice", "to": "end"} ] },
                        { "key": "end", "objective": "o1", "log": "l1", "terminal": true }
                      ] }
                  ]
                }
                """.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        return com.trojia.sim.actor.quest.QuestRegistry.bind(raws,
                new com.trojia.sim.actor.quest.QuestBindings() {
                    @Override
                    public int partyActorId(String questId, String partySymbol) {
                        return 1; // alice = actor 1
                    }

                    @Override
                    public short itemKind(String questId, String itemSymbol) {
                        return -1;
                    }

                    @Override
                    public int zoneId(String questId, String zoneSymbol) {
                        return -1;
                    }

                    @Override
                    public int cell(String questId, String cellSymbol) {
                        return -1;
                    }

                    @Override
                    public int skillRaw(String skillKey) {
                        return -1;
                    }

                    @Override
                    public int factionId(String factionKey) {
                        return -1;
                    }
                });
    }

    /** The Sprint-3 QuestLog frame rides the chunk: round-trips byte-identically + hashes. */
    @Test
    void questWiredChunkRoundTripsTheQuestLogFrame() throws IOException {
        ActorsSystem source = new ActorsSystem(1L, typeStats, jobs, new ActorRegistry(),
                new HomeRegistry(), new RelationshipRegistry(), new ItemsLiteRegistry(),
                new BankLedger(), null, CivicFixtures.NONE, SkillTrackRegistry.UNWIRED,
                FactionStandings.UNWIRED, questRegistry());
        source.registry().spawn(Serf.TYPE, typeStats.get(Serf.TYPE), PackedPos.pack(5, 5, 1));
        source.registry().spawn(Serf.TYPE, typeStats.get(Serf.TYPE), PackedPos.pack(6, 5, 1));
        source.questLog().bindOwnerAtBake(0, 0);
        source.questLog().noteTalk(0, 1, 7L); // an occupied latch must round-trip
        byte[] first = serialize(source);

        ActorsSystem loaded = new ActorsSystem(1L, typeStats, jobs, new ActorRegistry(),
                new HomeRegistry(), new RelationshipRegistry(), new ItemsLiteRegistry(),
                new BankLedger(), null, CivicFixtures.NONE, SkillTrackRegistry.UNWIRED,
                FactionStandings.UNWIRED, questRegistry());
        loaded.load(new DataInputStream(new ByteArrayInputStream(first)));
        assertArrayEquals(first, serialize(loaded), "the quest frame round-trips byte-identically");
        assertEquals(hash(source), hash(loaded), "hashInto covers the quest frame (landmine F)");
        assertEquals(0, loaded.questLog().ownerOf(0));

        // The frame guard: a load against DIFFERENT quest raws (none) fails loudly.
        ActorsSystem mismatched = freshLoadTarget(typeStats, jobs);
        IOException e = org.junit.jupiter.api.Assertions.assertThrows(IOException.class,
                () -> mismatched.load(new DataInputStream(new ByteArrayInputStream(first))));
        org.junit.jupiter.api.Assertions.assertTrue(e.getMessage().contains("questCount"),
                e.getMessage());
    }
}
