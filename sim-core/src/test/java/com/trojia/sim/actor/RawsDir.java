package com.trojia.sim.actor;

import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Finds the committed {@code content/raws} directory from wherever the test JVM was started —
 * the one copy of a walk that several actor-package test files had grown their own version of.
 */
final class RawsDir {

    private RawsDir() {
    }

    static Path locate() {
        Path dir = Path.of("").toAbsolutePath();
        for (int i = 0; i < 6 && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException("content/raws not found above "
                + Path.of("").toAbsolutePath());
    }
}
