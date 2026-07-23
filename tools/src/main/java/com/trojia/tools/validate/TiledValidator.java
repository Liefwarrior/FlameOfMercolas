package com.trojia.tools.validate;

import java.io.UncheckedIOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

import com.trojia.tools.tmx.TmxMap;
import com.trojia.tools.tmx.TmxParseException;
import com.trojia.tools.tmx.TmxReader;
import com.trojia.tools.tmx.TmxTileset;
import com.trojia.tools.tmx.TmxTilesetRef;
import com.trojia.tools.tmx.TsxReader;

/**
 * Runs an ordered {@link ValidationPass} list over a parsed map (ARCHITECTURE.md
 * section 3, tools entry).
 *
 * <p><strong>Determinism contract:</strong> passes run in list order and every pass
 * is itself deterministic, so {@link #validate} yields an identical
 * {@link ValidationReport} for identical inputs. The validator never mutates the
 * model and holds no per-run state; one instance may be reused for many maps.</p>
 */
public final class TiledValidator {

    private final List<ValidationPass> passes;

    /**
     * @param passes passes to run, in order; defensively copied
     * @throws NullPointerException     if the list or an element is {@code null}
     * @throws IllegalArgumentException if the list is empty
     */
    public TiledValidator(List<ValidationPass> passes) {
        this.passes = List.copyOf(passes);
        if (this.passes.isEmpty()) {
            throw new IllegalArgumentException("pass list must not be empty");
        }
    }

    /**
     * @return a validator with the standard seven passes in the canonical order:
     *         z-groups, sublayers, materials, gid-bounds, markers, chunk-align, stairs
     */
    public static TiledValidator standard() {
        return new TiledValidator(List.of(
                new ZGroupContiguityPass(),
                new SublayerContractPass(),
                new MaterialResolutionPass(),
                new GidBoundsPass(),
                new MarkerContractPass(),
                new ChunkAlignmentPass(),
                new StairRampPass()));
    }

    /**
     * Runs every pass in order over {@code context}.
     *
     * @param context immutable validation input, never {@code null}
     * @return all findings in deterministic emission order; never {@code null}
     */
    public ValidationReport validate(MapCheckContext context) {
        Objects.requireNonNull(context, "context");
        List<ValidationIssue> issues = new ArrayList<>();
        for (ValidationPass pass : passes) {
            pass.run(context, issues::add);
        }
        return new ValidationReport(issues);
    }

    /**
     * Convenience loader: parses {@code mapFile} and every external tileset it
     * references (resolved against the map file's directory), collecting reader
     * warnings into the context.
     *
     * @param mapFile path to the {@code .tmx} document, never {@code null}
     * @param raws    raws id universe to validate against, never {@code null}
     * @return a fully populated context for {@link #validate}
     * @throws TmxParseException    on malformed or out-of-scope map/tileset documents
     * @throws UncheckedIOException when the map or a referenced tileset cannot be read
     */
    public static MapCheckContext loadContext(Path mapFile, RawsIndex raws) {
        Objects.requireNonNull(mapFile, "mapFile");
        Objects.requireNonNull(raws, "raws");
        List<String> warnings = new ArrayList<>();
        TmxMap map = new TmxReader(warnings::add).read(mapFile);
        TsxReader tsxReader = new TsxReader(warnings::add);
        List<ResolvedTileset> tilesets = new ArrayList<>();
        Path baseDir = mapFile.toAbsolutePath().getParent();
        for (TmxTilesetRef ref : map.tilesets()) {
            TmxTileset tileset = tsxReader.read(baseDir.resolve(ref.source()));
            tilesets.add(new ResolvedTileset(ref, tileset));
        }
        String name = mapFile.getFileName().toString();
        return new MapCheckContext(name, map, tilesets, warnings, raws);
    }
}
