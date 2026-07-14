package com.trojia.client.face;

import com.trojia.client.boot.RepoPaths;
import com.trojia.client.sprite.SpriteIndex;

import java.io.IOException;
import java.io.StringReader;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Test access to the shipped faces content ({@code content/art/faces/**}), located by
 * walking up from the test working directory (the {@code ShippedArtMapping} convention, so
 * tests pass whether Gradle runs them from the repo root or from {@code client-observer/}).
 */
final class ShippedFaceContent {

    private ShippedFaceContent() {
    }

    static Path dir() {
        return RepoPaths.locate("content", "art", "faces");
    }

    static SpriteIndex index() {
        return SpriteIndex.load(new StringReader(read(dir().resolve("face-parts-index.json"))));
    }

    static FaceArchetypes archetypes() {
        return FaceArchetypes.load(new StringReader(read(dir().resolve("face-archetypes.json"))));
    }

    static FaceGen gen() {
        return new FaceGen(index(), archetypes());
    }

    static String read(Path path) {
        try {
            return Files.readString(path, StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read " + path, e);
        }
    }
}
