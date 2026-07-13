package com.trojia.tools.importer;

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
 * no material binding ({@link #hasMaterial(int)} is {@code false}); initial
 * fluid seeding from the {@code fluids} sublayer is deferred, so those tiles are
 * simply not resolvable through this binding.
 */
public final class MaterialBinding {

    /** Maximum Levenshtein distance still offered as a suggestion. */
    private static final int MAX_SUGGESTION_DISTANCE = 3;

    private final int firstGid;
    private final int tileCount;
    /** Per local-id material lane value, or {@code -1} when the tile has no material binding. */
    private final short[] materialRaw;
    /** Per local-id collapsed form, or {@code null} when the tile has no material binding. */
    private final TileForm[] form;

    /**
     * Precomputes the gid → (material, form) table from {@code tileset}.
     *
     * @param registry the material universe the {@code material=} strings resolve against
     * @param tileset  the parsed external tileset carrying the per-tile properties
     * @param firstGid the map's {@code firstgid} for this tileset (gid = localId + firstGid)
     * @throws NullPointerException     if an argument is {@code null}
     * @throws IllegalArgumentException if {@code firstGid < 1}
     * @throws TiledImportException     if a tile's {@code material=} id is unknown, or a
     *                                  material tile carries no/invalid {@code form=}
     */
    public MaterialBinding(MaterialRegistry registry, TmxTileset tileset, int firstGid) {
        Objects.requireNonNull(registry, "registry");
        Objects.requireNonNull(tileset, "tileset");
        if (firstGid < 1) {
            throw new IllegalArgumentException("firstGid must be >= 1, was " + firstGid);
        }
        this.firstGid = firstGid;
        this.tileCount = tileset.tileCount();
        this.materialRaw = new short[Math.max(0, tileCount)];
        this.form = new TileForm[Math.max(0, tileCount)];
        Arrays.fill(materialRaw, (short) -1);
        for (TmxTilesetTile tile : tileset.tiles()) {
            int localId = tile.localId();
            if (localId < 0 || localId >= tileCount) {
                continue; // outside the declared range; the gid-bounds pass owns this
            }
            Optional<TmxProperty> material = tile.properties().find("material");
            if (material.isEmpty()) {
                continue; // a fluid (or otherwise unbound) tile — fluid seeding is deferred
            }
            String id = material.get().value();
            if (!registry.contains(id)) {
                throw new TiledImportException(unknownMaterialMessage(id, registry));
            }
            materialRaw[localId] = registry.id(id).raw();
            form[localId] = resolveForm(tile);
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

    private void requireBound(int gid) {
        if (!hasMaterial(gid)) {
            throw new TiledImportException("gid " + gid + " carries no material binding");
        }
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
