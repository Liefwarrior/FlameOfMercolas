package com.trojia.client.scenario;

import com.trojia.sim.actor.RelationshipKind;
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
 * Strict, fail-fast reader for {@code content/raws/names/histories.json} — the Sprint-2
 * authored micro-histories (S2-1 "the stories between them"): feuds, debts, romances and
 * pacts among the Forty Notables, realized at bake as real {@code RelationshipRegistry}
 * edges plus a one-sentence bio addendum per side (appended by {@link NameForge}).
 *
 * <p><b>Edge vocabulary</b> (the CURRENT append-only {@code RelationshipKind} enum — the
 * file may only name kinds the sim already serializes): {@code NEIGHBOR} (feuds: a
 * known-adversary adjacency tie; the hostility itself is carried by bios and gossip),
 * {@code MENTOR} (debts: directed {@code a}=creditor → {@code b}=debtor, the "oversees"
 * sense), {@code FRIEND} (romances/pacts/secrets: symmetric — BarkSelector greets these
 * pairs as friends and Barter grants its kitchen-price discount, deliberately).
 * {@code HOUSEHOLD} and {@code EMPLOYER} are forbidden here: the first would merge surname
 * components, the second would rewrite forged "in X's pay" bios.
 */
final class HistoryRaws {

    /** The edge kinds an authored history may realize (see class Javadoc). */
    private static final Set<RelationshipKind> ALLOWED_EDGES = Set.of(
            RelationshipKind.NEIGHBOR, RelationshipKind.FRIEND, RelationshipKind.MENTOR);

    private static final Set<String> ALLOWED_KINDS = Set.of(
            "feud", "debt", "romance", "pact", "secret");

    /**
     * One authored micro-history.
     *
     * @param id     stable history id (kebab/lowercase), unique in the file
     * @param kind   the story flavor: {@code feud|debt|romance|pact|secret}
     * @param a      first party's notables.json id (the creditor/senior for MENTOR)
     * @param b      second party's notables.json id (the debtor/junior for MENTOR)
     * @param edge   the RelationshipKind realized at bake (directed a→b for MENTOR)
     * @param bioA   the sentence appended to a's bio
     * @param bioB   the sentence appended to b's bio
     * @param gossip the barks.json gossip table key that tells this story on the street
     */
    record History(String id, String kind, String a, String b, RelationshipKind edge,
            String bioA, String bioB, String gossip) {
    }

    private HistoryRaws() {
    }

    static List<History> load(Path historiesJson) {
        JsonValue tree;
        try {
            tree = MiniJson.parse(Files.readAllBytes(historiesJson), JsonNumberMode.INTEGER_ONLY);
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read " + historiesJson, e);
        } catch (JsonParseException e) {
            throw new IllegalStateException(historiesJson + ": " + e.getMessage(), e);
        }
        if (!(tree instanceof JsonObject root)
                || !(root.get("histories") instanceof JsonArray array)) {
            throw new IllegalStateException(
                    historiesJson + ": root must be an object with a \"histories\" array");
        }
        List<History> out = new ArrayList<>(array.size());
        Set<String> seenIds = new HashSet<>();
        Set<String> seenPairs = new HashSet<>();
        for (JsonValue element : array.values()) {
            if (!(element instanceof JsonObject entry)) {
                throw new IllegalStateException(historiesJson + ": every history must be an object");
            }
            History history = parseOne(historiesJson, entry);
            if (!seenIds.add(history.id())) {
                throw new IllegalStateException(
                        historiesJson + ": duplicate history id \"" + history.id() + "\"");
            }
            String pairKey = pairKey(history);
            if (!seenPairs.add(pairKey)) {
                throw new IllegalStateException(historiesJson + ": history \"" + history.id()
                        + "\" repeats the pair+edge " + pairKey);
            }
            out.add(history);
        }
        if (out.isEmpty()) {
            throw new IllegalStateException(historiesJson + ": \"histories\" must not be empty");
        }
        return List.copyOf(out);
    }

    /** The unordered-pair + kind uniqueness key (one authored edge per pair and kind). */
    private static String pairKey(History history) {
        String lo = history.a().compareTo(history.b()) <= 0 ? history.a() : history.b();
        String hi = lo.equals(history.a()) ? history.b() : history.a();
        return lo + "|" + hi + "|" + history.edge();
    }

    private static History parseOne(Path file, JsonObject entry) {
        String id = requireString(file, entry, "id");
        String kind = requireString(file, entry, "kind");
        if (!ALLOWED_KINDS.contains(kind)) {
            throw new IllegalStateException(file + ": history \"" + id + "\" kind \"" + kind
                    + "\" is not one of " + ALLOWED_KINDS);
        }
        String a = requireString(file, entry, "a");
        String b = requireString(file, entry, "b");
        if (a.equals(b)) {
            throw new IllegalStateException(
                    file + ": history \"" + id + "\" needs two distinct parties");
        }
        String edgeName = requireString(file, entry, "edge");
        RelationshipKind edge;
        try {
            edge = RelationshipKind.valueOf(edgeName);
        } catch (IllegalArgumentException e) {
            throw new IllegalStateException(file + ": history \"" + id + "\" names unknown edge kind \""
                    + edgeName + "\"", e);
        }
        if (!ALLOWED_EDGES.contains(edge)) {
            throw new IllegalStateException(file + ": history \"" + id + "\" edge " + edge
                    + " is not an authored-history kind (allowed: " + ALLOWED_EDGES + ")");
        }
        String gossip = requireString(file, entry, "gossip");
        if (!gossip.startsWith("gossip.")) {
            throw new IllegalStateException(file + ": history \"" + id
                    + "\" gossip key must start with \"gossip.\", got \"" + gossip + "\"");
        }
        return new History(id, kind, a, b, edge,
                requireBio(file, id, entry, "bioA"), requireBio(file, id, entry, "bioB"), gossip);
    }

    /** Bio addenda ride {@code IdentityRegistry.canonicalTable()}'s |-and-newline framing. */
    private static String requireBio(Path file, String id, JsonObject entry, String field) {
        String bio = requireString(file, entry, field);
        if (bio.indexOf('|') >= 0 || bio.indexOf('\n') >= 0) {
            throw new IllegalStateException(file + ": history \"" + id + "\" " + field
                    + " must not contain '|' or newlines (canonical-table framing)");
        }
        return bio;
    }

    private static String requireString(Path file, JsonObject entry, String field) {
        if (!(entry.get(field) instanceof JsonString string) || string.value().isBlank()) {
            throw new IllegalStateException(
                    file + ": history field \"" + field + "\" must be a non-blank string");
        }
        return string.value();
    }
}
