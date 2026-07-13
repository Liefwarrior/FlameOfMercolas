package com.trojia.client.atlas;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Test fixture access to the shipped {@code content/art/placeholder/art-mapping.json},
 * located by walking up from the test working directory (same convention as
 * {@code JsonTileArtResolverTest}).
 */
final class ShippedArtMapping {

    private ShippedArtMapping() {
    }

    static Path path() {
        Path dir = Paths.get(System.getProperty("user.dir")).toAbsolutePath();
        for (int i = 0; i < 5 && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve(
                    Paths.get("content", "art", "placeholder", "art-mapping.json"));
            if (Files.isRegularFile(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException(
                "art-mapping.json not found above " + System.getProperty("user.dir"));
    }

    static String json() {
        try {
            return Files.readString(path(), StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
    }
}
