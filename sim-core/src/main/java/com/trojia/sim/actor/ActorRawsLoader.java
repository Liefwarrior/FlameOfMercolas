package com.trojia.sim.actor;

import com.trojia.sim.json.JsonNumber;
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
import java.util.Comparator;
import java.util.List;
import java.util.stream.Stream;

/**
 * Strict, fail-fast reader for {@code content/raws/actors/<type_id>.json}
 * (ACTORS-SPEC.md §6). {@code household.json} lives in the same directory but
 * is a different, cross-cutting schema (§11.5) — skipped here by name.
 *
 * <p>Scoped to what this foundation milestone's starter policy library
 * consumes ({@link ActorTypeStats}) rather than the full v1 §6 schema
 * (scuffle stats, sightRadiusByLight, per-policy param blobs for the entire
 * policy library) — a later loader extension adds those fields without
 * touching this class's fail-fast shape.
 */
public final class ActorRawsLoader {

    private static final String HOUSEHOLD_FILE = "household.json";
    private static final List<String> ROOT_FIELDS = List.of(
            "id", "displayName", "glyph", "tint", "factionId", "hp", "speedTicksPerStep",
            "leashRadius", "inventoryCap", "needs", "deferWielder", "flee", "returnHome", "loiter");
    private static final List<String> NEED_KEYS =
            List.of("hunger", "rest", "coin", "safety", "duty");
    private static final List<String> NEED_FIELDS =
            List.of("start", "decayPerKilotick", "recoverPerTick", "lowBonus", "critBonus");
    private static final List<String> DEFER_FIELDS = List.of("enabled", "priority", "radius");
    private static final List<String> FLEE_FIELDS = List.of("priority");
    private static final List<String> RETURN_HOME_FIELDS =
            List.of("priority", "rhythmBonus", "nightWindowStart", "nightWindowEnd");
    private static final List<String> LOITER_FIELDS = List.of("priority");

    private ActorRawsLoader() {
    }

    /** Loads every {@code *.json} actor-type raw under {@code actorsRawsDir} (sorted-name order). */
    public static ActorTypeStatsTable load(Path actorsRawsDir) {
        List<Path> files;
        try (Stream<Path> listing = Files.list(actorsRawsDir)) {
            files = listing
                    .filter(Files::isRegularFile)
                    .filter(p -> p.getFileName().toString().endsWith(".json"))
                    .filter(p -> !HOUSEHOLD_FILE.equals(p.getFileName().toString()))
                    .sorted(Comparator.comparing(p -> p.getFileName().toString()))
                    .toList();
        } catch (IOException e) {
            throw new UncheckedIOException("failed listing actor raws dir " + actorsRawsDir, e);
        }
        List<ActorTypeStats> parsed = new ArrayList<>(files.size());
        for (Path file : files) {
            parsed.add(parseFile(file));
        }
        return ActorTypeStatsTable.of(parsed);
    }

    private static ActorTypeStats parseFile(Path path) {
        String name = path.getFileName().toString();
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(path);
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading " + path, e);
        }
        JsonValue tree;
        try {
            tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);
        } catch (JsonParseException e) {
            throw new ActorRawsValidationException(name, ActorRawsValidationException.NO_FIELD,
                    "malformed JSON: " + e.getMessage());
        }
        if (!(tree instanceof JsonObject root)) {
            throw new ActorRawsValidationException(name, ActorRawsValidationException.NO_FIELD,
                    "top-level value must be an object");
        }
        rejectUnknown(name, "", root, ROOT_FIELDS);

        ActorTypeId typeId = ActorTypeId.of(requireString(name, "id", root, "id"));
        String displayName = requireString(name, "displayName", root, "displayName");
        char glyph = requireGlyph(name, root);
        int tint = requireTint(name, root);
        String factionId = requireString(name, "factionId", root, "factionId");
        short hp = (short) requireInt(name, "hp", root, "hp", 1, Short.MAX_VALUE);
        int speedTicksPerStep = requireInt(name, "speedTicksPerStep", root, "speedTicksPerStep", 1, 1000);
        int leashRadius = requireInt(name, "leashRadius", root, "leashRadius", 0, 4096);
        int inventoryCap = requireInt(name, "inventoryCap", root, "inventoryCap", 0, 255);

        NeedConfig[] needs = parseNeeds(name, root);

        JsonObject defer = optionalObject(name, root, "deferWielder");
        boolean hasDefer = defer != null && requireBool(name, "deferWielder.enabled", defer, "enabled");
        int deferPriority = 0;
        int deferRadius = 0;
        if (defer != null) {
            rejectUnknown(name, "deferWielder.", defer, DEFER_FIELDS);
            if (hasDefer) {
                deferPriority = requireInt(name, "deferWielder.priority", defer, "priority", 900, 999);
                deferRadius = requireInt(name, "deferWielder.radius", defer, "radius", 0, 4096);
            }
        }

        JsonObject flee = requireObject(name, root, "flee");
        rejectUnknown(name, "flee.", flee, FLEE_FIELDS);
        int fleePriority = requireInt(name, "flee.priority", flee, "priority", 900, 999);

        JsonObject returnHome = requireObject(name, root, "returnHome");
        rejectUnknown(name, "returnHome.", returnHome, RETURN_HOME_FIELDS);
        int returnHomePriority =
                requireInt(name, "returnHome.priority", returnHome, "priority", 300, 499);
        int returnHomeRhythmBonus =
                requireInt(name, "returnHome.rhythmBonus", returnHome, "rhythmBonus", 0, 1000);
        int nightStart = requireInt(name, "returnHome.nightWindowStart", returnHome,
                "nightWindowStart", 0, (int) DailyRhythm.DAY);
        int nightEnd = requireInt(name, "returnHome.nightWindowEnd", returnHome,
                "nightWindowEnd", nightStart, (int) DailyRhythm.DAY);

        JsonObject loiter = requireObject(name, root, "loiter");
        rejectUnknown(name, "loiter.", loiter, LOITER_FIELDS);
        int loiterPriority = requireInt(name, "loiter.priority", loiter, "priority", 1, 99);

        return new ActorTypeStats(typeId, displayName,
                glyph, tint, factionId, hp, speedTicksPerStep, leashRadius, inventoryCap, needs,
                hasDefer, deferPriority, deferRadius, fleePriority,
                returnHomePriority, returnHomeRhythmBonus, nightStart, nightEnd, loiterPriority);
    }

    private static NeedConfig[] parseNeeds(String file, JsonObject root) {
        JsonObject needsObj = requireObject(file, root, "needs");
        rejectUnknownExact(file, "needs.", needsObj, NEED_KEYS);
        NeedConfig[] configs = new NeedConfig[Need.COUNT];
        for (String key : NEED_KEYS) {
            JsonObject row = requireObject(file, needsObj, key);
            String prefix = "needs." + key + ".";
            rejectUnknown(file, prefix, row, NEED_FIELDS);
            int start = requireInt(file, prefix + "start", row, "start", 0, NeedThresholds.MAX);
            int decay = requireInt(file, prefix + "decayPerKilotick", row, "decayPerKilotick",
                    0, Integer.MAX_VALUE);
            int recover = optionalInt(file, row, "recoverPerTick", 0);
            int lowBonus = optionalInt(file, row, "lowBonus", 0);
            int critBonus = optionalInt(file, row, "critBonus", 0);
            configs[Need.valueOf(key.toUpperCase(java.util.Locale.ROOT)).ordinal()] =
                    new NeedConfig(start, decay, recover, lowBonus, critBonus);
        }
        return configs;
    }

    // ---------------------------------------------------------------- utilities

    private static void rejectUnknown(String file, String prefix, JsonObject object,
            List<String> allowed) {
        for (JsonObject.Member member : object.members()) {
            if (!allowed.contains(member.name())) {
                throw new ActorRawsValidationException(file, prefix + member.name(), "unknown field");
            }
        }
    }

    private static void rejectUnknownExact(String file, String prefix, JsonObject object,
            List<String> allowed) {
        rejectUnknown(file, prefix, object, allowed);
    }

    private static String requireString(String file, String field, JsonObject object, String key) {
        JsonValue value = object.get(key);
        if (!(value instanceof JsonString s) || s.value().isEmpty()) {
            throw new ActorRawsValidationException(file, field,
                    "required non-empty string is " + (value == null ? "missing" : "invalid"));
        }
        return s.value();
    }

    private static boolean requireBool(String file, String field, JsonObject object, String key) {
        JsonValue value = object.get(key);
        if (!(value instanceof com.trojia.sim.json.JsonBool b)) {
            throw new ActorRawsValidationException(file, field, "required boolean is missing");
        }
        return b.value();
    }

    private static int requireInt(String file, String field, JsonObject object, String key,
            int min, int max) {
        JsonValue value = object.get(key);
        if (!(value instanceof JsonNumber number) || !number.isIntegral()) {
            throw new ActorRawsValidationException(file, field, "required integer is missing");
        }
        long v = number.asLong();
        if (v < min || v > max) {
            throw new ActorRawsValidationException(file, field,
                    "value " + v + " out of range " + min + ".." + max);
        }
        return (int) v;
    }

    private static int optionalInt(String file, JsonObject object, String key, int absent) {
        JsonValue value = object.get(key);
        if (value == null) {
            return absent;
        }
        if (!(value instanceof JsonNumber number) || !number.isIntegral()) {
            throw new ActorRawsValidationException(file, key, "must be an integer");
        }
        return number.asInt();
    }

    private static JsonObject requireObject(String file, JsonObject object, String field) {
        JsonValue value = object.get(field);
        if (!(value instanceof JsonObject nested)) {
            throw new ActorRawsValidationException(file, field, "required object is "
                    + (value == null ? "missing" : "not an object"));
        }
        return nested;
    }

    private static JsonObject optionalObject(String file, JsonObject object, String field) {
        JsonValue value = object.get(field);
        if (value == null) {
            return null;
        }
        if (!(value instanceof JsonObject nested)) {
            throw new ActorRawsValidationException(file, field, "must be an object");
        }
        return nested;
    }

    private static char requireGlyph(String file, JsonObject root) {
        String s = requireString(file, "glyph", root, "glyph");
        if (s.length() != 1) {
            throw new ActorRawsValidationException(file, "glyph", "must be exactly one character");
        }
        return s.charAt(0);
    }

    private static int requireTint(String file, JsonObject root) {
        String literal = requireString(file, "tint", root, "tint");
        if (literal.length() != 7 || literal.charAt(0) != '#') {
            throw new ActorRawsValidationException(file, "tint",
                    "tint must be \"#RRGGBB\", got \"" + literal + "\"");
        }
        int rgb = 0;
        for (int i = 1; i < 7; i++) {
            int digit = Character.digit(literal.charAt(i), 16);
            if (digit < 0) {
                throw new ActorRawsValidationException(file, "tint",
                        "tint must be \"#RRGGBB\", got \"" + literal + "\"");
            }
            rgb = (rgb << 4) | digit;
        }
        return rgb;
    }
}
