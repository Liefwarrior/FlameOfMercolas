package com.trojia.client.scenario;

import com.trojia.sim.json.JsonArray;
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
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Strict, fail-fast reader for {@code content/raws/names/notables.json} — the Forty Notables
 * (Sprint 1 S1-2): the gazetteer's blessed names bound onto real actors <em>by spawn site</em>
 * (an anchor key into {@link DocksPopulation#notableSpawnSites()}), never by raw ActorId, so a
 * map regeneration cannot silently orphan an identity.
 */
final class NotableRaws {

    /**
     * One authored notable.
     *
     * @param id               stable notable id (kebab/lowercase), unique in the file
     * @param name             the authored display name (a villain's is its COVER name —
     *                         presented-identity convention, flagged to SIM-CORE)
     * @param epithet          the authored byname
     * @param type             the actor-type key the binding must land on (e.g. "shopkeeper")
     * @param site             the spawn-site key into {@code notableSpawnSites()}
     * @param match            {@code "home"} (default) or {@code "anchor"} — which of the
     *                         actor's cells must sit at the site
     * @param radius           same-z Chebyshev slack around the site cell (defaults: 2 for
     *                         home — the spawn funnel may displace a dwelling cell — 0 for
     *                         anchor, which the bake pins exactly)
     * @param rank             which match to take, ascending ActorId (default 0 = first)
     * @param householdSurname when set, the notable's HOUSEHOLD component takes this surname
     *                         (the compound family houses); {@code null} otherwise
     * @param bio              the authored 2-3 sentence bio
     */
    record Notable(String id, String name, String epithet, String type, String site,
            String match, int radius, int rank, String householdSurname, String bio) {
    }

    private NotableRaws() {
    }

    static List<Notable> load(Path notablesJson) {
        JsonValue tree;
        try {
            tree = MiniJson.parse(Files.readAllBytes(notablesJson), JsonNumberMode.INTEGER_ONLY);
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read " + notablesJson, e);
        } catch (JsonParseException e) {
            throw new IllegalStateException(notablesJson + ": " + e.getMessage(), e);
        }
        if (!(tree instanceof JsonObject root)
                || !(root.get("notables") instanceof JsonArray array)) {
            throw new IllegalStateException(
                    notablesJson + ": root must be an object with a \"notables\" array");
        }
        List<Notable> out = new ArrayList<>(array.size());
        Set<String> seenIds = new HashSet<>();
        for (JsonValue element : array.values()) {
            if (!(element instanceof JsonObject entry)) {
                throw new IllegalStateException(notablesJson + ": every notable must be an object");
            }
            Notable notable = parseOne(notablesJson, entry);
            if (!seenIds.add(notable.id())) {
                throw new IllegalStateException(
                        notablesJson + ": duplicate notable id \"" + notable.id() + "\"");
            }
            out.add(notable);
        }
        if (out.isEmpty()) {
            throw new IllegalStateException(notablesJson + ": \"notables\" must not be empty");
        }
        return List.copyOf(out);
    }

    private static Notable parseOne(Path file, JsonObject entry) {
        String id = requireString(file, entry, "id");
        String match = optionalString(file, entry, "match", "home");
        if (!match.equals("home") && !match.equals("anchor")) {
            throw new IllegalStateException(file + ": notable \"" + id
                    + "\" match must be \"home\" or \"anchor\", got \"" + match + "\"");
        }
        int defaultRadius = match.equals("home") ? 2 : 0;
        return new Notable(
                id,
                requireString(file, entry, "name"),
                requireString(file, entry, "epithet"),
                requireString(file, entry, "type"),
                requireString(file, entry, "site"),
                match,
                optionalInt(file, entry, "radius", defaultRadius),
                optionalInt(file, entry, "rank", 0),
                entry.has("householdSurname") ? requireString(file, entry, "householdSurname")
                        : null,
                requireString(file, entry, "bio"));
    }

    private static String requireString(Path file, JsonObject entry, String field) {
        if (!(entry.get(field) instanceof JsonString string) || string.value().isBlank()) {
            throw new IllegalStateException(
                    file + ": notable field \"" + field + "\" must be a non-blank string");
        }
        return string.value();
    }

    private static String optionalString(Path file, JsonObject entry, String field,
            String fallback) {
        if (!entry.has(field)) {
            return fallback;
        }
        return requireString(file, entry, field);
    }

    private static int optionalInt(Path file, JsonObject entry, String field, int fallback) {
        if (!entry.has(field)) {
            return fallback;
        }
        if (!(entry.get(field) instanceof JsonNumber number)) {
            throw new IllegalStateException(
                    file + ": notable field \"" + field + "\" must be an integer");
        }
        return number.asInt();
    }
}
