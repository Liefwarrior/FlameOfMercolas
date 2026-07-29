package com.trojia.client.boot;

import com.trojia.client.render.PlaceSign;
import com.trojia.sim.world.Coords;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Tests for {@link PlaceSignsLoader} — the client-side read of a fixture's authored
 * {@code place_sign} markers (the importer defers marker baking, so the TROJSAV carries
 * none). Contract under test: only {@code place_sign} point objects load; pixel coordinates
 * collapse to tiles; door AND footprint ride the importer's own world offsets; incomplete
 * signs are skipped rather than crashing the frame; ordering is a deterministic ascending
 * scan, not document order; a missing file degrades to an empty list.
 */
class PlaceSignsLoaderTest {

    /**
     * A two-z-group map in the authoring convention of content/maps/README.md: z:-1 exists
     * (so minZ = -1) and carries one legal sign; z:+1 carries a legal sign, a
     * {@code script_anchor} (must be ignored), and a sign missing its footprint (must be
     * skipped). The z:+1 signs are authored OUT of ascending order to prove the sort.
     */
    private static final String TMX = """
            <?xml version="1.0" encoding="UTF-8"?>
            <map version="1.10" orientation="orthogonal" renderorder="right-down"
                 width="48" height="32" tilewidth="16" tileheight="16" infinite="0">
             <tileset firstgid="1" source="materials.tsx"/>
             <group id="1" name="z:-1">
              <objectgroup id="2" name="markers">
               <object id="9" name="sign_cellar" type="place_sign" x="24" y="24">
                <properties>
                 <property name="place" value="The Undercellars"/>
                 <property name="what" value="men and rats"/>
                 <property name="x0" type="int" value="1"/>
                 <property name="y0" type="int" value="1"/>
                 <property name="x1" type="int" value="4"/>
                 <property name="y1" type="int" value="4"/>
                </properties>
                <point/>
               </object>
              </objectgroup>
             </group>
             <group id="3" name="z:+1">
              <objectgroup id="4" name="markers">
               <object id="10" name="sign_k03_gilded_gull" type="place_sign" x="248" y="136">
                <properties>
                 <property name="place" value="The Gilded Gull"/>
                 <property name="what" value="captains' tavern"/>
                 <property name="x0" type="int" value="14"/>
                 <property name="y0" type="int" value="9"/>
                 <property name="x1" type="int" value="20"/>
                 <property name="y1" type="int" value="16"/>
                </properties>
                <point/>
               </object>
               <object id="11" name="sign_k04_bilge" type="place_sign" x="40" y="24">
                <properties>
                 <property name="place" value="The Bilge"/>
                 <property name="what" value="sailor dive on the Tarwalk"/>
                 <property name="x0" type="int" value="2"/>
                 <property name="y0" type="int" value="2"/>
                 <property name="x1" type="int" value="6"/>
                 <property name="y1" type="int" value="8"/>
                </properties>
                <point/>
               </object>
               <object id="12" name="some_anchor" type="script_anchor" x="24" y="24">
                <point/>
               </object>
               <object id="13" name="sign_footless" type="place_sign" x="8" y="8">
                <properties>
                 <property name="place" value="Nowhere"/>
                 <property name="what" value="no footprint authored"/>
                </properties>
                <point/>
               </object>
              </objectgroup>
             </group>
            </map>
            """;

    private static List<PlaceSign> load(@TempDir Path dir) throws IOException {
        Path tmx = dir.resolve("fixture.tmx");
        Files.writeString(tmx, TMX, StandardCharsets.UTF_8);
        return PlaceSignsLoader.load(tmx);
    }

    @Test
    void loadsOnlyCompletePlaceSigns(@TempDir Path dir) throws IOException {
        List<PlaceSign> signs = load(dir);
        assertEquals(3, signs.size(), () -> "script anchors and footless signs must not load: "
                + signs);
        assertTrue(signs.stream().noneMatch(s -> "Nowhere".equals(s.place())));
    }

    @Test
    void mapsDoorAndFootprintThroughTheImportersOwnPlacementRule(@TempDir Path dir)
            throws IOException {
        PlaceSign gull = load(dir).stream()
                .filter(s -> "The Gilded Gull".equals(s.place())).findFirst().orElseThrow();
        // pixel (248,136) -> tile (15,8); authored z:+1 with minZ = -1 -> z offset 2.
        assertEquals(Coords.CHUNK_SIZE_X + 15, gull.doorX());
        assertEquals(Coords.CHUNK_SIZE_Y + 8, gull.doorY());
        assertEquals(Coords.CHUNK_SIZE_Z + 2, gull.z());
        assertEquals(Coords.CHUNK_SIZE_X + 14, gull.x0());
        assertEquals(Coords.CHUNK_SIZE_Y + 9, gull.y0());
        assertEquals(Coords.CHUNK_SIZE_X + 20, gull.x1());
        assertEquals(Coords.CHUNK_SIZE_Y + 16, gull.y1());
        assertEquals("captains' tavern", gull.what());
    }

    @Test
    void theCellarSignLandsOnTheBottomBand(@TempDir Path dir) throws IOException {
        PlaceSign cellar = load(dir).get(0);
        assertEquals("The Undercellars", cellar.place(), "z:-1 sorts first");
        assertEquals(Coords.CHUNK_SIZE_Z, cellar.z());
    }

    @Test
    void orderIsAscendingScanNotDocumentOrder(@TempDir Path dir) throws IOException {
        List<PlaceSign> signs = load(dir);
        assertEquals(List.of("The Undercellars", "The Bilge", "The Gilded Gull"),
                signs.stream().map(PlaceSign::place).toList(),
                "ascending (z, y, x, name) — the Bilge sits at tile (2,1), the Gull at (15,8)");
    }

    @Test
    void loadingIsDeterministic(@TempDir Path dir) throws IOException {
        Path tmx = dir.resolve("fixture.tmx");
        Files.writeString(tmx, TMX, StandardCharsets.UTF_8);
        assertEquals(PlaceSignsLoader.load(tmx), PlaceSignsLoader.load(tmx));
    }

    @Test
    void aMissingFileLeavesTheWardUnlabelledRatherThanFailingBoot(@TempDir Path dir) {
        assertTrue(PlaceSignsLoader.load(dir.resolve("absent.tmx")).isEmpty());
    }

    @Test
    void malformedXmlDegradesToEmptyRatherThanThrowing(@TempDir Path dir) throws IOException {
        Path tmx = dir.resolve("broken.tmx");
        Files.writeString(tmx, "<map><group name=\"z:+0\"><objectgroup name=\"mark",
                StandardCharsets.UTF_8);
        assertTrue(PlaceSignsLoader.load(tmx).isEmpty());
    }

    @Test
    void theCommittedDocksMapCarriesTheWholeSignedRoster() {
        Path tmx = RepoPaths.locate("content", "maps", "src", "docks_surface.tmx");
        List<PlaceSign> signs = PlaceSignsLoader.load(tmx);
        assertEquals(39, signs.size(), () -> "the ward's authored places, from " + tmx);
        assertTrue(signs.stream().anyMatch(s -> "The Weighhouse".equals(s.place())));
        assertTrue(signs.stream().anyMatch(s -> "Mission of the Flame".equals(s.place())));
        assertTrue(signs.stream().anyMatch(s -> "The Ropewalk".equals(s.place())));
        assertTrue(signs.stream().allMatch(s -> !s.place().isBlank() && !s.what().isBlank()));
        // Every sign hangs on or beside its own lot (the marker contract, re-proved in
        // world coordinates after the loader's offsets).
        assertTrue(signs.stream().allMatch(s -> s.distanceTo(s.doorX(), s.doorY()) <= 2),
                "a sign must hang at its own door");
    }
}
