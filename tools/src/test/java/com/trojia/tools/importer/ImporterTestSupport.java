package com.trojia.tools.importer;

import com.trojia.sim.material.MaterialRawsLoader;
import com.trojia.sim.material.RawsBundle;
import com.trojia.tools.tmx.TmxMap;
import com.trojia.tools.tmx.TmxReader;
import com.trojia.tools.tmx.TmxTileset;
import com.trojia.tools.tmx.TmxTilesetRef;
import com.trojia.tools.tmx.TsxReader;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Shared fixture-location and load helpers for the importer tests: resolves the
 * repo's {@code content/} tree whether the tests run from {@code tools/} or the
 * repo root (mirroring {@code TavernFixtureIntegrationTest}).
 */
final class ImporterTestSupport {

    private ImporterTestSupport() {
    }

    /** The parsed map plus its single external tileset. */
    record Fixture(TmxMap map, TmxTileset tileset) {
    }

    /** Resolves a repo-relative directory, walking up from the working directory. */
    static Path locate(String... relative) {
        Path rel = Path.of(relative[0], java.util.Arrays.copyOfRange(relative, 1, relative.length));
        for (Path base = Path.of("").toAbsolutePath(); base != null; base = base.getParent()) {
            Path candidate = base.resolve(rel);
            if (Files.exists(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException("cannot locate " + rel + " above " + Path.of("").toAbsolutePath());
    }

    /** Parses the tavern fixture and its tileset with the production readers. */
    static Fixture tavernFixture() {
        Path mapsDir = locate("content", "maps", "src");
        List<String> warnings = new ArrayList<>();
        TmxMap map = new TmxReader(warnings::add).read(mapsDir.resolve("tavern_fixture.tmx"));
        TmxTilesetRef ref = map.tilesets().get(0);
        TmxTileset tileset = new TsxReader(warnings::add).read(mapsDir.resolve(ref.source()));
        return new Fixture(map, tileset);
    }

    /** Loads and validates the repo raws bundle ({@code content/raws}). */
    static RawsBundle raws() {
        return MaterialRawsLoader.load(locate("content", "raws"));
    }
}
