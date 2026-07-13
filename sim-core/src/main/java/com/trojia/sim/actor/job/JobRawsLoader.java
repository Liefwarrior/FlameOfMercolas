package com.trojia.sim.actor.job;

import com.trojia.sim.actor.ActorRawsValidationException;
import com.trojia.sim.actor.ActorTypeId;
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

/**
 * Strict, fail-fast reader for {@code content/raws/jobs/jobs.json}
 * (ACTORS-SPEC.md §10.3): one shared file, parsed via {@link MiniJson} in
 * integer-only mode. Every failure names the file and dotted field
 * ({@link ActorRawsValidationException}).
 */
public final class JobRawsLoader {

    private static final List<String> ROOT_FIELDS = List.of("version", "jobs");
    private static final List<String> JOB_FIELDS = List.of(
            "id", "goalKind", "priority", "rhythmWindow", "rhythmBonus", "workTicksPerUnit",
            "unitsToComplete", "renew", "assign", "defaultFor", "secret", "cover");
    private static final List<String> RENEW_FIELDS = List.of("mode", "cooldownTicks");
    private static final List<String> COVER_FIELDS = List.of("actorType", "presentedJob");

    private JobRawsLoader() {
    }

    public static List<JobRaw> load(Path jobsJsonFile) {
        String relative = jobsJsonFile.getFileName().toString();
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(jobsJsonFile);
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading " + jobsJsonFile, e);
        }
        JsonValue tree;
        try {
            tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);
        } catch (JsonParseException e) {
            throw new ActorRawsValidationException(relative, ActorRawsValidationException.NO_FIELD,
                    "malformed JSON: " + e.getMessage());
        }
        if (!(tree instanceof JsonObject root)) {
            throw new ActorRawsValidationException(relative, ActorRawsValidationException.NO_FIELD,
                    "top-level value must be an object");
        }
        rejectUnknown(relative, "", root, ROOT_FIELDS);
        requireInt(relative, "version", root, "version", 1, 1);
        JsonValue jobsValue = root.get("jobs");
        if (!(jobsValue instanceof JsonArray jobsArray)) {
            throw new ActorRawsValidationException(relative, "jobs", "required array is missing");
        }
        List<JobRaw> result = new ArrayList<>(jobsArray.size());
        for (int i = 0; i < jobsArray.size(); i++) {
            if (!(jobsArray.get(i) instanceof JsonObject entry)) {
                throw new ActorRawsValidationException(relative, "jobs[" + i + "]",
                        "each job entry must be an object");
            }
            result.add(parseJob(relative, entry));
        }
        return result;
    }

    private static JobRaw parseJob(String file, JsonObject raw) {
        rejectUnknown(file, "", raw, JOB_FIELDS);
        JobId id = JobId.of(requireString(file, "id", raw, "id"));
        GoalKind goalKind = requireEnum(file, "goalKind", raw, GoalKind.values());
        int priority = requireInt(file, "priority", raw, "priority",
                JobParams.JOB_BAND_MIN, JobParams.JOB_BAND_MAX);

        JsonValue windowValue = raw.get("rhythmWindow");
        if (!(windowValue instanceof JsonArray window) || window.size() != 2) {
            throw new ActorRawsValidationException(file, "rhythmWindow",
                    "must be a 2-element [start, end] array");
        }
        int rhythmStart = intAt(file, "rhythmWindow[0]", window, 0);
        int rhythmEnd = intAt(file, "rhythmWindow[1]", window, 1);
        int rhythmBonus = requireInt(file, "rhythmBonus", raw, "rhythmBonus", 0, Integer.MAX_VALUE);
        int workTicksPerUnit = requireInt(file, "workTicksPerUnit", raw, "workTicksPerUnit",
                1, Integer.MAX_VALUE);
        int unitsToComplete = requireInt(file, "unitsToComplete", raw, "unitsToComplete",
                1, Integer.MAX_VALUE);

        JsonValue renewValue = raw.get("renew");
        if (!(renewValue instanceof JsonObject renew)) {
            throw new ActorRawsValidationException(file, "renew", "required object is missing");
        }
        rejectUnknown(file, "renew.", renew, RENEW_FIELDS);
        RenewMode renewMode = requireEnum(file, "renew.mode", renew, RenewMode.values());
        int cooldownTicks = renewMode == RenewMode.COOLDOWN
                ? requireInt(file, "renew.cooldownTicks", renew, "cooldownTicks", 0, Integer.MAX_VALUE)
                : 0;

        List<JobRaw.AssignWeight> assign = parseAssign(file, raw);
        List<ActorTypeId> defaultFor = parseDefaultFor(file, raw);
        boolean secret = requireBool(file, "secret", raw, "secret");
        CoverSpec cover = parseCover(file, raw, secret);

        return new JobRaw(file, id, goalKind, priority, rhythmStart, rhythmEnd, rhythmBonus,
                workTicksPerUnit, unitsToComplete, renewMode, cooldownTicks, assign, defaultFor,
                secret, cover);
    }

    private static List<JobRaw.AssignWeight> parseAssign(String file, JsonObject raw) {
        JsonValue value = raw.get("assign");
        List<JobRaw.AssignWeight> assign = new ArrayList<>();
        if (value == null) {
            return assign;
        }
        if (!(value instanceof JsonObject assignObj)) {
            throw new ActorRawsValidationException(file, "assign", "must be an object");
        }
        for (JsonObject.Member member : assignObj.members()) {
            if (!(member.value() instanceof com.trojia.sim.json.JsonNumber number)
                    || !number.isIntegral() || number.asLong() <= 0) {
                throw new ActorRawsValidationException(file, "assign." + member.name(),
                        "weight must be a positive integer");
            }
            assign.add(new JobRaw.AssignWeight(ActorTypeId.of(member.name()), number.asInt()));
        }
        return assign;
    }

    private static List<ActorTypeId> parseDefaultFor(String file, JsonObject raw) {
        JsonValue value = raw.get("defaultFor");
        List<ActorTypeId> defaultFor = new ArrayList<>();
        if (value == null) {
            return defaultFor;
        }
        if (!(value instanceof JsonArray array)) {
            throw new ActorRawsValidationException(file, "defaultFor", "must be an array");
        }
        for (int i = 0; i < array.size(); i++) {
            if (!(array.get(i) instanceof JsonString s) || s.value().isEmpty()) {
                throw new ActorRawsValidationException(file, "defaultFor[" + i + "]",
                        "must be a non-empty string");
            }
            defaultFor.add(ActorTypeId.of(s.value()));
        }
        return defaultFor;
    }

    private static CoverSpec parseCover(String file, JsonObject raw, boolean secret) {
        JsonValue value = raw.get("cover");
        if (!secret) {
            if (value != null) {
                throw new ActorRawsValidationException(file, "cover",
                        "secret: false must not declare a cover block");
            }
            return null;
        }
        if (!(value instanceof JsonObject cover)) {
            throw new ActorRawsValidationException(file, "cover",
                    "secret: true requires a cover block");
        }
        rejectUnknown(file, "cover.", cover, COVER_FIELDS);
        ActorTypeId actorType = ActorTypeId.of(requireString(file, "cover.actorType", cover, "actorType"));
        JobId presentedJob = JobId.of(requireString(file, "cover.presentedJob", cover, "presentedJob"));
        return new CoverSpec(actorType, presentedJob);
    }

    // ---------------------------------------------------------------- utilities

    private static void rejectUnknown(String file, String prefix, JsonObject object,
            List<String> allowed) {
        for (JsonObject.Member member : object.members()) {
            if (!allowed.contains(member.name())) {
                throw new ActorRawsValidationException(file, prefix + member.name(),
                        "unknown field");
            }
        }
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
        if (!(value instanceof com.trojia.sim.json.JsonNumber number) || !number.isIntegral()) {
            throw new ActorRawsValidationException(file, field, "required integer is missing");
        }
        long v = number.asLong();
        if (v < min || v > max) {
            throw new ActorRawsValidationException(file, field,
                    "value " + v + " out of range " + min + ".." + max);
        }
        return (int) v;
    }

    private static int intAt(String file, String field, JsonArray array, int index) {
        if (!(array.get(index) instanceof com.trojia.sim.json.JsonNumber number)
                || !number.isIntegral()) {
            throw new ActorRawsValidationException(file, field, "must be an integer");
        }
        return number.asInt();
    }

    private static <E extends Enum<E>> E requireEnum(String file, String field, JsonObject object,
            E[] values) {
        String literal = requireString(file, field, object, field.contains(".")
                ? field.substring(field.lastIndexOf('.') + 1) : field);
        for (E value : values) {
            if (value.name().equals(literal)) {
                return value;
            }
        }
        throw new ActorRawsValidationException(file, field, "unknown value \"" + literal + "\"");
    }
}
