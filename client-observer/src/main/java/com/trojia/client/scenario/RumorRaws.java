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
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Strict, fail-fast reader for {@code content/raws/rumors/rumors.json} — the Sprint-4
 * rumor knowledge-domains ("the rumor verb"): WHO can gossip WHICH authored micro-history
 * beyond its own two parties. Shape-validates only; cross-references (history ids against
 * histories.json, knower ids against the bound notable map, knower-is-not-a-party) are
 * enforced by {@link AskTopicsBake} at bake, where both vocabularies are in hand — the
 * {@code FactionLeaningsBake} split precedent.
 *
 * <p><b>Degraded mode:</b> a missing file loads the empty list (parties still know their
 * own stories through {@link AskTopicsBake}; the wired-degraded contract) — but a PRESENT
 * file validates strictly.
 */
final class RumorRaws {

    /**
     * One knowledge domain: the extra souls who can speak {@code gossip.<history>}.
     *
     * @param history the histories.json id this domain extends
     * @param knowers notables.json ids beyond the history's own parties, file order
     * @param why     the authored rationale (doc-only, like leanings.json's)
     */
    record Domain(String history, List<String> knowers, String why) {
    }

    private RumorRaws() {
    }

    static List<Domain> load(Path rumorsJson) {
        if (!Files.isRegularFile(rumorsJson)) {
            return List.of(); // unauthored yet: parties-only gossip (wired-degraded)
        }
        JsonValue tree;
        try {
            tree = MiniJson.parse(Files.readAllBytes(rumorsJson), JsonNumberMode.INTEGER_ONLY);
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read " + rumorsJson, e);
        } catch (JsonParseException e) {
            throw new IllegalStateException(rumorsJson + ": " + e.getMessage(), e);
        }
        if (!(tree instanceof JsonObject root)
                || !(root.get("domains") instanceof JsonArray array)) {
            throw new IllegalStateException(
                    rumorsJson + ": root must be an object with a \"domains\" array");
        }
        List<Domain> out = new ArrayList<>(array.size());
        Set<String> seenHistories = new HashSet<>();
        for (JsonValue element : array.values()) {
            if (!(element instanceof JsonObject entry)) {
                throw new IllegalStateException(rumorsJson + ": every domain must be an object");
            }
            Domain domain = parseOne(rumorsJson, entry);
            if (!seenHistories.add(domain.history())) {
                throw new IllegalStateException(rumorsJson + ": history \"" + domain.history()
                        + "\" declares more than one domain row");
            }
            out.add(domain);
        }
        return List.copyOf(out);
    }

    private static Domain parseOne(Path file, JsonObject entry) {
        String history = requireString(file, entry, "history");
        if (!(entry.get("knowers") instanceof JsonArray knowersJson) || knowersJson.size() == 0) {
            throw new IllegalStateException(file + ": domain \"" + history
                    + "\" needs a non-empty \"knowers\" array");
        }
        List<String> knowers = new ArrayList<>(knowersJson.size());
        for (JsonValue value : knowersJson.values()) {
            if (!(value instanceof JsonString knower) || knower.value().isBlank()) {
                throw new IllegalStateException(file + ": domain \"" + history
                        + "\" knowers must be non-blank strings");
            }
            if (knowers.contains(knower.value())) {
                throw new IllegalStateException(file + ": domain \"" + history
                        + "\" repeats knower \"" + knower.value() + "\"");
            }
            knowers.add(knower.value());
        }
        return new Domain(history, List.copyOf(knowers), requireString(file, entry, "why"));
    }

    private static String requireString(Path file, JsonObject entry, String field) {
        if (!(entry.get(field) instanceof JsonString string) || string.value().isBlank()) {
            throw new IllegalStateException(
                    file + ": domain field \"" + field + "\" must be a non-blank string");
        }
        return string.value();
    }
}
