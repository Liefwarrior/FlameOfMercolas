package com.trojia.tools.importer;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;

import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.io.TrojSav;
import com.trojia.sim.world.io.WorldLoader;
import com.trojia.tools.tmx.TmxLayer;
import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxMap;
import com.trojia.tools.tmx.TmxTileLayer;
import com.trojia.tools.tmx.TmxTileset;
import com.trojia.tools.tmx.TmxTilesetTile;

import java.io.IOException;
import java.nio.file.Path;
import java.util.Optional;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/**
 * DoD1 — round-trip fidelity. Imports {@code content/maps/src/tavern_fixture.tmx}
 * to a tick-0 TROJSAV, reloads it through {@link WorldLoader}, and asserts the
 * collapsed {@code (material, form)} at a spread of authored cells across z −1,
 * +0, +1 — a WALL, a floor-sublayer FLOOR, a STAIR (authored {@code STAIR_*} →
 * {@link TileForm#STAIR}), and an empty cell (→ OPEN).
 *
 * <p>Every expected value is derived independently from the parsed fixture (the
 * gid grids + tileset properties + the §1.1 #17 collapse rule), not hardcoded,
 * so the assertions are grounded in the actual authored data.
 */
class TavernImportRoundTripTest {

    @Test
    void reloadedWorldMatchesAuthoredCells(@TempDir Path tmp) throws IOException {
        ImporterTestSupport.Fixture fixture = ImporterTestSupport.tavernFixture();
        TmxMap map = fixture.map();
        TmxTileset tileset = fixture.tileset();
        MaterialRegistry registry = ImporterTestSupport.raws().materials();
        int firstGid = map.tilesets().get(0).firstGid();
        int minZ = minZ(map);

        // Import -> write -> reload.
        TiledWorldImporter importer = new TiledWorldImporter();
        TickableWorld baked = importer.importWorld(map, tileset, registry);
        Path out = tmp.resolve("tavern.trojsav");
        importer.toTrojSav(baked, registry.fingerprint()).writeTo(out);
        TickableWorld reloaded = new WorldLoader().load(TrojSav.read(out));

        // Independently locate one cell of each shape, on distinct z-levels.
        Expected wall = findWall(map, tileset, firstGid, registry, minZ, -1);
        Expected floor = findFloorSublayer(map, tileset, firstGid, registry, minZ, 0);
        Expected stair = findStair(map, tileset, firstGid, registry, minZ, 0);
        Expected air = findOpen(map, minZ, 1);

        assertCell(reloaded, wall);
        assertCell(reloaded, floor);
        assertCell(reloaded, stair);
        assertCell(reloaded, air);

        // Sanity: the four samples really do span the three authored z-levels.
        assertEquals(TileForm.WALL, wall.form(), "sample WALL form");
        assertEquals(TileForm.FLOOR, floor.form(), "sample FLOOR form");
        assertEquals(TileForm.STAIR, stair.form(), "sample STAIR form");
        assertEquals(TileForm.OPEN, air.form(), "sample OPEN form");
    }

    private static void assertCell(TickableWorld world, Expected e) {
        TileCursor cursor = world.cursor().moveTo(e.pos());
        assertEquals(e.form(), cursor.form(),
                () -> "form mismatch at world " + e.pos() + " (" + e.describe() + ")");
        if (e.form() != TileForm.OPEN) {
            assertEquals(e.materialRaw(), cursor.materialId(),
                    () -> "material mismatch at world " + e.pos() + " (" + e.describe() + ")");
        }
    }

    // ---- independent oracle -------------------------------------------------

    /** An expected baked cell: its world position, collapsed form and material. */
    private record Expected(int pos, TileForm form, short materialRaw, String describe) {
    }

    private static Expected findWall(TmxMap map, TmxTileset tileset, int firstGid,
            MaterialRegistry registry, int minZ, int z) {
        TmxTileLayer terrain = sublayer(map, z, "terrain");
        assertNotNull(terrain, "z:" + signed(z) + " terrain layer");
        for (int y = 0; y < terrain.height(); y++) {
            for (int x = 0; x < terrain.width(); x++) {
                int gid = terrain.gidAt(x, y);
                if (gid == 0) {
                    continue;
                }
                TileForm form = formOf(tileset, gid - firstGid);
                if (form == TileForm.WALL) {
                    short mat = materialRaw(tileset, gid - firstGid, registry);
                    return new Expected(worldPos(x, y, z, minZ), TileForm.WALL, mat,
                            "WALL terrain at map(" + x + "," + y + ") z:" + signed(z));
                }
            }
        }
        throw new AssertionError("no WALL terrain cell on z:" + signed(z));
    }

    private static Expected findStair(TmxMap map, TmxTileset tileset, int firstGid,
            MaterialRegistry registry, int minZ, int z) {
        TmxTileLayer terrain = sublayer(map, z, "terrain");
        assertNotNull(terrain, "z:" + signed(z) + " terrain layer");
        for (int y = 0; y < terrain.height(); y++) {
            for (int x = 0; x < terrain.width(); x++) {
                int gid = terrain.gidAt(x, y);
                if (gid == 0) {
                    continue;
                }
                if (formOf(tileset, gid - firstGid) == TileForm.STAIR) {
                    short mat = materialRaw(tileset, gid - firstGid, registry);
                    return new Expected(worldPos(x, y, z, minZ), TileForm.STAIR, mat,
                            "STAIR terrain at map(" + x + "," + y + ") z:" + signed(z));
                }
            }
        }
        throw new AssertionError("no STAIR terrain cell on z:" + signed(z));
    }

    private static Expected findFloorSublayer(TmxMap map, TmxTileset tileset, int firstGid,
            MaterialRegistry registry, int minZ, int z) {
        TmxTileLayer terrain = sublayer(map, z, "terrain");
        TmxTileLayer floor = sublayer(map, z, "floor");
        assertNotNull(floor, "z:" + signed(z) + " floor layer");
        for (int y = 0; y < floor.height(); y++) {
            for (int x = 0; x < floor.width(); x++) {
                int fillGid = terrain == null ? 0 : terrain.gidAt(x, y);
                int floorGid = floor.gidAt(x, y);
                if (fillGid == 0 && floorGid != 0) {
                    short mat = materialRaw(tileset, floorGid - firstGid, registry);
                    return new Expected(worldPos(x, y, z, minZ), TileForm.FLOOR, mat,
                            "floor-sublayer FLOOR at map(" + x + "," + y + ") z:" + signed(z));
                }
            }
        }
        throw new AssertionError("no floor-only cell on z:" + signed(z));
    }

    private static Expected findOpen(TmxMap map, int minZ, int z) {
        TmxTileLayer terrain = sublayer(map, z, "terrain");
        TmxTileLayer floor = sublayer(map, z, "floor");
        int width = terrain != null ? terrain.width() : floor.width();
        int height = terrain != null ? terrain.height() : floor.height();
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int fillGid = terrain == null ? 0 : terrain.gidAt(x, y);
                int floorGid = floor == null ? 0 : floor.gidAt(x, y);
                if (fillGid == 0 && floorGid == 0) {
                    return new Expected(worldPos(x, y, z, minZ), TileForm.OPEN, (short) 0,
                            "empty cell -> OPEN at map(" + x + "," + y + ") z:" + signed(z));
                }
            }
        }
        throw new AssertionError("no empty cell on z:" + signed(z));
    }

    // ---- fixture helpers ----------------------------------------------------

    private static int worldPos(int x, int y, int z, int minZ) {
        return PackedPos.pack(Coords.CHUNK_SIZE_X + x, Coords.CHUNK_SIZE_Y + y,
                Coords.CHUNK_SIZE_Z + (z - minZ));
    }

    private static TileForm formOf(TmxTileset tileset, int localId) {
        String literal = property(tileset, localId, "form");
        return switch (literal) {
            case "WALL" -> TileForm.WALL;
            case "FLOOR" -> TileForm.FLOOR;
            case "OPEN" -> TileForm.OPEN;
            case "RAMP" -> TileForm.RAMP;
            case "STAIR_UP", "STAIR_DOWN" -> TileForm.STAIR;
            default -> throw new AssertionError("unexpected form \"" + literal + "\"");
        };
    }

    private static short materialRaw(TmxTileset tileset, int localId, MaterialRegistry registry) {
        return registry.id(property(tileset, localId, "material")).raw();
    }

    private static String property(TmxTileset tileset, int localId, String name) {
        Optional<TmxTilesetTile> tile = tileset.tile(localId);
        if (tile.isEmpty()) {
            throw new AssertionError("tileset tile " + localId + " missing metadata");
        }
        return tile.get().properties().find(name)
                .orElseThrow(() -> new AssertionError("tile " + localId + " has no " + name + "="))
                .value();
    }

    private static TmxTileLayer sublayer(TmxMap map, int z, String name) {
        for (TmxLayer layer : map.layers()) {
            if (layer instanceof TmxLayerGroup group && group.name().equals("z:" + signed(z))) {
                for (TmxLayer inner : group.layers()) {
                    if (inner instanceof TmxTileLayer tiles && tiles.name().equals(name)) {
                        return tiles;
                    }
                }
            }
        }
        return null;
    }

    private static int minZ(TmxMap map) {
        int min = Integer.MAX_VALUE;
        for (TmxLayer layer : map.layers()) {
            if (layer instanceof TmxLayerGroup group && group.name().matches("z:[+-]\\d+")) {
                int sign = group.name().charAt(2) == '-' ? -1 : 1;
                min = Math.min(min, sign * Integer.parseInt(group.name().substring(3)));
            }
        }
        return min;
    }

    private static String signed(int z) {
        return (z < 0 ? "-" : "+") + Math.abs(z);
    }
}
