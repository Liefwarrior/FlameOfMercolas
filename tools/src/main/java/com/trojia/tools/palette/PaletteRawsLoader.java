package com.trojia.tools.palette;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.io.IOException;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;

/**
 * Reads the raws directory into a {@link PaletteRaws} snapshot.
 *
 * <p><strong>Layout consumed:</strong> {@code <rawsDir>/materials/*.json} (required
 * directory), {@code <rawsDir>/treatments/*.json} and {@code <rawsDir>/fluids/*.json}
 * (each optional — absent means none). Non-{@code .json} files (READMEs) are ignored.
 * Only the fields palette generation needs are read: material {@code id}/{@code phase}/
 * {@code tags}; treatment {@code target}/{@code derivedId}/{@code addTags}; fluid
 * {@code id}. Full schema validation is the raws loader's job (ARCHITECTURE.md
 * section 10), not this tool's.</p>
 *
 * <p><strong>Treatment minting</strong> mirrors the registry rule (section 10): the
 * derived material clones the target's phase and tags, then appends {@code addTags}
 * (first occurrence wins). Palette-irrelevant {@code overrides}/{@code scaleQ8} are
 * ignored here.</p>
 *
 * <p><strong>Determinism contract:</strong> files are visited in sorted filename
 * order and the resulting snapshot is id-sorted by construction, so the result is a
 * pure function of directory content. Any malformed input fails with
 * {@link PaletteGenerationException} naming the offending file.</p>
 *
 * <p>Stateless and safe to reuse.</p>
 */
final class PaletteRawsLoader {

    private final ObjectMapper mapper = new ObjectMapper();

    /**
     * Loads the snapshot.
     *
     * @param rawsDir raws root (the directory containing {@code materials/}), never
     *                {@code null}
     * @return id-sorted snapshot of materials (base + minted) and fluids
     * @throws PaletteGenerationException if {@code rawsDir} or {@code materials/} is
     *                                    missing, a file is malformed, required fields
     *                                    are absent, a treatment target is unknown, or
     *                                    ids collide
     */
    PaletteRaws load(Path rawsDir) {
        Objects.requireNonNull(rawsDir, "rawsDir");
        if (!Files.isDirectory(rawsDir)) {
            throw new PaletteGenerationException("raws directory not found: " + rawsDir);
        }
        Path materialsDir = rawsDir.resolve("materials");
        if (!Files.isDirectory(materialsDir)) {
            throw new PaletteGenerationException("materials directory not found: " + materialsDir);
        }

        List<PaletteMaterial> materials = new ArrayList<>();
        for (Path file : jsonFilesSorted(materialsDir)) {
            materials.add(readMaterial(file));
        }
        for (Path file : jsonFilesSorted(rawsDir.resolve("treatments"))) {
            materials.add(mintTreatment(file, materials));
        }
        List<PaletteFluid> fluids = new ArrayList<>();
        for (Path file : jsonFilesSorted(rawsDir.resolve("fluids"))) {
            fluids.add(new PaletteFluid(requireText(readTree(file), "id", file)));
        }
        return new PaletteRaws(materials, fluids);
    }

    /** @return {@code *.json} in {@code dir} sorted by filename; empty if {@code dir} is absent */
    private static List<Path> jsonFilesSorted(Path dir) {
        if (!Files.isDirectory(dir)) {
            return List.of();
        }
        List<Path> files = new ArrayList<>();
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(dir, "*.json")) {
            stream.forEach(files::add);
        } catch (IOException e) {
            throw new PaletteGenerationException("cannot list " + dir, e);
        }
        files.sort(Comparator.comparing(p -> p.getFileName().toString()));
        return files;
    }

    private PaletteMaterial readMaterial(Path file) {
        JsonNode root = readTree(file);
        String id = requireText(root, "id", file);
        String phaseName = requireText(root, "phase", file);
        PalettePhase phase;
        try {
            phase = PalettePhase.valueOf(phaseName);
        } catch (IllegalArgumentException e) {
            throw new PaletteGenerationException(
                    file + ": unknown phase \"" + phaseName + "\" for material \"" + id + "\"", e);
        }
        return new PaletteMaterial(id, phase, textArray(root, "tags", file));
    }

    private PaletteMaterial mintTreatment(Path file, List<PaletteMaterial> loaded) {
        JsonNode root = readTree(file);
        String targetId = requireText(root, "target", file);
        String derivedId = requireText(root, "derivedId", file);
        PaletteMaterial target = loaded.stream()
                .filter(m -> m.id().equals(targetId))
                .findFirst()
                .orElseThrow(() -> new PaletteGenerationException(
                        file + ": treatment target \"" + targetId + "\" is not a known material"));
        List<String> tags = new ArrayList<>(target.tags());
        for (String tag : textArray(root, "addTags", file)) {
            if (!tags.contains(tag)) {
                tags.add(tag);
            }
        }
        return new PaletteMaterial(derivedId, target.phase(), tags);
    }

    private JsonNode readTree(Path file) {
        try {
            return mapper.readTree(file.toFile());
        } catch (IOException e) {
            throw new PaletteGenerationException("cannot parse " + file + ": " + e.getMessage(), e);
        }
    }

    private static String requireText(JsonNode root, String field, Path file) {
        JsonNode node = root.get(field);
        if (node == null || !node.isTextual() || node.asText().isBlank()) {
            throw new PaletteGenerationException(
                    file + ": missing or non-string required field \"" + field + "\"");
        }
        return node.asText();
    }

    private static List<String> textArray(JsonNode root, String field, Path file) {
        JsonNode node = root.get(field);
        if (node == null || node.isNull()) {
            return List.of();
        }
        if (!node.isArray()) {
            throw new PaletteGenerationException(file + ": field \"" + field + "\" must be an array");
        }
        List<String> out = new ArrayList<>(node.size());
        for (JsonNode item : node) {
            if (!item.isTextual()) {
                throw new PaletteGenerationException(
                        file + ": field \"" + field + "\" must contain only strings");
            }
            out.add(item.asText());
        }
        return out;
    }
}
