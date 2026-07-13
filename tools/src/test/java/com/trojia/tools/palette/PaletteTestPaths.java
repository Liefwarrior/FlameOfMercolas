package com.trojia.tools.palette;

import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Locates the committed content the palette tests run against, independent of the
 * Gradle working directory (same walk-up strategy as the tmx fixture tests).
 */
final class PaletteTestPaths {

    private PaletteTestPaths() {
    }

    /** @return the repo's {@code content/raws} directory */
    static Path rawsDir() {
        return repoRoot().resolve(Path.of("content", "raws"));
    }

    /** @return the committed hand-authored {@code content/maps/src/materials.tsx} */
    static Path handAuthoredTsx() {
        return repoRoot().resolve(Path.of("content", "maps", "src", "materials.tsx"));
    }

    /** Walks up from the working directory until {@code content/raws/materials} exists. */
    private static Path repoRoot() {
        Path marker = Path.of("content", "raws", "materials");
        for (Path base = Path.of("").toAbsolutePath(); base != null; base = base.getParent()) {
            if (Files.isDirectory(base.resolve(marker))) {
                return base;
            }
        }
        throw new IllegalStateException(
                "cannot locate content/raws above " + Path.of("").toAbsolutePath());
    }
}
