package com.trojia.sim.material;

import com.trojia.sim.fluid.FluidDefinition;
import com.trojia.sim.fluid.FluidRegistry;
import com.trojia.sim.json.JsonArray;
import com.trojia.sim.json.JsonNull;
import com.trojia.sim.json.JsonNumber;
import com.trojia.sim.json.JsonNumberMode;
import com.trojia.sim.json.JsonObject;
import com.trojia.sim.json.JsonParseException;
import com.trojia.sim.json.JsonString;
import com.trojia.sim.json.JsonValue;
import com.trojia.sim.json.MiniJson;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.nio.file.FileSystem;
import java.nio.file.FileSystemAlreadyExistsException;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.TreeMap;
import java.util.stream.Stream;

/**
 * The fail-fast raws loader (ARCHITECTURE.md §3, §10): reads every material,
 * fluid, treatment and reaction raw under a raws root via {@link MiniJson}
 * (strict, integer-only), mints treatment-derived materials, validates the
 * complete §10 rule set, and assembles the immutable {@link RawsBundle}.
 *
 * <p><strong>Sources.</strong> The raws root contains the subdirectories
 * {@code materials/}, {@code fluids/}, {@code treatments/} and
 * {@code reactions/}; only {@code *.json} files are read (READMEs and other
 * documentation are skipped), each in deterministic sorted-filename order. The
 * root may be a plain directory ({@link #load(Path)} — how tools read the repo
 * tree {@code content/raws/}) or a classpath resource root
 * ({@link #load(ClassLoader, String)} — how the runtime reads
 * {@code trojia/raws/} from the content jar).</p>
 *
 * <p><strong>Minting.</strong> Treatments are applied in sorted treatment-id
 * order: each mints its derived material from the target's source tree via
 * {@link Treatment#mint(JsonObject)}, and the minted tree runs through exactly
 * the same parse-and-validate path as a hand-written raw.</p>
 *
 * <p><strong>Validations (every failure is a {@link RawsValidationException}
 * naming file and field):</strong></p>
 * <ul>
 *   <li>strict JSON (MiniJson {@link JsonNumberMode#INTEGER_ONLY}); top level
 *       is an object; unknown members are rejected <em>except</em> the
 *       documentation members {@code provenance}/{@code notes}, which are
 *       ignored;</li>
 *   <li>{@code conductivityQ8 <= 256} (materials and fluids);</li>
 *   <li>per-material thermal stability {@code Σ(w/cap) <= ½} over the six
 *       neighbors with worst-case weight {@code w = conductivityQ8/256}:
 *       {@code 3*conductivityQ8 <= 64*heatCapacityQ8}, with the minimum heat
 *       capacity {@code heatCapacityQ8 >= 12} enforced;</li>
 *   <li>FLAMMABLE triple: {@code flammability > 0} ⇒ {@code ignitionK} +
 *       {@code fuelTicks} in 1..4095 + {@code burnsTo} (and the converse: an
 *       inert material declares none of them);</li>
 *   <li>melt ⇒ {@code meltsTo} + {@code meltYieldUnits >= 1} (and no melt
 *       products without {@code meltK});</li>
 *   <li>material {@code liquid} tag ⇒ {@code boilsTo}; fluids are exempt
 *       (BLESSING-QUEUE ruling 3 — water ships {@code boilsTo: null});</li>
 *   <li>chargeable/spike magnitudes fit 16 bits;</li>
 *   <li>chargeable {@code colorStops}: 1..4 stops, {@code uptoPct} strictly
 *       increasing (monotone), last stop exactly 100 — a pure fill ramp
 *       (BLESSING-QUEUE ruling 5);</li>
 *   <li>treatment targets exist; derived ids collide with nothing;</li>
 *   <li>substance references resolve — {@code meltsTo}/{@code boilsTo}/
 *       {@code freezesTo} in the <em>united</em> material∪fluid namespace
 *       (ruling 3), {@code burnsTo}/{@code shattersTo} in the material
 *       registry, reaction refs (solid, trigger tag, pulse gas) fully;</li>
 *   <li>reaction wear fields are optional-reserved (ruling 9): absent wear
 *       fields mean <em>no wear</em>;</li>
 *   <li>temperatures are integer Kelvin 1..6553 (deciK must fit the 2-byte
 *       TEMPERATURE lane).</li>
 * </ul>
 *
 * <p>Loading is a pure function of the raws bytes: identical raws yield
 * identical id assignments and an identical {@link RawsBundle#fingerprint()}
 * on every platform.</p>
 */
public final class MaterialRawsLoader {

    private static final String MATERIALS_DIR = "materials";
    private static final String FLUIDS_DIR = "fluids";
    private static final String TREATMENTS_DIR = "treatments";
    private static final String REACTIONS_DIR = "reactions";

    private static final int MAX_U16 = 65535;
    /** Highest raw Kelvin whose deciK (×10) fits the 2-byte TEMPERATURE lane. */
    private static final int MAX_TEMP_K = 6553;
    private static final int MAX_CONDUCTIVITY_Q8 = 256;
    private static final int MIN_HEAT_CAPACITY_Q8 = 12;
    private static final int MAX_FUEL_TICKS = 4095;
    private static final int MAX_OPACITY = 31;
    private static final int MAX_LIGHT_LEVEL = 31;
    private static final int MAX_COLOR_STOPS = 4;
    private static final int MAX_FLAMMABILITY = 3;
    private static final int MAX_HARDNESS = 255;
    private static final int MAX_FLUID_DEPTH = 7;
    private static final int MAX_Q16 = 65536;
    private static final int MAX_CHEBYSHEV_RADIUS = 15;

    /** Documentation members ignored on every raw (§10 "provenance"/"notes"). */
    private static final List<String> IGNORED_FIELDS = List.of("provenance", "notes");

    private static final List<String> MATERIAL_FIELDS = List.of(
            "id", "displayName", "phase", "density", "hardness", "flammability",
            "ignitionK", "meltK", "meltsTo", "meltYieldUnits", "boilK", "boilsTo",
            "conductivityQ8", "heatCapacityQ8", "fuelTicks", "burnsTo", "valueCp",
            "tags", "light", "features");
    private static final List<String> LIGHT_FIELDS = List.of("opacity");
    /** Feature keys in canonical (alphabetical) order — also the storage order. */
    private static final List<String> FEATURE_KEYS = List.of(
            "chargeable", "contactReactive", "emissive", "shatterOnSpike");
    private static final List<String> CHARGEABLE_FIELDS = List.of(
            "capacityCu", "maxSafeDischargePerTick", "saturationPct",
            "saturationHeatDeciKPerTick", "equilibriumDeciK", "colorStops");
    private static final List<String> COLOR_STOP_FIELDS = List.of("uptoPct", "tint", "lightLevel");
    private static final List<String> CONTACT_REACTIVE_FIELDS = List.of("reagentTag");
    private static final List<String> EMISSIVE_FIELDS = List.of("lightLevel", "tint");
    private static final List<String> SHATTER_FIELDS = List.of(
            "spikeCuPerTick", "shattersTo", "radiusChebyshev");
    private static final List<String> FLUID_FIELDS = List.of(
            "id", "displayName", "density", "conductivityQ8", "heatCapacityQ8",
            "freezeK", "freezesTo", "freezeMinDepth", "boilK", "boilsTo",
            "evapMaxDepth", "evapMinK", "evapChanceQ16", "tags");
    private static final List<String> TREATMENT_FIELDS = List.of(
            "id", "displayName", "target", "derivedId", "derivedDisplayName",
            "overrides", "scaleQ8", "addTags");
    private static final List<String> REACTION_FIELDS = List.of(
            "id", "displayName", "solid", "trigger", "expansion",
            "wearPerUnit", "wearCapacity", "pulse");
    private static final List<String> TRIGGER_FIELDS = List.of("kind", "fluidTag");
    private static final List<String> PULSE_FIELDS = List.of("gasId", "magnitudeCap");

    private MaterialRawsLoader() {
    }

    /**
     * Loads and validates the raws under a directory root (e.g. the repo tree
     * {@code content/raws}).
     *
     * @param rawsRoot the raws root directory
     * @return the validated bundle
     * @throws NullPointerException    if {@code rawsRoot} is {@code null}
     * @throws RawsValidationException on the first validation failure
     * @throws UncheckedIOException    if the directory cannot be read
     */
    public static RawsBundle load(Path rawsRoot) {
        Objects.requireNonNull(rawsRoot, "rawsRoot");
        if (!Files.isDirectory(rawsRoot)) {
            throw new RawsValidationException(rawsRoot.toString(),
                    RawsValidationException.NO_FIELD, "raws root is not a directory");
        }

        TreeMap<String, MaterialSource> materials = new TreeMap<>();
        for (RawFile file : readDir(rawsRoot, MATERIALS_DIR)) {
            Material material = parseMaterial(new Ctx(file.name(), ""), file.root());
            putMaterial(materials, new MaterialSource(file.name(), file.root(), material));
        }

        TreeMap<String, FluidSource> fluids = new TreeMap<>();
        for (RawFile file : readDir(rawsRoot, FLUIDS_DIR)) {
            FluidDefinition fluid = parseFluid(new Ctx(file.name(), ""), file.root());
            FluidSource previous = fluids.putIfAbsent(fluid.key(),
                    new FluidSource(file.name(), fluid));
            if (previous != null) {
                throw new RawsValidationException(file.name(), "id",
                        "duplicate fluid id \"" + fluid.key()
                                + "\" (also defined in " + previous.file() + ")");
            }
        }

        List<TreatmentSource> treatments = new ArrayList<>();
        for (RawFile file : readDir(rawsRoot, TREATMENTS_DIR)) {
            treatments.add(new TreatmentSource(file.name(),
                    parseTreatment(new Ctx(file.name(), ""), file.root())));
        }
        treatments.sort(Comparator.comparing(source -> source.treatment().key()));
        for (int i = 1; i < treatments.size(); i++) {
            if (treatments.get(i).treatment().key()
                    .equals(treatments.get(i - 1).treatment().key())) {
                throw new RawsValidationException(treatments.get(i).file(), "id",
                        "duplicate treatment id \"" + treatments.get(i).treatment().key()
                                + "\" (also defined in " + treatments.get(i - 1).file() + ")");
            }
        }

        TreeMap<String, ReactionSource> reactions = new TreeMap<>();
        for (RawFile file : readDir(rawsRoot, REACTIONS_DIR)) {
            ReactionDefinition reaction = parseReaction(new Ctx(file.name(), ""), file.root());
            ReactionSource previous = reactions.putIfAbsent(reaction.key(),
                    new ReactionSource(file.name(), reaction));
            if (previous != null) {
                throw new RawsValidationException(file.name(), "id",
                        "duplicate reaction id \"" + reaction.key()
                                + "\" (also defined in " + previous.file() + ")");
            }
        }

        for (TreatmentSource source : treatments) {
            mintDerived(source, materials, fluids);
        }

        validateCrossReferences(materials, fluids, reactions);

        List<Material> materialDefinitions = new ArrayList<>(materials.size());
        for (MaterialSource source : materials.values()) {
            materialDefinitions.add(source.material());
        }
        List<FluidDefinition> fluidDefinitions = new ArrayList<>(fluids.size());
        for (FluidSource source : fluids.values()) {
            fluidDefinitions.add(source.fluid());
        }
        List<ReactionDefinition> reactionDefinitions = new ArrayList<>(reactions.size());
        for (ReactionSource source : reactions.values()) {
            reactionDefinitions.add(source.reaction());
        }
        List<Treatment> treatmentDefinitions = new ArrayList<>(treatments.size());
        for (TreatmentSource source : treatments) {
            treatmentDefinitions.add(source.treatment());
        }
        return new RawsBundle(
                MaterialRegistry.of(materialDefinitions, reactionDefinitions),
                FluidRegistry.of(fluidDefinitions),
                treatmentDefinitions);
    }

    /**
     * Loads and validates the raws under a classpath resource root (e.g.
     * {@code "trojia/raws"} in the content jar). Both jar-packaged and
     * directory classpath entries are supported; jar entries are read through
     * a temporary zip filesystem.
     *
     * @param classLoader  the loader whose classpath carries the raws
     * @param resourceRoot the resource root, {@code '/'}-separated (leading and
     *                     trailing slashes tolerated)
     * @return the validated bundle
     * @throws NullPointerException    if an argument is {@code null}
     * @throws RawsValidationException if the root is missing from the classpath
     *                                 or a validation fails
     * @throws UncheckedIOException    if the resource bytes cannot be read
     */
    public static RawsBundle load(ClassLoader classLoader, String resourceRoot) {
        Objects.requireNonNull(classLoader, "classLoader");
        Objects.requireNonNull(resourceRoot, "resourceRoot");
        String normalized = trimSlashes(resourceRoot);
        URL url = classLoader.getResource(normalized);
        if (url == null) {
            url = classLoader.getResource(normalized + "/");
        }
        if (url == null) {
            throw new RawsValidationException(normalized, RawsValidationException.NO_FIELD,
                    "raws resource root not found on the classpath");
        }
        try {
            URI uri = url.toURI();
            if ("jar".equals(uri.getScheme())) {
                return loadFromJar(uri);
            }
            return load(Path.of(uri));
        } catch (URISyntaxException e) {
            throw new IllegalStateException("unusable raws resource URL: " + url, e);
        } catch (IOException e) {
            throw new UncheckedIOException("failed reading raws from " + url, e);
        }
    }

    private static RawsBundle loadFromJar(URI uri) throws IOException {
        String full = uri.toString();
        int separator = full.indexOf("!/");
        URI jarUri = URI.create(full.substring(0, separator));
        String entry = full.substring(separator + 1);
        try (FileSystem jarFs = FileSystems.newFileSystem(jarUri, Map.of())) {
            return load(jarFs.getPath(entry));
        } catch (FileSystemAlreadyExistsException e) {
            return load(FileSystems.getFileSystem(jarUri).getPath(entry));
        }
    }

    private static String trimSlashes(String resourceRoot) {
        int start = 0;
        int end = resourceRoot.length();
        while (start < end && resourceRoot.charAt(start) == '/') {
            start++;
        }
        while (end > start && resourceRoot.charAt(end - 1) == '/') {
            end--;
        }
        return resourceRoot.substring(start, end);
    }

    // ---------------------------------------------------------------- reading

    /** One raws file: root-relative '/'-separated name plus its parsed tree. */
    private record RawFile(String name, JsonObject root) {
    }

    private static List<RawFile> readDir(Path rawsRoot, String subdir) {
        Path dir = rawsRoot.resolve(subdir);
        if (!Files.isDirectory(dir)) {
            return List.of();
        }
        List<Path> files;
        try (Stream<Path> listing = Files.list(dir)) {
            files = listing
                    .filter(Files::isRegularFile)
                    .filter(path -> path.getFileName().toString().endsWith(".json"))
                    .sorted(Comparator.comparing(path -> path.getFileName().toString()))
                    .toList();
        } catch (IOException e) {
            throw new UncheckedIOException("failed listing raws dir " + dir, e);
        }
        List<RawFile> parsed = new ArrayList<>(files.size());
        for (Path path : files) {
            String name = subdir + "/" + path.getFileName();
            byte[] bytes;
            try {
                bytes = Files.readAllBytes(path);
            } catch (IOException e) {
                throw new UncheckedIOException("failed reading raws file " + path, e);
            }
            JsonValue tree;
            try {
                tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);
            } catch (JsonParseException e) {
                throw new RawsValidationException(name, RawsValidationException.NO_FIELD,
                        "malformed JSON: " + e.getMessage());
            }
            if (!(tree instanceof JsonObject root)) {
                throw new RawsValidationException(name, RawsValidationException.NO_FIELD,
                        "top-level value must be an object");
            }
            parsed.add(new RawFile(name, root));
        }
        return parsed;
    }

    private static void putMaterial(TreeMap<String, MaterialSource> materials,
            MaterialSource source) {
        MaterialSource previous = materials.putIfAbsent(source.material().key(), source);
        if (previous != null) {
            throw new RawsValidationException(source.file(), "id",
                    "duplicate material id \"" + source.material().key()
                            + "\" (also defined in " + previous.file() + ")");
        }
    }

    // ---------------------------------------------------------------- minting

    private static void mintDerived(TreatmentSource source,
            TreeMap<String, MaterialSource> materials, TreeMap<String, FluidSource> fluids) {
        Treatment treatment = source.treatment();
        MaterialSource target = materials.get(treatment.targetId());
        if (target == null) {
            throw new RawsValidationException(source.file(), "target",
                    "treatment target \"" + treatment.targetId() + "\" is not a known material");
        }
        if (materials.containsKey(treatment.derivedId())) {
            throw new RawsValidationException(source.file(), "derivedId",
                    "derived id \"" + treatment.derivedId()
                            + "\" collides with an existing material id");
        }
        FluidSource fluidCollision = fluids.get(treatment.derivedId());
        if (fluidCollision != null) {
            throw new RawsValidationException(source.file(), "derivedId",
                    "derived id \"" + treatment.derivedId()
                            + "\" collides with fluid id in " + fluidCollision.file());
        }
        JsonObject derivedTree;
        try {
            derivedTree = treatment.mint(target.source());
        } catch (IllegalArgumentException e) {
            throw new RawsValidationException(source.file(),
                    RawsValidationException.NO_FIELD, e.getMessage());
        }
        Material derived = parseMaterial(new Ctx(source.file(), ""), derivedTree);
        putMaterial(materials, new MaterialSource(source.file(), derivedTree, derived));
    }

    // ------------------------------------------------------ material parsing

    private static Material parseMaterial(Ctx ctx, JsonObject raw) {
        rejectUnknown(ctx, raw, MATERIAL_FIELDS);
        String key = requireString(ctx, raw, "id");
        String displayName = requireString(ctx, raw, "displayName");
        MaterialPhase phase = parsePhase(ctx, requireString(ctx, raw, "phase"));
        int density = requireInt(ctx, raw, "density", 1, MAX_U16);
        int hardness = requireInt(ctx, raw, "hardness", 0, MAX_HARDNESS);
        int flammability = requireInt(ctx, raw, "flammability", 0, MAX_FLAMMABILITY);
        int ignitionDeciK = optionalTempDeciK(ctx, raw, "ignitionK");
        int meltDeciK = optionalTempDeciK(ctx, raw, "meltK");
        String meltsTo = optionalString(ctx, raw, "meltsTo");
        int meltYieldUnits = optionalInt(ctx, raw, "meltYieldUnits", 0, MAX_U16, 0);
        int boilDeciK = optionalTempDeciK(ctx, raw, "boilK");
        String boilsTo = optionalString(ctx, raw, "boilsTo");
        int conductivityQ8 = requireInt(ctx, raw, "conductivityQ8", 0, MAX_CONDUCTIVITY_Q8);
        int heatCapacityQ8 = requireInt(ctx, raw, "heatCapacityQ8", MIN_HEAT_CAPACITY_Q8, MAX_U16);
        checkStability(ctx, conductivityQ8, heatCapacityQ8);
        int fuelTicks = optionalInt(ctx, raw, "fuelTicks", 0, MAX_FUEL_TICKS, 0);
        String burnsTo = optionalString(ctx, raw, "burnsTo");
        int valueCp = requireInt(ctx, raw, "valueCp", 0, Integer.MAX_VALUE);
        List<String> tags = stringArray(ctx, raw, "tags");
        int opacity = parseLight(ctx, raw);
        List<MaterialFeature> features = parseFeatures(ctx, raw);

        if (flammability > 0) {
            if (ignitionDeciK == Material.NONE) {
                throw ctx.err("ignitionK", "FLAMMABLE material must declare ignitionK");
            }
            if (fuelTicks < 1) {
                throw ctx.err("fuelTicks",
                        "FLAMMABLE material must declare fuelTicks in 1.." + MAX_FUEL_TICKS);
            }
            if (burnsTo == null) {
                throw ctx.err("burnsTo", "FLAMMABLE material must declare burnsTo");
            }
        } else {
            if (ignitionDeciK != Material.NONE) {
                throw ctx.err("ignitionK", "inert material (flammability 0) must not declare ignitionK");
            }
            if (fuelTicks != 0) {
                throw ctx.err("fuelTicks", "inert material (flammability 0) must not declare fuelTicks");
            }
            if (burnsTo != null) {
                throw ctx.err("burnsTo", "inert material (flammability 0) must not declare burnsTo");
            }
        }
        if (meltDeciK != Material.NONE) {
            if (meltsTo == null) {
                throw ctx.err("meltsTo", "melting material (meltK set) must declare meltsTo");
            }
            if (meltYieldUnits < 1) {
                throw ctx.err("meltYieldUnits",
                        "melting material (meltK set) must declare meltYieldUnits >= 1");
            }
        } else {
            if (meltsTo != null) {
                throw ctx.err("meltsTo", "meltsTo requires meltK");
            }
            if (meltYieldUnits != 0) {
                throw ctx.err("meltYieldUnits", "meltYieldUnits requires meltK");
            }
        }
        if (boilDeciK != Material.NONE && boilsTo == null) {
            throw ctx.err("boilsTo", "boiling material (boilK set) must declare boilsTo");
        }
        if (boilDeciK == Material.NONE && boilsTo != null) {
            throw ctx.err("boilsTo", "boilsTo requires boilK");
        }
        if (tags.contains("liquid") && boilsTo == null) {
            throw ctx.err("boilsTo",
                    "material with the \"liquid\" tag must declare boilsTo (§10)");
        }
        return new Material(key, displayName, phase, density, hardness, flammability,
                ignitionDeciK, meltDeciK, meltsTo, meltYieldUnits, boilDeciK, boilsTo,
                conductivityQ8, heatCapacityQ8, fuelTicks, burnsTo, valueCp, tags,
                opacity, features);
    }

    private static MaterialPhase parsePhase(Ctx ctx, String literal) {
        for (MaterialPhase phase : MaterialPhase.values()) {
            if (phase.name().equals(literal)) {
                return phase;
            }
        }
        throw ctx.err("phase", "unknown phase \"" + literal + "\" (expected SOLID or LIQUID)");
    }

    /**
     * Enforces the §10 per-material thermal stability invariant
     * {@code Σ(w/cap) <= ½} over the six axis neighbors with the worst-case
     * pair weight {@code w = conductivityQ8/256}:
     * {@code 6*(conductivityQ8/256)/heatCapacityQ8 <= 1/2}, i.e.
     * {@code 3*conductivityQ8 <= 64*heatCapacityQ8}. Together with the
     * enforced floor {@code heatCapacityQ8 >= 12} and cap
     * {@code conductivityQ8 <= 256} the invariant holds with equality exactly
     * at (256, 12).
     */
    private static void checkStability(Ctx ctx, int conductivityQ8, int heatCapacityQ8) {
        if (3 * conductivityQ8 > 64 * heatCapacityQ8) {
            throw ctx.err("heatCapacityQ8",
                    "thermal stability violated: 3*conductivityQ8 (" + 3 * conductivityQ8
                            + ") must be <= 64*heatCapacityQ8 (" + 64 * heatCapacityQ8 + ")");
        }
    }

    private static int parseLight(Ctx ctx, JsonObject raw) {
        JsonObject light = requireObject(ctx, raw, "light");
        Ctx lightCtx = ctx.sub("light");
        rejectUnknown(lightCtx, light, LIGHT_FIELDS);
        return requireInt(lightCtx, light, "opacity", 0, MAX_OPACITY);
    }

    private static List<MaterialFeature> parseFeatures(Ctx ctx, JsonObject raw) {
        JsonObject block = optionalObject(ctx, raw, "features");
        if (block == null) {
            return List.of();
        }
        Ctx featuresCtx = ctx.sub("features");
        rejectUnknownExact(featuresCtx, block, FEATURE_KEYS);
        List<MaterialFeature> features = new ArrayList<>(FEATURE_KEYS.size());
        for (String kind : FEATURE_KEYS) {
            JsonObject body = optionalObject(featuresCtx, block, kind);
            if (body == null) {
                continue;
            }
            Ctx kindCtx = featuresCtx.sub(kind);
            features.add(switch (kind) {
                case "chargeable" -> parseChargeable(kindCtx, body);
                case "contactReactive" -> parseContactReactive(kindCtx, body);
                case "emissive" -> parseEmissive(kindCtx, body);
                default -> parseShatterOnSpike(kindCtx, body);
            });
        }
        return features;
    }

    private static MaterialFeature parseChargeable(Ctx ctx, JsonObject body) {
        rejectUnknownExact(ctx, body, CHARGEABLE_FIELDS);
        int capacityCu = requireInt(ctx, body, "capacityCu", 1, MAX_U16);
        int maxSafeDischargePerTick = requireInt(ctx, body, "maxSafeDischargePerTick", 1, MAX_U16);
        int saturationPct = requireInt(ctx, body, "saturationPct", 0, 100);
        int saturationHeat = requireInt(ctx, body, "saturationHeatDeciKPerTick", 0, MAX_U16);
        int equilibriumDeciK = requireInt(ctx, body, "equilibriumDeciK", 0, MAX_U16);
        JsonValue stopsValue = body.get("colorStops");
        if (!(stopsValue instanceof JsonArray stops) || stops.size() == 0) {
            throw ctx.err("colorStops", "chargeable requires a non-empty colorStops array");
        }
        if (stops.size() > MAX_COLOR_STOPS) {
            throw ctx.err("colorStops",
                    "at most " + MAX_COLOR_STOPS + " color stops allowed, got " + stops.size());
        }
        List<MaterialFeature.Chargeable.ColorStop> parsed = new ArrayList<>(stops.size());
        int previousPct = 0;
        for (int i = 0; i < stops.size(); i++) {
            Ctx stopCtx = ctx.sub("colorStops[" + i + "]");
            if (!(stops.get(i) instanceof JsonObject stop)) {
                throw stopCtx.err(RawsValidationException.NO_FIELD, "color stop must be an object");
            }
            rejectUnknownExact(stopCtx, stop, COLOR_STOP_FIELDS);
            int uptoPct = requireInt(stopCtx, stop, "uptoPct", 1, 100);
            if (uptoPct <= previousPct) {
                throw stopCtx.err("uptoPct",
                        "color stops must be strictly increasing: " + uptoPct
                                + " after " + previousPct);
            }
            previousPct = uptoPct;
            int tint = parseTint(stopCtx, stop, "tint");
            int lightLevel = requireInt(stopCtx, stop, "lightLevel", 0, MAX_LIGHT_LEVEL);
            parsed.add(new MaterialFeature.Chargeable.ColorStop(uptoPct, tint, lightLevel));
        }
        if (previousPct != 100) {
            throw ctx.err("colorStops", "the last color stop must be at uptoPct 100, got "
                    + previousPct);
        }
        return new MaterialFeature.Chargeable(capacityCu, maxSafeDischargePerTick,
                saturationPct, saturationHeat, equilibriumDeciK, parsed);
    }

    private static MaterialFeature parseContactReactive(Ctx ctx, JsonObject body) {
        rejectUnknownExact(ctx, body, CONTACT_REACTIVE_FIELDS);
        return new MaterialFeature.ContactReactive(requireString(ctx, body, "reagentTag"));
    }

    private static MaterialFeature parseEmissive(Ctx ctx, JsonObject body) {
        rejectUnknownExact(ctx, body, EMISSIVE_FIELDS);
        return new MaterialFeature.Emissive(
                requireInt(ctx, body, "lightLevel", 0, MAX_LIGHT_LEVEL),
                parseTint(ctx, body, "tint"));
    }

    private static MaterialFeature parseShatterOnSpike(Ctx ctx, JsonObject body) {
        rejectUnknownExact(ctx, body, SHATTER_FIELDS);
        return new MaterialFeature.ShatterOnSpike(
                requireInt(ctx, body, "spikeCuPerTick", 1, MAX_U16),
                requireString(ctx, body, "shattersTo"),
                requireInt(ctx, body, "radiusChebyshev", 0, MAX_CHEBYSHEV_RADIUS));
    }

    private static int parseTint(Ctx ctx, JsonObject body, String field) {
        String literal = requireString(ctx, body, field);
        if (literal.length() != 7 || literal.charAt(0) != '#') {
            throw ctx.err(field, "tint must be \"#RRGGBB\", got \"" + literal + "\"");
        }
        int rgb = 0;
        for (int i = 1; i < 7; i++) {
            int digit = Character.digit(literal.charAt(i), 16);
            if (digit < 0) {
                throw ctx.err(field, "tint must be \"#RRGGBB\", got \"" + literal + "\"");
            }
            rgb = (rgb << 4) | digit;
        }
        return rgb;
    }

    // --------------------------------------------------------- fluid parsing

    private static FluidDefinition parseFluid(Ctx ctx, JsonObject raw) {
        rejectUnknown(ctx, raw, FLUID_FIELDS);
        String key = requireString(ctx, raw, "id");
        String displayName = requireString(ctx, raw, "displayName");
        int density = requireInt(ctx, raw, "density", 1, MAX_U16);
        int conductivityQ8 = requireInt(ctx, raw, "conductivityQ8", 0, MAX_CONDUCTIVITY_Q8);
        int heatCapacityQ8 = requireInt(ctx, raw, "heatCapacityQ8", MIN_HEAT_CAPACITY_Q8, MAX_U16);
        checkStability(ctx, conductivityQ8, heatCapacityQ8);
        int freezeDeciK = optionalTempDeciK(ctx, raw, "freezeK");
        String freezesTo = optionalString(ctx, raw, "freezesTo");
        int freezeMinDepth = optionalInt(ctx, raw, "freezeMinDepth", 0, MAX_FLUID_DEPTH, 0);
        int boilDeciK = optionalTempDeciK(ctx, raw, "boilK");
        String boilsTo = optionalString(ctx, raw, "boilsTo");
        int evapMaxDepth = optionalInt(ctx, raw, "evapMaxDepth", 0, MAX_FLUID_DEPTH, 0);
        int evapMinDeciK = optionalTempDeciK(ctx, raw, "evapMinK");
        int evapChanceQ16 = optionalInt(ctx, raw, "evapChanceQ16", 0, MAX_Q16, 0);
        List<String> tags = stringArray(ctx, raw, "tags");

        if (freezeDeciK != FluidDefinition.NONE) {
            if (freezesTo == null) {
                throw ctx.err("freezesTo", "freezing fluid (freezeK set) must declare freezesTo");
            }
            if (freezeMinDepth < 1) {
                throw ctx.err("freezeMinDepth",
                        "freezing fluid (freezeK set) must declare freezeMinDepth in 1.."
                                + MAX_FLUID_DEPTH);
            }
        } else {
            if (freezesTo != null) {
                throw ctx.err("freezesTo", "freezesTo requires freezeK");
            }
            if (freezeMinDepth != 0) {
                throw ctx.err("freezeMinDepth", "freezeMinDepth requires freezeK");
            }
        }
        // BLESSING-QUEUE ruling 3: the liquid-tag => boilsTo rule is NOT binding
        // for fluids; water ships boilsTo null (steam is the reserved seam).
        if (boilsTo != null && boilDeciK == FluidDefinition.NONE) {
            throw ctx.err("boilsTo", "boilsTo requires boilK");
        }
        if (evapMaxDepth > 0) {
            if (evapMinDeciK == FluidDefinition.NONE) {
                throw ctx.err("evapMinK", "evaporating fluid (evapMaxDepth > 0) must declare evapMinK");
            }
            if (evapChanceQ16 < 1) {
                throw ctx.err("evapChanceQ16",
                        "evaporating fluid (evapMaxDepth > 0) must declare evapChanceQ16 >= 1");
            }
        }
        return new FluidDefinition(key, displayName, density, conductivityQ8, heatCapacityQ8,
                freezeDeciK, freezesTo, freezeMinDepth, boilDeciK, boilsTo,
                evapMaxDepth, evapMinDeciK, evapChanceQ16, tags);
    }

    // ----------------------------------------------------- treatment parsing

    private static Treatment parseTreatment(Ctx ctx, JsonObject raw) {
        rejectUnknown(ctx, raw, TREATMENT_FIELDS);
        String key = requireString(ctx, raw, "id");
        String displayName = requireString(ctx, raw, "displayName");
        String targetId = requireString(ctx, raw, "target");
        String derivedId = requireString(ctx, raw, "derivedId");
        String derivedDisplayName = requireString(ctx, raw, "derivedDisplayName");
        JsonObject overrides = optionalObject(ctx, raw, "overrides");
        JsonObject scaleQ8 = optionalObject(ctx, raw, "scaleQ8");
        List<String> addTags = stringArray(ctx, raw, "addTags");
        if (derivedId.equals(targetId)) {
            throw ctx.err("derivedId", "derivedId must differ from the target id");
        }
        JsonObject empty = new JsonObject(List.of());
        return new Treatment(key, displayName, targetId, derivedId, derivedDisplayName,
                overrides == null ? empty : overrides,
                scaleQ8 == null ? empty : scaleQ8,
                addTags);
    }

    // ------------------------------------------------------ reaction parsing

    private static ReactionDefinition parseReaction(Ctx ctx, JsonObject raw) {
        rejectUnknown(ctx, raw, REACTION_FIELDS);
        String key = requireString(ctx, raw, "id");
        String displayName = requireString(ctx, raw, "displayName");
        String solidId = requireString(ctx, raw, "solid");

        JsonObject trigger = requireObject(ctx, raw, "trigger");
        Ctx triggerCtx = ctx.sub("trigger");
        rejectUnknownExact(triggerCtx, trigger, TRIGGER_FIELDS);
        String kind = requireString(triggerCtx, trigger, "kind");
        if (!"FLUID_CONTACT".equals(kind)) {
            throw triggerCtx.err("kind",
                    "unknown trigger kind \"" + kind + "\" (v0 knows only FLUID_CONTACT)");
        }
        String fluidTag = requireString(triggerCtx, trigger, "fluidTag");

        int expansion = requireInt(ctx, raw, "expansion", 1, MAX_U16);
        // BLESSING-QUEUE ruling 9: wear fields are optional-reserved — absent
        // wear fields mean NO wear (the solid is inexhaustible in v0).
        int wearPerUnit = optionalInt(ctx, raw, "wearPerUnit", 0, MAX_U16, 0);
        int wearCapacity = optionalInt(ctx, raw, "wearCapacity", 0, MAX_U16, 0);

        JsonObject pulse = requireObject(ctx, raw, "pulse");
        Ctx pulseCtx = ctx.sub("pulse");
        rejectUnknownExact(pulseCtx, pulse, PULSE_FIELDS);
        String pulseGasId = optionalString(pulseCtx, pulse, "gasId");
        int pulseMagnitudeCap = requireInt(pulseCtx, pulse, "magnitudeCap", 1, MAX_U16);

        return new ReactionDefinition(key, displayName, solidId, fluidTag, expansion,
                wearPerUnit, wearCapacity, pulseGasId, pulseMagnitudeCap);
    }

    // ------------------------------------------------------ cross validation

    private static void validateCrossReferences(TreeMap<String, MaterialSource> materials,
            TreeMap<String, FluidSource> fluids, TreeMap<String, ReactionSource> reactions) {
        for (FluidSource source : fluids.values()) {
            MaterialSource collision = materials.get(source.fluid().key());
            if (collision != null) {
                throw new RawsValidationException(source.file(), "id",
                        "fluid id \"" + source.fluid().key()
                                + "\" collides with material id in " + collision.file()
                                + " (substance ids share one namespace, ruling 3)");
            }
        }
        for (MaterialSource source : materials.values()) {
            Material material = source.material();
            requireSubstance(source.file(), "meltsTo", material.meltsTo(), materials, fluids);
            requireSubstance(source.file(), "boilsTo", material.boilsTo(), materials, fluids);
            requireMaterial(source.file(), "burnsTo", material.burnsTo(), materials);
            MaterialFeature.ShatterOnSpike shatter =
                    material.feature(MaterialFeature.ShatterOnSpike.class);
            if (shatter != null) {
                requireMaterial(source.file(), "features.shatterOnSpike.shattersTo",
                        shatter.shattersTo(), materials);
            }
        }
        for (FluidSource source : fluids.values()) {
            FluidDefinition fluid = source.fluid();
            requireSubstance(source.file(), "freezesTo", fluid.freezesTo(), materials, fluids);
            requireSubstance(source.file(), "boilsTo", fluid.boilsTo(), materials, fluids);
        }
        for (ReactionSource source : reactions.values()) {
            ReactionDefinition reaction = source.reaction();
            MaterialSource solid = materials.get(reaction.solidId());
            if (solid == null) {
                throw new RawsValidationException(source.file(), "solid",
                        "reaction solid \"" + reaction.solidId() + "\" is not a known material");
            }
            MaterialFeature.ContactReactive contact =
                    solid.material().feature(MaterialFeature.ContactReactive.class);
            if (contact == null) {
                throw new RawsValidationException(source.file(), "solid",
                        "reaction solid \"" + reaction.solidId()
                                + "\" does not carry the contactReactive feature");
            }
            if (!contact.reagentTag().equals(reaction.triggerFluidTag())) {
                throw new RawsValidationException(source.file(), "trigger.fluidTag",
                        "trigger tag \"" + reaction.triggerFluidTag()
                                + "\" does not match the solid's reagentTag \""
                                + contact.reagentTag() + "\"");
            }
            if (!anyFluidTagged(fluids, reaction.triggerFluidTag())) {
                throw new RawsValidationException(source.file(), "trigger.fluidTag",
                        "no fluid carries the trigger tag \"" + reaction.triggerFluidTag() + "\"");
            }
            if (reaction.pulseGasId() != null && !fluids.containsKey(reaction.pulseGasId())) {
                throw new RawsValidationException(source.file(), "pulse.gasId",
                        "pulse gas \"" + reaction.pulseGasId() + "\" is not a known fluid");
            }
        }
    }

    private static boolean anyFluidTagged(TreeMap<String, FluidSource> fluids, String tag) {
        for (FluidSource source : fluids.values()) {
            if (source.fluid().tagged(tag)) {
                return true;
            }
        }
        return false;
    }

    private static void requireSubstance(String file, String field, String reference,
            TreeMap<String, MaterialSource> materials, TreeMap<String, FluidSource> fluids) {
        if (reference != null && !materials.containsKey(reference)
                && !fluids.containsKey(reference)) {
            throw new RawsValidationException(file, field,
                    "unresolved substance id \"" + reference
                            + "\" (neither a material nor a fluid)");
        }
    }

    private static void requireMaterial(String file, String field, String reference,
            TreeMap<String, MaterialSource> materials) {
        if (reference != null && !materials.containsKey(reference)) {
            throw new RawsValidationException(file, field,
                    "unresolved material id \"" + reference + "\"");
        }
    }

    // ------------------------------------------------------- field utilities

    /** Error context: the raws file plus the dotted path prefix of a sub-object. */
    private record Ctx(String file, String prefix) {

        Ctx sub(String name) {
            return new Ctx(file, path(name));
        }

        String path(String field) {
            if (RawsValidationException.NO_FIELD.equals(field)) {
                return prefix.isEmpty() ? field : prefix;
            }
            return prefix.isEmpty() ? field : prefix + "." + field;
        }

        RawsValidationException err(String field, String detail) {
            return new RawsValidationException(file, path(field), detail);
        }
    }

    private static void rejectUnknown(Ctx ctx, JsonObject object, List<String> allowed) {
        for (JsonObject.Member member : object.members()) {
            if (!allowed.contains(member.name()) && !IGNORED_FIELDS.contains(member.name())) {
                throw ctx.err(member.name(), "unknown field");
            }
        }
    }

    /** Rejects unknown members without the provenance/notes documentation escape. */
    private static void rejectUnknownExact(Ctx ctx, JsonObject object, List<String> allowed) {
        for (JsonObject.Member member : object.members()) {
            if (!allowed.contains(member.name())) {
                throw ctx.err(member.name(), "unknown field");
            }
        }
    }

    private static String requireString(Ctx ctx, JsonObject object, String field) {
        JsonValue value = object.get(field);
        if (!(value instanceof JsonString string) || string.value().isEmpty()) {
            throw ctx.err(field, "required non-empty string is "
                    + (value == null ? "missing" : "not a string or empty"));
        }
        return string.value();
    }

    private static String optionalString(Ctx ctx, JsonObject object, String field) {
        JsonValue value = object.get(field);
        if (value == null || value instanceof JsonNull) {
            return null;
        }
        if (!(value instanceof JsonString string) || string.value().isEmpty()) {
            throw ctx.err(field, "must be a non-empty string or null");
        }
        return string.value();
    }

    private static int requireInt(Ctx ctx, JsonObject object, String field, int min, int max) {
        JsonValue value = object.get(field);
        if (value == null) {
            throw ctx.err(field, "required integer is missing");
        }
        return checkedInt(ctx, field, value, min, max);
    }

    private static int optionalInt(Ctx ctx, JsonObject object, String field, int min, int max,
            int absentValue) {
        JsonValue value = object.get(field);
        if (value == null || value instanceof JsonNull) {
            return absentValue;
        }
        return checkedInt(ctx, field, value, min, max);
    }

    private static int checkedInt(Ctx ctx, String field, JsonValue value, int min, int max) {
        if (!(value instanceof JsonNumber number) || !number.isIntegral()) {
            throw ctx.err(field, "must be an integer");
        }
        long parsed = number.asLong();
        if (parsed < min || parsed > max) {
            throw ctx.err(field, "value " + parsed + " out of range " + min + ".." + max);
        }
        return (int) parsed;
    }

    /**
     * Reads an optional integer-Kelvin temperature and converts it to deciK
     * ({@code K * 10}); absent or JSON {@code null} yields
     * {@link Material#NONE}. Kelvin must be 1..6553 so the deciK value fits
     * the 2-byte TEMPERATURE lane.
     */
    private static int optionalTempDeciK(Ctx ctx, JsonObject object, String field) {
        JsonValue value = object.get(field);
        if (value == null || value instanceof JsonNull) {
            return Material.NONE;
        }
        return checkedInt(ctx, field, value, 1, MAX_TEMP_K) * 10;
    }

    private static List<String> stringArray(Ctx ctx, JsonObject object, String field) {
        JsonValue value = object.get(field);
        if (value == null) {
            return List.of();
        }
        if (!(value instanceof JsonArray array)) {
            throw ctx.err(field, "must be an array of strings");
        }
        List<String> strings = new ArrayList<>(array.size());
        for (int i = 0; i < array.size(); i++) {
            if (!(array.get(i) instanceof JsonString string) || string.value().isEmpty()) {
                throw ctx.err(field + "[" + i + "]", "must be a non-empty string");
            }
            strings.add(string.value());
        }
        return strings;
    }

    private static JsonObject requireObject(Ctx ctx, JsonObject object, String field) {
        JsonValue value = object.get(field);
        if (!(value instanceof JsonObject nested)) {
            throw ctx.err(field, "required object is "
                    + (value == null ? "missing" : "not an object"));
        }
        return nested;
    }

    private static JsonObject optionalObject(Ctx ctx, JsonObject object, String field) {
        JsonValue value = object.get(field);
        if (value == null || value instanceof JsonNull) {
            return null;
        }
        if (!(value instanceof JsonObject nested)) {
            throw ctx.err(field, "must be an object");
        }
        return nested;
    }

    // ------------------------------------------------------------ file pairs

    /** A parsed material plus its source tree (treatments mint from the tree). */
    private record MaterialSource(String file, JsonObject source, Material material) {
    }

    private record FluidSource(String file, FluidDefinition fluid) {
    }

    private record TreatmentSource(String file, Treatment treatment) {
    }

    private record ReactionSource(String file, ReactionDefinition reaction) {
    }
}
