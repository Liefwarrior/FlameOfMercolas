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

    @Test
    void rejectsSeekFoodPriorityBelowReturnHomePriority(@org.junit.jupiter.api.io.TempDir Path dir)
            throws Exception {
        copyRealRawsInto(dir);
        // serf.json authors seekFood.priority=305 == returnHome.priority; drop it below.
        String serfJson = Files.readString(dir.resolve("serf.json"))
                .replace("\"seekFood\": { \"priority\": 305 }", "\"seekFood\": { \"priority\": 304 }");
        Files.writeString(dir.resolve("serf.json"), serfJson);

        ActorRawsValidationException ex = assertThrows(ActorRawsValidationException.class,
                () -> ActorRawsLoader.load(dir));
        assertTrue(ex.getMessage().contains("seekFood.priority"), ex.getMessage());
    }

    @Test
    void rejectsHungerLowBonusBelowRestLowBonus(@org.junit.jupiter.api.io.TempDir Path dir)
            throws Exception {
        copyRealRawsInto(dir);
        // serf.json (post-fix) authors hunger.lowBonus=500 >= rest.lowBonus=250; invert that
        // (same-band LOW-vs-LOW case).
        String serfJson = Files.readString(dir.resolve("serf.json"))
                .replace("\"hunger\": { \"start\": 8000, \"decayPerKilotick\": 1000, "
                                + "\"recoverPerTick\": 0, \"lowBonus\": 500, \"critBonus\": 1000 }",
                        "\"hunger\": { \"start\": 8000, \"decayPerKilotick\": 1000, "
                                + "\"recoverPerTick\": 0, \"lowBonus\": 200, \"critBonus\": 1000 }");
        Files.writeString(dir.resolve("serf.json"), serfJson);

        ActorRawsValidationException ex = assertThrows(ActorRawsValidationException.class,
                () -> ActorRawsLoader.load(dir));
        assertTrue(ex.getMessage().contains("needs.hunger.lowBonus"), ex.getMessage());
    }

    // Note: there is no standalone "CRITICAL-vs-CRITICAL fails independently" test. Given
    // NeedConfig's own per-need monotonicity (critBonus >= lowBonus, enforced at construction),
    // the (HUNGER=CRITICAL, REST=CRITICAL) and (HUNGER=CRITICAL, REST=LOW) comparisons can only
    // ever fail if (HUNGER=LOW, REST=CRITICAL) — the hardest case, checked earlier in the loop —
    // also fails, so those two branches are deliberate defense-in-depth against a future relaxation
    // of that invariant, not independently reachable with today's NeedConfig. The exhaustive
    // truth-table test below proves all 4 combinations hold for every real committed type.

    @Test
    void rejectsHungerLowBonusBelowRestCritBonus_crossBand(
            @org.junit.jupiter.api.io.TempDir Path dir) throws Exception {
        // The real, previously-undetected gap (adversarial verify pass): HUNGER merely LOW
        // while REST is CRITICAL. Reproduces serf's exact pre-fix numbers
        // (hunger.lowBonus=300 < rest.critBonus=500) to prove this cross-band case is now
        // caught — a prior version of this loader only compared same-band pairs and let this
        // combination load silently.
        copyRealRawsInto(dir);
        String serfJson = Files.readString(dir.resolve("serf.json"))
                .replace("\"hunger\": { \"start\": 8000, \"decayPerKilotick\": 1000, "
                                + "\"recoverPerTick\": 0, \"lowBonus\": 500, \"critBonus\": 1000 }",
                        "\"hunger\": { \"start\": 8000, \"decayPerKilotick\": 1000, "
                                + "\"recoverPerTick\": 0, \"lowBonus\": 300, \"critBonus\": 600 }");
        Files.writeString(dir.resolve("serf.json"), serfJson);

        ActorRawsValidationException ex = assertThrows(ActorRawsValidationException.class,
                () -> ActorRawsLoader.load(dir));
        assertTrue(ex.getMessage().contains("needs.hunger.lowBonus"), ex.getMessage());
        assertTrue(ex.getMessage().contains("CRITICAL"), ex.getMessage());
    }

    @Test
    void everyCommittedActorTypeSatisfiesAllFourHungerVsRestBandCombinations() {
        // The exhaustiveness proof: for every real committed actor type, recompute the exact
        // SEEK_FOOD/RETURN_HOME scoring formula (excluding RETURN_HOME's night-rhythm term, a
        // deliberate schedule-effect exclusion) for all 2x2 = 4 (HUNGER band x REST band)
        // combinations and assert SEEK_FOOD's score is never lower — a truth table over real
        // data, not just the one previously-known counterexample.
        ActorTypeStatsTable table = ActorRawsLoader.load(committedActorsRawsDir());
        for (ActorTypeStats stats : table.all()) {
            NeedConfig hunger = stats.need(Need.HUNGER);
            NeedConfig rest = stats.need(Need.REST);
            int[] hungerBonus = {hunger.lowBonus(), hunger.critBonus()};
            int[] restBonus = {rest.lowBonus(), rest.critBonus()};
            String[] band = {"LOW", "CRITICAL"};
            for (int hi = 0; hi < 2; hi++) {
                for (int ri = 0; ri < 2; ri++) {
                    int seekFoodScore = stats.seekFoodPriority() + hungerBonus[hi];
                    int returnHomeScore = stats.returnHomePriority() + restBonus[ri];
                    assertTrue(seekFoodScore >= returnHomeScore,
                            stats.typeId() + ": HUNGER=" + band[hi] + " (score " + seekFoodScore
                                    + ") must be >= REST=" + band[ri] + " (score " + returnHomeScore
                                    + ")");
                }
            }
        }
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
