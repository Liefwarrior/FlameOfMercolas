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
 * Error-UX half of the validator acceptance: deliberately-broken inline fixtures
 * each produce exactly the right single error, attributed to the right pass, with
 * map name / layer path / coordinates / fix hint as applicable.
 *
 * <p>Maps here are 4x4 (deliberately chunk-misaligned), so every case also proves
 * the alignment pass stays a warning; assertions therefore filter to errors.</p>
 */
class BrokenFixtureValidationTest {

    private static final String MAP_NAME = "inline.tmx";

    private static RawsIndex raws;

    @BeforeAll
    static void loadRaws() {
        raws = new RawsLoader().load(TestRepo.rawsDir()).index();
    }

    // ------------------------------------------------------------ pass 1: z-groups

    @Test
    void badGroupNameIsSingleZGroupError() {
        ValidationReport report = validate(map(group("cellar", GRANITE_FILL, EMPTY, markers())), tsx());
        ValidationIssue issue = singleError(report, "z-groups");
        assertTrue(issue.message().contains("\"cellar\""), issue::format);
        assertTrue(issue.hint().contains("z:+0"), issue::format);
    }

    @Test
    void topLevelTileLayerIsSingleZGroupError() {
        String loose = "<layer name=\"loose\" width=\"4\" height=\"4\"><data encoding=\"csv\">"
                + EMPTY + "</data></layer>";
        ValidationReport report = validate(map(goodGroup("z:+0") + loose), tsx());
        ValidationIssue issue = singleError(report, "z-groups");
        assertTrue(issue.message().contains("\"loose\""), issue::format);
    }

    @Test
    void zGapIsSingleContiguityError() {
        ValidationReport report = validate(map(goodGroup("z:+0") + goodGroup("z:+2")), tsx());
        ValidationIssue issue = singleError(report, "z-groups");
        assertTrue(issue.message().contains("gap between z=0 and z=2"), issue::format);
        assertTrue(issue.hint().contains("z:+1"), issue::format);
    }

    @Test
    void missingStreetLevelIsSingleZOriginError() {
        ValidationReport report = validate(map(goodGroup("z:+1")), tsx());
        ValidationIssue issue = singleError(report, "z-groups");
        assertTrue(issue.message().contains("z:+0"), issue::format);
    }

    // ----------------------------------------------------------- pass 2: sublayers

    @Test
    void missingFloorIsSingleSublayerError() {
        String group = "<group name=\"z:+0\">" + tileLayer("terrain", GRANITE_FILL) + markers() + "</group>";
        ValidationReport report = validate(map(group), tsx());
        ValidationIssue issue = singleError(report, "sublayers");
        assertTrue(issue.message().contains("\"floor\""), issue::format);
        assertEquals("z:+0", issue.layerPath(), issue::format);
    }

    @Test
    void unknownSublayerIsSingleSublayerError() {
        String group = "<group name=\"z:+0\">" + tileLayer("terrain", GRANITE_FILL)
                + tileLayer("floor", EMPTY) + tileLayer("decor", EMPTY) + markers() + "</group>";
        ValidationReport report = validate(map(group), tsx());
        ValidationIssue issue = singleError(report, "sublayers");
        assertTrue(issue.message().contains("\"decor\""), issue::format);
        assertEquals("z:+0/decor", issue.layerPath(), issue::format);
    }

    // ----------------------------------------------------------- pass 3: materials

    @Test
    void unknownMaterialIsSingleErrorWithSuggestionAndCoordinates() {
        // gid 4 = the tileset's "granit" tile, painted once at (2,1).
        String terrain = "1,1,1,1, 1,1,4,1, 1,1,1,1, 1,1,1,1";
        String group = "<group name=\"z:+0\">" + tileLayer("terrain", terrain)
                + tileLayer("floor", EMPTY) + markers() + "</group>";
        ValidationReport report = validate(map(group), tsx("granit", "water"));
        ValidationIssue issue = singleError(report, "materials");
        assertTrue(issue.message().contains("unknown material \"granit\""), issue::format);
        assertTrue(issue.hint().contains("did you mean \"granite\"?"), issue::format);
        assertEquals("z:+0/terrain", issue.layerPath(), issue::format);
        assertEquals(2, issue.x(), issue::format);
        assertEquals(1, issue.y(), issue::format);
    }

    @Test
    void unknownFluidIsSingleErrorWithSuggestion() {
        // gid 3 = the tileset's fluid tile, declared as unknown fluid "wine".
        String fluids = "0,0,0,0, 0,3,0,0, 0,0,0,0, 0,0,0,0";
        String group = "<group name=\"z:+0\">" + tileLayer("terrain", GRANITE_FILL)
                + tileLayer("floor", EMPTY) + tileLayer("fluids", fluids) + markers() + "</group>";
        ValidationReport report = validate(map(group), tsx("granite", "wine"));
        ValidationIssue issue = singleError(report, "materials");
        assertTrue(issue.message().contains("unknown fluid \"wine\""), issue::format);
        assertTrue(issue.hint().contains("did you mean \"water\"?"), issue::format);
        assertEquals("z:+0/fluids", issue.layerPath(), issue::format);
    }

    @Test
    void fluidTileOnTerrainIsSingleError() {
        String terrain = "1,1,1,1, 1,1,1,3, 1,1,1,1, 1,1,1,1";
        String group = "<group name=\"z:+0\">" + tileLayer("terrain", terrain)
                + tileLayer("floor", EMPTY) + markers() + "</group>";
        ValidationReport report = validate(map(group), tsx());
        ValidationIssue issue = singleError(report, "materials");
        assertTrue(issue.message().contains("fluid tile"), issue::format);
        assertTrue(issue.hint().contains("fluids sublayer"), issue::format);
        assertEquals(3, issue.x(), issue::format);
        assertEquals(1, issue.y(), issue::format);
    }

    // ---------------------------------------------------------- pass 4: gid bounds

    @Test
    void gidOutOfBoundsIsSingleErrorAtCoordinates() {
        String terrain = "1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,99";
        String group = "<group name=\"z:+0\">" + tileLayer("terrain", terrain)
                + tileLayer("floor", EMPTY) + markers() + "</group>";
        ValidationReport report = validate(map(group), tsx());
        ValidationIssue issue = singleError(report, "gid-bounds");
        assertTrue(issue.message().contains("gid 99"), issue::format);
        assertTrue(issue.hint().contains("1..4"), issue::format);
        assertEquals(3, issue.x(), issue::format);
        assertEquals(3, issue.y(), issue::format);
    }

    @Test
    void flippedTileIsSingleRejectionError() {
        // 2147483649 = gid 1 with the horizontal-flip bit set.
        String terrain = "1,1,1,1, 1,2147483649,1,1, 1,1,1,1, 1,1,1,1";
        String group = "<group name=\"z:+0\">" + tileLayer("terrain", terrain)
                + tileLayer("floor", EMPTY) + markers() + "</group>";
        ValidationReport report = validate(map(group), tsx());
        ValidationIssue issue = singleError(report, "gid-bounds");
        assertTrue(issue.message().contains("flipped/rotated tiles are not allowed"), issue::format);
        assertTrue(issue.message().contains("terrain"), issue::format);
    }

    // ------------------------------------------------------------- pass 5: markers

    @Test
    void unnamedMarkerIsSingleError() {
        String group = goodGroupWithMarkers("z:+0",
                "<object id=\"7\" type=\"script_anchor\" x=\"24\" y=\"24\"><point/></object>");
        ValidationReport report = validate(map(group), tsx());
        ValidationIssue issue = singleError(report, "markers");
        assertTrue(issue.message().contains("object 7 has no name"), issue::format);
        assertEquals("z:+0/markers", issue.layerPath(), issue::format);
    }

    @Test
    void unknownMarkerClassIsSingleError() {
        String group = goodGroupWithMarkers("z:+0",
                "<object id=\"1\" name=\"spawn_a\" type=\"spawner\" x=\"24\" y=\"24\"><point/></object>");
        ValidationReport report = validate(map(group), tsx());
        ValidationIssue issue = singleError(report, "markers");
        assertTrue(issue.message().contains("unknown class \"spawner\""), issue::format);
        assertTrue(issue.hint().contains("light_source or script_anchor"), issue::format);
    }

    @Test
    void luminanceOutOfRangeIsSingleError() {
        String group = goodGroupWithMarkers("z:+0",
                "<object id=\"1\" name=\"torch\" type=\"light_source\" x=\"8\" y=\"8\">"
                        + "<properties><property name=\"luminance\" type=\"int\" value=\"40\"/></properties>"
                        + "<point/></object>");
        ValidationReport report = validate(map(group), tsx());
        ValidationIssue issue = singleError(report, "markers");
        assertTrue(issue.message().contains("illegal luminance \"40\""), issue::format);
        assertTrue(issue.hint().contains("0..31"), issue::format);
    }

    @Test
    void ignitionAnchorOnGraniteIsSingleError() {
        String group = goodGroupWithMarkers("z:+0",
                "<object id=\"1\" name=\"ignition_test\" type=\"script_anchor\" x=\"24\" y=\"24\"><point/></object>");
        ValidationReport report = validate(map(group), tsx());
        ValidationIssue issue = singleError(report, "markers");
        assertTrue(issue.message().contains("\"granite\", which is not flammable"), issue::format);
        assertEquals(1, issue.x(), issue::format);
        assertEquals(1, issue.y(), issue::format);
    }

    @Test
    void duplicateAnchorNamesAreSingleError() {
        String group = goodGroupWithMarkers("z:+0",
                "<object id=\"1\" name=\"anchor_a\" type=\"script_anchor\" x=\"8\" y=\"8\"><point/></object>"
                        + "<object id=\"2\" name=\"anchor_a\" type=\"script_anchor\" x=\"40\" y=\"40\"><point/></object>");
        ValidationReport report = validate(map(group), tsx());
        ValidationIssue issue = singleError(report, "markers");
        assertTrue(issue.message().contains("duplicate script_anchor name \"anchor_a\""), issue::format);
    }

    // --------------------------------------------------------- pass 6: chunk align

    @Test
    void misalignedMapIsWarningOnlyNeverError() {
        ValidationReport report = validate(map(goodGroup("z:+0")), tsx());
        assertEquals(0, report.errors().size(), report::render);
        assertEquals(1, report.warnings().size(), report::render);
        ValidationIssue warning = report.warnings().get(0);
        assertEquals("chunk-align", warning.passId(), warning::format);
        assertEquals(ValidationIssue.Severity.WARNING, warning.severity(), warning::format);
        assertTrue(warning.message().contains("4x4"), warning::format);
        assertTrue(warning.hint().contains("32x32"), warning::format);
    }

    // ----------------------------------------------------------------- scaffolding

    /** All-granite fill (gid 1 = granite/WALL). */
    private static final String GRANITE_FILL = "1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1";
    /** All-empty layer. */
    private static final String EMPTY = "0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0";

    private static ValidationIssue singleError(ValidationReport report, String expectedPassId) {
        List<ValidationIssue> errors = report.errors();
        assertEquals(1, errors.size(), () -> "expected exactly one error:\n" + report.render());
        ValidationIssue issue = errors.get(0);
        assertEquals(expectedPassId, issue.passId(), issue::format);
        assertEquals(MAP_NAME, issue.source(), issue::format);
        return issue;
    }

    private static ValidationReport validate(String mapXml, String tsxXml) {
        List<String> warnings = new ArrayList<>();
        TmxMap map = new TmxReader(warnings::add).read(new StringReader(mapXml));
        TmxTileset tileset = new TsxReader(warnings::add).read(new StringReader(tsxXml));
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

    /** A fully valid z-group: granite terrain, empty floor, empty markers. */
    private static String goodGroup(String name) {
        return group(name, GRANITE_FILL, EMPTY, markers());
    }

    private static String goodGroupWithMarkers(String name, String objectsXml) {
        return group(name, GRANITE_FILL, EMPTY, "<objectgroup name=\"markers\">" + objectsXml + "</objectgroup>");
    }

    private static String group(String name, String terrainCsv, String floorCsv, String markersXml) {
        return "<group name=\"" + name + "\">" + tileLayer("terrain", terrainCsv)
                + tileLayer("floor", floorCsv) + markersXml + "</group>";
    }

    private static String tileLayer(String name, String csv) {
        return "<layer name=\"" + name + "\" width=\"4\" height=\"4\"><data encoding=\"csv\">"
                + csv + "</data></layer>";
    }

    private static String markers() {
        return "<objectgroup name=\"markers\"/>";
    }

    /** Tileset: gid 1 granite/WALL, gid 2 oak/FLOOR, gid 3 fluid tile, gid 4 parameterized material. */
    private static String tsx() {
        return tsx("granite", "water");
    }

    private static String tsx(String materialOfGid4, String fluidOfGid3) {
        return """
                <?xml version="1.0" encoding="UTF-8"?>
                <tileset version="1.10" name="test" tilewidth="16" tileheight="16" tilecount="4" columns="0">
                 <tile id="0"><properties>
                  <property name="material" value="granite"/><property name="form" value="WALL"/>
                 </properties></tile>
                 <tile id="1"><properties>
                  <property name="material" value="oak"/><property name="form" value="FLOOR"/>
                 </properties></tile>
                 <tile id="2"><properties>
                  <property name="fluid" value="%s"/><property name="depth" type="int" value="2"/>
                 </properties></tile>
                 <tile id="3"><properties>
                  <property name="material" value="%s"/><property name="form" value="WALL"/>
                 </properties></tile>
                </tileset>
                """.formatted(fluidOfGid3, materialOfGid4);
    }
}
