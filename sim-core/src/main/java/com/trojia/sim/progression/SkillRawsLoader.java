package com.trojia.sim.progression;

import com.trojia.sim.json.JsonArray;
import com.trojia.sim.json.JsonNumberMode;
import com.trojia.sim.json.JsonObject;
import com.trojia.sim.json.JsonParseException;
import com.trojia.sim.json.JsonString;
import com.trojia.sim.json.JsonValue;
import com.trojia.sim.json.MiniJson;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

/**
 * The fail-fast raws loader for the 16-skill table (PROGRESSION-SPEC.md
 * &sect;2), mirroring {@code com.trojia.sim.material.MaterialRawsLoader}'s
 * conventions but reading a single file: {@code content/raws/skills/skills.json}
 * (one file, unlike the one-file-per-material convention, per this pass's
 * assignment).
 *
 * <p><strong>Schema</strong> (top-level object): {@code "skills"}, an array of
 * skill objects with fields {@code id}, {@code displayName}, {@code covers},
 * {@code governingAttribute} ({@link GoverningAttribute} name),
 * {@code aptitudeTier} ({@link AptitudeTier} name). {@code canonAnchor},
 * {@code provenance} and {@code notes} are documentation-only and ignored,
 * exactly like the materials raws' {@code provenance}/{@code notes}
 * convention.</p>
 *
 * <p>Loading is a pure function of the raws bytes: identical raws yield
 * identical id assignments on every platform.</p>
 */
public final class SkillRawsLoader {

    private static final String SKILLS_FILE = "skills/skills.json";

    /**
     * {@code "id"} is this document's own raw-level identifier (required by the
     * project-wide {@code RawsRoundTripTest} convention that every top-level
     * raws JSON object declares an id); it is otherwise unused by this loader
     * since {@link SkillId}s are assigned per-entry from {@code "skills"}.
     */
    private static final List<String> TOP_FIELDS = List.of("id", "skills", "notes", "provenance");
    private static final List<String> SKILL_FIELDS = List.of(
            "id", "displayName", "covers", "governingAttribute", "aptitudeTier",
            "canonAnchor", "provenance", "notes");
    private static final List<String> IGNORED_FIELDS = List.of("canonAnchor", "provenance", "notes");

    private SkillRawsLoader() {
    }

    /**
     * Loads and validates {@code skills/skills.json} under a raws root (e.g.
     * the repo tree {@code content/raws}).
     *
     * @param rawsRoot the raws root directory
     * @return the immutable registry
     * @throws NullPointerException             if {@code rawsRoot} is {@code null}
     * @throws SkillRawsValidationException     on the first validation failure
     * @throws UncheckedIOException              if the file cannot be read
     */
    public static SkillRegistry load(Path rawsRoot) {
        java.util.Objects.requireNonNull(rawsRoot, "rawsRoot");
        Path file = rawsRoot.resolve(SKILLS_FILE.replace('/', java.io.File.separatorChar));
        if (!Files.isRegularFile(file)) {
            throw new SkillRawsValidationException(SKILLS_FILE, SkillRawsValidationException.NO_FIELD,
                    "skills raws file not found at " + file);
        }
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(file);
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading raws file " + file, e);
        }
        return parse(bytes);
    }

    /**
     * Loads and validates {@code skills/skills.json} from a classpath resource
     * root (e.g. {@code "trojia/raws"} in the content jar).
     *
     * @param classLoader  the loader whose classpath carries the raws
     * @param resourceRoot the resource root, {@code '/'}-separated
     * @return the immutable registry
     * @throws NullPointerException          if an argument is {@code null}
     * @throws SkillRawsValidationException  if the resource is missing or invalid
     * @throws UncheckedIOException           if the resource bytes cannot be read
     */
    public static SkillRegistry load(ClassLoader classLoader, String resourceRoot) {
        java.util.Objects.requireNonNull(classLoader, "classLoader");
        java.util.Objects.requireNonNull(resourceRoot, "resourceRoot");
        String trimmed = trimSlashes(resourceRoot);
        String resourcePath = trimmed.isEmpty() ? SKILLS_FILE : trimmed + "/" + SKILLS_FILE;
        byte[] bytes;
        try (var stream = classLoader.getResourceAsStream(resourcePath)) {
            if (stream == null) {
                throw new SkillRawsValidationException(SKILLS_FILE, SkillRawsValidationException.NO_FIELD,
                        "skills raws resource not found: " + resourcePath);
            }
            bytes = stream.readAllBytes();
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading raws resource " + resourcePath, e);
        }
        return parse(bytes);
    }

    private static String trimSlashes(String s) {
        int start = 0;
        int end = s.length();
        while (start < end && s.charAt(start) == '/') {
            start++;
        }
        while (end > start && s.charAt(end - 1) == '/') {
            end--;
        }
        return s.substring(start, end);
    }

    private static SkillRegistry parse(byte[] bytes) {
        JsonValue tree;
        try {
            tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);
        } catch (JsonParseException e) {
            throw new SkillRawsValidationException(SKILLS_FILE, SkillRawsValidationException.NO_FIELD,
                    "malformed JSON: " + e.getMessage());
        }
        if (!(tree instanceof JsonObject root)) {
            throw new SkillRawsValidationException(SKILLS_FILE, SkillRawsValidationException.NO_FIELD,
                    "top-level value must be an object");
        }
        rejectUnknown(root, TOP_FIELDS);
        JsonValue skillsValue = root.get("skills");
        if (!(skillsValue instanceof JsonArray skillsArray)) {
            throw new SkillRawsValidationException(SKILLS_FILE, "skills",
                    "required array is " + (skillsValue == null ? "missing" : "not an array"));
        }
        TreeMap<String, SkillDefinition> byKey = new TreeMap<>();
        for (int i = 0; i < skillsArray.size(); i++) {
            if (!(skillsArray.get(i) instanceof JsonObject skillObject)) {
                throw new SkillRawsValidationException(SKILLS_FILE, "skills[" + i + "]",
                        "skill entry must be an object");
            }
            SkillDefinition definition = parseSkill(skillObject, i);
            SkillDefinition previous = byKey.putIfAbsent(definition.key(), definition);
            if (previous != null) {
                throw new SkillRawsValidationException(SKILLS_FILE, "skills[" + i + "].id",
                        "duplicate skill id \"" + definition.key() + "\"");
            }
        }
        List<SkillDefinition> definitions = new ArrayList<>(byKey.values());
        return SkillRegistry.of(definitions);
    }

    private static SkillDefinition parseSkill(JsonObject raw, int index) {
        rejectUnknown(raw, SKILL_FIELDS);
        String key = requireString(raw, "skills[" + index + "].id", "id");
        String displayName = requireString(raw, "skills[" + index + "].displayName", "displayName");
        String covers = requireString(raw, "skills[" + index + "].covers", "covers");
        String govLiteral = requireString(raw, "skills[" + index + "].governingAttribute", "governingAttribute");
        String aptLiteral = requireString(raw, "skills[" + index + "].aptitudeTier", "aptitudeTier");
        GoverningAttribute governingAttribute = parseGoverningAttribute(govLiteral, index);
        AptitudeTier aptitudeTier = parseAptitudeTier(aptLiteral, index);
        return new SkillDefinition(key, displayName, covers, governingAttribute, aptitudeTier);
    }

    private static GoverningAttribute parseGoverningAttribute(String literal, int index) {
        for (GoverningAttribute value : GoverningAttribute.values()) {
            if (value.name().equals(literal)) {
                return value;
            }
        }
        throw new SkillRawsValidationException(SKILLS_FILE, "skills[" + index + "].governingAttribute",
                "unknown governingAttribute \"" + literal + "\"");
    }

    private static AptitudeTier parseAptitudeTier(String literal, int index) {
        for (AptitudeTier value : AptitudeTier.values()) {
            if (value.name().equals(literal)) {
                return value;
            }
        }
        throw new SkillRawsValidationException(SKILLS_FILE, "skills[" + index + "].aptitudeTier",
                "unknown aptitudeTier \"" + literal + "\"");
    }

    private static void rejectUnknown(JsonObject object, List<String> allowed) {
        for (JsonObject.Member member : object.members()) {
            if (!allowed.contains(member.name()) && !IGNORED_FIELDS.contains(member.name())) {
                throw new SkillRawsValidationException(SKILLS_FILE, member.name(), "unknown field");
            }
        }
    }

    private static String requireString(JsonObject object, String path, String field) {
        JsonValue value = object.get(field);
        if (!(value instanceof JsonString string) || string.value().isEmpty()) {
            throw new SkillRawsValidationException(SKILLS_FILE, path,
                    "required non-empty string is "
                            + (value == null ? "missing" : "not a string or empty"));
        }
        return string.value();
    }
}
