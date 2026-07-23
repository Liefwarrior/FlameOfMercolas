package com.trojia.client.scenario;

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
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Strict, fail-fast reader for {@code content/raws/names/names.json} — the Trojian NameForge
 * pools (Sprint 1): given names and epithets per actor group, the shared household-surname
 * pool, and the kennel pool for keeper-owned beasts. Same {@link MiniJson} discipline as the
 * actor raws loaders: source order preserved (draw indices address pools positionally, so
 * pool order is part of the deterministic contract — append-only, never reorder).
 */
final class NameRaws {

    private final Map<String, List<String>> givenByGroup;
    private final List<String> surnames;
    private final Map<String, List<String>> epithetsByGroup;
    private final List<String> kennel;

    private NameRaws(Map<String, List<String>> givenByGroup, List<String> surnames,
            Map<String, List<String>> epithetsByGroup, List<String> kennel) {
        this.givenByGroup = givenByGroup;
        this.surnames = surnames;
        this.epithetsByGroup = epithetsByGroup;
        this.kennel = kennel;
    }

    List<String> givenFor(String groupKey) {
        List<String> pool = givenByGroup.get(groupKey);
        if (pool == null) {
            throw new IllegalStateException("names.json has no given-name pool for group \""
                    + groupKey + "\"");
        }
        return pool;
    }

    List<String> surnames() {
        return surnames;
    }

    List<String> epithetsFor(String groupKey) {
        List<String> pool = epithetsByGroup.get(groupKey);
        if (pool == null) {
            throw new IllegalStateException("names.json has no epithet pool for group \""
                    + groupKey + "\"");
        }
        return pool;
    }

    List<String> kennel() {
        return kennel;
    }

    static NameRaws load(Path namesJson) {
        JsonObject root = parseRoot(namesJson);
        Map<String, List<String>> given = stringListsByKey(namesJson, root, "givenByGroup");
        List<String> surnames = stringList(namesJson, root.get("surnames"), "surnames");
        Map<String, List<String>> epithets =
                stringListsByKey(namesJson, root, "epithetsByGroup");
        List<String> kennel = stringList(namesJson, root.get("kennel"), "kennel");
        return new NameRaws(given, surnames, epithets, kennel);
    }

    private static JsonObject parseRoot(Path file) {
        JsonValue tree;
        try {
            tree = MiniJson.parse(Files.readAllBytes(file), JsonNumberMode.INTEGER_ONLY);
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read " + file, e);
        } catch (JsonParseException e) {
            throw new IllegalStateException(file + ": " + e.getMessage(), e);
        }
        if (!(tree instanceof JsonObject root)) {
            throw new IllegalStateException(file + ": root must be an object");
        }
        return root;
    }

    private static Map<String, List<String>> stringListsByKey(Path file, JsonObject root,
            String field) {
        if (!(root.get(field) instanceof JsonObject byKey)) {
            throw new IllegalStateException(file + ": \"" + field + "\" must be an object");
        }
        Map<String, List<String>> out = new LinkedHashMap<>();
        for (JsonObject.Member member : byKey.members()) {
            out.put(member.name(), stringList(file, member.value(), field + "." + member.name()));
        }
        return out;
    }

    private static List<String> stringList(Path file, JsonValue value, String what) {
        if (!(value instanceof JsonArray array)) {
            throw new IllegalStateException(file + ": \"" + what + "\" must be an array");
        }
        List<String> out = new ArrayList<>(array.size());
        for (JsonValue element : array.values()) {
            if (!(element instanceof JsonString string) || string.value().isBlank()) {
                throw new IllegalStateException(
                        file + ": \"" + what + "\" must hold non-blank strings");
            }
            out.add(string.value());
        }
        if (out.isEmpty()) {
            throw new IllegalStateException(file + ": \"" + what + "\" must not be empty");
        }
        return out;
    }
}
