package com.trojia.client.sprite;

import com.trojia.client.boot.RepoPaths;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.io.StringReader;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The integration contract of THE unified index (DECISIONS.md Art register pillars 3+4:
 * ONE tag-queryable {@code SpriteIndex} serves actor sprites AND face parts): the shipped
 * {@code content/art/sprites/sprite-index.json} must contain every entry of the face
 * generator's canonical {@code content/art/faces/face-parts-index.json} <em>verbatim</em>
 * (same id, tags, geometry, weight — only the sheet cell row shifts below the actor rows),
 * and merging must not perturb either side's pools: every face-part tag query resolves to
 * the same id list on both indices, and no face part can leak into an actor query.
 */
class UnifiedIndexParityTest {

    @Test
    void unifiedIndexContainsEveryFacePartVerbatimRowShifted() {
        SpriteIndex unified = load(RepoPaths.locate(
                "content", "art", "sprites", "sprite-index.json"));
        SpriteIndex faces = load(RepoPaths.locate(
                "content", "art", "faces", "face-parts-index.json"));

        int rowOffset = unified.rows() - faces.rows();
        assertTrue(rowOffset > 0, "unified sheet must append face rows below actor rows");
        for (SpriteRef face : faces.all()) {
            SpriteRef merged = unified.byId(face.id());
            assertNotNull(merged, "face part missing from unified index: " + face.id());
            assertEquals(face.tags(), merged.tags(), face.id() + " tags");
            assertEquals(face.col(), merged.col(), face.id() + " col");
            assertEquals(face.row() + rowOffset, merged.row(), face.id() + " row offset");
            assertEquals(face.cellsW(), merged.cellsW(), face.id() + " w");
            assertEquals(face.cellsH(), merged.cellsH(), face.id() + " h");
            assertEquals(face.weight(), merged.weight(), face.id() + " weight");
        }
    }

    @Test
    void mergeDoesNotPerturbAnyPool() {
        SpriteIndex unified = load(RepoPaths.locate(
                "content", "art", "sprites", "sprite-index.json"));
        SpriteIndex faces = load(RepoPaths.locate(
                "content", "art", "faces", "face-parts-index.json"));

        // Every face tag query yields the identical id list on both indices (FaceGen's
        // pools are tag queries — identical pools mean identical golden compositions).
        Set<String> faceTags = new HashSet<>();
        for (SpriteRef ref : faces.all()) {
            faceTags.addAll(ref.tags());
        }
        assertFalse(faceTags.isEmpty());
        for (String tag : faceTags) {
            assertEquals(ids(faces.query(Set.of(tag))), ids(unified.query(Set.of(tag))),
                    "pool for tag \"" + tag + "\" perturbed by the merge");
        }

        // No actor query can reach a face part (tag vocabularies are disjoint).
        for (String typeId : unified.actorTypeIds()) {
            for (long actorId = 0; actorId < 64; actorId++) {
                String picked = unified.forActor(typeId, actorId).id();
                assertTrue(picked.startsWith("actor_"),
                        typeId + " resolved a non-actor sprite: " + picked);
            }
        }
    }

    private static List<String> ids(List<SpriteRef> refs) {
        return refs.stream().map(SpriteRef::id).toList();
    }

    private static SpriteIndex load(Path path) {
        try {
            return SpriteIndex.load(new StringReader(
                    Files.readString(path, StandardCharsets.UTF_8)));
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }
}
