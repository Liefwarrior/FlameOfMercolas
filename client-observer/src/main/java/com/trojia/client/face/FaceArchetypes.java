package com.trojia.client.face;

import com.badlogic.gdx.utils.JsonReader;
import com.badlogic.gdx.utils.JsonValue;
import com.trojia.client.art.ArtMappingException;

import java.io.Reader;
import java.util.ArrayList;
import java.util.EnumMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Pattern;

/**
 * GL-free parse of {@code content/art/faces/face-archetypes.json} — the hand-authored
 * archetype table (unified art spec §4.6 / FACES-SPEC §3.3, retained) plus the
 * {@code actorArchetypes} actor-type &rarr; archetype map. Beast types ({@code animal},
 * {@code feral}) are deliberately absent from that map — beasts show their actor sprite in
 * the inspector, never a generated face.
 *
 * <p>Validation is boot-fatal and aggregated into one {@link ArtMappingException}
 * (TILE-ART-SPEC §7.2 rule, generalized): archetype/actor-type id format, unknown
 * {@link HeadwearClass} names, non-positive headwear weights, negative tag multipliers, an
 * {@code actorArchetypes} value naming a missing archetype. Pool <em>coverage</em> (every
 * archetype &times; reachable class &times; slot resolves non-empty) is validated against a
 * concrete part index by {@link FaceGen#validateCoverage()}, not here — it needs the parts.
 * Immutable after construction.
 */
public final class FaceArchetypes {

    private static final Pattern TOKEN = Pattern.compile("[a-z0-9_]+");

    private final Map<String, FaceArchetype> archetypes;
    private final Map<String, String> actorArchetypes;

    private FaceArchetypes(Map<String, FaceArchetype> archetypes,
                           Map<String, String> actorArchetypes) {
        this.archetypes = archetypes;
        this.actorArchetypes = actorArchetypes;
    }

    /**
     * Parses and validates a {@code face-archetypes.json} document (schemaVersion 1).
     * Unknown fields are ignored ({@code provenance}/{@code notes} raws convention).
     *
     * @throws ArtMappingException aggregating every defect found, or wrapping a parse error
     */
    public static FaceArchetypes load(Reader json) {
        Objects.requireNonNull(json, "json");
        JsonValue root;
        try {
            root = new JsonReader().parse(json);
        } catch (RuntimeException e) {
            throw new ArtMappingException("face-archetypes: malformed JSON: " + e.getMessage(), e);
        }
        if (root == null || !root.isObject()) {
            throw new ArtMappingException("face-archetypes: document is empty or not an object");
        }
        List<String> errors = new ArrayList<>();

        JsonValue version = root.get("schemaVersion");
        if (version == null || !version.isNumber() || version.asInt() != 1) {
            errors.add("schemaVersion: must be 1 (found "
                    + (version == null ? "nothing" : version.toString()) + ")");
        }

        Map<String, FaceArchetype> archetypes = parseArchetypes(root, errors);
        Map<String, String> actorArchetypes = parseActorArchetypes(root, archetypes, errors);

        if (!errors.isEmpty()) {
            throw new ArtMappingException(String.join("\n", errors));
        }
        return new FaceArchetypes(archetypes, actorArchetypes);
    }

    private static Map<String, FaceArchetype> parseArchetypes(JsonValue root,
                                                              List<String> errors) {
        Map<String, FaceArchetype> out = new TreeMap<>();
        JsonValue arr = root.get("archetypes");
        if (arr == null || !arr.isObject() || arr.child == null) {
            errors.add("archetypes: missing or empty (need at least one archetype)");
            return out;
        }
        for (JsonValue a = arr.child; a != null; a = a.next) {
            String where = "archetypes." + a.name;
            if (!TOKEN.matcher(a.name).matches()) {
                errors.add(where + ": id must match [a-z0-9_]+");
                continue;
            }
            if (!a.isObject()) {
                errors.add(where + ": must be an object");
                continue;
            }
            Map<HeadwearClass, Integer> weights = new EnumMap<>(HeadwearClass.class);
            JsonValue hw = a.get("headwearWeights");
            if (hw == null || !hw.isObject() || hw.child == null) {
                errors.add(where + ".headwearWeights: missing or empty");
            } else {
                for (JsonValue w = hw.child; w != null; w = w.next) {
                    HeadwearClass cls = headwearClass(w.name);
                    if (cls == null) {
                        errors.add(where + ".headwearWeights." + w.name
                                + ": unknown headwear class");
                    } else if (!w.isNumber() || w.asInt() < 1) {
                        errors.add(where + ".headwearWeights." + w.name
                                + ": weight must be an integer >= 1 (found " + w + ")");
                    } else {
                        weights.put(cls, w.asInt());
                    }
                }
            }
            Map<String, Integer> multipliers = new TreeMap<>();
            JsonValue tm = a.get("tagMultipliers");
            if (tm != null && tm.isObject()) {
                for (JsonValue m = tm.child; m != null; m = m.next) {
                    if (!TOKEN.matcher(m.name).matches()) {
                        errors.add(where + ".tagMultipliers." + m.name
                                + ": tag must match [a-z0-9_]+");
                    } else if (!m.isNumber() || m.asInt() < 0) {
                        errors.add(where + ".tagMultipliers." + m.name
                                + ": multiplier must be an integer >= 0 (found " + m + ")");
                    } else {
                        multipliers.put(m.name, m.asInt());
                    }
                }
            } else if (tm != null) {
                errors.add(where + ".tagMultipliers: must be an object");
            }
            if (!weights.isEmpty()) {
                out.put(a.name, new FaceArchetype(a.name, weights, multipliers));
            }
        }
        return out;
    }

    private static Map<String, String> parseActorArchetypes(JsonValue root,
                                                            Map<String, FaceArchetype> archetypes,
                                                            List<String> errors) {
        Map<String, String> out = new LinkedHashMap<>();
        JsonValue map = root.get("actorArchetypes");
        if (map == null) {
            return out;            // legal: an index used only for named faces / tests
        }
        if (!map.isObject()) {
            errors.add("actorArchetypes: must be an object of actor type id -> archetype id");
            return out;
        }
        for (JsonValue e = map.child; e != null; e = e.next) {
            String where = "actorArchetypes." + e.name;
            if (!TOKEN.matcher(e.name).matches()) {
                errors.add(where + ": actor type id must match [a-z0-9_]+");
            } else if (!e.isString()) {
                errors.add(where + ": must be an archetype id string");
            } else if (!archetypes.containsKey(e.asString())) {
                errors.add(where + ": unknown archetype \"" + e.asString() + "\" (have "
                        + archetypes.keySet() + ")");
            } else {
                out.put(e.name, e.asString());
            }
        }
        return out;
    }

    private static HeadwearClass headwearClass(String name) {
        for (HeadwearClass cls : HeadwearClass.values()) {
            if (cls.name().equals(name)) {
                return cls;
            }
        }
        return null;
    }

    /**
     * The archetype with this id.
     *
     * @throws IllegalArgumentException if unknown (load-time validation makes this a
     *                                  programming error)
     */
    public FaceArchetype archetype(String id) {
        FaceArchetype a = id == null ? null : archetypes.get(id);
        if (a == null) {
            throw new IllegalArgumentException(
                    "unknown archetype \"" + id + "\" (have " + archetypes.keySet() + ")");
        }
        return a;
    }

    /** All archetype ids in ascending ASCII order; unmodifiable. */
    public Set<String> archetypeIds() {
        return Set.copyOf(archetypes.keySet());
    }

    /** All archetypes in ascending id order; unmodifiable. */
    public List<FaceArchetype> all() {
        return List.copyOf(archetypes.values());
    }

    /**
     * The archetype id for an actor type, or {@code null} when the type has no face
     * (beasts: {@code animal}, {@code feral} — spec §4.6).
     */
    public String archetypeForActorType(String actorTypeId) {
        return actorTypeId == null ? null : actorArchetypes.get(actorTypeId);
    }

    /** Actor type ids carrying a face archetype; unmodifiable, insertion-ordered. */
    public Set<String> actorTypeIds() {
        return Set.copyOf(actorArchetypes.keySet());
    }
}
