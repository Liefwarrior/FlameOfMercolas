package com.trojia.sim.bark;

import com.trojia.sim.actor.ActorRawsValidationException;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-2 bark schema contract: a missing barks.json degrades to EMPTY (World's text
 * tables land later), a present file validates strictly and loads deterministic tables.
 */
final class BarkRawsLoaderTest {

    private static final String VALID = """
            {
              "id": "barks",
              "tables": [
                { "key": "greet.watch.cold.night",
                  "rows": ["Move along, you.", "The Watch sees you."] },
                { "key": "mood.held", "rows": ["A day in the cage..."] }
              ],
              "notes": "test fixture"
            }
            """;

    @Test
    void aMissingFileLoadsTheEmptyRegistry(@TempDir Path tmp) {
        BarkTableRegistry tables = BarkRawsLoader.load(tmp);
        assertEquals(0, tables.size(), "unauthored -> EMPTY, never an error");
        assertEquals(0, tables.rowCount("greet.watch"));
    }

    @Test
    void aPresentFileLoadsSortedDeterministicTables(@TempDir Path tmp) throws IOException {
        Files.createDirectories(tmp.resolve("barks"));
        Files.write(tmp.resolve("barks").resolve("barks.json"),
                VALID.getBytes(StandardCharsets.UTF_8));
        BarkTableRegistry tables = BarkRawsLoader.load(tmp);
        assertEquals(2, tables.size());
        assertTrue(tables.contains("greet.watch.cold.night"));
        assertEquals(2, tables.rowCount("greet.watch.cold.night"));
        assertEquals("The Watch sees you.", tables.row("greet.watch.cold.night", 1));
        assertEquals("A day in the cage...", tables.row("mood.held", 0));
    }

    @Test
    void aPresentFileValidatesStrictly() {
        assertThrows(ActorRawsValidationException.class,
                () -> BarkRawsLoader.parse("{}".getBytes(StandardCharsets.UTF_8)),
                "missing id/tables must fail loudly");
        assertThrows(ActorRawsValidationException.class, () -> BarkRawsLoader.parse("""
                {"id": "barks", "tables": [ {"key": "a", "rows": []} ]}
                """.getBytes(StandardCharsets.UTF_8)), "empty rows must fail loudly");
        assertThrows(ActorRawsValidationException.class, () -> BarkRawsLoader.parse("""
                {"id": "barks", "tables": [
                  {"key": "a", "rows": ["x"]}, {"key": "a", "rows": ["y"]} ]}
                """.getBytes(StandardCharsets.UTF_8)), "duplicate keys must fail loudly");
    }
}
