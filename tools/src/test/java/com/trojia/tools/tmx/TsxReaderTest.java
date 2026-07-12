package com.trojia.tools.tmx;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.StringReader;
import java.util.ArrayList;
import java.util.List;

import org.junit.jupiter.api.Test;

/**
 * TsxReader contract tests over small inline documents: tileset attributes,
 * tileset-level and per-tile custom properties, document ordering, and hard failures.
 */
class TsxReaderTest {

    private final List<String> warnings = new ArrayList<>();
    private final TsxReader reader = new TsxReader(warnings::add);

    private TmxTileset parse(String xml) {
        return reader.read(new StringReader(xml));
    }

    @Test
    void tilesetAttributesAndTilesetLevelProperties() {
        TmxTileset ts = parse("""
                <?xml version="1.0" encoding="UTF-8"?>
                <tileset version="1.10" name="materials" tilewidth="16" tileheight="16"
                         tilecount="4" columns="2">
                 <properties>
                  <property name="palette" value="v0"/>
                 </properties>
                 <image source="materials.png" width="32" height="32"/>
                </tileset>
                """);

        assertEquals("materials", ts.name());
        assertEquals(16, ts.tileWidth());
        assertEquals(16, ts.tileHeight());
        assertEquals(4, ts.tileCount());
        assertEquals(2, ts.columns());
        assertEquals("v0", ts.properties().find("palette").orElseThrow().value());
        assertTrue(ts.tiles().isEmpty());
        assertTrue(warnings.isEmpty(), "known presentation elements must be skipped silently");
    }

    @Test
    void perTileCustomPropertiesWithTypesAndLookup() {
        TmxTileset ts = parse("""
                <?xml version="1.0" encoding="UTF-8"?>
                <tileset name="materials" tilewidth="16" tileheight="16" tilecount="4" columns="2">
                 <tile id="0" class="granite">
                  <properties>
                   <property name="material" value="granite"/>
                   <property name="solid" type="bool" value="true"/>
                  </properties>
                 </tile>
                 <tile id="2" type="oak">
                  <properties>
                   <property name="material" value="oak"/>
                   <property name="fuelTicks" type="int" value="900"/>
                   <property name="conductivityQ8" type="float" value="12.5"/>
                  </properties>
                 </tile>
                </tileset>
                """);

        assertEquals(2, ts.tiles().size());
        assertEquals(0, ts.tiles().get(0).localId(), "tiles must preserve document order");
        assertEquals(2, ts.tiles().get(1).localId());

        TmxTilesetTile granite = ts.tile(0).orElseThrow();
        assertEquals("granite", granite.typeName());
        assertEquals("granite", granite.properties().find("material").orElseThrow().value());
        assertTrue(granite.properties().find("solid").orElseThrow().asBool());

        TmxTilesetTile oak = ts.tile(2).orElseThrow();
        assertEquals("oak", oak.typeName(), "legacy type attribute must map to typeName");
        assertEquals(900, oak.properties().find("fuelTicks").orElseThrow().asInt());
        assertEquals(12.5, oak.properties().find("conductivityQ8").orElseThrow().asDouble());
        assertEquals(List.of("material", "fuelTicks", "conductivityQ8"),
                oak.properties().asList().stream().map(TmxProperty::name).toList());

        assertTrue(ts.tile(1).isEmpty(), "tiles without metadata are absent from the model");
        assertTrue(warnings.isEmpty());
    }

    @Test
    void multiLineStringPropertyUsesElementText() {
        TmxTileset ts = parse("""
                <?xml version="1.0" encoding="UTF-8"?>
                <tileset name="t" tilewidth="16" tileheight="16">
                 <tile id="0">
                  <properties>
                   <property name="note">line one
                line two</property>
                  </properties>
                 </tile>
                </tileset>
                """);
        String note = ts.tile(0).orElseThrow().properties().find("note").orElseThrow().value();
        assertTrue(note.contains("line one"), note);
        assertTrue(note.contains("line two"), note);
    }

    @Test
    void duplicateTileIdsFail() {
        TmxParseException e = assertThrows(TmxParseException.class, () -> parse("""
                <?xml version="1.0" encoding="UTF-8"?>
                <tileset name="t" tilewidth="16" tileheight="16">
                 <tile id="1"/>
                 <tile id="1"/>
                </tileset>
                """));
        assertTrue(e.getMessage().contains("duplicate"), e.getMessage());
    }

    @Test
    void duplicatePropertyNamesOnTileFail() {
        assertThrows(TmxParseException.class, () -> parse("""
                <?xml version="1.0" encoding="UTF-8"?>
                <tileset name="t" tilewidth="16" tileheight="16">
                 <tile id="0">
                  <properties>
                   <property name="material" value="oak"/>
                   <property name="material" value="granite"/>
                  </properties>
                 </tile>
                </tileset>
                """));
    }

    @Test
    void wrongRootElementFails() {
        assertThrows(TmxParseException.class, () -> parse(
                "<map width=\"1\" height=\"1\" tilewidth=\"16\" tileheight=\"16\"/>"));
    }

    @Test
    void unknownElementIsSkippedWithWarning() {
        TmxTileset ts = parse("""
                <?xml version="1.0" encoding="UTF-8"?>
                <tileset name="t" tilewidth="16" tileheight="16">
                 <mystery/>
                </tileset>
                """);
        assertEquals("t", ts.name());
        assertEquals(1, warnings.size());
        assertTrue(warnings.get(0).contains("mystery"), warnings.get(0));
    }
}
