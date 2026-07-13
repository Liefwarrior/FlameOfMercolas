package com.trojia.client.atlas;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Test fixture access to the shipped art mappings — the procedural
 * {@code content/art/placeholder/art-mapping.json} and the real
 * {@code content/art/kenney/art-mapping.json} — located by walking up from the test working
 * directory (same convention as {@code JsonTileArtResolverTest}).
 */
final class ShippedArtMapping {

    private ShippedArtMapping() {
    }

    static Path path() {
        return locate("placeholder");
    }

    static String json() {
        return read(path());
    }

    /** The shipped real Kenney pack mapping. */
    static Path kenneyPath() {
        return locate("kenney");
    }

    static String kenneyJson() {
        return read(kenneyPath());
    }

    private static Path locate(String pack) {
        Path dir = Paths.get(System.getProperty("user.dir")).toAbsolutePath();
        for (int i = 0; i < 5 && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve(
                    Paths.get("content", "art", pack, "art-mapping.json"));
            if (Files.isRegularFile(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException(
                "content/art/" + pack + "/art-mapping.json not found above "
                        + System.getProperty("user.dir"));
    }

    private static String read(Path path) {
        try {
            return Files.readString(path, StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
    }
}
