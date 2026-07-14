package com.trojia.client.sprite;

import com.badlogic.gdx.utils.JsonReader;
import com.badlogic.gdx.utils.JsonValue;
import com.trojia.client.art.ArtMappingException;

import java.io.Reader;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.NavigableMap;
import java.util.Objects;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.regex.Pattern;

/**
 * The GL-free half of the tag-queryable sprite index (unified art spec §2 / DECISIONS.md Art
 * register pillar 3, Eli 2026-07-13 fourth revision): parses and validates
 * {@code content/art/sprites/sprite-index.json}, answers all-of tag queries over its entries,
 * and picks one deterministic sprite per living actor. One index serves BOTH actor sprites
 * and face parts — face-part pools are just tag queries over the same file.
 *
 * <p>The GL-free / GL split mirrors {@link com.trojia.client.atlas.SheetAtlasSpec} /
 * {@link com.trojia.client.atlas.SheetTileAtlas}: this class is unit-testable headless;
 * {@link SpriteSheet} slices the actual texture from it at boot.
 *
 * <p><b>Validation is boot-fatal</b> (TILE-ART-SPEC §7.2 rule, generalized): every schema
 * defect — id format/order/uniqueness, cell bounds, tag format, weight range, an
 * {@code actorQueries} entry that resolves to no sprite — is aggregated into one
 * {@link ArtMappingException}. There is no runtime fallback: an actor type either has a
 * non-empty sprite pool at load or the observer refuses to boot.
 *
 * <p><b>Query semantics (§2.3, pinned):</b> {@link #query} returns every sprite whose tag set
 * is a <em>superset</em> of the query (all-of match), sorted ascending by id — ASCII ordinal,
 * never file order. Unknown tags are not an error; they simply match nothing.
 *
 * <p><b>Per-actor pick (§2.4, pinned):</b> {@code forActor(typeId, actorId)} indexes the
 * type's pool with {@code Math.floorMod(mix64(actorId ^ SPRITE_SALT), pool.size())}, where
 * {@link #mix64} is FACES-SPEC §4.2's pinned SplitMix64 finalizer. Pure and stateless — an
 * actor keeps its sprite for life, reproducible on any machine, never serialized.
 */
public final class SpriteIndex {

    /** {@code "SPRITEX1"} — the per-actor pick salt (unified art spec §2.4, placeholder). */
    public static final long SPRITE_SALT = 0x5350524954455831L;

    private static final Pattern TOKEN = Pattern.compile("[a-z0-9_]+");

    private final String sheetPath;
    private final int tilePx;
    private final int columns;
    private final int rows;
    private final NavigableMap<String, SpriteRef> sprites;
    /** actor type id -> pre-resolved, id-ordered candidate pool (validated non-empty). */
    private final Map<String, List<SpriteRef>> actorPools;

    private SpriteIndex(String sheetPath, int tilePx, int columns, int rows,
                        NavigableMap<String, SpriteRef> sprites,
                        Map<String, List<SpriteRef>> actorPools) {
        this.sheetPath = sheetPath;
        this.tilePx = tilePx;
        this.columns = columns;
        this.rows = rows;
        this.sprites = sprites;
        this.actorPools = actorPools;
    }

    /**
     * Parses and validates a {@code sprite-index.json} document (schemaVersion 1).
     * Unknown fields are ignored ({@code provenance}/{@code notes} raws convention).
     *
     * @throws ArtMappingException aggregating every defect found, or wrapping a parse error
     */
    public static SpriteIndex load(Reader json) {
        Objects.requireNonNull(json, "json");
        JsonValue root;
        try {
            root = new JsonReader().parse(json);
        } catch (RuntimeException e) {
            throw new ArtMappingException("sprite-index: malformed JSON: " + e.getMessage(), e);
        }
        if (root == null || !root.isObject()) {
            throw new ArtMappingException("sprite-index: document is empty or not a JSON object");
        }
        List<String> errors = new ArrayList<>();

        JsonValue version = root.get("schemaVersion");
        if (version == null || !version.isNumber() || version.asInt() != 1) {
            errors.add("schemaVersion: must be 1 (found "
                    + (version == null ? "nothing" : version.toString()) + ")");
        }
        String sheetPath = readString(root, "sheet", errors);
        int tilePx = readPositiveInt(root, "tilePx", errors);
        int columns = readPositiveInt(root, "columns", errors);
        int rows = readPositiveInt(root, "rows", errors);

        NavigableMap<String, SpriteRef> sprites = parseSprites(root, columns, rows, errors);
        Map<String, List<SpriteRef>> actorPools = parseActorQueries(root, sprites, errors);

        if (!errors.isEmpty()) {
            throw new ArtMappingException(String.join("\n", errors));
        }
        return new SpriteIndex(sheetPath, tilePx, columns, rows, sprites, actorPools);
    }

    private static NavigableMap<String, SpriteRef> parseSprites(JsonValue root, int columns,
                                                                int rows, List<String> errors) {
        NavigableMap<String, SpriteRef> sprites = new TreeMap<>();
        JsonValue arr = root.get("sprites");
        if (arr == null || !arr.isArray() || arr.child == null) {
            errors.add("sprites: missing or empty (need at least one entry)");
            return sprites;
        }
        String previousId = null;
        int i = 0;
        for (JsonValue entry = arr.child; entry != null; entry = entry.next, i++) {
            String where = "sprites[" + i + "]";
            if (!entry.isObject()) {
                errors.add(where + ": must be an object");
                continue;
            }
            String id = entry.getString("id", null);
            if (id == null || !TOKEN.matcher(id).matches()) {
                errors.add(where + ": id must match [a-z0-9_]+ (found "
                        + (id == null ? "nothing" : "\"" + id + "\"") + ")");
                continue;
            }
            where = "sprites." + id;
            if (previousId != null && previousId.compareTo(id) >= 0) {
                errors.add(where + ": ids must be unique and in ascending ASCII order"
                        + " (\"" + previousId + "\" precedes it in the file)");
            }
            previousId = id;

            int w = entry.getInt("w", 1);
            int h = entry.getInt("h", 1);
            int weight = entry.getInt("weight", 1);
            if (w < 1 || h < 1) {
                errors.add(where + ": w/h must be >= 1 (found " + w + "x" + h + ")");
            }
            if (weight < 1) {
                errors.add(where + ": weight must be >= 1 (found " + weight + ")");
            }

            JsonValue cell = entry.get("cell");
            int col = -1;
            int row = -1;
            if (cell == null || !cell.isArray() || cell.size != 2
                    || !cell.child.isNumber() || !cell.child.next.isNumber()) {
                errors.add(where + ": cell must be a [col, row] pair of integers");
            } else {
                col = cell.child.asInt();
                row = cell.child.next.asInt();
                if (col < 0 || (columns > 0 && w >= 1 && col + w > columns)) {
                    errors.add(where + ": cells [" + col + ".." + (col + w - 1)
                            + "] outside sheet columns 0.." + (columns - 1));
                }
                if (row < 0 || (rows > 0 && h >= 1 && row + h > rows)) {
                    errors.add(where + ": cells [" + row + ".." + (row + h - 1)
                            + "] outside sheet rows 0.." + (rows - 1));
                }
            }

            Set<String> tags = new TreeSet<>();
            JsonValue tagsValue = entry.get("tags");
            if (tagsValue == null || !tagsValue.isArray() || tagsValue.child == null) {
                errors.add(where + ": tags must be a non-empty array");
            } else {
                for (JsonValue t = tagsValue.child; t != null; t = t.next) {
                    if (!t.isString() || !TOKEN.matcher(t.asString()).matches()) {
                        errors.add(where + ": tag must match [a-z0-9_]+ (found "
                                + t.toString() + ")");
                    } else {
                        tags.add(t.asString());
                    }
                }
            }

            if (col >= 0 && row >= 0 && !tags.isEmpty() && w >= 1 && h >= 1 && weight >= 1
                    && !sprites.containsKey(id)) {
                sprites.put(id, new SpriteRef(id, col, row, w, h, weight, tags));
            }
        }
        return sprites;
    }

    private static Map<String, List<SpriteRef>> parseActorQueries(
            JsonValue root, NavigableMap<String, SpriteRef> sprites, List<String> errors) {
        Map<String, List<SpriteRef>> pools = new LinkedHashMap<>();
        JsonValue queries = root.get("actorQueries");
        if (queries == null) {
            return pools;          // legal: a face-part-only index has no actor mapping
        }
        if (!queries.isObject()) {
            errors.add("actorQueries: must be an object of typeId -> [tags]");
            return pools;
        }
        for (JsonValue q = queries.child; q != null; q = q.next) {
            String where = "actorQueries." + q.name;
            if (!q.isArray() || q.child == null) {
                errors.add(where + ": must be a non-empty array of tags");
                continue;
            }
            Set<String> tags = new TreeSet<>();
            boolean ok = true;
            for (JsonValue t = q.child; t != null; t = t.next) {
                if (!t.isString() || !TOKEN.matcher(t.asString()).matches()) {
                    errors.add(where + ": tag must match [a-z0-9_]+ (found " + t + ")");
                    ok = false;
                } else {
                    tags.add(t.asString());
                }
            }
            if (!ok) {
                continue;
            }
            List<SpriteRef> pool = resolve(sprites, tags);
            if (pool.isEmpty()) {
                // §2.2: every entry must resolve non-empty at load — boot fails, no
                // runtime fallback (FACES-SPEC §3.2 invariant, generalized).
                errors.add(where + ": query " + tags + " matches no sprite");
            } else {
                pools.put(q.name, pool);
            }
        }
        return pools;
    }

    private static List<SpriteRef> resolve(NavigableMap<String, SpriteRef> sprites,
                                           Set<String> tags) {
        List<SpriteRef> hits = new ArrayList<>();
        for (SpriteRef ref : sprites.values()) {   // TreeMap walk = ascending id order
            if (ref.tags().containsAll(tags)) {
                hits.add(ref);
            }
        }
        return List.copyOf(hits);
    }

    private static String readString(JsonValue obj, String field, List<String> errors) {
        JsonValue v = obj.get(field);
        if (v == null || !v.isString() || v.asString().isBlank()) {
            errors.add(field + ": must be a non-blank string");
            return null;
        }
        return v.asString();
    }

    private static int readPositiveInt(JsonValue obj, String field, List<String> errors) {
        JsonValue v = obj.get(field);
        if (v == null || !v.isNumber() || v.asInt() <= 0) {
            errors.add(field + ": must be a positive integer (found "
                    + (v == null ? "nothing" : v.toString()) + ")");
            return 0;
        }
        return v.asInt();
    }

    /**
     * Every sprite whose tag set is a superset of {@code tags} (all-of match), sorted
     * ascending by id (ASCII ordinal — never file order); unmodifiable, possibly empty.
     * Unknown tags match nothing. Callers wanting one-of-many pick
     * {@code candidates.get(Math.floorMod(mix64(seed), candidates.size()))} (§2.3).
     *
     * @throws IllegalArgumentException if {@code tags} is null or empty
     */
    public List<SpriteRef> query(Set<String> tags) {
        if (tags == null || tags.isEmpty()) {
            throw new IllegalArgumentException("query tags must be non-empty");
        }
        return resolve(sprites, tags);
    }

    /**
     * The sprite actor {@code actorId} of type {@code typeId} wears for life: the type's
     * {@code actorQueries} pool indexed by
     * {@code Math.floorMod(mix64(actorId ^ SPRITE_SALT), pool.size())}. Pure and stateless.
     *
     * @throws IllegalArgumentException if {@code typeId} has no {@code actorQueries} entry
     *                                  (the load-time guarantee makes this a programming
     *                                  error, e.g. a scenario spawning an unmapped type)
     */
    public SpriteRef forActor(String typeId, long actorId) {
        List<SpriteRef> pool = actorPools.get(typeId);
        if (pool == null) {
            throw new IllegalArgumentException("no actorQueries entry for actor type \""
                    + typeId + "\" (have " + actorPools.keySet() + ")");
        }
        return pool.get(Math.floorMod(mix64(actorId ^ SPRITE_SALT), pool.size()));
    }

    /** Actor type ids carrying an {@code actorQueries} pool; unmodifiable. */
    public Set<String> actorTypeIds() {
        return Set.copyOf(actorPools.keySet());
    }

    /** The sprite with exactly this id, or {@code null}. */
    public SpriteRef byId(String id) {
        return id == null ? null : sprites.get(id);
    }

    /** All entries in ascending id order; unmodifiable. */
    public List<SpriteRef> all() {
        return List.copyOf(sprites.values());
    }

    /** The sheet image path, relative to the {@code content/} root. */
    public String sheetPath() {
        return sheetPath;
    }

    /** Cell edge length in sheet pixels (16 in v0). */
    public int tilePx() {
        return tilePx;
    }

    /** Sheet grid width in cells. */
    public int columns() {
        return columns;
    }

    /** Sheet grid height in cells. */
    public int rows() {
        return rows;
    }

    /**
     * FACES-SPEC §4.2's pinned SplitMix64 finalizer — the one deterministic mixer every
     * art-side pick shares (per-actor sprite pick here; FaceGen draws; §2.3 variety
     * tiebreaks). Stateless; never touches the engine {@code RandomSource} stream.
     */
    public static long mix64(long z) {
        z ^= z >>> 30;
        z *= 0xBF58476D1CE4E5B9L;
        z ^= z >>> 27;
        z *= 0x94D049BB133111EBL;
        z ^= z >>> 31;
        return z;
    }
}
