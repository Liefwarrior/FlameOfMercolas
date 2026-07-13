package com.trojia.tools.validate;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * Acceptance half of the validator (ARCHITECTURE.md section 12, M1): both authored
 * fixtures pass the standard pass list with zero errors against the committed raws,
 * and the raws themselves are consistent.
 */
class FixtureValidationTest {

    private static RawsLoadResult raws;

    @BeforeAll
    static void loadRaws() {
        raws = new RawsLoader().load(TestRepo.rawsDir());
    }

    @Test
    void committedRawsAreConsistent() {
        assertFalse(raws.hasErrors(), () -> "raws must be clean: "
                + raws.issues().stream().map(ValidationIssue::format).toList());
        assertTrue(raws.index().isMaterial("granite"));
        assertTrue(raws.index().isMaterial("trudgeon_wood@getilia_soak"),
                "treatment-minted derived id must be in the material universe");
        assertEquals(0, raws.index().flammability("trudgeon_wood@getilia_soak").orElseThrow(),
                "getilia soak overrides flammability to 0");
        assertTrue(raws.index().flammability("oak").orElseThrow() > 0);
        assertTrue(raws.index().isFluid("water"));
    }

    @Test
    void tavernFixturePassesWithZeroErrors() {
        MapCheckContext context = TiledValidator.loadContext(
                TestRepo.mapsDir().resolve("tavern_fixture.tmx"), raws.index());
        ValidationReport report = TiledValidator.standard().validate(context);
        assertEquals(0, report.errors().size(), report::render);
        // 48x32 is not chunk-aligned: exactly the advisory warning, nothing else.
        assertEquals(1, report.warnings().size(), report::render);
        assertEquals("chunk-align", report.warnings().get(0).passId());
    }

    @Test
    void ubendFixturePassesWithZeroErrors() {
        MapCheckContext context = TiledValidator.loadContext(
                TestRepo.mapsDir().resolve("ubend_fixture.tmx"), raws.index());
        ValidationReport report = TiledValidator.standard().validate(context);
        assertEquals(0, report.errors().size(), report::render);
        assertEquals(1, report.warnings().size(), report::render);
        assertEquals("chunk-align", report.warnings().get(0).passId());
    }

    @Test
    void validationIsDeterministic() {
        MapCheckContext context = TiledValidator.loadContext(
                TestRepo.mapsDir().resolve("tavern_fixture.tmx"), raws.index());
        ValidationReport first = TiledValidator.standard().validate(context);
        ValidationReport second = TiledValidator.standard().validate(context);
        assertEquals(first, second, "identical input must yield an identical report");
    }
}
