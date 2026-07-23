package com.trojia.sim.bark;

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
 * Loader for {@code content/raws/barks/barks.json} (Sprint 2 bark selection core) — THE
 * SCHEMA CONTRACT between the sim's selector and the World team's text tables:
 *
 * <pre>{@code
 * {
 *   "id": "barks",
 *   "tables": [
 *     { "key": "greet.watch.cold.night",
 *       "rows": ["Move along, you.", "The Watch sees you, gutter-blood."] },
 *     { "key": "mood.held",
 *       "rows": ["A day in the cage for nothing..."] }
 *   ],
 *   "notes": "documentation-only"
 * }
 * }</pre>
 *
 * <p><b>Key vocabulary</b> (produced by {@code com.trojia.sim.actor.BarkSelector} — author
 * tables against exactly these shapes):
 * <ul>
 *   <li>{@code mood.<state>} — status-bit overrides, in priority order:
 *       {@code dead}, {@code downed}, {@code held}, {@code confined} (house arrest),
 *       {@code panicked}, {@code harried} (move-along).</li>
 *   <li>{@code greet.<family>.<attitude>.<time>} — the default greeting bark:
 *       {@code family} = the speaker's PRESENTED job family ({@code serf}, {@code wastrel},
 *       {@code watch}, {@code clergy}, {@code trade}, {@code maritime}, {@code husbandry},
 *       {@code beast}, {@code flame_of_merc}; a disguised villain speaks as its cover);
 *       {@code attitude} = {@code kin} | {@code friend} | {@code hostile} | {@code cold} |
 *       {@code warm} | {@code neutral} (relationship first, then the LISTENER's standing
 *       with the SPEAKER's faction — the "you robbed their guild" reactivity);
 *       {@code time} = {@code morning} | {@code day} | {@code evening} | {@code night}.</li>
 * </ul>
 * Authoring is sparse-friendly: a consumer that misses a specific key falls back
 * ({@code greet.<family>.<attitude>.<time>} → {@code greet.<family>.<attitude>} →
 * {@code greet.<family>}) before going silent, so World can start with nine coarse family
 * tables and refine.
 *
 * <p><b>Degraded mode:</b> a missing {@code barks/} dir or {@code barks.json} loads
 * {@link BarkTableRegistry#EMPTY} (the tables are World's Sprint-2 deliverable and land
 * after this seam) — but a PRESENT file validates strictly (the fail-fast raws contract).
 */
public final class BarkRawsLoader {

    private static final String BARKS_FILE = "barks/barks.json";

    private static final List<String> TOP_FIELDS = List.of("id", "tables", "notes", "provenance");
    private static final List<String> TABLE_FIELDS = List.of(
            "key", "rows", "canonAnchor", "provenance", "notes");

    private BarkRawsLoader() {
    }

    /**
     * Loads {@code barks/barks.json} under a raws root, or {@link BarkTableRegistry#EMPTY}
     * when the file does not exist yet (the sparse-authoring contract above).
     *
     * @throws ActorRawsValidationException on the first validation failure of a PRESENT file
     * @throws UncheckedIOException         if a present file cannot be read
     */
    public static BarkTableRegistry load(Path rawsRoot) {
        java.util.Objects.requireNonNull(rawsRoot, "rawsRoot");
        Path file = rawsRoot.resolve("barks").resolve("barks.json");
        if (!Files.isRegularFile(file)) {
            return BarkTableRegistry.EMPTY; // unauthored yet: the wired-degraded mode
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
    public static BarkTableRegistry parse(byte[] bytes) {
        JsonValue tree;
        try {
            tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);
        } catch (JsonParseException e) {
            throw new ActorRawsValidationException(BARKS_FILE,
                    ActorRawsValidationException.NO_FIELD, "malformed JSON: " + e.getMessage());
        }
        if (!(tree instanceof JsonObject root)) {
            throw new ActorRawsValidationException(BARKS_FILE,
                    ActorRawsValidationException.NO_FIELD, "top-level value must be an object");
        }
        for (JsonObject.Member member : root.members()) {
            if (!TOP_FIELDS.contains(member.name())) {
                throw new ActorRawsValidationException(BARKS_FILE, member.name(), "unknown field");
            }
        }
        if (!(root.get("id") instanceof JsonString)) {
            throw new ActorRawsValidationException(BARKS_FILE, "id",
                    "required string field is missing");
        }
        if (!(root.get("tables") instanceof JsonArray tables) || tables.size() == 0) {
            throw new ActorRawsValidationException(BARKS_FILE, "tables",
                    "required non-empty array is missing");
        }
        List<BarkTableRegistry.BarkTable> parsed = new ArrayList<>(tables.size());
        for (int i = 0; i < tables.size(); i++) {
            parsed.add(parseTable(i, tables.get(i)));
        }
        try {
            return BarkTableRegistry.of(parsed);
        } catch (IllegalArgumentException e) {
            throw new ActorRawsValidationException(BARKS_FILE, "tables", e.getMessage());
        }
    }

    private static BarkTableRegistry.BarkTable parseTable(int index, JsonValue value) {
        String where = "tables[" + index + "]";
        if (!(value instanceof JsonObject table)) {
            throw new ActorRawsValidationException(BARKS_FILE, where, "must be an object");
        }
        for (JsonObject.Member member : table.members()) {
            if (!TABLE_FIELDS.contains(member.name())) {
                throw new ActorRawsValidationException(BARKS_FILE,
                        where + "." + member.name(), "unknown field");
            }
        }
        if (!(table.get("key") instanceof JsonString key) || key.value().isEmpty()) {
            throw new ActorRawsValidationException(BARKS_FILE, where + ".key",
                    "required non-empty string is missing");
        }
        if (!(table.get("rows") instanceof JsonArray rows) || rows.size() == 0) {
            throw new ActorRawsValidationException(BARKS_FILE, where + ".rows",
                    "required non-empty array is missing");
        }
        List<String> texts = new ArrayList<>(rows.size());
        for (int r = 0; r < rows.size(); r++) {
            if (!(rows.get(r) instanceof JsonString s) || s.value().isEmpty()) {
                throw new ActorRawsValidationException(BARKS_FILE,
                        where + ".rows[" + r + "]", "must be a non-empty string");
            }
            texts.add(s.value());
        }
        return new BarkTableRegistry.BarkTable(key.value(), texts);
    }
}
