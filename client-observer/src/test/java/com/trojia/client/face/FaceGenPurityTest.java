package com.trojia.client.face;

import com.trojia.client.boot.RepoPaths;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.regex.Pattern;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * FACES-SPEC §7 T15's "no floats in facegen" rule, kept as a source scan (no ArchUnit
 * dependency in this module): the GL-free facegen classes must be integer-only — no
 * {@code float}/{@code double}, no {@code java.util.Random}, no {@code Math.random} — so
 * face identity can never drift across JVMs or platforms. The GL classes
 * ({@code InspectorFaces}) legitimately use floats for batch geometry and are exempt.
 */
class FaceGenPurityTest {

    private static final List<String> GL_FREE_SOURCES = List.of(
            "FaceGen.java", "FaceComposition.java", "FaceArchetype.java",
            "FaceArchetypes.java", "HairColor.java", "HeadwearClass.java");

    private static final Pattern FORBIDDEN = Pattern.compile(
            "\\bfloat\\b|\\bdouble\\b|java\\.util\\.Random\\b|Math\\.random");

    @Test
    void glFreeFacegenSources_areIntegerOnly() throws IOException {
        Path dir = RepoPaths.locate("client-observer", "src", "main", "java",
                "com", "trojia", "client", "face");
        for (String file : GL_FREE_SOURCES) {
            String source = Files.readString(dir.resolve(file), StandardCharsets.UTF_8);
            var matcher = FORBIDDEN.matcher(source);
            boolean found = matcher.find();
            assertTrue(!found, file + ": facegen must stay integer-only, found \""
                    + (found ? matcher.group() : "") + "\"");
        }
    }
}
