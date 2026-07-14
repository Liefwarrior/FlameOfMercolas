package com.trojia.sim.actor;

import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link ActorRawsLoader}'s cross-check against {@link ActorTypes#ALL} —
 * previously untested (unlike {@code JobBinder}'s analogous 1:1 job/leaf
 * validation, which has its own test coverage). Confirms both directions
 * fail fast instead of silently loading a mismatched raws set, and that the
 * real committed content still loads cleanly.
 */
final class ActorRawsLoaderTest {

    private static Path committedActorsRawsDir() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws").resolve("actors");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws/actors not found above "
                + Path.of("").toAbsolutePath());
    }

    @Test
    void loadsTheCommittedContentCleanly() {
        ActorTypeStatsTable table = ActorRawsLoader.load(committedActorsRawsDir());

        assertEquals(ActorTypes.ALL.size(), table.size(),
                "every registered ActorTypes.ALL entry must have exactly one raws file");
        for (ActorTypes.Registration reg : ActorTypes.ALL) {
            assertEquals(reg.id(), table.get(reg.id()).typeId());
        }
    }

    @Test
    void rejectsARawsIdWithNoRegisteredActorType(@org.junit.jupiter.api.io.TempDir Path dir)
            throws Exception {
        copyRealRawsInto(dir);
        // Introduce a typo'd id that matches nothing in ActorTypes.ALL.
        String serfJson = Files.readString(dir.resolve("serf.json"))
                .replace("\"id\": \"serf\"", "\"id\": \"serfx\"");
        Files.writeString(dir.resolve("serf.json"), serfJson);

        ActorRawsValidationException ex = assertThrows(ActorRawsValidationException.class,
                () -> ActorRawsLoader.load(dir));
        assertTrue(ex.getMessage().contains("serfx"), ex.getMessage());
    }

    @Test
    void rejectsARegisteredActorTypeWithNoRawsFile(@org.junit.jupiter.api.io.TempDir Path dir)
            throws Exception {
        copyRealRawsInto(dir);
        Files.delete(dir.resolve("serf.json"));

        ActorRawsValidationException ex = assertThrows(ActorRawsValidationException.class,
                () -> ActorRawsLoader.load(dir));
        assertTrue(ex.getMessage().contains("serf"), ex.getMessage());
    }

    /** Copies every real committed actor raws file (including household.json) into {@code dir}. */
    private static void copyRealRawsInto(Path dir) throws Exception {
        Path real = committedActorsRawsDir();
        try (var files = Files.list(real)) {
            for (Path file : files.toList()) {
                Files.copy(file, dir.resolve(file.getFileName()));
            }
        }
    }
}
