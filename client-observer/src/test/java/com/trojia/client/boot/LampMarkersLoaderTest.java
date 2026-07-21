package com.trojia.client.boot;

import com.trojia.client.render.LampGlowMap.Lamp;
import com.trojia.sim.world.Coords;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Tests for {@link LampMarkersLoader} — the client-side read of a fixture's authored
 * {@code light_source} markers (the importer defers marker baking, so the TROJSAV carries
 * none). Contract under test: only {@code light_source} point objects load; pixel
 * coordinates collapse to tiles ({@code floor(px/16)}); the authored z and the map's
 * {@code minZ} map to world z exactly like {@code TiledWorldImporter}'s placement rule
 * ({@code CHUNK_SIZE + offset}); warmth is classed from the marker name; malformed
 * luminance skips the lamp; a missing file degrades to an empty list.
 */
class LampMarkersLoaderTest {

    /**
     * A minimal two-z-group map in the authoring convention of content/maps/README.md:
     * z:-1 exists (so minZ = -1) but has empty markers; z:+1 carries one lantern, one
     * brazier, one script_anchor (must be ignored), and one light_source with garbage
     * luminance (must be skipped).
     */
    private static final String TMX = """
            <?xml version="1.0" encoding="UTF-8"?>
            <map version="1.10" orientation="orthogonal" renderorder="right-down"
                 width="48" height="32" tilewidth="16" tileheight="16" infinite="0">
             <tileset firstgid="1" source="materials.tsx"/>
             <group id="1" name="z:-1">
              <objectgroup id="2" name="markers"/>
             </group>
             <group id="3" name="z:+1">
              <objectgroup id="4" name="markers">
               <object id="10" name="lamp_dock_gate" type="light_source" x="248" y="136">
                <properties>
                 <property name="luminance" type="int" value="18"/>
                </properties>
                <point/>
               </object>
               <object id="11" name="brazier_plaza" type="light_source" x="8" y="8">
                <properties>
                 <property name="luminance" type="int" value="22"/>
                </properties>
                <point/>
               </object>
               <object id="12" name="some_anchor" type="script_anchor" x="24" y="24">
                <point/>
               </object>
               <object id="13" name="lamp_broken" type="light_source" x="40" y="40">
                <properties>
                 <property name="luminance" type="int" value="notanumber"/>
                </properties>
                <point/>
               </object>
              </objectgroup>
             </group>
            </map>
            """;

    private static Path write(Path dir, String content) throws IOException {
        Path file = dir.resolve("fixture.tmx");
        Files.writeString(file, content, StandardCharsets.UTF_8);
        return file;
    }

    @Test
    void loadsOnlyLightSourcesAtImporterPlacedWorldCoordinates(@TempDir Path dir)
            throws IOException {
        List<Lamp> lamps = LampMarkersLoader.load(write(dir, TMX));
        assertEquals(2, lamps.size(), "one lantern + one brazier (anchor and garbage skip)");

        // lamp_dock_gate: pixel (248,136) -> tile (15,8); authored z:+1 with minZ -1
        // -> world z = CHUNK_SIZE_Z + (1 - (-1)).
        Lamp lantern = lamps.get(0);
        assertEquals(Coords.CHUNK_SIZE_X + 15, lantern.x());
        assertEquals(Coords.CHUNK_SIZE_Y + 8, lantern.y());
        assertEquals(Coords.CHUNK_SIZE_Z + 2, lantern.z());
        assertEquals(18, lantern.luminance());
        assertFalse(lantern.fire(), "lamp_* names class as lantern-warm");

        Lamp brazier = lamps.get(1);
        assertEquals(Coords.CHUNK_SIZE_X, brazier.x());
        assertEquals(Coords.CHUNK_SIZE_Y, brazier.y());
        assertEquals(22, brazier.luminance());
        assertTrue(brazier.fire(), "brazier_* names class as ember-warm fire");
    }

    @Test
    void missingFileDegradesToNoLamps(@TempDir Path dir) {
        assertEquals(List.of(), LampMarkersLoader.load(dir.resolve("nope.tmx")),
                "a checkout without the source map boots with no lamp pools, no throw");
    }

    @Test
    void mapWithNoMarkersLoadsEmpty(@TempDir Path dir) throws IOException {
        String bare = """
                <?xml version="1.0" encoding="UTF-8"?>
                <map version="1.10" width="8" height="8" tilewidth="16" tileheight="16">
                 <group id="1" name="z:+0">
                  <objectgroup id="2" name="markers"/>
                 </group>
                </map>
                """;
        assertEquals(List.of(), LampMarkersLoader.load(write(dir, bare)));
    }

    @Test
    void objectsOutsideTheMarkersLayerAreIgnored(@TempDir Path dir) throws IOException {
        String stray = """
                <?xml version="1.0" encoding="UTF-8"?>
                <map version="1.10" width="8" height="8" tilewidth="16" tileheight="16">
                 <group id="1" name="z:+0">
                  <objectgroup id="2" name="decals">
                   <object id="9" name="lamp_fake" type="light_source" x="8" y="8">
                    <properties>
                     <property name="luminance" type="int" value="12"/>
                    </properties>
                    <point/>
                   </object>
                  </objectgroup>
                  <objectgroup id="3" name="markers"/>
                 </group>
                </map>
                """;
        assertEquals(List.of(), LampMarkersLoader.load(write(dir, stray)),
                "only the contract 'markers' sublayer is scanned");
    }
}
