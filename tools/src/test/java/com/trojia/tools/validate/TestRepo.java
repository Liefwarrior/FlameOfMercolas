package com.trojia.tools.validate;

import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Test-only path locator: resolves repo content directories whether the JVM runs
 * from {@code tools/} (Gradle) or the repo root (IDE), by walking up from the
 * working directory.
 */
final class TestRepo {

    private TestRepo() {
    }

    /** @return the {@code content/maps/src} directory holding the authored fixtures */
    static Path mapsDir() {
        return locate(Path.of("content", "maps", "src"), "tavern_fixture.tmx");
    }

    /** @return the {@code content/raws} directory */
    static Path rawsDir() {
        return locate(Path.of("content", "raws", "materials"), "granite.json").getParent();
    }

    private static Path locate(Path rel, String proofFile) {
        for (Path base = Path.of("").toAbsolutePath(); base != null; base = base.getParent()) {
            Path candidate = base.resolve(rel);
            if (Files.isRegularFile(candidate.resolve(proofFile))) {
                return candidate;
            }
        }
        throw new IllegalStateException("cannot locate " + rel + " above " + Path.of("").toAbsolutePath());
    }
}
