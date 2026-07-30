package com.trojia.sim.actor.spell;

import com.trojia.sim.json.JsonArray;
import com.trojia.sim.json.JsonNumber;
import com.trojia.sim.json.JsonNumberMode;
import com.trojia.sim.json.JsonObject;
import com.trojia.sim.json.JsonParseException;
import com.trojia.sim.json.JsonString;
import com.trojia.sim.json.JsonValue;
import com.trojia.sim.json.MiniJson;
import com.trojia.sim.progression.AttributeId;
import com.trojia.sim.progression.SkillRegistry;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

/**
 * The fail-fast loader for {@code content/raws/spells/spells.json} — a single container file,
 * the {@code skills.json} / {@code jobs.json} convention, NOT one file per spell.
 *
 * <p><b>This file is the deliverable.</b> Everything a crafting is lives in the JSON: which
 * axis it moves, by how much, for how long, how far it reaches, how wide it spreads, which
 * skill gates it and at what level. Adding a spell is adding a row here — no Java, no rebuild
 * of any table, no new enum. {@link #parse(SkillRegistry, String)} is public precisely so a
 * spellcrafting screen (or a test proving the vocabulary is real) can compose a definition at
 * runtime out of the same parts, with no file on disk at all.
 *
 * <p><b>The skill universe is an argument, not an afterthought.</b> Every route in takes a
 * {@link SkillRegistry} and every {@code "skill"} key is checked against it, because a crafting
 * names the skill that gates it, checks it and grows with it — so an unknown key is not a typo
 * with a cosmetic consequence, it is a crafting with no gate, no dice and no XP.
 *
 * <p><b>Schema</b> (top-level object): {@code "id"} and {@code "spells"}, an array of spell
 * objects:
 * <pre>
 * {
 *   "id": "warm_the_hands",
 *   "displayName": "Warm the Hands",
 *   "skill": "linkcraft",
 *   "minLevel": 0,
 *   "baseResist": 0,
 *   "cooldownTicks": 200,
 *   "target": "TOUCH",          // SELF | TOUCH | RANGED
 *   "range": 1,                 // required for RANGED; 0 for SELF, 1 for TOUCH
 *   "areaRadius": 0,
 *   "components": [
 *     { "effect": "TEMPERATURE", "mode": "WHILE_ACTIVE", "magnitude": 15,
 *       "durationTicks": 600 }
 *   ]
 * }
 * </pre>
 * {@code param} is required for the {@code ATTRIBUTE} axis (an {@code AttributeId} name) and
 * forbidden elsewhere.
 *
 * <p><b>Not every (effect x mode) pair is authorable, and the refusal is loud.</b>
 * {@link EffectPairing} is the table: TEMPERATURE and ATTRIBUTE are HELD axes
 * ({@code WHILE_ACTIVE} only), VITALITY is a DELIVERED one ({@code INSTANT} or
 * {@code OVER_TIME}), a magnitude of 0 is refused, an ATTRIBUTE magnitude past
 * {@link ActiveEffects#ATTRIBUTE_MODIFIER_LIMIT} is refused, and a lingering component shorter
 * than its own cadence ({@link EffectPairing#minimumEffectiveTicks}) is refused. Each of those
 * used to load, resolve, be charged its full difficulty and change nothing; each is now a
 * {@link SpellRawsValidationException} naming the exact {@code components[i]} that is wrong and
 * why. A crafting that gets past this loader does something.
 *
 * <p>{@code canonAnchor}, {@code provenance} and {@code notes} are
 * documentation-only and ignored — the materials/skills raws convention, and the project's way
 * of flagging what is canon and what is invention on the row itself.
 *
 * <p>Loading is a pure function of the raws bytes: spells are keyed into a {@link TreeMap}
 * before the registry is built, so identical raws yield identical raw indices on every
 * platform.
 */
public final class SpellRawsLoader {

    private static final String SPELLS_FILE = "spells/spells.json";

    private static final List<String> TOP_FIELDS = List.of("id", "spells", "notes", "provenance");
    private static final List<String> SPELL_FIELDS = List.of(
            "id", "displayName", "skill", "minLevel", "baseResist", "cooldownTicks", "target",
            "range", "areaRadius", "components", "canonAnchor", "provenance", "notes");
    private static final List<String> COMPONENT_FIELDS = List.of(
            "effect", "mode", "magnitude", "param", "durationTicks", "notes");
    private static final List<String> IGNORED_FIELDS =
            List.of("canonAnchor", "provenance", "notes");

    private SpellRawsLoader() {
    }

    /**
     * Loads and validates {@code spells/spells.json} under a raws root (e.g.
     * {@code content/raws}), gating every {@code "skill"} key against the skill universe in the
     * SAME raws root.
     */
    public static SpellRegistry load(Path rawsRoot) {
        java.util.Objects.requireNonNull(rawsRoot, "rawsRoot");
        return load(rawsRoot, com.trojia.sim.progression.SkillRawsLoader.load(rawsRoot));
    }

    /**
     * Loads and validates {@code spells/spells.json} under a raws root against an
     * already-built skill universe — what the bake uses, so the registry that GATES a crafting
     * is provably the same object the sim later measures it in.
     */
    public static SpellRegistry load(Path rawsRoot, SkillRegistry skills) {
        java.util.Objects.requireNonNull(rawsRoot, "rawsRoot");
        java.util.Objects.requireNonNull(skills, "skills");
        Path file = rawsRoot.resolve(SPELLS_FILE.replace('/', java.io.File.separatorChar));
        if (!Files.isRegularFile(file)) {
            throw new SpellRawsValidationException(SPELLS_FILE,
                    SpellRawsValidationException.NO_FIELD, "spell raws file not found at " + file);
        }
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(file);
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading raws file " + file, e);
        }
        return parse(skills, bytes);
    }

    /**
     * Loads and validates {@code spells/spells.json} from a classpath resource root, gating
     * every {@code "skill"} key against the skill universe in the same resource root.
     */
    public static SpellRegistry load(ClassLoader classLoader, String resourceRoot) {
        java.util.Objects.requireNonNull(classLoader, "classLoader");
        java.util.Objects.requireNonNull(resourceRoot, "resourceRoot");
        SkillRegistry skills =
                com.trojia.sim.progression.SkillRawsLoader.load(classLoader, resourceRoot);
        String trimmed = trimSlashes(resourceRoot);
        String resourcePath = trimmed.isEmpty() ? SPELLS_FILE : trimmed + "/" + SPELLS_FILE;
        byte[] bytes;
        try (var stream = classLoader.getResourceAsStream(resourcePath)) {
            if (stream == null) {
                throw new SpellRawsValidationException(SPELLS_FILE,
                        SpellRawsValidationException.NO_FIELD,
                        "spell raws resource not found: " + resourcePath);
            }
            bytes = stream.readAllBytes();
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading raws resource " + resourcePath, e);
        }
        return parse(skills, bytes);
    }

    /**
     * Parses spell raws straight out of a string — the seam that proves the vocabulary is real.
     * A new crafting can be composed and cast without a file, a rebuild or a line of Java.
     *
     * <p>The skill universe is REQUIRED, not optional: a crafting names the skill that gates it,
     * checks it and grows with it, so a spell parsed against no registry is a spell with no gate.
     */
    public static SpellRegistry parse(SkillRegistry skills, String json) {
        java.util.Objects.requireNonNull(skills, "skills");
        return parse(skills, json.getBytes(StandardCharsets.UTF_8));
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

    private static SpellRegistry parse(SkillRegistry skills, byte[] bytes) {
        JsonValue tree;
        try {
            tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);
        } catch (JsonParseException e) {
            throw new SpellRawsValidationException(SPELLS_FILE,
                    SpellRawsValidationException.NO_FIELD, "malformed JSON: " + e.getMessage());
        }
        if (!(tree instanceof JsonObject root)) {
            throw new SpellRawsValidationException(SPELLS_FILE,
                    SpellRawsValidationException.NO_FIELD, "top-level value must be an object");
        }
        rejectUnknown(root, TOP_FIELDS, SpellRawsValidationException.NO_FIELD);
        JsonValue spellsValue = root.get("spells");
        if (!(spellsValue instanceof JsonArray spellsArray)) {
            throw new SpellRawsValidationException(SPELLS_FILE, "spells",
                    "required array is " + (spellsValue == null ? "missing" : "not an array"));
        }
        TreeMap<String, SpellDefinition> byKey = new TreeMap<>();
        for (int i = 0; i < spellsArray.size(); i++) {
            if (!(spellsArray.get(i) instanceof JsonObject spellObject)) {
                throw new SpellRawsValidationException(SPELLS_FILE, "spells[" + i + "]",
                        "spell entry must be an object");
            }
            SpellDefinition definition = parseSpell(spellObject, i, skills);
            if (byKey.putIfAbsent(definition.key(), definition) != null) {
                throw new SpellRawsValidationException(SPELLS_FILE, "spells[" + i + "].id",
                        "duplicate spell id \"" + definition.key() + "\"");
            }
        }
        return SpellRegistry.of(new ArrayList<>(byKey.values()));
    }

    private static SpellDefinition parseSpell(JsonObject raw, int index, SkillRegistry skills) {
        String path = "spells[" + index + "]";
        rejectUnknown(raw, SPELL_FIELDS, path);
        String key = requireString(raw, path + ".id", "id");
        String displayName = requireString(raw, path + ".displayName", "displayName");
        String skill = requireSkill(raw, path + ".skill", skills);
        TargetShape target = parseTarget(requireString(raw, path + ".target", "target"), path);
        int minLevel = optionalInt(raw, path, "minLevel", 0);
        int baseResist = optionalInt(raw, path, "baseResist", 0);
        int cooldownTicks = optionalInt(raw, path, "cooldownTicks", 0);
        int defaultRange = switch (target) {
            case SELF -> 0;
            case TOUCH -> 1;
            case RANGED -> -1;
        };
        int range = optionalInt(raw, path, "range", defaultRange);
        if (range < 0) {
            throw new SpellRawsValidationException(SPELLS_FILE, path + ".range",
                    "a RANGED spell must declare its range");
        }
        int areaRadius = optionalInt(raw, path, "areaRadius", 0);
        JsonValue componentsValue = raw.get("components");
        if (!(componentsValue instanceof JsonArray componentsArray)
                || componentsArray.size() == 0) {
            throw new SpellRawsValidationException(SPELLS_FILE, path + ".components",
                    "required non-empty array is "
                            + (componentsValue == null ? "missing" : "empty or not an array"));
        }
        List<EffectComponent> components = new ArrayList<>();
        for (int i = 0; i < componentsArray.size(); i++) {
            if (!(componentsArray.get(i) instanceof JsonObject componentObject)) {
                throw new SpellRawsValidationException(SPELLS_FILE,
                        path + ".components[" + i + "]", "component must be an object");
            }
            components.add(parseComponent(componentObject, path + ".components[" + i + "]"));
        }
        try {
            return new SpellDefinition(key, displayName, skill, minLevel, baseResist,
                    cooldownTicks, target, range, areaRadius, components);
        } catch (IllegalArgumentException e) {
            throw new SpellRawsValidationException(SPELLS_FILE, path, e.getMessage());
        }
    }

    private static EffectComponent parseComponent(JsonObject raw, String path) {
        rejectUnknown(raw, COMPONENT_FIELDS, path);
        EffectKind kind = parseKind(requireString(raw, path + ".effect", "effect"), path);
        EffectMode mode = parseMode(requireString(raw, path + ".mode", "mode"), path);
        int magnitude = requireInt(raw, path + ".magnitude", "magnitude");
        int durationTicks = optionalInt(raw, path, "durationTicks", 0);
        int param = parseParam(raw, path, kind);
        try {
            return new EffectComponent(kind, mode, magnitude, param, durationTicks);
        } catch (IllegalArgumentException e) {
            throw new SpellRawsValidationException(SPELLS_FILE, path, e.getMessage());
        }
    }

    /**
     * The axis selector: an {@code AttributeId} name for {@link EffectKind#ATTRIBUTE} (which
     * attribute the crafting nudges), and forbidden on every axis that needs none — so a typo'd
     * {@code param} on a temperature spell is a loud content error, not a silently ignored one.
     */
    private static int parseParam(JsonObject raw, String path, EffectKind kind) {
        JsonValue value = raw.get("param");
        if (kind != EffectKind.ATTRIBUTE) {
            if (value != null) {
                throw new SpellRawsValidationException(SPELLS_FILE, path + ".param",
                        "the " + kind + " axis takes no param");
            }
            return 0;
        }
        if (!(value instanceof JsonString string)) {
            throw new SpellRawsValidationException(SPELLS_FILE, path + ".param",
                    "the ATTRIBUTE axis requires an AttributeId name (MGT/AGI/VIG/WIT)");
        }
        for (AttributeId attribute : AttributeId.values()) {
            if (attribute.name().equals(string.value())) {
                return attribute.ordinal();
            }
        }
        throw new SpellRawsValidationException(SPELLS_FILE, path + ".param",
                "unknown attribute \"" + string.value() + "\"");
    }

    /**
     * The governing skill, checked against the SKILL REGISTRY exactly as {@code effect},
     * {@code mode}, {@code target} and {@code param} are checked against their own universes.
     *
     * <p><b>This used to be a bare {@code requireString}.</b> A misspelt {@code "linkraft"}
     * loaded clean and shipped a crafting with no gate at all:
     * {@code SkillTrackRegistry.rawOfSkill} returns {@code Actor.NONE} for a key it does not
     * know, {@code level()} reads 0 off {@code NONE}, so the {@code minLevel} gate passed for
     * everybody, {@code award()} trained nothing, and the check ran against a skill that did not
     * exist. The typo was invisible in every direction — the crafting simply worked, for anyone,
     * forever, and grew nobody. Refused by name now, with the field path and the skills the ward
     * actually has.
     */
    private static String requireSkill(JsonObject raw, String path, SkillRegistry skills) {
        String key = requireString(raw, path, "skill");
        if (!skills.contains(key)) {
            StringBuilder known = new StringBuilder();
            for (var skill : skills.all()) {
                known.append(known.length() == 0 ? "" : ", ").append(skill.key());
            }
            throw new SpellRawsValidationException(SPELLS_FILE, path,
                    "unknown skill \"" + key + "\" -- a crafting names the skill that gates it,"
                            + " checks it and grows with it, so a skill the registry does not"
                            + " know is a crafting with no gate, no dice and no XP. Known skills:"
                            + " " + known);
        }
        return key;
    }

    private static EffectKind parseKind(String literal, String path) {
        for (EffectKind kind : EffectKind.values()) {
            if (kind.name().equals(literal)) {
                return kind;
            }
        }
        throw new SpellRawsValidationException(SPELLS_FILE, path + ".effect",
                "unknown effect axis \"" + literal + "\"");
    }

    private static EffectMode parseMode(String literal, String path) {
        for (EffectMode mode : EffectMode.values()) {
            if (mode.name().equals(literal)) {
                return mode;
            }
        }
        throw new SpellRawsValidationException(SPELLS_FILE, path + ".mode",
                "unknown effect mode \"" + literal + "\"");
    }

    private static TargetShape parseTarget(String literal, String path) {
        for (TargetShape shape : TargetShape.values()) {
            if (shape.name().equals(literal)) {
                return shape;
            }
        }
        throw new SpellRawsValidationException(SPELLS_FILE, path + ".target",
                "unknown target shape \"" + literal + "\"");
    }

    private static void rejectUnknown(JsonObject object, List<String> allowed, String path) {
        for (JsonObject.Member member : object.members()) {
            if (!allowed.contains(member.name()) && !IGNORED_FIELDS.contains(member.name())) {
                throw new SpellRawsValidationException(SPELLS_FILE, path + "." + member.name(),
                        "unknown field");
            }
        }
    }

    private static String requireString(JsonObject object, String path, String field) {
        JsonValue value = object.get(field);
        if (!(value instanceof JsonString string) || string.value().isEmpty()) {
            throw new SpellRawsValidationException(SPELLS_FILE, path,
                    "required non-empty string is "
                            + (value == null ? "missing" : "not a string or empty"));
        }
        return string.value();
    }

    private static int requireInt(JsonObject object, String path, String field) {
        JsonValue value = object.get(field);
        if (!(value instanceof JsonNumber number) || !number.isIntegral()) {
            throw new SpellRawsValidationException(SPELLS_FILE, path,
                    "required integer is " + (value == null ? "missing" : "not an integer"));
        }
        return number.asInt();
    }

    private static int optionalInt(JsonObject object, String path, String field, int fallback) {
        JsonValue value = object.get(field);
        if (value == null) {
            return fallback;
        }
        if (!(value instanceof JsonNumber number) || !number.isIntegral()) {
            throw new SpellRawsValidationException(SPELLS_FILE, path + "." + field,
                    "must be an integer");
        }
        return number.asInt();
    }
}
