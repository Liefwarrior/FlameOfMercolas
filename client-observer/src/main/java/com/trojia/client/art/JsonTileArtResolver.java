package com.trojia.client.art;

import com.badlogic.gdx.utils.JsonReader;
import com.badlogic.gdx.utils.JsonValue;

import java.io.Reader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

/**
 * {@link TileArtResolver} backed by {@code content/art/placeholder/art-mapping.json}
 * (schema: TILE-ART-SPEC section 7.1). GL-free: parses with libGDX's
 * {@link JsonReader}, which needs no graphics context — construction and every query
 * work headless.
 *
 * <p>Fallback resolution happens once at load, never at render (TILE-ART-SPEC
 * section 7.3): each material's {@code byAppearance} arrays are expanded to exactly
 * {@value #BUCKETS} entries with the clamp-high rule, so {@link #regionName} is two map
 * reads and an array index. Unknown forms fall back to the {@code block} entry at query
 * time (the form vocabulary is open — adding a {@code TileForm} value never blocks on
 * art); unknown materials resolve to {@link #missingRegionName()}.
 *
 * <p>Load-time validation (TILE-ART-SPEC section 7.2, "boot fails") aggregates
 * <em>every</em> defect into one {@link ArtMappingException}. Checks that need the GL
 * side — atlas file existence and region names resolving in {@code AtlasRegionTable} —
 * are deferred to that boot step; {@link #referencedRegionNames()} hands it the exact
 * name set to verify. Two schema extensions beyond the section 7.1 table are read here
 * under the unknown-fields-ignored convention (other loaders skip them):
 * {@code materials.<id>.heatGlowTint} ({@code #RRGGBB}, BLESSING-QUEUE.md ruling 5 —
 * chromatis' discharge/saturation overlay tint {@code #E8842A}); {@code provenance} /
 * {@code notes} / {@code placeholderGen} are ignored per the spec.
 *
 * <p>Immutable after construction; all queries are deterministic pure functions of the
 * parsed document.
 */
public final class JsonTileArtResolver implements TileArtResolver {

    /** Dense appearance-bucket table width (buckets 0..3, TILE-ART-SPEC section 2). */
    public static final int BUCKETS = RegionNameGrammar.MAX_BUCKET + 1;

    /** Required {@code schemaVersion} (TILE-ART-SPEC section 7.1). */
    public static final int SCHEMA_VERSION = 1;

    /** Required {@code tilePx} in v0 (TILE-ART-SPEC section 4). */
    public static final int TILE_PX = 16;

    /** FLUID-lane depth range: 0..7, so {@code depthAlphaQ8} has 8 entries. */
    public static final int FLUID_DEPTHS = 8;

    private final String atlasPath;
    private final String missingRegion;
    private final int voidColorRgb;
    private final LightTintTable lightTintTable;
    private final ZPeekDimTable zPeekDimTable;
    private final Map<String, MaterialArt> materials;
    private final Map<String, FluidArt> fluids;
    private final Set<String> referencedRegionNames;

    private JsonTileArtResolver(String atlasPath, String missingRegion, int voidColorRgb,
                                LightTintTable lightTintTable, ZPeekDimTable zPeekDimTable,
                                Map<String, MaterialArt> materials, Map<String, FluidArt> fluids,
                                Set<String> referencedRegionNames) {
        this.atlasPath = atlasPath;
        this.missingRegion = missingRegion;
        this.voidColorRgb = voidColorRgb;
        this.lightTintTable = lightTintTable;
        this.zPeekDimTable = zPeekDimTable;
        this.materials = Map.copyOf(materials);
        this.fluids = Map.copyOf(fluids);
        this.referencedRegionNames = Set.copyOf(referencedRegionNames);
    }

    /**
     * Parses and validates a mapping document.
     *
     * @param json the full {@code art-mapping.json} text
     * @return the immutable resolver
     * @throws ArtMappingException listing every validation failure (one per line), or
     *                             wrapping the parser error if the document is not JSON
     */
    public static JsonTileArtResolver parse(String json) {
        JsonValue root;
        try {
            root = new JsonReader().parse(json);
        } catch (RuntimeException e) {
            throw new ArtMappingException("art-mapping: malformed JSON: " + e.getMessage(), e);
        }
        return fromRoot(root);
    }

    /**
     * Parses and validates a mapping document from a character stream.
     *
     * @param reader the {@code art-mapping.json} content; not closed by this method
     * @return the immutable resolver
     * @throws ArtMappingException listing every validation failure (one per line), or
     *                             wrapping the parser error if the document is not JSON
     */
    public static JsonTileArtResolver parse(Reader reader) {
        JsonValue root;
        try {
            root = new JsonReader().parse(reader);
        } catch (RuntimeException e) {
            throw new ArtMappingException("art-mapping: malformed JSON: " + e.getMessage(), e);
        }
        return fromRoot(root);
    }

    private static JsonTileArtResolver fromRoot(JsonValue root) {
        List<String> errors = new ArrayList<>();
        if (root == null || !root.isObject()) {
            throw new ArtMappingException("art-mapping: document is empty or not a JSON object");
        }

        JsonValue version = root.get("schemaVersion");
        if (version == null || !version.isNumber() || version.asInt() != SCHEMA_VERSION) {
            errors.add("schemaVersion: must be " + SCHEMA_VERSION
                    + " (found " + (version == null ? "nothing" : version.toString()) + ")");
        }
        JsonValue tile = root.get("tilePx");
        if (tile == null || !tile.isNumber() || tile.asInt() != TILE_PX) {
            errors.add("tilePx: must be " + TILE_PX + " in v0"
                    + " (found " + (tile == null ? "nothing" : tile.toString()) + ")");
        }

        String atlasPath = readRequiredString(root, "atlas", errors);
        String missingRegion = readRequiredString(root, "missingRegion", errors);
        if (missingRegion == null) {
            missingRegion = "missing"; // placeholder so later baking cannot NPE; boot still fails
        }
        int voidColor = readColor(root.get("voidColor"), "voidColor", true, errors);

        LightTintTable lightTint = null;
        int[] lightCurve = readIntArray(root.get("lightTintQ8"), "lightTintQ8", errors);
        if (lightCurve != null) {
            try {
                lightTint = LightTintTable.fromQ8(lightCurve);
            } catch (IllegalArgumentException e) {
                errors.add(e.getMessage());
            }
        }
        ZPeekDimTable zPeekDim = null;
        int[] peekCurve = readIntArray(root.get("zPeekDimQ8"), "zPeekDimQ8", errors);
        if (peekCurve != null) {
            try {
                zPeekDim = ZPeekDimTable.fromQ8(peekCurve);
            } catch (IllegalArgumentException e) {
                errors.add(e.getMessage());
            }
        }

        Map<String, MaterialArt> materials = new HashMap<>();
        JsonValue materialsNode = root.get("materials");
        if (materialsNode != null && materialsNode.isObject()) {
            for (JsonValue mat = materialsNode.child; mat != null; mat = mat.next) {
                MaterialArt art = parseMaterial(mat, errors);
                if (art != null) {
                    materials.put(mat.name, art);
                }
            }
        } else if (materialsNode != null) {
            errors.add("materials: must be an object");
        }

        Map<String, FluidArt> fluids = new HashMap<>();
        JsonValue fluidsNode = root.get("fluids");
        if (fluidsNode != null && fluidsNode.isObject()) {
            for (JsonValue fluid = fluidsNode.child; fluid != null; fluid = fluid.next) {
                FluidArt art = parseFluid(fluid, errors);
                if (art != null) {
                    fluids.put(fluid.name, art);
                }
            }
        } else if (fluidsNode != null) {
            errors.add("fluids: must be an object");
        }

        if (!errors.isEmpty()) {
            throw new ArtMappingException(String.join("\n", errors));
        }

        Set<String> referenced = new TreeSet<>();
        referenced.add(missingRegion);
        for (MaterialArt art : materials.values()) {
            for (String[] buckets : art.formsToBuckets.values()) {
                for (String name : buckets) {
                    referenced.add(name);
                }
            }
        }
        for (FluidArt art : fluids.values()) {
            referenced.add(art.region);
        }

        return new JsonTileArtResolver(atlasPath, missingRegion, voidColor,
                lightTint, zPeekDim, materials, fluids, referenced);
    }

    private static MaterialArt parseMaterial(JsonValue mat, List<String> errors) {
        String where = "materials." + mat.name;
        if (!mat.isObject()) {
            errors.add(where + ": must be an object");
            return null;
        }
        Map<String, String[]> forms = new HashMap<>();
        JsonValue formsNode = mat.get("forms");
        if (formsNode == null || !formsNode.isObject() || formsNode.child == null) {
            errors.add(where + ".forms: missing or empty (at least one form entry required)");
        } else {
            for (JsonValue form = formsNode.child; form != null; form = form.next) {
                String formKey = form.name.toLowerCase(Locale.ROOT);
                String[] buckets = parseByAppearance(form, where + ".forms." + form.name, errors);
                if (buckets != null) {
                    forms.put(formKey, buckets);
                }
            }
        }

        int minLight = 0;
        JsonValue minLightNode = mat.get("minLight");
        if (minLightNode != null) {
            if (!minLightNode.isNumber() || minLightNode.asInt() < 0
                    || minLightNode.asInt() >= LightTintTable.LEVELS) {
                errors.add(where + ".minLight: must be an int 0.." + (LightTintTable.LEVELS - 1)
                        + " (found " + minLightNode.toString() + ")");
            } else {
                minLight = minLightNode.asInt();
            }
        }

        int heatGlowTint = readColor(mat.get("heatGlowTint"), where + ".heatGlowTint",
                false, errors);

        return new MaterialArt(forms, minLight, heatGlowTint);
    }

    private static String[] parseByAppearance(JsonValue form, String where, List<String> errors) {
        if (!form.isObject()) {
            errors.add(where + ": must be an object");
            return null;
        }
        JsonValue arr = form.get("byAppearance");
        if (arr == null || !arr.isArray()) {
            errors.add(where + ".byAppearance: missing or not an array");
            return null;
        }
        int len = arr.size;
        if (len < 1 || len > BUCKETS) {
            errors.add(where + ".byAppearance: length " + len + " outside 1.." + BUCKETS);
            return null;
        }
        String[] names = new String[len];
        int i = 0;
        for (JsonValue entry = arr.child; entry != null; entry = entry.next, i++) {
            if (!entry.isString() || entry.asString().isBlank()) {
                errors.add(where + ".byAppearance[" + i + "]: must be a non-blank region name");
                return null;
            }
            names[i] = entry.asString();
        }
        String[] baked = new String[BUCKETS];
        for (int b = 0; b < BUCKETS; b++) {
            baked[b] = names[Math.min(b, len - 1)]; // clamp high, never wrap (section 2)
        }
        return baked;
    }

    private static FluidArt parseFluid(JsonValue fluid, List<String> errors) {
        String where = "fluids." + fluid.name;
        if (!fluid.isObject()) {
            errors.add(where + ": must be an object");
            return null;
        }
        String region = readRequiredString(fluid, "region", errors, where);
        int[] alpha = readIntArray(fluid.get("depthAlphaQ8"), where + ".depthAlphaQ8", errors);
        boolean shapeOk = true;
        if (alpha == null) {
            shapeOk = false;
        } else if (alpha.length != FLUID_DEPTHS) {
            errors.add(where + ".depthAlphaQ8: length " + alpha.length
                    + " (expected " + FLUID_DEPTHS + ")");
            shapeOk = false;
        } else {
            if (alpha[0] != 0) {
                errors.add(where + ".depthAlphaQ8[0] = " + alpha[0]
                        + " (must be 0 — depth 0 renders nothing)");
                shapeOk = false;
            }
            int prev = 0;
            for (int i = 0; i < alpha.length; i++) {
                if (alpha[i] < 0 || alpha[i] > 256) {
                    errors.add(where + ".depthAlphaQ8[" + i + "] = " + alpha[i]
                            + " outside 0..256");
                    shapeOk = false;
                }
                if (alpha[i] < prev) {
                    errors.add(where + ".depthAlphaQ8[" + i + "] = " + alpha[i]
                            + " decreases (previous " + prev
                            + "); curve must be monotone non-decreasing");
                    shapeOk = false;
                }
                prev = alpha[i];
            }
        }
        if (region == null || !shapeOk) {
            return null;
        }
        return new FluidArt(region, alpha);
    }

    private static String readRequiredString(JsonValue obj, String field, List<String> errors) {
        return readRequiredString(obj, field, errors, null);
    }

    private static String readRequiredString(JsonValue obj, String field, List<String> errors,
                                             String parent) {
        String where = parent == null ? field : parent + "." + field;
        JsonValue v = obj.get(field);
        if (v == null || !v.isString() || v.asString().isBlank()) {
            errors.add(where + ": must be a non-blank string");
            return null;
        }
        return v.asString();
    }

    private static int[] readIntArray(JsonValue v, String where, List<String> errors) {
        if (v == null || !v.isArray()) {
            errors.add(where + ": missing or not an array");
            return null;
        }
        try {
            return v.asIntArray();
        } catch (RuntimeException e) {
            errors.add(where + ": entries must all be integers (" + e.getMessage() + ")");
            return null;
        }
    }

    /**
     * Parses {@code #RRGGBB}; returns {@link TileArtResolver#NO_TINT} when the node is
     * absent and the field is optional.
     */
    private static int readColor(JsonValue v, String where, boolean required,
                                 List<String> errors) {
        if (v == null) {
            if (required) {
                errors.add(where + ": missing (expected \"#RRGGBB\")");
            }
            return NO_TINT;
        }
        if (v.isString()) {
            String s = v.asString();
            if (s.length() == 7 && s.charAt(0) == '#') {
                try {
                    return Integer.parseInt(s.substring(1), 16);
                } catch (NumberFormatException ignored) {
                    // falls through to the error below
                }
            }
        }
        errors.add(where + ": must be \"#RRGGBB\" (found " + v.toString() + ")");
        return NO_TINT;
    }

    // ------------------------------------------------------------------ queries

    @Override
    public String regionName(String materialId, String form, int appearanceBucket) {
        requireNonBlank(materialId, "materialId");
        requireNonBlank(form, "form");
        if (appearanceBucket < 0) {
            throw new IllegalArgumentException("appearanceBucket " + appearanceBucket + " < 0");
        }
        MaterialArt art = materials.get(materialId);
        if (art == null) {
            return missingRegion;
        }
        String[] buckets = art.formsToBuckets.get(form.toLowerCase(Locale.ROOT));
        if (buckets == null) {
            buckets = art.formsToBuckets.get(RegionNameGrammar.DEFAULT_FORM);
        }
        if (buckets == null) {
            return missingRegion;
        }
        return buckets[Math.min(appearanceBucket, RegionNameGrammar.MAX_BUCKET)];
    }

    @Override
    public String missingRegionName() {
        return missingRegion;
    }

    @Override
    public int minLight(String materialId) {
        requireNonBlank(materialId, "materialId");
        MaterialArt art = materials.get(materialId);
        return art == null ? 0 : art.minLight;
    }

    @Override
    public int heatGlowTintRgb(String materialId) {
        requireNonBlank(materialId, "materialId");
        MaterialArt art = materials.get(materialId);
        return art == null ? NO_TINT : art.heatGlowTintRgb;
    }

    /** The atlas path from the mapping, relative to the {@code content/} root. */
    public String atlasPath() {
        return atlasPath;
    }

    /** Tile edge length in atlas pixels; always {@value #TILE_PX} in v0. */
    public int tilePx() {
        return TILE_PX;
    }

    /**
     * Fill color (packed {@code 0xRRGGBB}) for cells beyond
     * {@code zPeekDimTable().maxPeekDepth()} (TILE-ART-SPEC section 5.2).
     */
    public int voidColorRgb() {
        return voidColorRgb;
    }

    /** The light-level tint curve loaded from {@code lightTintQ8}. */
    public LightTintTable lightTintTable() {
        return lightTintTable;
    }

    /** The z-peek dim curve loaded from {@code zPeekDimQ8}. */
    public ZPeekDimTable zPeekDimTable() {
        return zPeekDimTable;
    }

    /**
     * The overlay region for a pooled fluid (TILE-ART-SPEC section 5.3).
     *
     * @param fluidId canonical raws fluid id
     * @return the region name, or {@link #missingRegionName()} for unmapped fluids
     * @throws IllegalArgumentException if {@code fluidId} is null or blank
     */
    public String fluidRegion(String fluidId) {
        requireNonBlank(fluidId, "fluidId");
        FluidArt art = fluids.get(fluidId);
        return art == null ? missingRegion : art.region;
    }

    /**
     * The Q8 overlay alpha for a pooled-fluid depth (TILE-ART-SPEC section 5.3).
     *
     * @param fluidId canonical raws fluid id
     * @param depth   FLUID-lane depth 0..7
     * @return the alpha factor 0..256; 0 for unmapped fluids (renders nothing)
     * @throws IllegalArgumentException if {@code fluidId} is null or blank, or
     *                                  {@code depth} is outside 0..7
     */
    public int fluidDepthAlphaQ8(String fluidId, int depth) {
        requireNonBlank(fluidId, "fluidId");
        if (depth < 0 || depth >= FLUID_DEPTHS) {
            throw new IllegalArgumentException(
                    "depth " + depth + " outside 0.." + (FLUID_DEPTHS - 1));
        }
        FluidArt art = fluids.get(fluidId);
        return art == null ? 0 : art.depthAlphaQ8[depth];
    }

    /**
     * Every region name this mapping can ever return — all baked {@code byAppearance}
     * entries, all fluid overlay regions, and {@link #missingRegionName()} — in
     * ascending ASCII order. The GL-side {@code AtlasRegionTable} boot check verifies
     * each resolves in the atlas (TILE-ART-SPEC section 7.2), reporting the full list
     * of missing names.
     */
    public Set<String> referencedRegionNames() {
        return referencedRegionNames;
    }

    private static void requireNonBlank(String value, String what) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException(what + " must be non-blank");
        }
    }

    /** Per-material baked lookup: form token &rarr; region name per bucket 0..3. */
    private static final class MaterialArt {
        final Map<String, String[]> formsToBuckets;
        final int minLight;
        final int heatGlowTintRgb;

        MaterialArt(Map<String, String[]> formsToBuckets, int minLight, int heatGlowTintRgb) {
            this.formsToBuckets = formsToBuckets;
            this.minLight = minLight;
            this.heatGlowTintRgb = heatGlowTintRgb;
        }
    }

    /** Per-fluid overlay data (TILE-ART-SPEC section 5.3). */
    private static final class FluidArt {
        final String region;
        final int[] depthAlphaQ8;

        FluidArt(String region, int[] depthAlphaQ8) {
            this.region = region;
            this.depthAlphaQ8 = depthAlphaQ8;
        }
    }
}
