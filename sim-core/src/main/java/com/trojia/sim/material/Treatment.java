package com.trojia.sim.material;

import com.trojia.sim.json.JsonArray;
import com.trojia.sim.json.JsonNumber;
import com.trojia.sim.json.JsonObject;
import com.trojia.sim.json.JsonString;
import com.trojia.sim.json.JsonValue;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * One treatment raw: a recipe that <em>mints a derived material at load</em>
 * (ARCHITECTURE.md §3, §10 — getilia soak mints
 * {@code trudgeon_wood@getilia_soak}). Minting is a pure JSON-tree transform of
 * the target material's raw source, so the derived material flows through
 * exactly the same parse-and-validate path as a hand-written raw — a minted
 * material can never dodge a §10 invariant.
 *
 * <p><strong>Semantics (BLESSING-QUEUE ruling 2):</strong></p>
 * <ul>
 *   <li>{@code overrides} — absolute member replacement, <em>including</em>
 *       JSON {@code null} (e.g. {@code "ignitionK": null} erases the base
 *       ignition point);</li>
 *   <li>{@code scaleQ8} — {@code derived = floor(base * value / 256)} on an
 *       integer member;</li>
 *   <li>{@code addTags} — appended to the base {@code tags} array in order.</li>
 * </ul>
 *
 * @param key                unique string id from the raw
 * @param displayName        human-readable treatment name
 * @param targetId           string id of the base material; must exist
 * @param derivedId          string id minted for the derived material; must not
 *                           collide with any material or derived id
 * @param derivedDisplayName human-readable name of the derived material
 * @param overrides          member-name → replacement-value object (may be empty)
 * @param scaleQ8            member-name → Q8 scale-factor object (may be empty)
 * @param addTags            tags appended to the base tag list (may be empty)
 */
public record Treatment(
        String key,
        String displayName,
        String targetId,
        String derivedId,
        String derivedDisplayName,
        JsonObject overrides,
        JsonObject scaleQ8,
        List<String> addTags) {

    /**
     * Validates presence of all parts and defensively copies the tag list.
     *
     * @throws NullPointerException if any component is {@code null}
     */
    public Treatment {
        Objects.requireNonNull(key, "key");
        Objects.requireNonNull(displayName, "displayName");
        Objects.requireNonNull(targetId, "targetId");
        Objects.requireNonNull(derivedId, "derivedId");
        Objects.requireNonNull(derivedDisplayName, "derivedDisplayName");
        Objects.requireNonNull(overrides, "overrides");
        Objects.requireNonNull(scaleQ8, "scaleQ8");
        addTags = List.copyOf(addTags);
    }

    /**
     * Mints the derived material's raw source tree from the base material's
     * source tree: sets {@code id}/{@code displayName} to the derived pair,
     * applies {@code overrides} (absolute replace, {@code null} included), then
     * {@code scaleQ8} ({@code floor(base * v / 256)}), then appends
     * {@code addTags}. Member order of the base tree is preserved; members
     * introduced by {@code overrides} are appended in override order.
     *
     * @param baseSource the parsed raw tree of the target material
     * @return the derived material's source tree, ready for the standard
     *         parse-and-validate path
     * @throws NullPointerException     if {@code baseSource} is {@code null}
     * @throws IllegalArgumentException if a {@code scaleQ8} member names a
     *                                  missing or non-integer base member, or a
     *                                  scale factor is not a positive integer
     *                                  (message names the offending field; the
     *                                  loader adds file context)
     */
    public JsonObject mint(JsonObject baseSource) {
        Objects.requireNonNull(baseSource, "baseSource");
        List<JsonObject.Member> members = new ArrayList<>();
        for (JsonObject.Member member : baseSource.members()) {
            members.add(switch (member.name()) {
                case "id" -> new JsonObject.Member("id", new JsonString(derivedId));
                case "displayName" ->
                        new JsonObject.Member("displayName", new JsonString(derivedDisplayName));
                default -> member;
            });
        }
        for (JsonObject.Member override : overrides.members()) {
            replaceOrAppend(members, override.name(), override.value());
        }
        for (JsonObject.Member scale : scaleQ8.members()) {
            applyScale(members, scale.name(), scale.value());
        }
        if (!addTags.isEmpty()) {
            appendTags(members);
        }
        return new JsonObject(members);
    }

    private static void replaceOrAppend(List<JsonObject.Member> members, String name,
            JsonValue value) {
        for (int i = 0; i < members.size(); i++) {
            if (members.get(i).name().equals(name)) {
                members.set(i, new JsonObject.Member(name, value));
                return;
            }
        }
        members.add(new JsonObject.Member(name, value));
    }

    private void applyScale(List<JsonObject.Member> members, String name, JsonValue factorValue) {
        if (!(factorValue instanceof JsonNumber factorNumber) || !factorNumber.isIntegral()
                || factorNumber.asLong() < 1) {
            throw new IllegalArgumentException(
                    "scaleQ8." + name + ": scale factor must be a positive integer");
        }
        long factor = factorNumber.asLong();
        for (int i = 0; i < members.size(); i++) {
            JsonObject.Member member = members.get(i);
            if (!member.name().equals(name)) {
                continue;
            }
            if (!(member.value() instanceof JsonNumber base) || !base.isIntegral()) {
                throw new IllegalArgumentException(
                        "scaleQ8." + name + ": base member is not an integer");
            }
            long scaled = Math.floorDiv(base.asLong() * factor, 256L);
            members.set(i, new JsonObject.Member(name, JsonNumber.of(scaled)));
            return;
        }
        throw new IllegalArgumentException(
                "scaleQ8." + name + ": base material has no such member");
    }

    private void appendTags(List<JsonObject.Member> members) {
        for (int i = 0; i < members.size(); i++) {
            JsonObject.Member member = members.get(i);
            if (!member.name().equals("tags")) {
                continue;
            }
            if (!(member.value() instanceof JsonArray baseTags)) {
                throw new IllegalArgumentException("addTags: base member \"tags\" is not an array");
            }
            List<JsonValue> merged = new ArrayList<>(baseTags.values());
            for (String tag : addTags) {
                merged.add(new JsonString(tag));
            }
            members.set(i, new JsonObject.Member("tags", new JsonArray(merged)));
            return;
        }
        List<JsonValue> fresh = new ArrayList<>();
        for (String tag : addTags) {
            fresh.add(new JsonString(tag));
        }
        members.add(new JsonObject.Member("tags", new JsonArray(fresh)));
    }
}
