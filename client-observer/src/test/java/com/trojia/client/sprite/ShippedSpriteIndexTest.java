package com.trojia.client.sprite;

import org.junit.jupiter.api.Test;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The SHIPPED {@code content/art/sprites/sprite-index.json} + {@code sprites.png} (emitted
 * together by {@code tools/scripts/gen_actor_sprites.py}) honor the unified art spec's actor
 * contract: all 9 actor types resolve with at least the §3.2 minimum variant counts, and
 * every sheet pixel is MERCOLAS-24 or transparent. Located by walking up from the test
 * working directory (same convention as {@code ShippedArtMapping}).
 */
class ShippedSpriteIndexTest {

    /** §3.2 minimum variants per type — the whole table, all 9 types. */
    private static final Map<String, Integer> MIN_VARIANTS = Map.of(
            "militia_watch", 3, "serf", 3, "wastrel", 3,
            "priest_of_the_flame", 2, "disciple_of_the_flame", 2,
            "shopkeeper", 2, "animal_keeper", 2,
            "animal", 4, "feral", 2);

    /** MERCOLAS-24 (unified art spec §1.2), as 0xRRGGBB. */
    private static final Set<Integer> MERCOLAS_24 = Set.of(
            0x0D0B10, 0x2B2A31, 0x3F3E47, 0x57565F, 0x75747C, 0xC9C2B0, 0xE4DCC6,
            0x16211A, 0x26382C, 0x3C523E, 0x5A6E4C, 0x221B14, 0x382C1F, 0x533F2B,
            0x70573A, 0x8E7452, 0x491722, 0x7A1F26, 0xA72C2A, 0x18242F, 0x2B4257,
            0x46708A, 0x83A7B4, 0xB98F42);

    @Test
    void allNineActorTypesResolveWithSpecMinimumVariants() {
        SpriteIndex index = loadShipped();
        assertEquals(MIN_VARIANTS.keySet(), index.actorTypeIds(),
                "actorQueries must map exactly the 9 raws actor types");
        for (Map.Entry<String, Integer> entry : MIN_VARIANTS.entrySet()) {
            Set<String> seen = new java.util.HashSet<>();
            for (long actorId = 0; actorId < 512; actorId++) {
                seen.add(index.forActor(entry.getKey(), actorId).id());
            }
            assertTrue(seen.size() >= entry.getValue(),
                    entry.getKey() + ": expected >= " + entry.getValue()
                            + " variants in use, saw " + seen);
        }
    }

    @Test
    void clergyPoolsAreDisjoint() {
        // §3.2: the robed/ragged split keeps the two clergy pools disjoint.
        SpriteIndex index = loadShipped();
        List<String> priests = index.query(Set.of("actor", "humanoid", "clergy", "robed"))
                .stream().map(SpriteRef::id).toList();
        List<String> disciples = index.query(Set.of("actor", "humanoid", "clergy", "ragged"))
                .stream().map(SpriteRef::id).toList();
        assertTrue(priests.stream().noneMatch(disciples::contains),
                "robed and ragged clergy overlap: " + priests + " vs " + disciples);
    }

    @Test
    void sheetIsPaletteTrueAndCoveredByTheIndex() throws IOException {
        SpriteIndex index = loadShipped();
        BufferedImage sheet = ImageIO.read(
                locate().resolveSibling("sprites.png").toFile());
        assertEquals(index.columns() * index.tilePx(), sheet.getWidth());
        assertEquals(index.rows() * index.tilePx(), sheet.getHeight());
        for (int y = 0; y < sheet.getHeight(); y++) {
            for (int x = 0; x < sheet.getWidth(); x++) {
                int argb = sheet.getRGB(x, y);
                int alpha = argb >>> 24;
                if (alpha == 0) {
                    continue;              // transparent background
                }
                assertEquals(0xFF, alpha, "translucent pixel at (" + x + "," + y + ")");
                assertTrue(MERCOLAS_24.contains(argb & 0xFFFFFF),
                        "off-palette pixel at (" + x + "," + y + "): #"
                                + Integer.toHexString(argb & 0xFFFFFF));
            }
        }
        // Every indexed cell block stays inside the sheet.
        for (SpriteRef ref : index.all()) {
            assertTrue((ref.col() + ref.cellsW()) * index.tilePx() <= sheet.getWidth()
                            && (ref.row() + ref.cellsH()) * index.tilePx() <= sheet.getHeight(),
                    ref.id() + " overflows the sheet");
        }
    }

    private static SpriteIndex loadShipped() {
        try (BufferedReader reader = Files.newBufferedReader(locate(),
                StandardCharsets.UTF_8)) {
            return SpriteIndex.load(reader);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    private static Path locate() {
        Path dir = Paths.get(System.getProperty("user.dir")).toAbsolutePath();
        for (int i = 0; i < 5 && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve(
                    Paths.get("content", "art", "sprites", "sprite-index.json"));
            if (Files.exists(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException("cannot locate content/art/sprites/sprite-index.json");
    }
}
