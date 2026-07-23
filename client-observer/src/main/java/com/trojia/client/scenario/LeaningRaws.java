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
 * Strict, fail-fast reader for {@code content/raws/factions/leanings.json} — the Sprint-2
 * per-actor faction-roster refinement: authored standing seeds for notables whose
 * job→faction mapping is too coarse (a Temple-devout serf captain, a Merchant-hostile
 * wastrel), applied at bake as deterministic {@code FactionStandings} deltas on the
 * notable's spawn-site-bound actor id.
 *
 * <p><b>Bars guard (authored contract, re-checked at load):</b> negative {@code watch}
 * seeds must stay above {@code -20} — below the Barter surcharge band and far above the
 * refusal line — so no authored leaning can price anyone out of food or move the
 * starvation bars. Faction keys are validated against the wired registry at bake
 * ({@link FactionLeaningsBake}), not here.
 */
final class LeaningRaws {

    /** The strictest negative watch seed the raws may author (exclusive). */
    static final int MIN_WATCH_SEED = -19;

    /**
     * One authored leaning.
     *
     * @param notable  the notables.json id whose bound actor takes the seed
     * @param faction  the factions.json key the seed lands on
     * @param standing the seeded standing delta, in {@code [-100, 100]}, nonzero
     */
    record Leaning(String notable, String faction, int standing) {
    }

    private LeaningRaws() {
    }

    static List<Leaning> load(Path leaningsJson) {
        JsonValue tree;
        try {
            tree = MiniJson.parse(Files.readAllBytes(leaningsJson), JsonNumberMode.INTEGER_ONLY);
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read " + leaningsJson, e);
        } catch (JsonParseException e) {
            throw new IllegalStateException(leaningsJson + ": " + e.getMessage(), e);
        }
        if (!(tree instanceof JsonObject root)
                || !(root.get("leanings") instanceof JsonArray array)) {
            throw new IllegalStateException(
                    leaningsJson + ": root must be an object with a \"leanings\" array");
        }
        List<Leaning> out = new ArrayList<>(array.size());
        Set<String> seen = new HashSet<>();
        for (JsonValue element : array.values()) {
            if (!(element instanceof JsonObject entry)) {
                throw new IllegalStateException(leaningsJson + ": every leaning must be an object");
            }
            Leaning leaning = parseOne(leaningsJson, entry);
            if (!seen.add(leaning.notable() + "|" + leaning.faction())) {
                throw new IllegalStateException(leaningsJson + ": duplicate leaning for notable \""
                        + leaning.notable() + "\" and faction \"" + leaning.faction() + "\"");
            }
            out.add(leaning);
        }
        if (out.isEmpty()) {
            throw new IllegalStateException(leaningsJson + ": \"leanings\" must not be empty");
        }
        return List.copyOf(out);
    }

    private static Leaning parseOne(Path file, JsonObject entry) {
        String notable = requireString(file, entry, "notable");
        String faction = requireString(file, entry, "faction");
        if (!(entry.get("standing") instanceof JsonNumber number)) {
            throw new IllegalStateException(file + ": leaning \"" + notable
                    + "\" standing must be an integer");
        }
        int standing = number.asInt();
        if (standing == 0 || standing < -100 || standing > 100) {
            throw new IllegalStateException(file + ": leaning \"" + notable + "\"/\"" + faction
                    + "\" standing must be nonzero in [-100, 100], got " + standing);
        }
        if (faction.equals("watch") && standing < MIN_WATCH_SEED) {
            throw new IllegalStateException(file + ": leaning \"" + notable
                    + "\" watch seed " + standing + " breaks the bars guard (must be >= "
                    + MIN_WATCH_SEED + " - the Barter surcharge band starts at -20)");
        }
        return new Leaning(notable, faction, standing);
    }

    private static String requireString(Path file, JsonObject entry, String field) {
        if (!(entry.get(field) instanceof JsonString string) || string.value().isBlank()) {
            throw new IllegalStateException(
                    file + ": leaning field \"" + field + "\" must be a non-blank string");
        }
        return string.value();
    }
}
