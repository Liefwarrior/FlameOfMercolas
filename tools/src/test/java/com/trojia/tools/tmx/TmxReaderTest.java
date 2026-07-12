package com.trojia.tools.tmx;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.StringReader;
import java.util.ArrayList;
import java.util.List;

import org.junit.jupiter.api.Test;

/**
 * TmxReader contract tests over small inline documents: csv decoding, flip-bit
 * masking with warnings, layer-group nesting, object layers, tileset references,
 * property blocks, and out-of-scope hard failures.
 */
class TmxReaderTest {

    private final List<String> warnings = new ArrayList<>();
    private final TmxReader reader = new TmxReader(warnings::add);

    private TmxMap parse(String xml) {
        return reader.read(new StringReader(xml));
    }

    private static String map(String body) {
        return """
                <?xml version="1.0" encoding="UTF-8"?>
                <map version="1.10" orientation="orthogonal" renderorder="right-down"
                     width="3" height="2" tilewidth="16" tileheight="16">
                """ + body + "\n</map>";
    }

    // ------------------------------------------------------------ csv decode

    @Test
    void csvDataDecodesRowMajor() {
        TmxMap m = parse(map("""
                <layer id="1" name="floor" width="3" height="2">
                 <data encoding="csv">
                1,2,3,
                4,5,6
                 </data>
                </layer>
                """));

        assertEquals(3, m.width());
        assertEquals(2, m.height());
        assertEquals(16, m.tileWidth());
        assertEquals("orthogonal", m.orientation());
        assertEquals("right-down", m.renderOrder());

        TmxTileLayer layer = assertInstanceOf(TmxTileLayer.class, m.layers().get(0));
        assertEquals(1, layer.id());
        assertEquals("floor", layer.name());
        assertArrayEquals(new int[] { 1, 2, 3, 4, 5, 6 }, layer.gids());
        assertEquals(1, layer.gidAt(0, 0));
        assertEquals(3, layer.gidAt(2, 0));
        assertEquals(4, layer.gidAt(0, 1));
        assertEquals(6, layer.gidAt(2, 1));
        assertTrue(warnings.isEmpty(), "clean document must produce no warnings");
    }

    @Test
    void gidsAccessorReturnsDefensiveCopy() {
        TmxMap m = parse(map("""
                <layer id="1" name="floor" width="3" height="2">
                 <data encoding="csv">1,2,3,4,5,6</data>
                </layer>
                """));
        TmxTileLayer layer = (TmxTileLayer) m.layers().get(0);
        layer.gids()[0] = 999;
        assertEquals(1, layer.gidAt(0, 0), "mutating the returned array must not affect the layer");
    }

    @Test
    void csvEntryCountMismatchFails() {
        TmxParseException e = assertThrows(TmxParseException.class, () -> parse(map("""
                <layer id="1" name="floor" width="3" height="2">
                 <data encoding="csv">1,2,3</data>
                </layer>
                """)));
        assertTrue(e.getMessage().contains("expected 6"), e.getMessage());
    }

    @Test
    void nonCsvEncodingFails() {
        TmxParseException e = assertThrows(TmxParseException.class, () -> parse(map("""
                <layer id="1" name="floor" width="3" height="2">
                 <data encoding="base64">AAAA</data>
                </layer>
                """)));
        assertTrue(e.getMessage().contains("base64"), e.getMessage());
    }

    // --------------------------------------------------- flip bits + warning

    @Test
    void flipBitsAreMaskedAndWarned() {
        // 0x80000001, 0x40000002, 0x20000003, 0xE0000004 — each flip bit alone, then all three.
        TmxMap m = parse(map("""
                <layer id="7" name="walls" width="3" height="2">
                 <data encoding="csv">
                2147483649,1073741826,536870915,
                3758096388,5,0
                 </data>
                </layer>
                """));

        TmxTileLayer layer = (TmxTileLayer) m.layers().get(0);
        assertArrayEquals(new int[] { 1, 2, 3, 4, 5, 0 }, layer.gids());

        assertEquals(1, warnings.size(), "exactly one aggregated warning per affected layer");
        String w = warnings.get(0);
        assertTrue(w.contains("walls"), w);
        assertTrue(w.contains("4 of 6"), w);
        assertTrue(w.contains("flip"), w);
    }

    @Test
    void unflippedLayerEmitsNoFlipWarning() {
        parse(map("""
                <layer id="1" name="floor" width="3" height="2">
                 <data encoding="csv">0,0,0,0,0,0</data>
                </layer>
                """));
        assertTrue(warnings.isEmpty());
    }

    // ------------------------------------------------------------- grouping

    @Test
    void layerGroupsNestAndPreserveDocumentOrder() {
        TmxMap m = parse(map("""
                <layer id="1" name="base" width="3" height="2">
                 <data encoding="csv">1,1,1,1,1,1</data>
                </layer>
                <group id="10" name="outer">
                 <properties>
                  <property name="zLevel" type="int" value="1"/>
                 </properties>
                 <layer id="2" name="fill" width="3" height="2">
                  <data encoding="csv">2,2,2,2,2,2</data>
                 </layer>
                 <group id="11" name="inner">
                  <objectgroup id="3" name="annotations">
                   <object id="70" name="spawn" x="8" y="16"/>
                  </objectgroup>
                 </group>
                </group>
                """));

        assertEquals(2, m.layers().size());
        assertInstanceOf(TmxTileLayer.class, m.layers().get(0));

        TmxLayerGroup outer = assertInstanceOf(TmxLayerGroup.class, m.layers().get(1));
        assertEquals(10, outer.id());
        assertEquals("outer", outer.name());
        assertEquals(1, outer.properties().find("zLevel").orElseThrow().asInt());
        assertEquals(2, outer.layers().size());

        TmxTileLayer fill = assertInstanceOf(TmxTileLayer.class, outer.layers().get(0));
        assertEquals("fill", fill.name());

        TmxLayerGroup inner = assertInstanceOf(TmxLayerGroup.class, outer.layers().get(1));
        assertEquals("inner", inner.name());
        TmxObjectLayer annotations = assertInstanceOf(TmxObjectLayer.class, inner.layers().get(0));
        assertEquals("annotations", annotations.name());
        assertEquals(70, annotations.objects().get(0).id());
        assertTrue(warnings.isEmpty());
    }

    // --------------------------------------------------------- object layers

    @Test
    void objectLayerParsesObjectsInDocumentOrder() {
        TmxMap m = parse(map("""
                <objectgroup id="4" name="sites">
                 <object id="1" name="tavern" class="site" x="0" y="0" width="64" height="32">
                  <properties>
                   <property name="siteKind" value="TAVERN"/>
                   <property name="capacity" type="int" value="12"/>
                  </properties>
                 </object>
                 <object id="2" name="well" type="marker" x="5.5" y="7.25">
                  <point/>
                 </object>
                </objectgroup>
                """));

        TmxObjectLayer layer = assertInstanceOf(TmxObjectLayer.class, m.layers().get(0));
        assertEquals(4, layer.id());
        assertEquals("sites", layer.name());
        assertEquals(2, layer.objects().size());

        TmxObject tavern = layer.objects().get(0);
        assertEquals(1, tavern.id());
        assertEquals("tavern", tavern.name());
        assertEquals("site", tavern.typeName());
        assertEquals(0.0, tavern.x());
        assertEquals(64.0, tavern.width());
        assertEquals(32.0, tavern.height());
        assertEquals("TAVERN", tavern.properties().find("siteKind").orElseThrow().value());
        assertEquals(12, tavern.properties().find("capacity").orElseThrow().asInt());

        TmxObject well = layer.objects().get(1);
        assertEquals("marker", well.typeName(), "legacy type attribute must map to typeName");
        assertEquals(5.5, well.x());
        assertEquals(7.25, well.y());
        assertEquals(0.0, well.width());
        assertTrue(well.properties().isEmpty());
        assertTrue(warnings.isEmpty(), "point shape marker must be skipped silently");
    }

    @Test
    void tileObjectGidFlipBitsAreMaskedAndWarned() {
        TmxMap m = parse(map("""
                <objectgroup id="4" name="props">
                 <object id="9" name="barrel" gid="2147483653" x="16" y="32"/>
                </objectgroup>
                """));
        TmxObject barrel = ((TmxObjectLayer) m.layers().get(0)).objects().get(0);
        assertEquals(5, barrel.gid(), "0x80000005 must decode to gid 5");
        assertEquals(1, warnings.size());
        assertTrue(warnings.get(0).contains("object 9"), warnings.get(0));
    }

    // ------------------------------------------------- tilesets + properties

    @Test
    void externalTilesetRefsPreserveDocumentOrder() {
        TmxMap m = parse(map("""
                <tileset firstgid="1" source="materials.tsx"/>
                <tileset firstgid="65" source="annotations.tsx"/>
                <layer id="1" name="floor" width="3" height="2">
                 <data encoding="csv">0,0,0,0,0,0</data>
                </layer>
                """));
        assertEquals(2, m.tilesets().size());
        assertEquals(new TmxTilesetRef(1, "materials.tsx"), m.tilesets().get(0));
        assertEquals(new TmxTilesetRef(65, "annotations.tsx"), m.tilesets().get(1));
    }

    @Test
    void embeddedTilesetFails() {
        TmxParseException e = assertThrows(TmxParseException.class, () -> parse(map("""
                <tileset firstgid="1" name="embedded" tilewidth="16" tileheight="16">
                 <image source="x.png" width="16" height="16"/>
                </tileset>
                """)));
        assertTrue(e.getMessage().contains("embedded"), e.getMessage());
    }

    @Test
    void mapPropertiesPreserveDocumentOrderAndTypes() {
        TmxMap m = parse(map("""
                <properties>
                 <property name="worldName" value="Mercolas"/>
                 <property name="zLevels" type="int" value="4"/>
                 <property name="indoor" type="bool" value="true"/>
                </properties>
                <layer id="1" name="floor" width="3" height="2">
                 <data encoding="csv">0,0,0,0,0,0</data>
                </layer>
                """));

        List<TmxProperty> props = m.properties().asList();
        assertEquals(List.of("worldName", "zLevels", "indoor"),
                props.stream().map(TmxProperty::name).toList());
        assertEquals("Mercolas", m.properties().find("worldName").orElseThrow().value());
        assertEquals(4, m.properties().find("zLevels").orElseThrow().asInt());
        assertTrue(m.properties().find("indoor").orElseThrow().asBool());
    }

    @Test
    void duplicatePropertyNamesFail() {
        assertThrows(TmxParseException.class, () -> parse(map("""
                <properties>
                 <property name="x" value="1"/>
                 <property name="x" value="2"/>
                </properties>
                <layer id="1" name="floor" width="3" height="2">
                 <data encoding="csv">0,0,0,0,0,0</data>
                </layer>
                """)));
    }

    // ------------------------------------------------------------ hard fails

    @Test
    void infiniteMapFails() {
        String xml = """
                <?xml version="1.0" encoding="UTF-8"?>
                <map version="1.10" orientation="orthogonal" width="3" height="2"
                     tilewidth="16" tileheight="16" infinite="1">
                </map>
                """;
        TmxParseException e = assertThrows(TmxParseException.class, () -> parse(xml));
        assertTrue(e.getMessage().contains("infinite"), e.getMessage());
    }

    @Test
    void wrongRootElementFails() {
        assertThrows(TmxParseException.class, () -> parse("<tileset name=\"x\"/>"));
    }

    @Test
    void unknownElementIsSkippedWithWarning() {
        TmxMap m = parse(map("""
                <imagelayer id="9" name="backdrop"/>
                <layer id="1" name="floor" width="3" height="2">
                 <data encoding="csv">0,0,0,0,0,0</data>
                </layer>
                """));
        assertEquals(1, m.layers().size(), "imagelayer must not appear in the model");
        assertEquals(1, warnings.size());
        assertTrue(warnings.get(0).contains("imagelayer"), warnings.get(0));
    }
}
