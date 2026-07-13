package com.trojia.tools.validate;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.TreeSet;
import java.util.function.Consumer;
import java.util.stream.Stream;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import com.trojia.tools.validate.ValidationIssue.Severity;

/**
 * Parses {@code content/raws/**}{@code /*.json} (Jackson) into a {@link RawsIndex} and
 * reports raws consistency findings — the {@code check-raws} half of the validator.
 *
 * <p><strong>Categories</strong> are directory names directly under the raws root:
 * {@code materials}, {@code fluids}, {@code treatments}, {@code reactions}. Files in
 * other directories are skipped with a warning. Treatment raws mint their
 * {@code derivedId} into the material universe with {@code overrides} applied
 * (ARCHITECTURE.md section 10).</p>
 *
 * <p><strong>Determinism contract:</strong> files are visited in sorted relative-path
 * order; categories are processed materials → fluids → treatments → reactions; the
 * cross-reference sweep walks material ids in lexicographic order. The issue sequence
 * is therefore a pure function of the directory contents.</p>
 *
 * <p>Stateless and reusable; not synchronized (each call is independent).</p>
 */
public final class RawsLoader {

    private static final String PASS_ID = "raws";

    /** Categories this loader actually parses and cross-checks; see {@link #load}. */
    private static final Set<String> KNOWN_CATEGORIES =
            Set.of("materials", "fluids", "treatments", "reactions");

    private final ObjectMapper mapper = new ObjectMapper();

    /** Creates a loader. */
    public RawsLoader() {
    }

    /**
     * Loads and cross-checks every raws file under {@code rawsRoot}.
     *
     * @param rawsRoot the {@code content/raws} directory, never {@code null}
     * @return the distilled index plus all findings; never {@code null}
     * @throws UncheckedIOException     on directory walk failure
     * @throws IllegalArgumentException if {@code rawsRoot} is not a directory
     */
    public RawsLoadResult load(Path rawsRoot) {
        Objects.requireNonNull(rawsRoot, "rawsRoot");
        if (!Files.isDirectory(rawsRoot)) {
            throw new IllegalArgumentException("raws root is not a directory: " + rawsRoot);
        }
        List<ValidationIssue> issues = new ArrayList<>();
        Consumer<ValidationIssue> out = issues::add;

        List<ParsedRaw> materials = new ArrayList<>();
        List<ParsedRaw> fluids = new ArrayList<>();
        List<ParsedRaw> treatments = new ArrayList<>();
        List<ParsedRaw> reactions = new ArrayList<>();

        for (Path file : listJsonFiles(rawsRoot)) {
            String source = relativeName(rawsRoot, file);
            String category = categoryOf(rawsRoot, file);
            if (!KNOWN_CATEGORIES.contains(category)) {
                // This loader only cross-checks materials/fluids/treatments/reactions;
                // other raws categories (actors, jobs, skills, ...) are someone else's
                // contract (their own loaders/tests own their own hygiene), including
                // container/config files with no top-level "id" (e.g. jobs/jobs.json,
                // actors/household.json) — never even attempt the "id" requirement below.
                out.accept(new ValidationIssue(Severity.WARNING, PASS_ID, source, "",
                        ValidationIssue.NO_COORD, ValidationIssue.NO_COORD,
                        "unknown raws category \"" + category + "\"; file ignored.",
                        "known categories: materials, fluids, treatments, reactions."));
                continue;
            }
            JsonNode node;
            try {
                node = mapper.readTree(file.toFile());
            } catch (IOException e) {
                out.accept(error(source, "cannot parse JSON: " + firstLine(e.getMessage()),
                        "fix the JSON syntax; raws must be plain JSON objects."));
                continue;
            }
            String id = textOrNull(node, "id");
            if (id == null || id.isBlank()) {
                out.accept(error(source, "raw file has no \"id\" field.",
                        "every raw must declare a unique string \"id\"."));
                continue;
            }
            ParsedRaw raw = new ParsedRaw(source, id, node);
            switch (category) {
                case "materials" -> materials.add(raw);
                case "fluids" -> fluids.add(raw);
                case "treatments" -> treatments.add(raw);
                case "reactions" -> reactions.add(raw);
                default -> throw new IllegalStateException("unreachable: " + category);
            }
        }

        Map<String, Integer> flammability = new LinkedHashMap<>();
        Map<String, JsonNode> materialNodes = new LinkedHashMap<>();
        for (ParsedRaw raw : materials) {
            if (materialNodes.containsKey(raw.id())) {
                out.accept(error(raw.source(), "duplicate material id \"" + raw.id() + "\".",
                        "material ids must be unique across all files in raws/materials."));
                continue;
            }
            materialNodes.put(raw.id(), raw.node());
            flammability.put(raw.id(), raw.node().path("flammability").asInt(0));
        }

        TreeSet<String> fluidIds = new TreeSet<>();
        Map<String, JsonNode> fluidNodes = new LinkedHashMap<>();
        for (ParsedRaw raw : fluids) {
            if (!fluidIds.add(raw.id())) {
                out.accept(error(raw.source(), "duplicate fluid id \"" + raw.id() + "\".",
                        "fluid ids must be unique across all files in raws/fluids."));
                continue;
            }
            fluidNodes.put(raw.id(), raw.node());
        }

        for (ParsedRaw raw : treatments) {
            String target = textOrNull(raw.node(), "target");
            String derivedId = textOrNull(raw.node(), "derivedId");
            if (target == null || !materialNodes.containsKey(target)) {
                out.accept(error(raw.source(), "treatment target \"" + target + "\" is not a known material.",
                        "point \"target\" at an existing raws/materials id."));
                continue;
            }
            if (derivedId == null || derivedId.isBlank()) {
                out.accept(error(raw.source(), "treatment declares no \"derivedId\".",
                        "declare the minted id, e.g. \"" + target + "@" + raw.id() + "\"."));
                continue;
            }
            if (flammability.containsKey(derivedId)) {
                out.accept(error(raw.source(), "derived id \"" + derivedId + "\" collides with an existing material id.",
                        "rename the derivedId; minted ids must not shadow raw material ids."));
                continue;
            }
            JsonNode override = raw.node().path("overrides").path("flammability");
            int derivedFlammability = override.isInt() ? override.asInt() : flammability.get(target);
            flammability.put(derivedId, derivedFlammability);
        }

        for (ParsedRaw raw : reactions) {
            String solid = textOrNull(raw.node(), "solid");
            if (solid == null || !flammability.containsKey(solid)) {
                out.accept(error(raw.source(), "reaction solid \"" + solid + "\" is not a known material.",
                        "point \"solid\" at an existing raws/materials id."));
            }
        }

        crossCheckMaterials(materialNodes, flammability, fluidIds, materials, out);
        crossCheckFluids(fluidNodes, flammability, fluids, out);

        return new RawsLoadResult(RawsIndex.of(flammability, fluidIds), issues);
    }

    // ---------------------------------------------------------------- checks

    private void crossCheckMaterials(Map<String, JsonNode> materialNodes, Map<String, Integer> flammability,
                                     TreeSet<String> fluidIds, List<ParsedRaw> materials,
                                     Consumer<ValidationIssue> out) {
        for (String id : new TreeSet<>(materialNodes.keySet())) {
            JsonNode node = materialNodes.get(id);
            String source = sourceOf(materials, id);
            if (node.path("flammability").asInt(0) > 0) {
                boolean ignition = node.path("ignitionK").isInt();
                int fuelTicks = node.path("fuelTicks").asInt(0);
                boolean burns = textOrNull(node, "burnsTo") != null;
                if (!ignition || fuelTicks < 1 || fuelTicks > 4095 || !burns) {
                    out.accept(error(source, "flammable material \"" + id
                                    + "\" must declare ignitionK, fuelTicks (1..4095) and burnsTo.",
                            "complete the FLAMMABLE field triple or set flammability to 0 (ARCHITECTURE section 10)."));
                }
            }
            String burnsTo = textOrNull(node, "burnsTo");
            if (burnsTo != null && !flammability.containsKey(burnsTo)) {
                out.accept(error(source, "burnsTo \"" + burnsTo + "\" of material \"" + id
                        + "\" is not a known material.", "point burnsTo at an existing material id."));
            }
            String meltsTo = textOrNull(node, "meltsTo");
            if (node.path("meltK").isInt() && meltsTo == null) {
                out.accept(error(source, "material \"" + id + "\" declares meltK but no meltsTo.",
                        "melt implies meltsTo + meltYieldUnits (ARCHITECTURE section 10)."));
            }
            if (meltsTo != null && !flammability.containsKey(meltsTo) && !fluidIds.contains(meltsTo)) {
                out.accept(error(source, "meltsTo \"" + meltsTo + "\" of material \"" + id
                        + "\" resolves to neither a material nor a fluid.",
                        "point meltsTo at an existing material or fluid id."));
            }
        }
    }

    private void crossCheckFluids(Map<String, JsonNode> fluidNodes, Map<String, Integer> flammability,
                                  List<ParsedRaw> fluids, Consumer<ValidationIssue> out) {
        for (String id : new TreeSet<>(fluidNodes.keySet())) {
            String freezesTo = textOrNull(fluidNodes.get(id), "freezesTo");
            if (freezesTo != null && !flammability.containsKey(freezesTo)) {
                out.accept(error(sourceOf(fluids, id), "freezesTo \"" + freezesTo + "\" of fluid \"" + id
                        + "\" is not a known material.", "point freezesTo at an existing material id."));
            }
        }
    }

    // --------------------------------------------------------------- helpers

    /** One successfully parsed raw file. */
    private record ParsedRaw(String source, String id, JsonNode node) {
    }

    private static List<Path> listJsonFiles(Path rawsRoot) {
        try (Stream<Path> walk = Files.walk(rawsRoot)) {
            return walk.filter(Files::isRegularFile)
                    .filter(p -> p.getFileName().toString().endsWith(".json"))
                    .sorted((a, b) -> relativeName(rawsRoot, a).compareTo(relativeName(rawsRoot, b)))
                    .toList();
        } catch (IOException e) {
            throw new UncheckedIOException("cannot walk raws directory " + rawsRoot, e);
        }
    }

    private static String relativeName(Path rawsRoot, Path file) {
        return rawsRoot.relativize(file).toString().replace('\\', '/');
    }

    private static String categoryOf(Path rawsRoot, Path file) {
        Path rel = rawsRoot.relativize(file);
        return rel.getNameCount() < 2 ? "" : rel.getName(0).toString();
    }

    private static String sourceOf(List<ParsedRaw> raws, String id) {
        for (ParsedRaw raw : raws) {
            if (raw.id().equals(id)) {
                return raw.source();
            }
        }
        return id + ".json";
    }

    private static String textOrNull(JsonNode node, String field) {
        JsonNode value = node.path(field);
        return value.isTextual() ? value.asText() : null;
    }

    private static String firstLine(String message) {
        if (message == null) {
            return "unknown error";
        }
        int nl = message.indexOf('\n');
        return (nl < 0 ? message : message.substring(0, nl)).trim();
    }

    private static ValidationIssue error(String source, String message, String hint) {
        return new ValidationIssue(Severity.ERROR, PASS_ID, source, "",
                ValidationIssue.NO_COORD, ValidationIssue.NO_COORD, message, hint);
    }
}
