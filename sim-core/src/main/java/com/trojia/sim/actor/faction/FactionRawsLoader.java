package com.trojia.sim.actor.faction;

import com.trojia.sim.actor.ActorRawsValidationException;
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
 * Strict, fail-fast loader for {@code content/raws/factions/factions.json} (Sprint 1
 * faction registry) — the {@code SkillRawsLoader} single-file convention: a top-level
 * object with a raws {@code "id"}, a {@code "factions"} array of
 * {@code {id, displayName, memberJobs}} rows, and documentation-only {@code notes}.
 * Loading is a pure function of the raws bytes: identical raws yield identical id
 * assignments on every platform.
 */
public final class FactionRawsLoader {

    private static final String FACTIONS_FILE = "factions/factions.json";

    private static final List<String> TOP_FIELDS = List.of("id", "factions", "notes", "provenance");
    private static final List<String> FACTION_FIELDS = List.of(
            "id", "displayName", "memberJobs", "canonAnchor", "provenance", "notes");

    private FactionRawsLoader() {
    }

    /**
     * Loads and validates {@code factions/factions.json} under a raws root (e.g. the repo
     * tree {@code content/raws}).
     *
     * @throws ActorRawsValidationException on the first validation failure
     * @throws UncheckedIOException         if the file cannot be read
     */
    public static FactionRegistry load(Path rawsRoot) {
        java.util.Objects.requireNonNull(rawsRoot, "rawsRoot");
        Path file = rawsRoot.resolve("factions").resolve("factions.json");
        if (!Files.isRegularFile(file)) {
            throw new ActorRawsValidationException(FACTIONS_FILE,
                    ActorRawsValidationException.NO_FIELD,
                    "factions raws file not found at " + file);
        }
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(file);
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading raws file " + file, e);
        }
        return parse(bytes);
    }

    /** Parses raws bytes directly (the loader's pure core; the path overload reads then calls this). */
    public static FactionRegistry parse(byte[] bytes) {
        JsonValue tree;
        try {
            tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);
        } catch (JsonParseException e) {
            throw new ActorRawsValidationException(FACTIONS_FILE,
                    ActorRawsValidationException.NO_FIELD, "malformed JSON: " + e.getMessage());
        }
        if (!(tree instanceof JsonObject root)) {
            throw new ActorRawsValidationException(FACTIONS_FILE,
                    ActorRawsValidationException.NO_FIELD, "top-level value must be an object");
        }
        for (JsonObject.Member member : root.members()) {
            if (!TOP_FIELDS.contains(member.name())) {
                throw new ActorRawsValidationException(FACTIONS_FILE, member.name(),
                        "unknown field");
            }
        }
        if (!(root.get("id") instanceof JsonString)) {
            throw new ActorRawsValidationException(FACTIONS_FILE, "id",
                    "required string field is missing");
        }
        if (!(root.get("factions") instanceof JsonArray factions) || factions.size() == 0) {
            throw new ActorRawsValidationException(FACTIONS_FILE, "factions",
                    "required non-empty array is missing");
        }
        List<FactionDefinition> defs = new ArrayList<>(factions.size());
        for (int i = 0; i < factions.size(); i++) {
            defs.add(parseFaction(i, factions.get(i)));
        }
        try {
            return FactionRegistry.of(defs);
        } catch (IllegalArgumentException e) {
            throw new ActorRawsValidationException(FACTIONS_FILE, "factions", e.getMessage());
        }
    }

    private static FactionDefinition parseFaction(int index, JsonValue value) {
        String where = "factions[" + index + "]";
        if (!(value instanceof JsonObject faction)) {
            throw new ActorRawsValidationException(FACTIONS_FILE, where, "must be an object");
        }
        for (JsonObject.Member member : faction.members()) {
            if (!FACTION_FIELDS.contains(member.name())) {
                throw new ActorRawsValidationException(FACTIONS_FILE,
                        where + "." + member.name(), "unknown field");
            }
        }
        String id = requireString(faction, where, "id");
        String displayName = requireString(faction, where, "displayName");
        if (!(faction.get("memberJobs") instanceof JsonArray jobs)) {
            throw new ActorRawsValidationException(FACTIONS_FILE, where + ".memberJobs",
                    "required array is missing (may be empty)");
        }
        List<String> memberJobs = new ArrayList<>(jobs.size());
        for (int j = 0; j < jobs.size(); j++) {
            if (!(jobs.get(j) instanceof JsonString s) || s.value().isEmpty()) {
                throw new ActorRawsValidationException(FACTIONS_FILE,
                        where + ".memberJobs[" + j + "]", "must be a non-empty job key string");
            }
            memberJobs.add(s.value());
        }
        return new FactionDefinition(id, displayName, memberJobs);
    }

    private static String requireString(JsonObject obj, String where, String field) {
        if (!(obj.get(field) instanceof JsonString s) || s.value().isEmpty()) {
            throw new ActorRawsValidationException(FACTIONS_FILE, where + "." + field,
                    "required non-empty string is missing");
        }
        return s.value();
    }
}
