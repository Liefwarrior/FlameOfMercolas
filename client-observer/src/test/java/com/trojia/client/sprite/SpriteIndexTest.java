package com.trojia.client.sprite;

import com.trojia.client.art.ArtMappingException;
import org.junit.jupiter.api.Test;

import java.io.StringReader;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The GL-free sprite index (unified art spec §2): schema validation aggregates every defect
 * into one boot-fatal {@link ArtMappingException}; queries are all-of tag matches ordered by
 * id ordinal; the per-actor pick is pure, deterministic, and varied across a population.
 */
class SpriteIndexTest {

    /** A small well-formed document exercising defaults, multi-cell, and shared tags. */
    private static final String VALID = """
            {
              "schemaVersion": 1,
              "provenance": "test fixture - unknown fields are ignored",
              "sheet": "art/sprites/sprites.png",
              "tilePx": 16,
              "columns": 16, "rows": 4,
              "sprites": [
                { "id": "actor_guard_0", "cell": [0, 0], "tags": ["actor", "humanoid", "guard", "armored"] },
                { "id": "actor_guard_1", "cell": [1, 0], "tags": ["actor", "humanoid", "guard"] },
                { "id": "actor_keeper_0", "cell": [2, 0], "tags": ["actor", "humanoid", "laborer", "keeper"] },
                { "id": "actor_laborer_0", "cell": [3, 0], "tags": ["actor", "humanoid", "laborer"] },
                { "id": "face_base_tan_1", "cell": [0, 1], "w": 3, "h": 3, "weight": 20,
                  "tags": ["face_base", "skin_tan"] }
              ],
              "actorQueries": {
                "militia_watch": ["actor", "humanoid", "guard"],
                "serf": ["actor", "humanoid", "laborer"],
                "animal_keeper": ["actor", "humanoid", "laborer", "keeper"]
              }
            }
            """;

    private static SpriteIndex load(String json) {
        return SpriteIndex.load(new StringReader(json));
    }

    // ------------------------------------------------------------------ load + fields

    @Test
    void loadsSheetGeometryAndDefaults() {
        SpriteIndex index = load(VALID);
        assertEquals("art/sprites/sprites.png", index.sheetPath());
        assertEquals(16, index.tilePx());
        assertEquals(16, index.columns());
        assertEquals(4, index.rows());

        SpriteRef guard = index.byId("actor_guard_0");
        assertEquals(0, guard.col());
        assertEquals(0, guard.row());
        assertEquals(1, guard.cellsW());     // w/h default 1
        assertEquals(1, guard.cellsH());
        assertEquals(1, guard.weight());     // weight defaults 1

        SpriteRef face = index.byId("face_base_tan_1");
        assertEquals(3, face.cellsW());      // multi-cell entry
        assertEquals(3, face.cellsH());
        assertEquals(20, face.weight());
    }

    // ------------------------------------------------------------------ query semantics

    @Test
    void queryIsAllOfSupersetMatch() {
        SpriteIndex index = load(VALID);
        // "guard" alone matches both guards; adding "armored" narrows to the tagged one.
        assertEquals(List.of("actor_guard_0", "actor_guard_1"),
                ids(index.query(Set.of("actor", "guard"))));
        assertEquals(List.of("actor_guard_0"),
                ids(index.query(Set.of("actor", "guard", "armored"))));
        // Broader laborer query also catches the keeper — sanctioned overlap (§3.2).
        assertEquals(List.of("actor_keeper_0", "actor_laborer_0"),
                ids(index.query(Set.of("actor", "humanoid", "laborer"))));
    }

    @Test
    void queryOrdersByIdOrdinalNotFileOrder() {
        // File order z-then-a: the query must still come back a-then-z.
        SpriteIndex index = load("""
                {
                  "schemaVersion": 1, "sheet": "s.png", "tilePx": 16, "columns": 4, "rows": 1,
                  "sprites": [
                    { "id": "a_first", "cell": [0, 0], "tags": ["x"] },
                    { "id": "z_last", "cell": [1, 0], "tags": ["x"] }
                  ]
                }
                """);
        assertEquals(List.of("a_first", "z_last"), ids(index.query(Set.of("x"))));
    }

    @Test
    void unknownTagMatchesNothing() {
        assertTrue(load(VALID).query(Set.of("no_such_tag")).isEmpty());
    }

    @Test
    void emptyQueryThrows() {
        SpriteIndex index = load(VALID);
        assertThrows(IllegalArgumentException.class, () -> index.query(Set.of()));
        assertThrows(IllegalArgumentException.class, () -> index.query(null));
    }

    // ------------------------------------------------------------------ forActor

    @Test
    void forActorIsDeterministicAcrossLoads() {
        SpriteIndex first = load(VALID);
        SpriteIndex second = load(VALID);
        for (long actorId = 0; actorId < 64; actorId++) {
            assertEquals(first.forActor("militia_watch", actorId).id(),
                    second.forActor("militia_watch", actorId).id());
        }
    }

    @Test
    void forActorKeepsTheSpriteForLife() {
        SpriteIndex index = load(VALID);
        SpriteRef pick = index.forActor("serf", 7);
        assertSame(pick, index.forActor("serf", 7)); // pure + pre-resolved pool
    }

    @Test
    void forActorSpreadsAPopulationOverThePool() {
        SpriteIndex index = load(VALID);
        Set<String> seen = new HashSet<>();
        for (long actorId = 0; actorId < 100; actorId++) {
            seen.add(index.forActor("serf", actorId).id());
        }
        // serf's pool is {actor_keeper_0, actor_laborer_0}; 100 actors must hit both.
        assertEquals(Set.of("actor_keeper_0", "actor_laborer_0"), seen);
    }

    @Test
    void forActorPicksOnlyFromTheTypesPool() {
        SpriteIndex index = load(VALID);
        for (long actorId = 0; actorId < 100; actorId++) {
            String id = index.forActor("animal_keeper", actorId).id();
            assertEquals("actor_keeper_0", id); // single-candidate pool
        }
    }

    @Test
    void forActorUnknownTypeThrows() {
        SpriteIndex index = load(VALID);
        assertThrows(IllegalArgumentException.class, () -> index.forActor("dragon", 1));
    }

    @Test
    void actorTypeIdsListsTheMappedTypes() {
        assertEquals(Set.of("militia_watch", "serf", "animal_keeper"),
                load(VALID).actorTypeIds());
    }

    // ------------------------------------------------------------------ mix64

    @Test
    void mix64IsThePinnedSplitMix64Finalizer() {
        // FACES-SPEC §4.2 vectors: the finalizer fixes 0 and must avalanche neighbors.
        assertEquals(0L, SpriteIndex.mix64(0L));
        assertEquals(6238072747940578789L, SpriteIndex.mix64(1L));
        assertEquals(-2606959012126976886L, SpriteIndex.mix64(2L));
        assertNotEquals(SpriteIndex.mix64(1L), SpriteIndex.mix64(2L));
    }

    // ------------------------------------------------------------------ validation

    @Test
    void aggregatesEveryDefectIntoOneException() {
        String bad = """
                {
                  "schemaVersion": 2,
                  "sheet": "",
                  "tilePx": 0,
                  "columns": 2, "rows": 1,
                  "sprites": [
                    { "id": "b_dup", "cell": [0, 0], "tags": ["ok"] },
                    { "id": "b_dup", "cell": [1, 0], "tags": ["ok"] },
                    { "id": "a_out_of_order", "cell": [5, 9], "tags": [] },
                    { "id": "Bad-Id!", "cell": [1, 0], "tags": ["ok"] },
                    { "id": "c_bad_weight", "cell": [1, 0], "weight": 0, "tags": ["UPPER"] }
                  ],
                  "actorQueries": {
                    "ghost": ["never_matches"]
                  }
                }
                """;
        ArtMappingException e = assertThrows(ArtMappingException.class, () -> load(bad));
        String msg = e.getMessage();
        assertTrue(msg.contains("schemaVersion"), msg);
        assertTrue(msg.contains("sheet"), msg);
        assertTrue(msg.contains("tilePx"), msg);
        assertTrue(msg.contains("ascending ASCII order"), msg);   // dup + out-of-order
        assertTrue(msg.contains("Bad-Id!"), msg);
        assertTrue(msg.contains("outside sheet columns"), msg);   // col 5 on a 2-wide sheet
        assertTrue(msg.contains("outside sheet rows"), msg);
        assertTrue(msg.contains("sprites.a_out_of_order: tags"), msg);
        assertTrue(msg.contains("weight"), msg);
        assertTrue(msg.contains("UPPER"), msg);
        assertTrue(msg.contains("actorQueries.ghost"), msg);      // resolves empty = defect
    }

    @Test
    void multiCellEntryMustFitTheSheet() {
        String bad = """
                {
                  "schemaVersion": 1, "sheet": "s.png", "tilePx": 16, "columns": 4, "rows": 2,
                  "sprites": [
                    { "id": "wide", "cell": [3, 0], "w": 2, "tags": ["x"] }
                  ]
                }
                """;
        ArtMappingException e = assertThrows(ArtMappingException.class, () -> load(bad));
        assertTrue(e.getMessage().contains("outside sheet columns"), e.getMessage());
    }

    @Test
    void missingActorQueriesIsLegal() {
        SpriteIndex index = load("""
                {
                  "schemaVersion": 1, "sheet": "s.png", "tilePx": 16, "columns": 1, "rows": 1,
                  "sprites": [ { "id": "face_only", "cell": [0, 0], "tags": ["face_base"] } ]
                }
                """);
        assertTrue(index.actorTypeIds().isEmpty());
        assertThrows(IllegalArgumentException.class, () -> index.forActor("serf", 1));
    }

    @Test
    void malformedJsonThrowsArtMappingException() {
        assertThrows(ArtMappingException.class, () -> load("{ not json"));
        assertThrows(ArtMappingException.class, () -> load("[]"));
    }

    private static List<String> ids(List<SpriteRef> refs) {
        return refs.stream().map(SpriteRef::id).toList();
    }
}
