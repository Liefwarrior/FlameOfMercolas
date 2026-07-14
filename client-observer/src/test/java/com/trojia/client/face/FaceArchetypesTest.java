package com.trojia.client.face;

import com.trojia.client.art.ArtMappingException;
import org.junit.jupiter.api.Test;

import java.io.StringReader;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Loader validation for {@code face-archetypes.json} (boot-fatal, aggregated). */
class FaceArchetypesTest {

    private static FaceArchetypes load(String json) {
        return FaceArchetypes.load(new StringReader(json));
    }

    @Test
    void shippedArchetypes_loadAndExposeWeights() {
        FaceArchetypes archetypes = ShippedFaceContent.archetypes();
        assertEquals(6, archetypes.archetypeIds().size());
        FaceArchetype cultist = archetypes.archetype("cultist");
        assertEquals(70, cultist.headwearWeights().get(HeadwearClass.HOOD));
        assertEquals(0, cultist.multiplier("fine"));
        assertEquals(3, cultist.multiplier("grim"));
        assertEquals(1, cultist.multiplier("never_mentioned"), "missing tag = x1");
    }

    @Test
    void unknownHeadwearClass_isAggregatedBootFailure() {
        ArtMappingException e = assertThrows(ArtMappingException.class, () -> load("""
                { "schemaVersion": 1, "archetypes": {
                    "guard": { "headwearWeights": { "FEDORA": 10, "BARE": -1 } } } }"""));
        assertTrue(e.getMessage().contains("FEDORA"), e.getMessage());
        assertTrue(e.getMessage().contains("BARE"), "both defects reported: " + e.getMessage());
    }

    @Test
    void negativeMultiplier_rejected_zeroAllowed() {
        assertThrows(ArtMappingException.class, () -> load("""
                { "schemaVersion": 1, "archetypes": {
                    "guard": { "headwearWeights": { "BARE": 1 },
                               "tagMultipliers": { "grim": -2 } } } }"""));
        FaceArchetypes ok = load("""
                { "schemaVersion": 1, "archetypes": {
                    "guard": { "headwearWeights": { "BARE": 1 },
                               "tagMultipliers": { "fine": 0 } } } }""");
        assertEquals(0, ok.archetype("guard").multiplier("fine"));
    }

    @Test
    void actorArchetypes_mustReferenceExistingArchetype() {
        ArtMappingException e = assertThrows(ArtMappingException.class, () -> load("""
                { "schemaVersion": 1,
                  "archetypes": { "guard": { "headwearWeights": { "BARE": 1 } } },
                  "actorArchetypes": { "militia_watch": "paladin" } }"""));
        assertTrue(e.getMessage().contains("paladin"), e.getMessage());
    }

    @Test
    void missingArchetypesBlock_rejected() {
        assertThrows(ArtMappingException.class, () -> load("{ \"schemaVersion\": 1 }"));
    }

    @Test
    void unknownArchetypeLookup_isProgrammingError() {
        assertThrows(IllegalArgumentException.class,
                () -> ShippedFaceContent.archetypes().archetype("paladin"));
    }
}
