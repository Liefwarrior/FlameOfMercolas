package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorsSystem;
import com.trojia.sim.actor.BankLedger;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.RestrictedZoneTable;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.io.WorldHasher;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Tarry Jek's spawn gates (S1-2): the gazetteer's strand mudlark is real — a Wastrel homed on
 * the Beaching Strand's z:+10 shingle, appended last so no pre-existing ActorId moved — and
 * the roster including him passes the persisted determinism triad (serialize / load /
 * hashInto): a mid-run snapshot round-trips byte-identically and hash-stably.
 */
class DocksJekTest {

    private static final int JEK_ID = 691;      // appended last: ids 0..690 are untouched
    private static final int STRAND_WORLD_Z = 18; // authored z:+10 + CHUNK_SIZE_Z

    @Test
    void jekSpawnsAsTheStrandMudlark() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        ActorRegistry registry = population.registry();
        assertEquals(692, registry.size(), "Jek grows the roster 691 -> 692");

        Actor jek = registry.get(JEK_ID);
        assertEquals("wastrel", jek.typeId().key());
        assertEquals(STRAND_WORLD_Z, PackedPos.z(jek.cell()), "Jek lives on the strand plane");
        assertTrue(jek.homeId() != Actor.NONE, "Jek is homed (soloHome at his spawn)");
        assertEquals(jek.cell(), population.homes().get(jek.homeId()).homeCell(),
                "home == the strand berth");
        assertEquals(jek.cell(), jek.anchorCell(), "anchor == home == the spot (non-commuter)");

        IdentityRegistry.Identity who = population.identity().get(JEK_ID);
        assertEquals("Tarry Jek", who.fullName());
        assertEquals("jek", who.notableId());
    }

    @Test
    void theRosterWithJekPassesTheSerializerTriadMidRun() throws IOException {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));
        for (int t = 0; t < 300; t++) {
            driver.requestStep(); // real mid-run state: movement, needs, goal timers
        }

        byte[] first = serialize(population.system());
        ActorsSystem reloaded = new ActorsSystem(loaded.worldSeed(), population.typeStats(),
                population.jobs(), new ActorRegistry(), new HomeRegistry(),
                new RelationshipRegistry(), new ItemsLiteRegistry(), new BankLedger(), null,
                Actor.NONE, RestrictedZoneTable.EMPTY);
        reloaded.load(new DataInputStream(new ByteArrayInputStream(first)));
        byte[] second = serialize(reloaded);

        assertArrayEquals(first, second, "serialize -> load -> serialize must be byte-identical");
        assertEquals(hash(population.system()), hash(reloaded), "hashInto must match after load");
        assertEquals(692, reloaded.registry().size(), "all 692 souls round-trip");
        assertEquals("wastrel", reloaded.registry().get(JEK_ID).typeId().key());
        assertEquals(population.registry().get(JEK_ID).cell(),
                reloaded.registry().get(JEK_ID).cell(), "Jek's live cell round-trips");
    }

    private static byte[] serialize(ActorsSystem system) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        system.serialize(new DataOutputStream(bytes));
        return bytes.toByteArray();
    }

    private static long hash(ActorsSystem system) {
        WorldHasher hasher = new WorldHasher();
        system.hashInto(hasher.sectionSink(system.id()));
        return hasher.sectionHash(system.id());
    }
}
