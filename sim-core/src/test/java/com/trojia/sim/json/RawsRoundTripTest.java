package com.trojia.sim.json;

import org.junit.jupiter.api.DynamicTest;
import org.junit.jupiter.api.TestFactory;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.List;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Parses every committed raws file under {@code content/raws} in
 * {@link JsonNumberMode#INTEGER_ONLY} mode (raws are integer-only by project
 * convention) and verifies the canonical round trip: parse → write → reparse
 * yields an equal tree, and the canonical text is a fixed point.
 *
 * <p>Because the strict parser rejects duplicate keys, decimals, exponents,
 * comments and trailing commas outright, a green run doubles as a hygiene
 * audit of the committed raws themselves.</p>
 */
final class RawsRoundTripTest {

    /** Minimum committed raws count at the time this test was written. */
    private static final int MIN_EXPECTED_RAWS = 18;

    @TestFactory
    Stream<DynamicTest> everyCommittedRawRoundTrips() throws IOException {
        Path rawsDir = locateRawsDir();
        List<Path> files;
        try (Stream<Path> walk = Files.walk(rawsDir)) {
            files = walk
                    .filter(p -> p.toString().endsWith(".json"))
                    .sorted(Comparator.comparing(p -> p.toString().replace('\\', '/')))
                    .toList();
        }
        assertTrue(files.size() >= MIN_EXPECTED_RAWS,
                "expected at least " + MIN_EXPECTED_RAWS + " raws files under "
                        + rawsDir + " but found " + files.size());
        return files.stream().map(file -> DynamicTest.dynamicTest(
                rawsDir.relativize(file).toString().replace('\\', '/'),
                () -> assertRoundTrips(file)));
    }

    private static void assertRoundTrips(Path file) throws IOException {
        byte[] bytes = Files.readAllBytes(file);

        // Strict UTF-8 byte parse, integer-only (the raws hygiene contract).
        JsonValue tree = MiniJson.parse(bytes, JsonNumberMode.INTEGER_ONLY);

        // The String entry point must agree with the byte entry point.
        JsonValue viaString = MiniJson.parse(
                new String(bytes, StandardCharsets.UTF_8), JsonNumberMode.INTEGER_ONLY);
        assertEquals(tree, viaString, "byte and String parses disagree");

        // Every raw is a top-level object declaring an id.
        JsonObject root = assertInstanceOf(JsonObject.class, tree);
        assertTrue(root.has("id"), "raw is missing \"id\": " + file);

        // Canonical round trip: reparse equals, and the text is a fixed point.
        String canonical = MiniJson.write(tree);
        JsonValue reparsed = MiniJson.parse(canonical, JsonNumberMode.INTEGER_ONLY);
        assertEquals(tree, reparsed, "canonical text reparsed to a different tree");
        assertEquals(canonical, MiniJson.write(reparsed), "canonical text is not a fixed point");
    }

    /**
     * Walks up from the test working directory (the sim-core module dir under
     * Gradle) to the repo root containing {@code content/raws}.
     */
    private static Path locateRawsDir() {
        Path dir = Path.of("").toAbsolutePath();
        for (int i = 0; i < 6 && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException(
                "content/raws not found above " + Path.of("").toAbsolutePath());
    }
}
