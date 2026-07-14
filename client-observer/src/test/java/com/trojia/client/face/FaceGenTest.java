package com.trojia.client.face;

import com.trojia.client.face.FaceComposition.PlacedPart;
import com.trojia.client.sprite.SpriteIndex;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;

/**
 * FaceGen behavior against the shipped content (FACES-SPEC §7's list, re-targeted to tile
 * parts per the unified art spec §4.9): determinism, purity, archetype-independent draws,
 * cross-language reference vectors, tag-multiplier exclusion, retired scar-drop rule,
 * named override, class-gating coherence, and pool coverage.
 */
class FaceGenTest {

    /** The Python generator's sampler seed — reference vectors below were produced by it. */
    private static final long WORLD_SEED = 0x5EEDF00DL;

    private static final Set<String> ARCHETYPE_IDS =
            Set.of("guard", "cultist", "monk", "noble", "thug", "laborer");

    private static List<String> ids(FaceComposition composition) {
        return composition.parts().stream().map(p -> p.part().id()).toList();
    }

    // T1 — sameSeedSameActor_identicalComposition
    @Test
    void sameInputs_twoIndependentInstances_identicalCompositions() {
        FaceGen a = ShippedFaceContent.gen();
        FaceGen b = ShippedFaceContent.gen();
        for (long actorId = 0; actorId < 64; actorId++) {
            for (String archetype : ARCHETYPE_IDS) {
                FaceComposition ca = a.compose(WORLD_SEED, actorId, archetype);
                FaceComposition cb = b.compose(WORLD_SEED, actorId, archetype);
                assertEquals(ca, cb, "actor " + actorId + " archetype " + archetype);
            }
        }
    }

    // T3 — facegenIsPure_noStateAcrossCalls
    @Test
    void shuffledInterleavedCalls_matchIsolatedCalls() {
        FaceGen gen = ShippedFaceContent.gen();
        List<long[]> inputs = new ArrayList<>();
        for (long actorId = 0; actorId < 40; actorId++) {
            inputs.add(new long[]{actorId, actorId % ARCHETYPE_IDS.size()});
        }
        List<String> archetypes = ARCHETYPE_IDS.stream().sorted().toList();
        Map<Long, FaceComposition> isolated = new HashMap<>();
        for (long[] in : inputs) {
            isolated.put(in[0], ShippedFaceContent.gen()
                    .compose(WORLD_SEED, in[0], archetypes.get((int) in[1])));
        }
        Collections.shuffle(inputs, new Random(42));   // test-only shuffle, not facegen
        for (long[] in : inputs) {
            assertEquals(isolated.get(in[0]),
                    gen.compose(WORLD_SEED, in[0], archetypes.get((int) in[1])),
                    "actor " + in[0]);
        }
    }

    // T2 — draws are archetype-independent: the archetype shapes the pool, not the draw.
    // laborer and monk have identical tagMultipliers-in-effect (empty vs {grim:1}), so
    // whenever both roll the same headwear class, every pick must coincide.
    @Test
    void equalPools_differentArchetypeIds_shareEveryPick() {
        FaceGen gen = ShippedFaceContent.gen();
        int compared = 0;
        for (long actorId = 0; actorId < 400; actorId++) {
            FaceComposition laborer = gen.compose(WORLD_SEED, actorId, "laborer");
            FaceComposition monk = gen.compose(WORLD_SEED, actorId, "monk");
            String laborerTop = laborer.parts().get(laborer.parts().size() - 1).part().id();
            String monkTop = monk.parts().get(monk.parts().size() - 1).part().id();
            boolean bothBare = laborerTop.startsWith("face_hair_") && monkTop.startsWith("face_hair_");
            if (bothBare) {
                assertEquals(laborer, monk, "actor " + actorId
                        + ": same pools + same draws must give the same face");
                compared++;
            } else {
                // Base/eyes/nose picks never depend on the headwear class either way.
                assertEquals(ids(laborer).get(0), ids(monk).get(0), "base, actor " + actorId);
            }
        }
        assertTrue(compared > 50, "sweep should hit many both-BARE pairs, got " + compared);
    }

    // T11 — cross-language reference vectors: the committed Python generator
    // (tools/scripts/gen_face_parts.py --samples, worldSeed 0x5EEDF00D) mirrors this
    // algorithm; these compositions were produced by it and pin both implementations.
    @Test
    void referenceVectors_matchPythonMirror() {
        FaceGen gen = ShippedFaceContent.gen();
        assertEquals(List.of("face_base_pale_1", "face_mouth_moustache_black",
                        "face_nose_blunt", "face_eyes_hard", "face_brow_heavy",
                        "face_headwear_hood_1"),
                ids(gen.compose(WORLD_SEED, 1000, "cultist")));
        assertEquals(List.of("face_base_dark_1", "face_scar_slash", "face_mouth_line",
                        "face_nose_long", "face_eyes_squint", "face_brow_scowl",
                        "face_hair_tuft_white"),
                ids(gen.compose(WORLD_SEED, 1111, "monk")));
        assertEquals(List.of("face_base_pale_2", "face_scar_cross", "face_scar_slash",
                        "face_mouth_line", "face_nose_hook", "face_eyes_glint",
                        "face_brow_flat", "face_hair_crop_black"),
                ids(gen.compose(WORLD_SEED, 1296, "laborer")));
        assertEquals(List.of("face_base_tan_0", "face_mouth_frown", "face_nose_blunt",
                        "face_eyes_weary", "face_brow_scowl", "face_headwear_closed_helm_1"),
                ids(gen.compose(WORLD_SEED, 1481, "guard")));
    }

    // T12 — zeroTagMultiplier_excludesPart: cultist has fine:0; smirk mouth and arch brow
    // carry the fine tag and must never appear across a full sweep.
    @Test
    void zeroMultiplier_excludesTaggedParts() {
        FaceGen gen = ShippedFaceContent.gen();
        for (long actorId = 0; actorId < 1000; actorId++) {
            for (String id : ids(gen.compose(WORLD_SEED, actorId, "cultist"))) {
                assertTrue(!id.equals("face_mouth_smirk") && !id.equals("face_brow_arch"),
                        "cultist rolled fine-tagged part " + id + " (actor " + actorId + ")");
            }
        }
    }

    // T13 (amended) — the ASCII blank-cell drop rule is retired: every rolled scar is
    // composed at layer 2, even when hair/headwear will occlude it, and the composition
    // stays byte-stable.
    @Test
    void rolledScars_alwaysComposed_atLayerTwo() {
        FaceGen gen = ShippedFaceContent.gen();
        int scarred = 0;
        int scarredUnderHeadwear = 0;
        for (long actorId = 0; actorId < 1000; actorId++) {
            FaceComposition c = gen.compose(WORLD_SEED, actorId, "thug");
            List<PlacedPart> parts = c.parts();
            assertTrue(parts.get(0).part().id().startsWith("face_base_"), "layer 1 is base");
            int i = 1;
            while (i < parts.size() && parts.get(i).part().id().startsWith("face_scar_")) {
                i++;
            }
            scarred += i > 1 ? 1 : 0;
            for (int j = i; j < parts.size(); j++) {
                assertTrue(!parts.get(j).part().id().startsWith("face_scar_"),
                        "scars only at layer 2 (actor " + actorId + ")");
            }
            String top = parts.get(parts.size() - 1).part().id();
            if (i > 1 && top.startsWith("face_headwear_")) {
                scarredUnderHeadwear++;
            }
            assertEquals(c, gen.compose(WORLD_SEED, actorId, "thug"), "byte-stable");
        }
        assertTrue(scarred > 100, "13/2/1 weighting should scar ~19%, got " + scarred);
        assertTrue(scarredUnderHeadwear > 0,
                "sweep should include occluded scars that are still composed");
    }

    // T14 — namedFace_overridesGenerator, zero draws consumed
    @Test
    void namedFace_winsOverGenerator() {
        FaceGen gen = ShippedFaceContent.gen();
        FaceComposition devin = gen.compose(WORLD_SEED, 4101, "monk", "devin");
        assertEquals(1, devin.parts().size());
        assertEquals("face_named_devin", devin.parts().get(0).part().id());
        assertEquals(0, devin.parts().get(0).x());
        assertEquals(0, devin.parts().get(0).y());
        assertNotNull(gen.composeNamed("john"));
        assertNull(gen.composeNamed("nobody_authored_this"));
        assertEquals(gen.compose(WORLD_SEED, 4101, "monk"),
                gen.compose(WORLD_SEED, 4101, "monk", "nobody_authored_this"),
                "unknown npc id falls through to the generator");
    }

    // §4.3 — hair-color class coherence: every hair_*-tagged part in one face matches one
    // drawn color class.
    @Test
    void hairColorClass_coherentAcrossSlots() {
        FaceGen gen = ShippedFaceContent.gen();
        for (long actorId = 0; actorId < 1000; actorId++) {
            for (String archetype : ARCHETYPE_IDS) {
                SpriteIndex index = ShippedFaceContent.index();
                Set<String> colors = new java.util.TreeSet<>();
                for (String id : ids(gen.compose(WORLD_SEED, actorId, archetype))) {
                    for (String tag : index.byId(id).tags()) {
                        if (tag.startsWith("hair_")) {
                            colors.add(tag);
                        }
                    }
                }
                assertTrue(colors.size() <= 1,
                        "one color class per face, got " + colors + " (actor " + actorId
                                + ", " + archetype + ")");
            }
        }
    }

    // §4.3 — class gating: exactly one of hair/headwear composes, always on top.
    @Test
    void composition_hasExactlyOneOfHairOrHeadwear_onTop() {
        FaceGen gen = ShippedFaceContent.gen();
        for (long actorId = 0; actorId < 500; actorId++) {
            for (String archetype : ARCHETYPE_IDS) {
                List<String> ids = ids(gen.compose(WORLD_SEED, actorId, archetype));
                long crowns = ids.stream().filter(id -> id.startsWith("face_hair_")
                        || id.startsWith("face_headwear_")).count();
                assertEquals(1, crowns, "actor " + actorId + " " + archetype + ": " + ids);
                String top = ids.get(ids.size() - 1);
                assertTrue(top.startsWith("face_hair_") || top.startsWith("face_headwear_"),
                        "crown layer is topmost: " + ids);
            }
        }
    }

    // T10 — every pool the generator can consult is non-empty on shipped content.
    @Test
    void shippedContent_coverageValidates() {
        assertDoesNotThrow(() -> ShippedFaceContent.gen().validateCoverage());
    }

    // Beast types have no archetype — the inspector's no-face gate.
    @Test
    void beastTypes_haveNoArchetypeMapping() {
        FaceArchetypes archetypes = ShippedFaceContent.archetypes();
        assertNull(archetypes.archetypeForActorType("animal"));
        assertNull(archetypes.archetypeForActorType("feral"));
        assertEquals("guard", archetypes.archetypeForActorType("militia_watch"));
        assertEquals(7, archetypes.actorTypeIds().size(),
                "all seven humanoid types carry a face archetype");
    }
}
