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
        relationships.addSymmetric(a0.id(), a1.id(), RelationshipKind.HOUSEHOLD);

        // Items: a purse, a larder, a stamped ID_CARD, and a sunk stack (populates the free stack).
        items.addCarried(a0.id(), ItemKinds.COIN, 5);
        items.addCarried(a1.id(), ItemKinds.FOOD, 3);
        items.mintIdCard(a0.id(), a0.id());
        int scrap = items.mintCarried(ItemKinds.FOOD, a1.id());
        items.sink(scrap); // leaves a free-slot to round-trip

        // Ledger: two accounts with non-trivial balances.
        bank.openAccount();
        bank.openAccount();
        bank.credit(0, 500);
        bank.credit(1, 250);

        return new ActorsSystem(1L, typeStats, jobs, registry, homes, relationships, items, bank,
                null, Actor.NONE, RestrictedZoneTable.EMPTY);
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

        // The ID_CARD's stamped accountId survived the round-trip.
        int card = loaded.items().firstCarriedOfKind(0, ItemKinds.ID_CARD);
        assertEquals(0, loaded.items().get(card).accountId());

        // The sunk stack and its recycled free slot round-tripped: the next mint reuses that slot.
        assertEquals(1, loaded.items().freeSlotCount());
        int reused = loaded.items().mintCarried(ItemKinds.COIN, 1);
        assertEquals(3, reused, "the recycled slot (itemId 3) is reused before appending");
    }
}
