package com.trojia.sim.actor;

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
import java.util.Comparator;
import java.util.List;
import java.util.stream.Stream;

/**
 * Strict, fail-fast reader for {@code content/raws/actors/<type_id>.json}
 * (ACTORS-SPEC.md §6). {@code household.json} lives in the same directory but
 * is a different, cross-cutting schema (§11.5) — skipped here by name.
 *
 * <p>Scoped to what this foundation milestone's starter policy library
 * consumes ({@link ActorTypeStats}) rather than the full v1 §6 schema
 * (scuffle stats, sightRadiusByLight, per-policy param blobs for the entire
 * policy library) — a later loader extension adds those fields without
 * touching this class's fail-fast shape.
 */
public final class ActorRawsLoader {

    private static final String HOUSEHOLD_FILE = "household.json";
    private static final List<String> ROOT_FIELDS = List.of(
            "id", "displayName", "glyph", "tint", "factionId", "hp", "speedTicksPerStep",
            "leashRadius", "inventoryCap", "needs", "deferWielder", "flee", "seekFood",
            "returnHome", "loiter");
    private static final List<String> NEED_KEYS =
            List.of("hunger", "rest", "coin", "safety", "duty");
    private static final List<String> NEED_FIELDS =
            List.of("start", "decayPerKilotick", "recoverPerTick", "lowBonus", "critBonus");
    private static final List<String> DEFER_FIELDS = List.of("enabled", "priority", "radius");
    private static final List<String> FLEE_FIELDS = List.of("priority");
    private static final List<String> SEEK_FOOD_FIELDS = List.of("priority");
    private static final List<String> RETURN_HOME_FIELDS =
            List.of("priority", "rhythmBonus", "nightWindowStart", "nightWindowEnd");
    private static final List<String> LOITER_FIELDS = List.of("priority");

    private ActorRawsLoader() {
    }

    /** Loads every {@code *.json} actor-type raw under {@code actorsRawsDir} (sorted-name order). */
    public static ActorTypeStatsTable load(Path actorsRawsDir) {
        List<Path> files;
        try (Stream<Path> listing = Files.list(actorsRawsDir)) {
            files = listing
                    .filter(Files::isRegularFile)
                    .filter(p -> p.getFileName().toString().endsWith(".json"))
                    .filter(p -> !HOUSEHOLD_FILE.equals(p.getFileName().toString()))
                    .sorted(Comparator.comparing(p -> p.getFileName().toString()))
                    .toList();
        } catch (IOException e) {
            throw new UncheckedIOException("failed listing actor raws dir " + actorsRawsDir, e);
        }
        List<ActorTypeStats> parsed = new ArrayList<>(files.size());
        for (Path file : files) {
            parsed.add(parseFile(file));
        }
        crossCheckAgainstRegisteredTypes(actorsRawsDir, parsed);
        return ActorTypeStatsTable.of(parsed);
    }

    /**
     * Fail-fast, both-directions cross-check against {@link ActorTypes#ALL} — the actor-type
     * analogue of {@code JobBinder}'s 1:1 job/leaf validation. Without this, a typo'd {@code id}
     * field or a newly-registered {@link ActorTypes} entry missing its raws file both load
     * silently and surface only as a lazy {@code IllegalArgumentException} the first time
     * {@link ActorTypeStatsTable#get} is asked for that type (e.g. at first spawn), which a
     * scenario that never exercises the type could ship without ever hitting.
     */
    private static void crossCheckAgainstRegisteredTypes(Path actorsRawsDir,
            List<ActorTypeStats> parsed) {
        String dir = actorsRawsDir.toString();

        // No duplicate ids across raws files (e.g. a copy-paste "id" typo colliding with
        // another file's real id).
        for (int i = 0; i < parsed.size(); i++) {
            for (int j = i + 1; j < parsed.size(); j++) {
                if (parsed.get(i).typeId().equals(parsed.get(j).typeId())) {
                    throw new ActorRawsValidationException(dir, "id",
                            "duplicate actor type id \"" + parsed.get(i).typeId()
                                    + "\" across raws files");
                }
            }
        }

        // Every parsed raws id must match a registered ActorTypes.ALL entry — otherwise a
        // typo'd id is silently accepted as a "valid" but orphaned type, while the actor
        // subclass it was meant to bind stays unbound.
        for (ActorTypeStats stats : parsed) {
            boolean registered = false;
            for (ActorTypes.Registration reg : ActorTypes.ALL) {
                if (reg.id().equals(stats.typeId())) {
                    registered = true;
                    break;
                }
            }
            if (!registered) {
                throw new ActorRawsValidationException(dir, "id",
                        "actor raws id \"" + stats.typeId() + "\" has no registered "
                                + "ActorTypes.ALL entry (Actor subclass) — either the id is "
                                + "misspelled or the type was never registered");
            }
        }

        // Every registered ActorTypes.ALL entry must have a matching raws file — adding a
        // new Actor subclass without (or with a misspelled) content/raws/actors/<type>.json
        // must fail at load time, not lazily at first spawn/stat lookup.
        for (ActorTypes.Registration reg : ActorTypes.ALL) {
            boolean bound = false;
            for (ActorTypeStats stats : parsed) {
                if (stats.typeId().equals(reg.id())) {
                    bound = true;
                    break;
                }
            }
            if (!bound) {
                throw new ActorRawsValidationException(dir, "id",
                        "registered actor type \"" + reg.id() + "\" (ActorTypes.ALL) has no "
                                + "content/raws/actors/*.json entry — every registered type "
                                + "needs data");
            }
        }
    }

    private static ActorTypeStats parseFile(Path path) {
        String name = path.getFileName().toString();
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(path);
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading " + path, e);
        }
        JsonValue tree;
        try {
            tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);
        } catch (JsonParseException e) {
            throw new ActorRawsValidationException(name, ActorRawsValidationException.NO_FIELD,
                    "malformed JSON: " + e.getMessage());
        }
        if (!(tree instanceof JsonObject root)) {
            throw new ActorRawsValidationException(name, ActorRawsValidationException.NO_FIELD,
                    "top-level value must be an object");
        }
        rejectUnknown(name, "", root, ROOT_FIELDS);

        ActorTypeId typeId = ActorTypeId.of(requireString(name, "id", root, "id"));
        String displayName = requireString(name, "displayName", root, "displayName");
        char glyph = requireGlyph(name, root);
        int tint = requireTint(name, root);
        String factionId = requireString(name, "factionId", root, "factionId");
        short hp = (short) requireInt(name, "hp", root, "hp", 1, Short.MAX_VALUE);
        int speedTicksPerStep = requireInt(name, "speedTicksPerStep", root, "speedTicksPerStep", 1, 1000);
        int leashRadius = requireInt(name, "leashRadius", root, "leashRadius", 0, 4096);
        int inventoryCap = requireInt(name, "inventoryCap", root, "inventoryCap", 0, 255);

        NeedConfig[] needs = parseNeeds(name, root);

        JsonObject defer = optionalObject(name, root, "deferWielder");
        boolean hasDefer = defer != null && requireBool(name, "deferWielder.enabled", defer, "enabled");
        int deferPriority = 0;
        int deferRadius = 0;
        if (defer != null) {
            rejectUnknown(name, "deferWielder.", defer, DEFER_FIELDS);
            if (hasDefer) {
                deferPriority = requireInt(name, "deferWielder.priority", defer, "priority", 900, 999);
                deferRadius = requireInt(name, "deferWielder.radius", defer, "radius", 0, 4096);
            }
        }

        JsonObject flee = requireObject(name, root, "flee");
        rejectUnknown(name, "flee.", flee, FLEE_FIELDS);
        int fleePriority = requireInt(name, "flee.priority", flee, "priority", 900, 999);

        JsonObject seekFood = requireObject(name, root, "seekFood");
        rejectUnknown(name, "seekFood.", seekFood, SEEK_FOOD_FIELDS);
        int seekFoodPriority =
                requireInt(name, "seekFood.priority", seekFood, "priority", 300, 499);

        JsonObject returnHome = requireObject(name, root, "returnHome");
        rejectUnknown(name, "returnHome.", returnHome, RETURN_HOME_FIELDS);
        int returnHomePriority =
                requireInt(name, "returnHome.priority", returnHome, "priority", 300, 499);
        int returnHomeRhythmBonus =
                requireInt(name, "returnHome.rhythmBonus", returnHome, "rhythmBonus", 0, 1000);
        int nightStart = requireInt(name, "returnHome.nightWindowStart", returnHome,
                "nightWindowStart", 0, (int) DailyRhythm.DAY);
        int nightEnd = requireInt(name, "returnHome.nightWindowEnd", returnHome,
                "nightWindowEnd", nightStart, (int) DailyRhythm.DAY);

        // The needs-hierarchy invariant (design §2a): SEEK_FOOD (HUNGER) must always be able to
        // outrank RETURN_HOME (REST) at comparable urgency, so an actor never quietly prioritizes
        // sleep over starving. The priority floor is checked first, then every (HUNGER band x
        // REST band) combination the two policies can actually compete in.
        if (seekFoodPriority < returnHomePriority) {
            throw new ActorRawsValidationException(name, "seekFood.priority",
                    "seekFood.priority (" + seekFoodPriority + ") must be >= returnHome.priority ("
                            + returnHomePriority + ") so SEEK_FOOD always outranks RETURN_HOME");
        }
        NeedConfig hunger = needs[Need.HUNGER.ordinal()];
        NeedConfig rest = needs[Need.REST.ordinal()];
        // Exhaustive cross-band check (fixes a real validation gap): SEEK_FOOD and RETURN_HOME
        // each pick exactly one of {lowBonus, critBonus} depending on which threshold band their
        // own need (HUNGER, REST respectively) is in — independently of each other. A prior
        // version of this check only compared same-band pairs (LOW-vs-LOW, CRITICAL-vs-CRITICAL),
        // missing the cross-band case where HUNGER is merely LOW while REST is CRITICAL. Using the
        // real committed raws, 7 of 9 actor types had hunger.lowBonus < rest.critBonus, so an actor
        // with HUNGER low-not-critical and REST critical would have RETURN_HOME silently outscore
        // SEEK_FOOD — contradicting the "food first" invariant. Comparing every one of the 4
        // (hunger band x rest band) pairs directly here — rather than deriving the cross-band case
        // from the two same-band checks via NeedConfig's own lowBonus/critBonus monotonicity —
        // keeps this check self-evidently exhaustive and independent of that unrelated invariant.
        int[] hungerBonus = {hunger.lowBonus(), hunger.critBonus()};
        int[] restBonus = {rest.lowBonus(), rest.critBonus()};
        String[] hungerField = {"needs.hunger.lowBonus", "needs.hunger.critBonus"};
        String[] hungerBandLabel = {"lowBonus", "critBonus"};
        String[] restBandLabel = {"lowBonus", "critBonus"};
        String[] bandName = {"LOW", "CRITICAL"};
        for (int hi = 0; hi < 2; hi++) {
            for (int ri = 0; ri < 2; ri++) {
                int seekFoodScore = seekFoodPriority + hungerBonus[hi];
                int returnHomeScore = returnHomePriority + restBonus[ri];
                if (seekFoodScore < returnHomeScore) {
                    throw new ActorRawsValidationException(name, hungerField[hi],
                            "SEEK_FOOD score (seekFood.priority " + seekFoodPriority + " + hunger."
                                    + hungerBandLabel[hi] + " " + hungerBonus[hi] + " = " + seekFoodScore
                                    + ") must be >= RETURN_HOME score (returnHome.priority "
                                    + returnHomePriority + " + rest." + restBandLabel[ri] + " "
                                    + restBonus[ri] + " = " + returnHomeScore + ") when HUNGER is "
                                    + bandName[hi] + " and REST is " + bandName[ri] + ", so SEEK_FOOD "
                                    + "always outranks RETURN_HOME (excluding RETURN_HOME's "
                                    + "night-rhythm term, a deliberate schedule-effect exclusion)");
                }
            }
        }

        JsonObject loiter = requireObject(name, root, "loiter");
        rejectUnknown(name, "loiter.", loiter, LOITER_FIELDS);
        int loiterPriority = requireInt(name, "loiter.priority", loiter, "priority", 1, 99);

        return new ActorTypeStats(typeId, displayName,
                glyph, tint, factionId, hp, speedTicksPerStep, leashRadius, inventoryCap, needs,
                hasDefer, deferPriority, deferRadius, fleePriority, seekFoodPriority,
                returnHomePriority, returnHomeRhythmBonus, nightStart, nightEnd, loiterPriority);
    }

    private static NeedConfig[] parseNeeds(String file, JsonObject root) {
        JsonObject needsObj = requireObject(file, root, "needs");
        rejectUnknownExact(file, "needs.", needsObj, NEED_KEYS);
        NeedConfig[] configs = new NeedConfig[Need.COUNT];
        for (String key : NEED_KEYS) {
            JsonObject row = requireObject(file, needsObj, key);
            String prefix = "needs." + key + ".";
            rejectUnknown(file, prefix, row, NEED_FIELDS);
            int start = requireInt(file, prefix + "start", row, "start", 0, NeedThresholds.MAX);
            int decay = requireInt(file, prefix + "decayPerKilotick", row, "decayPerKilotick",
                    0, Integer.MAX_VALUE);
            int recover = optionalInt(file, row, "recoverPerTick", 0);
            int lowBonus = optionalInt(file, row, "lowBonus", 0);
            int critBonus = optionalInt(file, row, "critBonus", 0);
            configs[Need.valueOf(key.toUpperCase(java.util.Locale.ROOT)).ordinal()] =
                    new NeedConfig(start, decay, recover, lowBonus, critBonus);
        }
        return configs;
    }

    // ---------------------------------------------------------------- utilities

    private static void rejectUnknown(String file, String prefix, JsonObject object,
            List<String> allowed) {
        for (JsonObject.Member member : object.members()) {
            if (!allowed.contains(member.name())) {
                throw new ActorRawsValidationException(file, prefix + member.name(), "unknown field");
            }
        }
    }

    private static void rejectUnknownExact(String file, String prefix, JsonObject object,
            List<String> allowed) {
        rejectUnknown(file, prefix, object, allowed);
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
        if (!(value instanceof JsonNumber number) || !number.isIntegral()) {
            throw new ActorRawsValidationException(file, field, "required integer is missing");
        }
        long v = number.asLong();
        if (v < min || v > max) {
            throw new ActorRawsValidationException(file, field,
                    "value " + v + " out of range " + min + ".." + max);
        }
        return (int) v;
    }

    private static int optionalInt(String file, JsonObject object, String key, int absent) {
        JsonValue value = object.get(key);
        if (value == null) {
            return absent;
        }
        if (!(value instanceof JsonNumber number) || !number.isIntegral()) {
            throw new ActorRawsValidationException(file, key, "must be an integer");
        }
        return number.asInt();
    }

    private static JsonObject requireObject(String file, JsonObject object, String field) {
        JsonValue value = object.get(field);
        if (!(value instanceof JsonObject nested)) {
            throw new ActorRawsValidationException(file, field, "required object is "
                    + (value == null ? "missing" : "not an object"));
        }
        return nested;
    }

    private static JsonObject optionalObject(String file, JsonObject object, String field) {
        JsonValue value = object.get(field);
        if (value == null) {
            return null;
        }
        if (!(value instanceof JsonObject nested)) {
            throw new ActorRawsValidationException(file, field, "must be an object");
        }
        return nested;
    }

    private static char requireGlyph(String file, JsonObject root) {
        String s = requireString(file, "glyph", root, "glyph");
        if (s.length() != 1) {
            throw new ActorRawsValidationException(file, "glyph", "must be exactly one character");
        }
        return s.charAt(0);
    }

    private static int requireTint(String file, JsonObject root) {
        String literal = requireString(file, "tint", root, "tint");
        if (literal.length() != 7 || literal.charAt(0) != '#') {
            throw new ActorRawsValidationException(file, "tint",
                    "tint must be \"#RRGGBB\", got \"" + literal + "\"");
        }
        int rgb = 0;
        for (int i = 1; i < 7; i++) {
            int digit = Character.digit(literal.charAt(i), 16);
            if (digit < 0) {
                throw new ActorRawsValidationException(file, "tint",
                        "tint must be \"#RRGGBB\", got \"" + literal + "\"");
            }
            rgb = (rgb << 4) | digit;
        }
        return rgb;
    }
}
