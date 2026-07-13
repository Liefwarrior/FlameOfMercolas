package com.trojia.client.boot;

import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Locates repo-relative files by walking up from the JVM working directory — the same
 * convention {@code tools}' importer tests use ({@code ImporterTestSupport.locate},
 * {@code ShippedArtMapping.path}) so this observer finds {@code content/} whether it is
 * launched from the repo root or from {@code client-observer/} (Gradle's
 * {@code application} plugin runs {@code :run} with the subproject directory as the
 * working directory).
 *
 * <p><b>Packaging caveat (flagged for later milestones):</b> this assumes the observer
 * always runs from inside a git checkout of this repo, which holds for every milestone
 * through v0 — the client only ever ships as a Gradle-run dev build so far. A packaged
 * distribution would need to bundle {@code content/} alongside the jar instead; revisit
 * this class before that day comes.
 */
public final class RepoPaths {

    private static final int MAX_LEVELS_UP = 6;

    private RepoPaths() {
    }

    /**
     * Resolves a path relative to the repo root, searching the working directory and
     * successive parents for an entry that already exists.
     *
     * @param first first path segment (e.g. {@code "content"})
     * @param more  remaining path segments
     * @return the resolved, existing path
     * @throws IllegalStateException if no ancestor of the working directory carries it
     *                                within {@value #MAX_LEVELS_UP} levels
     */
    public static Path locate(String first, String... more) {
        Path relative = Path.of(first, more);
        Path dir = Path.of("").toAbsolutePath();
        for (int i = 0; i < MAX_LEVELS_UP && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve(relative);
            if (Files.exists(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException(
                "cannot locate " + relative + " above " + Path.of("").toAbsolutePath());
    }
}
