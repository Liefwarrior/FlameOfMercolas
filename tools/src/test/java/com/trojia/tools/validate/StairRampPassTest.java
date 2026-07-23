package com.trojia.tools.validate;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.StringReader;
import java.util.ArrayList;
import java.util.List;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import com.trojia.tools.tmx.TmxMap;
import com.trojia.tools.tmx.TmxReader;
import com.trojia.tools.tmx.TmxTileset;
import com.trojia.tools.tmx.TsxReader;

/**
 * Pass 7 ({@link StairRampPass}, Sprint 4's climb): the README's STAIR_UP/STAIR_DOWN
 * same-column pairing rule and the RAMP head-room + walkable-z+1-exit rules — broken
 * inline fixtures each yield exactly the right single error, well-formed connectors and
 * every committed map yield none.
 */
class StairRampPassTest {

    private static final String MAP_NAME = "inline.tmx";

    /** 4x4 layers: gid 1 granite/WALL fill, 0 = nothing. */
    private static final String GRANITE_FILL = "1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1";
    private static final String EMPTY = "0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0";

    private static RawsIndex raws;

    @BeforeAll
    static void loadRaws() {
        raws = new RawsLoader().load(TestRepo.rawsDir()).index();
    }

    // ------------------------------------------------------------------ well-formed

    @Test
    void pairedStairsAndAnExitedRampAreClean() {
        // z:+0: STAIR_UP at (1,1) + RAMP at (2,2) in a granite fill;
        // z:+1: STAIR_DOWN at (1,1), open air over the ramp, a floor exit at (3,2).
        String z0terrain = "1,1,1,1, 1,3,1,1, 1,1,5,1, 1,1,1,1";
        String z1terrain = "0,0,0,0, 0,4,0,0, 0,0,0,0, 0,0,0,0";
        String z1floor = "0,0,0,0, 0,0,0,0, 0,0,0,2, 0,0,0,0";
        ValidationReport report = validate(map(
                group("z:+0", z0terrain, EMPTY) + group("z:+1", z1terrain, z1floor)));
        assertEquals(0, report.errors().size(), report::render);
    }

    // ---------------------------------------------------------------- broken stairs

    @Test
    void aStairUpWithoutItsDownHalfIsSingleError() {
        String z0terrain = "1,1,1,1, 1,3,1,1, 1,1,1,1, 1,1,1,1";
        ValidationReport report = validate(map(
                group("z:+0", z0terrain, EMPTY) + group("z:+1", EMPTY, EMPTY)));
        ValidationIssue issue = singleError(report);
        assertTrue(issue.message().contains("STAIR_UP at z=0 has no STAIR_DOWN"), issue::format);
        assertEquals(1, issue.x(), issue::format);
        assertEquals(1, issue.y(), issue::format);
        assertTrue(issue.hint().contains("pairing rule"), issue::format);
    }

    @Test
    void aStairDownOverSolidFillIsSingleError() {
        String z1terrain = "0,0,0,0, 0,4,0,0, 0,0,0,0, 0,0,0,0";
        ValidationReport report = validate(map(
                group("z:+0", GRANITE_FILL, EMPTY) + group("z:+1", z1terrain, EMPTY)));
        ValidationIssue issue = singleError(report);
        assertTrue(issue.message().contains("STAIR_DOWN at z=1 has no STAIR_UP"), issue::format);
        assertTrue(issue.message().contains("found WALL"), issue::format);
    }

    // ----------------------------------------------------------------- broken ramps

    @Test
    void aRampWithFillDirectlyAboveIsSingleError() {
        String z0terrain = "1,1,1,1, 1,1,1,1, 1,1,5,1, 1,1,1,1";
        String z1terrain = "0,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0"; // WALL over the ramp column
        String z1floor = "0,0,0,0, 0,0,0,0, 0,0,0,2, 0,0,0,0";   // exit exists: isolate the error
        ValidationReport report = validate(map(
                group("z:+0", z0terrain, EMPTY) + group("z:+1", z1terrain, z1floor)));
        ValidationIssue issue = singleError(report);
        assertTrue(issue.message().contains("RAMP at z=0 has WALL directly above"), issue::format);
        assertEquals(2, issue.x(), issue::format);
        assertEquals(2, issue.y(), issue::format);
    }

    @Test
    void aRampToNowhereIsSingleError() {
        String z0terrain = "1,1,1,1, 1,1,1,1, 1,1,5,1, 1,1,1,1";
        ValidationReport report = validate(map(
                group("z:+0", z0terrain, EMPTY) + group("z:+1", EMPTY, EMPTY)));
        ValidationIssue issue = singleError(report);
        assertTrue(issue.message().contains("RAMP at z=0 has no walkable exit"), issue::format);
        assertTrue(issue.hint().contains("climb TO somewhere"), issue::format);
    }

    // -------------------------------------------------- committed maps are connector-clean

    @Test
    void everyCommittedFixtureMapPassesTheStairRampRules() {
        for (String mapFile : List.of("tavern_fixture.tmx", "ubend_fixture.tmx",
                "compound_block.tmx", "docks_surface.tmx")) {
            MapCheckContext context = TiledValidator.loadContext(
                    TestRepo.mapsDir().resolve(mapFile), raws);
            ValidationReport report = new TiledValidator(List.of(new StairRampPass()))
                    .validate(context);
            assertEquals(0, report.errors().size(),
                    () -> mapFile + " has connector defects:\n" + report.render());
        }
    }

    /**
     * The docks known-defect LEDGER (Sprint 4) — now EMPTY. The pass's first run surfaced
     * two real sealed stairs (interior-detail painting had overwritten the stair HEAD):
     * K01 Weighhouse {@code z:+11 (58,48)} (the z12 strongroom lockbox painted on the stair
     * head — lockbox moved to (57,49)) and C4 Gullet {@code z:+12 (188,90)} (the K35
     * Roost's south border painted over the roof-access head — stair relocated to
     * (187,91)). Both fixed in gen_docks_surface.py + regenerated; the docks now rides
     * {@link #everyCommittedFixtureMapPassesTheStairRampRules} with every other committed
     * map, and this ledger stays pinned at ZERO — any future regen that seals a connector
     * fails the clean loop immediately.
     */
    @Test
    void theDocksSealedStairLedgerIsShrunkToZero() {
        MapCheckContext context = TiledValidator.loadContext(
                TestRepo.mapsDir().resolve("docks_surface.tmx"), raws);
        ValidationReport report = new TiledValidator(List.of(new StairRampPass()))
                .validate(context);
        assertEquals(0, report.errors().size(),
                () -> "the docks sealed-stair ledger must stay empty:\n" + report.render());
    }

    // ----------------------------------------------------------------- scaffolding

    private static ValidationIssue singleError(ValidationReport report) {
        List<ValidationIssue> errors = report.errors();
        assertEquals(1, errors.size(), () -> "expected exactly one error:\n" + report.render());
        ValidationIssue issue = errors.get(0);
        assertEquals("stairs", issue.passId(), issue::format);
        assertEquals(MAP_NAME, issue.source(), issue::format);
        return issue;
    }

    private static ValidationReport validate(String mapXml) {
        List<String> warnings = new ArrayList<>();
        TmxMap map = new TmxReader(warnings::add).read(new StringReader(mapXml));
        TmxTileset tileset = new TsxReader(warnings::add).read(new StringReader(TSX));
        MapCheckContext context = new MapCheckContext(MAP_NAME, map,
                List.of(new ResolvedTileset(map.tilesets().get(0), tileset)), warnings, raws);
        return TiledValidator.standard().validate(context);
    }

    private static String map(String groups) {
        return """
                <?xml version="1.0" encoding="UTF-8"?>
                <map version="1.10" orientation="orthogonal" renderorder="right-down" width="4" height="4"
                     tilewidth="16" tileheight="16" infinite="0">
                 <tileset firstgid="1" source="test.tsx"/>
                """ + groups + "</map>";
    }

    private static String group(String name, String terrainCsv, String floorCsv) {
        return "<group name=\"" + name + "\">" + tileLayer("terrain", terrainCsv)
                + tileLayer("floor", floorCsv) + "<objectgroup name=\"markers\"/></group>";
    }

    private static String tileLayer(String name, String csv) {
        return "<layer name=\"" + name + "\" width=\"4\" height=\"4\"><data encoding=\"csv\">"
                + csv + "</data></layer>";
    }

    /** gid 1 granite/WALL, 2 oak/FLOOR, 3 oak/STAIR_UP, 4 oak/STAIR_DOWN, 5 granite/RAMP. */
    private static final String TSX = """
            <?xml version="1.0" encoding="UTF-8"?>
            <tileset version="1.10" name="test" tilewidth="16" tileheight="16" tilecount="5" columns="0">
             <tile id="0"><properties>
              <property name="material" value="granite"/><property name="form" value="WALL"/>
             </properties></tile>
             <tile id="1"><properties>
              <property name="material" value="oak"/><property name="form" value="FLOOR"/>
             </properties></tile>
             <tile id="2"><properties>
              <property name="material" value="oak"/><property name="form" value="STAIR_UP"/>
             </properties></tile>
             <tile id="3"><properties>
              <property name="material" value="oak"/><property name="form" value="STAIR_DOWN"/>
             </properties></tile>
             <tile id="4"><properties>
              <property name="material" value="granite"/><property name="form" value="RAMP"/>
             </properties></tile>
            </tileset>
            """;
}
