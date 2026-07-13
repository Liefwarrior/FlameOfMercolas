package com.trojia.tools.importer;

import com.trojia.sim.fluid.FluidRegistry;
import com.trojia.sim.material.Material;
import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.world.TileForm;
import com.trojia.tools.tmx.TmxProperty;
import com.trojia.tools.tmx.TmxTileset;
import com.trojia.tools.tmx.TmxTilesetTile;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * Resolves a map's global tile ids (gids) to a baked {@code (material, form)}
 * pair, built once from the single external tileset and the material registry.
 *
 * <p>The binding is precomputed per tileset-local tile at construction, so an
 * unknown {@code material=} id fails <em>fast and deterministically</em>
 * (before any world storage is touched) with a nearest-name suggestion. The
 * {@code STAIR_UP}/{@code STAIR_DOWN} authoring forms both collapse to the
 * single sim {@link TileForm#STAIR}; the up/down pairing is positional
 * (content/maps/README.md) and any directional encoding is out of scope here.
 *
 * <p>Tiles carrying a {@code fluid=} property instead of {@code material=} carry
 * no material binding ({@link #hasMaterial(int)} is {@code false}) but a
 * <em>fluid</em> binding instead ({@link #hasFluid(int)}): the {@code fluid=}
 * string resolves against the {@link FluidRegistry} to a FLUID-lane raw id and
 * the required {@code depth=} int (1..7) becomes the lane's depth field. The
 * importer bakes these from the {@code fluids} sublayer (content/maps/README.md
 * "Initial pooled fluid"); the superseded-history note in this class's git
 * history recorded fluid seeding as deferred until the faux-3D composite pass
 * needed the docks harbor to stop rendering as void (2026-07-13).
 */
public final class MaterialBinding {

    /** Maximum Levenshtein distance still offered as a suggestion. */
    private static final int MAX_SUGGESTION_DISTANCE = 3;

    /** Maximum FLUID-lane pooled depth (the lane's 3-bit depth field). */
    private static final int MAX_FLUID_DEPTH = 7;

    private final int firstGid;
    private final int tileCount;
    /** Per local-id material lane value, or {@code -1} when the tile has no material binding. */
    private final short[] materialRaw;
    /** Per local-id collapsed form, or {@code null} when the tile has no material binding. */
    private final TileForm[] form;
    /** Per local-id FLUID-lane raw fluid id, or {@code -1} when the tile has no fluid binding. */
    private final short[] fluidRaw;
    /** Per local-id fluid depth 1..7; meaningful only where {@code fluidRaw >= 0}. */
    private final byte[] fluidDepth;

    /**
     * Precomputes the gid → (material, form) and gid → (fluid, depth) tables from
     * {@code tileset}.
     *
     * @param registry the material universe the {@code material=} strings resolve against
     * @param fluids   the fluid universe the {@code fluid=} strings resolve against
     * @param tileset  the parsed external tileset carrying the per-tile properties
     * @param firstGid the map's {@code firstgid} for this tileset (gid = localId + firstGid)
     * @throws NullPointerException     if an argument is {@code null}
     * @throws IllegalArgumentException if {@code firstGid < 1}
     * @throws TiledImportException     if a tile's {@code material=} id is unknown, a
     *                                  material tile carries no/invalid {@code form=}, a
     *                                  tile's {@code fluid=} id is unknown, or a fluid tile
     *                                  carries no/out-of-range {@code depth=}
     */
    public MaterialBinding(MaterialRegistry registry, FluidRegistry fluids, TmxTileset tileset,
            int firstGid) {
        Objects.requireNonNull(registry, "registry");
        Objects.requireNonNull(fluids, "fluids");
        Objects.requireNonNull(tileset, "tileset");
        if (firstGid < 1) {
            throw new IllegalArgumentException("firstGid must be >= 1, was " + firstGid);
        }
        this.firstGid = firstGid;
        this.tileCount = tileset.tileCount();
        this.materialRaw = new short[Math.max(0, tileCount)];
        this.form = new TileForm[Math.max(0, tileCount)];
        this.fluidRaw = new short[Math.max(0, tileCount)];
        this.fluidDepth = new byte[Math.max(0, tileCount)];
        Arrays.fill(materialRaw, (short) -1);
        Arrays.fill(fluidRaw, (short) -1);
        for (TmxTilesetTile tile : tileset.tiles()) {
            int localId = tile.localId();
            if (localId < 0 || localId >= tileCount) {
                continue; // outside the declared range; the gid-bounds pass owns this
            }
            Optional<TmxProperty> material = tile.properties().find("material");
            if (material.isPresent()) {
                String id = material.get().value();
                if (!registry.contains(id)) {
                    throw new TiledImportException(unknownMaterialMessage(id, registry));
                }
                materialRaw[localId] = registry.id(id).raw();
                form[localId] = resolveForm(tile);
                continue;
            }
            Optional<TmxProperty> fluid = tile.properties().find("fluid");
            if (fluid.isEmpty()) {
                continue; // an otherwise unbound tile — the gid-usage passes own any error
            }
            String id = fluid.get().value();
            if (!fluids.contains(id)) {
                throw new TiledImportException("tile localId " + localId
                        + " has unknown fluid \"" + id + "\" (not in the fluid registry)");
            }
            fluidRaw[localId] = fluids.id(id).raw();
            fluidDepth[localId] = resolveDepth(tile);
        }
    }

    /**
     * @param gid a decoded global tile id ({@code 0} means "no tile")
     * @return {@code true} iff {@code gid} maps to a tile with a resolved material binding
     */
    public boolean hasMaterial(int gid) {
        int localId = gid - firstGid;
        return localId >= 0 && localId < tileCount && materialRaw[localId] >= 0;
    }

    /**
     * @param gid a gid with {@link #hasMaterial(int)} {@code true}
     * @return the tile's MATERIAL-lane value
     * @throws TiledImportException if {@code gid} carries no material binding
     */
    public short materialRaw(int gid) {
        requireBound(gid);
        return materialRaw[gid - firstGid];
    }

    /**
     * @param gid a gid with {@link #hasMaterial(int)} {@code true}
     * @return the tile's collapsed {@link TileForm} ({@code STAIR_*} → {@link TileForm#STAIR})
     * @throws TiledImportException if {@code gid} carries no material binding
     */
    public TileForm form(int gid) {
        requireBound(gid);
        return form[gid - firstGid];
    }

    /**
     * @param gid a decoded global tile id ({@code 0} means "no tile")
     * @return {@code true} iff {@code gid} maps to a tile with a resolved fluid binding
     */
    public boolean hasFluid(int gid) {
        int localId = gid - firstGid;
        return localId >= 0 && localId < tileCount && fluidRaw[localId] >= 0;
    }

    /**
     * @param gid a gid with {@link #hasFluid(int)} {@code true}
     * @return the tile's FLUID-lane raw fluid id
     * @throws TiledImportException if {@code gid} carries no fluid binding
     */
    public short fluidRaw(int gid) {
        requireFluidBound(gid);
        return fluidRaw[gid - firstGid];
    }

    /**
     * @param gid a gid with {@link #hasFluid(int)} {@code true}
     * @return the tile's authored pooled depth, 1..7
     * @throws TiledImportException if {@code gid} carries no fluid binding
     */
    public int fluidDepth(int gid) {
        requireFluidBound(gid);
        return fluidDepth[gid - firstGid];
    }

    private void requireBound(int gid) {
        if (!hasMaterial(gid)) {
            throw new TiledImportException("gid " + gid + " carries no material binding");
        }
    }

    private void requireFluidBound(int gid) {
        if (!hasFluid(gid)) {
            throw new TiledImportException("gid " + gid + " carries no fluid binding");
        }
    }

    private byte resolveDepth(TmxTilesetTile tile) {
        Optional<TmxProperty> value = tile.properties().find("depth");
        if (value.isEmpty()) {
            throw new TiledImportException(
                    "tile localId " + tile.localId() + " carries fluid= but no depth= property");
        }
        int depth;
        try {
            depth = Integer.parseInt(value.get().value());
        } catch (NumberFormatException e) {
            throw new TiledImportException("tile localId " + tile.localId()
                    + " has non-integer depth \"" + value.get().value() + "\"");
        }
        if (depth < 1 || depth > MAX_FLUID_DEPTH) {
            throw new TiledImportException("tile localId " + tile.localId() + " has depth "
                    + depth + " outside 1.." + MAX_FLUID_DEPTH
                    + " (content/maps/README.md fluid tiles)");
        }
        return (byte) depth;
    }

    private TileForm resolveForm(TmxTilesetTile tile) {
        Optional<TmxProperty> value = tile.properties().find("form");
        if (value.isEmpty()) {
            throw new TiledImportException(
                    "tile localId " + tile.localId() + " carries material= but no form= property");
        }
        String literal = value.get().value();
        return switch (literal) {
            case "WALL" -> TileForm.WALL;
            case "FLOOR" -> TileForm.FLOOR;
            case "OPEN" -> TileForm.OPEN;
            case "RAMP" -> TileForm.RAMP;
            case "STAIR_UP", "STAIR_DOWN" -> TileForm.STAIR;
            default -> throw new TiledImportException("tile localId " + tile.localId()
                    + " has unknown form \"" + literal + "\" (expected WALL|FLOOR|OPEN|RAMP|STAIR_UP|STAIR_DOWN)");
        };
    }

    private static String unknownMaterialMessage(String id, MaterialRegistry registry) {
        List<String> keys = new ArrayList<>(registry.size());
        for (Material material : registry.all()) {
            keys.add(material.key());
        }
        String suggestion = nearest(id, keys)
                .map(s -> " (did you mean \"" + s + "\"?)")
                .orElse(" (no similar material id exists in the registry)");
        return "unknown material \"" + id + "\"" + suggestion;
    }

    /**
     * Nearest known id within {@link #MAX_SUGGESTION_DISTANCE} edits, tiebroken
     * (distance, then lexicographically) for determinism (ARCHITECTURE.md §3,
     * tools: "suggestion tiebreak (distance, lex)").
     */
    private static Optional<String> nearest(String unknown, List<String> candidates) {
        String best = null;
        int bestDistance = MAX_SUGGESTION_DISTANCE + 1;
        for (String candidate : candidates) {
            int d = levenshtein(unknown, candidate);
            if (d < bestDistance || (d == bestDistance && best != null && candidate.compareTo(best) < 0)) {
                best = candidate;
                bestDistance = d;
            }
        }
        return Optional.ofNullable(best);
    }

    /** Classic two-row Levenshtein edit distance. */
    private static int levenshtein(String a, String b) {
        int[] prev = new int[b.length() + 1];
        int[] curr = new int[b.length() + 1];
        for (int j = 0; j <= b.length(); j++) {
            prev[j] = j;
        }
        for (int i = 1; i <= a.length(); i++) {
            curr[0] = i;
            for (int j = 1; j <= b.length(); j++) {
                int substitution = prev[j - 1] + (a.charAt(i - 1) == b.charAt(j - 1) ? 0 : 1);
                curr[j] = Math.min(substitution, Math.min(prev[j] + 1, curr[j - 1] + 1));
            }
            int[] swap = prev;
            prev = curr;
            curr = swap;
        }
        return prev[b.length()];
    }
}
