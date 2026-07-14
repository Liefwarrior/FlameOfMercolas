package com.trojia.tools.importer;

import com.trojia.sim.fluid.FluidRegistry;
import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.world.ChunkWriter;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.World;
import com.trojia.sim.world.WorldBuilder;
import com.trojia.sim.world.WorldConfig;
import com.trojia.sim.world.io.ChunkCodec;
import com.trojia.sim.world.io.TrojSav;
import com.trojia.sim.world.io.WorldSaver;
import com.trojia.tools.tmx.TmxLayer;
import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxMap;
import com.trojia.tools.tmx.TmxTileLayer;
import com.trojia.tools.tmx.TmxTileset;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The M1 importer: bakes a hand-authored Tiled map into a real, loadable world
 * whose tick-0 TROJSAV <em>is</em> that map (ARCHITECTURE.md §1.1 #17, §8, §9).
 *
 * <p><b>Collapse (ruling #17, authoritative).</b> Per cell, across every
 * z-level: a filled {@code terrain} tile wins as {@code (fillMaterial,
 * fillForm)}; else a {@code floor} tile becomes {@code (floorMaterial,
 * FLOOR)} (the floor tile's own form is ignored); else the cell is left OPEN
 * (air) — OPEN is authored by leaving both tile sublayers empty. The optional
 * {@code fluids} sublayer seeds the FLUID lane: each fluid tile's registry raw
 * id and authored depth (1..7) are packed as {@code depth | (fluidId << 3)}
 * (lane layout per {@code Lanes.FLUID}: depth bits 0–2, fluidId bits 3–5; the
 * SETTLED bit 6 is left clear — a freshly seeded pool has not been settled by
 * the fluid system, which owns that bit). The {@code markers} object layer is
 * parsed but not baked here (marker baking is deferred). Superseded-history
 * note: fluid seeding itself was deferred until the faux-3D composite render
 * pass (2026-07-13) needed the docks harbor to render as water, not void.
 *
 * <p><b>Placement.</b> The map is baked at the origin of the first interior
 * chunk (just inside the permanent VOID border): map cell {@code (x, y)} at
 * z-level {@code z} lands at world tile {@code (CHUNK_SIZE_X + x,
 * CHUNK_SIZE_Y + y, CHUNK_SIZE_Z + (z - minZ))}. The {@link WorldConfig} is
 * sized to the smallest chunk box that holds the map plus that border.
 *
 * <p><b>Determinism.</b> Cells are written in z-group document order, then
 * row-major; the emitted TROJSAV is byte-deterministic (the same map always
 * bakes to the same bytes). This importer is pure: it holds no state and reads
 * no clock.
 */
public final class TiledWorldImporter {

    /** Pattern of a legal z-group name: {@code z:} + explicit sign + digits. */
    private static final Pattern Z_NAME = Pattern.compile("z:([+-])(\\d+)");
    private static final String TERRAIN = "terrain";
    private static final String FLOOR = "floor";
    private static final String FLUIDS = "fluids";

    /** Bit shift of the FLUID lane's fluidId field ({@code Lanes.FLUID}: bits 3–5). */
    private static final int FLUID_ID_SHIFT = 3;

    /** Creates a stateless importer. */
    public TiledWorldImporter() {
    }

    /**
     * Bakes {@code map} into a fresh in-memory world at tick 0.
     *
     * @param map      the parsed map (exactly one external tileset; z-group layout per README)
     * @param tileset  the parsed external tileset the map's gids index into
     * @param registry the material universe the {@code material=} ids resolve against
     * @param fluids   the fluid universe the {@code fluid=} ids resolve against
     * @return the baked world (all interior chunks concrete, tick 0, uncommitted — save-legal)
     * @throws NullPointerException if an argument is {@code null}
     * @throws TiledImportException on any authoring/binding error (unknown material or fluid,
     *                              bad z-group, a non-material tile on terrain/floor, a
     *                              non-fluid tile on fluids, or a rejected write)
     */
    public TickableWorld importWorld(TmxMap map, TmxTileset tileset, MaterialRegistry registry,
            FluidRegistry fluids) {
        Objects.requireNonNull(map, "map");
        Objects.requireNonNull(tileset, "tileset");
        Objects.requireNonNull(registry, "registry");
        Objects.requireNonNull(fluids, "fluids");
        if (map.tilesets().size() != 1) {
            throw new TiledImportException("v0 maps must reference exactly one external tileset, found "
                    + map.tilesets().size());
        }
        int firstGid = map.tilesets().get(0).firstGid();

        List<ZGroup> groups = zGroups(map);
        if (groups.isEmpty()) {
            throw new TiledImportException("map has no z-group layers (expected groups named z:<sign><n>)");
        }
        int minZ = Integer.MAX_VALUE;
        int maxZ = Integer.MIN_VALUE;
        for (ZGroup group : groups) {
            minZ = Math.min(minZ, group.z());
            maxZ = Math.max(maxZ, group.z());
        }
        int zRange = maxZ - minZ + 1;

        int interiorChunksX = ceilDiv(map.width(), Coords.CHUNK_SIZE_X);
        int interiorChunksY = ceilDiv(map.height(), Coords.CHUNK_SIZE_Y);
        int interiorChunksZ = ceilDiv(zRange, Coords.CHUNK_SIZE_Z);
        WorldConfig config;
        try {
            config = new WorldConfig(interiorChunksX + 2, interiorChunksY + 2, interiorChunksZ + 2);
        } catch (IllegalArgumentException e) {
            throw new TiledImportException("map does not fit a v0 world: " + e.getMessage());
        }
        TickableWorld world = WorldBuilder.create(config).build();

        MaterialBinding binding = new MaterialBinding(registry, fluids, tileset, firstGid);
        ChunkWriter writer = world.writer();
        for (ZGroup group : groups) {
            int worldZ = Coords.CHUNK_SIZE_Z + (group.z() - minZ);
            bakeGroup(group, worldZ, binding, writer);
        }
        return world;
    }

    /**
     * Serializes {@code world} into a tick-0 TROJSAV: the world-owned META and
     * WRLD sections (ARCHITECTURE.md §9). System sections are absent — a
     * freshly imported world carries no system state. Byte-deterministic.
     *
     * @param world           the baked world
     * @param rawsFingerprint the loaded raws' fingerprint (the save's raws guard)
     * @return the assembled container, ready for {@link TrojSav#writeTo}
     * @throws NullPointerException if {@code world} is {@code null}
     * @throws IOException          if a world section cannot be rendered
     */
    public TrojSav toTrojSav(World world, long rawsFingerprint) throws IOException {
        Objects.requireNonNull(world, "world");
        WorldSaver saver = new WorldSaver(new ChunkCodec(world.lanes()));
        TrojSav save = TrojSav.create(
                new TrojSav.Header(TrojSav.FORMAT_VERSION, 0L, 0L, rawsFingerprint));
        save.putSection(TrojSav.META, saver.writeMetaSection(world));
        save.putSection(TrojSav.WRLD, saver.writeWorldSection(world));
        return save;
    }

    private void bakeGroup(ZGroup group, int worldZ, MaterialBinding binding, ChunkWriter writer) {
        TmxTileLayer terrain = group.terrain();
        TmxTileLayer floor = group.floor();
        TmxTileLayer fluids = group.fluids();
        int width = group.width();
        int height = group.height();
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int fillGid = terrain != null ? terrain.gidAt(x, y) : 0;
                int floorGid = floor != null ? floor.gidAt(x, y) : 0;
                if (fillGid != 0) {
                    if (!binding.hasMaterial(fillGid)) {
                        throw new TiledImportException("terrain cell (" + x + "," + y + ") in "
                                + group.name() + " references non-material tile gid " + fillGid
                                + " (fluid tiles are illegal on terrain/floor)");
                    }
                    TileForm form = binding.form(fillGid);
                    if (form != TileForm.OPEN) {
                        // an authored OPEN fill is air — leave the material/form cell empty,
                        // but (unlike a `continue` here) still fall through below so an
                        // authored fluid tile at this same (x,y) is not silently dropped.
                        apply(writer,
                                PackedPos.pack(Coords.CHUNK_SIZE_X + x, Coords.CHUNK_SIZE_Y + y, worldZ),
                                binding.materialRaw(fillGid), form, group, x, y);
                    }
                } else if (floorGid != 0) {
                    if (!binding.hasMaterial(floorGid)) {
                        throw new TiledImportException("floor cell (" + x + "," + y + ") in "
                                + group.name() + " references non-material tile gid " + floorGid);
                    }
                    apply(writer, PackedPos.pack(Coords.CHUNK_SIZE_X + x, Coords.CHUNK_SIZE_Y + y, worldZ),
                            binding.materialRaw(floorGid), TileForm.FLOOR, group, x, y);
                }
                // else: no fill and no floor -> OPEN (the fresh interior chunk is already OPEN)

                int fluidGid = fluids != null ? fluids.gidAt(x, y) : 0;
                if (fluidGid != 0) {
                    if (!binding.hasFluid(fluidGid)) {
                        throw new TiledImportException("fluids cell (" + x + "," + y + ") in "
                                + group.name() + " references non-fluid tile gid " + fluidGid
                                + " (material tiles are illegal on the fluids sublayer)");
                    }
                    applyFluid(writer,
                            PackedPos.pack(Coords.CHUNK_SIZE_X + x, Coords.CHUNK_SIZE_Y + y, worldZ),
                            binding.fluidDepth(fluidGid)
                                    | (binding.fluidRaw(fluidGid) << FLUID_ID_SHIFT),
                            group, x, y);
                }
            }
        }
    }

    private static void apply(ChunkWriter writer, int packedPos, short materialRaw, TileForm form,
            ZGroup group, int x, int y) {
        int status = writer.setMaterialAndForm(packedPos, materialRaw, form);
        if (status != ChunkWriter.APPLIED) {
            throw new TiledImportException("write rejected (writer status " + status + ") for cell ("
                    + x + "," + y + ") in " + group.name()
                    + " — the map does not fit the interior chunk box");
        }
    }

    private static void applyFluid(ChunkWriter writer, int packedPos, int fluidBits,
            ZGroup group, int x, int y) {
        int status = writer.setFluidBits(packedPos, fluidBits);
        if (status != ChunkWriter.APPLIED) {
            throw new TiledImportException("fluid write rejected (writer status " + status
                    + ") for cell (" + x + "," + y + ") in " + group.name()
                    + " — the map does not fit the interior chunk box");
        }
    }

    private static List<ZGroup> zGroups(TmxMap map) {
        List<ZGroup> groups = new ArrayList<>();
        for (TmxLayer layer : map.layers()) {
            if (!(layer instanceof TmxLayerGroup group)) {
                continue;
            }
            Integer z = zOf(group.name());
            if (z == null) {
                continue;
            }
            TmxTileLayer terrain = tileSublayer(group, TERRAIN);
            TmxTileLayer floor = tileSublayer(group, FLOOR);
            TmxTileLayer fluids = tileSublayer(group, FLUIDS);
            if (terrain == null && floor == null) {
                throw new TiledImportException("z-group " + group.name()
                        + " has neither a terrain nor a floor tile sublayer");
            }
            int width = terrain != null ? terrain.width() : floor.width();
            int height = terrain != null ? terrain.height() : floor.height();
            groups.add(new ZGroup(group.name(), z, terrain, floor, fluids, width, height));
        }
        return groups;
    }

    private static TmxTileLayer tileSublayer(TmxLayerGroup group, String name) {
        for (TmxLayer layer : group.layers()) {
            if (layer instanceof TmxTileLayer tiles && tiles.name().equals(name)) {
                return tiles;
            }
        }
        return null;
    }

    /**
     * Parses a z-group name to its signed z value, or {@code null} when the name
     * is not a legal z-group name. {@code z:-0} is rejected (street level is
     * written {@code z:+0}).
     */
    private static Integer zOf(String groupName) {
        Matcher m = Z_NAME.matcher(groupName);
        if (!m.matches()) {
            return null;
        }
        int magnitude;
        try {
            magnitude = Integer.parseInt(m.group(2));
        } catch (NumberFormatException e) {
            return null;
        }
        boolean negative = m.group(1).equals("-");
        if (negative && magnitude == 0) {
            return null;
        }
        return negative ? -magnitude : magnitude;
    }

    private static int ceilDiv(int a, int b) {
        return (a + b - 1) / b;
    }

    /** One z-level's baking inputs: its z value and its three tile sublayers. */
    private record ZGroup(String name, int z, TmxTileLayer terrain, TmxTileLayer floor,
            TmxTileLayer fluids, int width, int height) {
    }
}
