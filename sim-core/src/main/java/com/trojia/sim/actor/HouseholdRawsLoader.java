package com.trojia.sim.actor;

import com.trojia.sim.json.JsonArray;
import com.trojia.sim.json.JsonNumber;
import com.trojia.sim.json.JsonNumberMode;
import com.trojia.sim.json.JsonObject;
import com.trojia.sim.json.JsonParseException;
import com.trojia.sim.json.JsonValue;
import com.trojia.sim.json.MiniJson;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * Strict, fail-fast reader for {@code content/raws/actors/household.json}
 * (ACTORS-SPEC.md §11.5).
 */
public final class HouseholdRawsLoader {

    private static final List<String> ROOT_FIELDS = List.of(
            "version", "householdSizeWeights", "staffCountRange",
            "neighborFlavorCountRange", "friendFlavorCountRange");

    private HouseholdRawsLoader() {
    }

    public static HouseholdRaws load(Path householdJsonFile) {
        String file = householdJsonFile.getFileName().toString();
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(householdJsonFile);
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading " + householdJsonFile, e);
        }
        JsonValue tree;
        try {
            tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);
        } catch (JsonParseException e) {
            throw new ActorRawsValidationException(file, ActorRawsValidationException.NO_FIELD,
                    "malformed JSON: " + e.getMessage());
        }
        if (!(tree instanceof JsonObject root)) {
            throw new ActorRawsValidationException(file, ActorRawsValidationException.NO_FIELD,
                    "top-level value must be an object");
        }
        for (JsonObject.Member member : root.members()) {
            if (!ROOT_FIELDS.contains(member.name())) {
                throw new ActorRawsValidationException(file, member.name(), "unknown field");
            }
        }
        requireVersionOne(file, root);
        int[] weights = parseWeights(file, root);
        int[] staffRange = parseRange(file, root, "staffCountRange");
        int[] neighborRange = parseRange(file, root, "neighborFlavorCountRange");
        int[] friendRange = parseRange(file, root, "friendFlavorCountRange");
        return new HouseholdRaws(weights, staffRange[0], staffRange[1],
                neighborRange[0], neighborRange[1], friendRange[0], friendRange[1]);
    }

    private static void requireVersionOne(String file, JsonObject root) {
        JsonValue version = root.get("version");
        if (!(version instanceof JsonNumber n) || !n.isIntegral() || n.asLong() != 1) {
            throw new ActorRawsValidationException(file, "version", "must be integer 1");
        }
    }

    private static int[] parseWeights(String file, JsonObject root) {
        JsonValue value = root.get("householdSizeWeights");
        if (!(value instanceof JsonObject weightsObj) || weightsObj.size() == 0) {
            throw new ActorRawsValidationException(file, "householdSizeWeights",
                    "required non-empty object is missing");
        }
        // Keys are the household size "1".."N"; stored densely by (size - 1).
        int max = 0;
        for (JsonObject.Member member : weightsObj.members()) {
            int size = parsePositiveIntKey(file, "householdSizeWeights", member.name());
            max = Math.max(max, size);
        }
        int[] weights = new int[max];
        for (JsonObject.Member member : weightsObj.members()) {
            int size = Integer.parseInt(member.name());
            if (!(member.value() instanceof JsonNumber n) || !n.isIntegral() || n.asLong() < 0) {
                throw new ActorRawsValidationException(file,
                        "householdSizeWeights." + member.name(), "weight must be a non-negative integer");
            }
            weights[size - 1] = n.asInt();
        }
        return weights;
    }

    private static int parsePositiveIntKey(String file, String field, String key) {
        try {
            int value = Integer.parseInt(key);
            if (value < 1) {
                throw new NumberFormatException();
            }
            return value;
        } catch (NumberFormatException e) {
            throw new ActorRawsValidationException(file, field,
                    "key \"" + key + "\" must be a positive integer household size");
        }
    }

    private static int[] parseRange(String file, JsonObject root, String field) {
        JsonValue value = root.get(field);
        if (!(value instanceof JsonArray array) || array.size() != 2) {
            throw new ActorRawsValidationException(file, field, "must be a 2-element [min, max] array");
        }
        int min = intAt(file, field, array, 0);
        int max = intAt(file, field, array, 1);
        if (min < 0 || max < min) {
            throw new ActorRawsValidationException(file, field,
                    "invalid range [" + min + ", " + max + "]");
        }
        return new int[] {min, max};
    }

    private static int intAt(String file, String field, JsonArray array, int index) {
        if (!(array.get(index) instanceof JsonNumber n) || !n.isIntegral()) {
            throw new ActorRawsValidationException(file, field + "[" + index + "]", "must be an integer");
        }
        return n.asInt();
    }
}
