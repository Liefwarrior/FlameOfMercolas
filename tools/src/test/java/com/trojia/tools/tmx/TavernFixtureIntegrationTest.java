package com.trojia.tools.tmx;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * Integration test: parses the real authored fixture
 * {@code content/maps/src/tavern_fixture.tmx} (and its external tileset
 * {@code materials.tsx}) with the production readers, proving the authored
 * content and the parser agree before the M1 importer exists.
 *
 * <p>Structural contract checked here (per content/maps/README.md):
 * z-groups named {@code z:<sign><n>}, each with {@code terrain} + {@code floor}
 * tile layers (optional {@code fluids}) and a {@code markers} object layer;
 * all gids resolve inside the single external tileset; the Tavern Fire
 * {@code ignition_point} script anchor is present and sits on a material tile.</p>
 */
class TavernFixtureIntegrationTest {

    private static final Path MAPS_DIR = locateMapsDir();

    private static TmxMap map;
    private static TmxTileset tileset;
    private static final List<String> warnings = new ArrayList<>();

    /** Resolves content/maps/src whether the test runs from tools/ or the repo root. */
    private static Path locateMapsDir() {
        Path rel = Path.of("content", "maps", "src");
        for (Path base = Path.of("").toAbsolutePath(); base != null; base = base.getParent()) {
            Path candidate = base.resolve(rel);
            if (Files.isRegularFile(candidate.resolve("tavern_fixture.tmx"))) {
                return candidate;
            }
        }
        throw new IllegalStateException("cannot locate content/maps/src above " + Path.of("").toAbsolutePath());
    }

    @BeforeAll
    static void parseFixture() {
        map = new TmxReader(warnings::add).read(MAPS_DIR.resolve("tavern_fixture.tmx"));
        TmxTilesetRef ref = map.tilesets().get(0);
        tileset = new TsxReader(warnings::add).read(MAPS_DIR.resolve(ref.source()));
    }

    @Test
    void fixtureParsesCleanly() {
        assertEquals(48, map.width());
        assertEquals(32, map.height());
        assertEquals(16, map.tileWidth());
        assertEquals("orthogonal", map.orientation());
        assertEquals(1, map.tilesets().size(), "exactly one external tileset");
        assertEquals(1, map.tilesets().get(0).firstGid());
        assertTrue(map.tilesets().get(0).source().endsWith("materials.tsx"));
        assertTrue(warnings.isEmpty(), "fixture must parse without warnings: " + warnings);
    }

    @Test
    void tilesetMatchesFixtureGidRange() {
        assertEquals(40, tileset.tileCount());
        int firstGid = map.tilesets().get(0).firstGid();
        int maxGid = firstGid + tileset.tileCount() - 1;

        for (TmxLayer top : map.layers()) {
            TmxLayerGroup group = assertInstanceOf(TmxLayerGroup.class, top,
                    "all top-level layers are z-groups");
            for (TmxLayer inner : group.layers()) {
                if (inner instanceof TmxTileLayer tiles) {
                    for (int gid : tiles.gids()) {
                        assertTrue(gid == 0 || (gid >= firstGid && gid <= maxGid),
                                group.name() + "/" + tiles.name() + ": gid " + gid + " out of range");
                    }
                }
            }
        }
    }

    @Test
    void everyTileCarriesMaterialOrFluidProperty() {
        for (int localId = 0; localId < tileset.tileCount(); localId++) {
            Optional<TmxTilesetTile> tile = tileset.tile(localId);
            assertTrue(tile.isPresent(), "tileset tile " + localId + " missing");
            TmxProperties props = tile.get().properties();
            boolean material = props.find("material").isPresent() && props.find("form").isPresent();
            boolean fluid = props.find("fluid").isPresent() && props.find("depth").isPresent();
            assertTrue(material ^ fluid,
                    "tile " + localId + " must carry material+form xor fluid+depth");
        }
    }

    @Test
    void zGroupsFollowLayerContract() {
        Set<String> groupNames = new HashSet<>();
        for (TmxLayer top : map.layers()) {
            TmxLayerGroup group = (TmxLayerGroup) top;
            assertTrue(group.name().matches("z:[+-]\\d+"),
                    "group name '" + group.name() + "' violates z:<sign><n> convention");
            groupNames.add(group.name());

            Set<String> tileLayers = new HashSet<>();
            boolean markers = false;
            for (TmxLayer inner : group.layers()) {
                if (inner instanceof TmxTileLayer tiles) {
                    tileLayers.add(tiles.name());
                } else if (inner instanceof TmxObjectLayer objects && objects.name().equals("markers")) {
                    markers = true;
                }
            }
            assertTrue(tileLayers.contains("terrain"), group.name() + ": terrain layer required");
            assertTrue(tileLayers.contains("floor"), group.name() + ": floor layer required");
            assertTrue(markers, group.name() + ": markers objectgroup required");
        }
        assertEquals(Set.of("z:-1", "z:+0", "z:+1"), groupNames);
    }

    @Test
    void ignitionPointAnchorSitsOnAnOakTile() {
        TmxLayerGroup groundFloor = map.layers().stream()
                .map(TmxLayerGroup.class::cast)
                .filter(g -> g.name().equals("z:+0"))
                .findFirst().orElseThrow();

        TmxObject anchor = groundFloor.layers().stream()
                .filter(TmxObjectLayer.class::isInstance)
                .map(TmxObjectLayer.class::cast)
                .flatMap(l -> l.objects().stream())
                .filter(o -> o.name().equals("ignition_point"))
                .findFirst().orElseThrow(() -> new AssertionError("ignition_point anchor missing on z:+0"));
        assertEquals("script_anchor", anchor.typeName());

        int tx = (int) (anchor.x() / map.tileWidth());
        int ty = (int) (anchor.y() / map.tileHeight());
        TmxTileLayer terrain = groundFloor.layers().stream()
                .filter(TmxTileLayer.class::isInstance)
                .map(TmxTileLayer.class::cast)
                .filter(l -> l.name().equals("terrain"))
                .findFirst().orElseThrow();

        int gid = terrain.gidAt(tx, ty);
        assertTrue(gid > 0, "ignition_point must target a solid tile");
        TmxTilesetTile tile = tileset.tile(gid - map.tilesets().get(0).firstGid()).orElseThrow();
        assertEquals("oak", tile.properties().find("material").orElseThrow().value(),
                "Tavern Fire ignition target is the oak table");
    }
}
