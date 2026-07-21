package com.trojia.client.boot;

import com.trojia.client.render.LampGlowMap;
import com.trojia.sim.world.Coords;

import javax.xml.stream.XMLInputFactory;
import javax.xml.stream.XMLStreamConstants;
import javax.xml.stream.XMLStreamException;
import javax.xml.stream.XMLStreamReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Reads a fixture's {@code light_source} markers straight from its authored Tiled source
 * ({@code content/maps/src/*.tmx}) into world-tile {@link LampGlowMap.Lamp}s — the day/night
 * pass's client-side path to the static lamps/braziers.
 *
 * <p><b>Why the client reads a .tmx here</b> (a deliberate, presentation-only exception to
 * the "client only reads baked TROJSAV" contract of {@code content/maps/README.md}):
 * {@code TiledWorldImporter} parses the {@code markers} object layer but does not bake it —
 * "marker baking is deferred" — so light sources simply do not exist in the TROJSAV the
 * client loads. Rather than touching sim-core's save format or the importer, this loader
 * takes the cheapest read-only path: a JDK-StAX scan of the same source map the baked world
 * came from, extracting only {@code type="light_source"} point objects. Nothing here feeds
 * the sim — lamp positions light pixels, not tiles. <b>When marker baking lands in the
 * TROJSAV, delete this class and read them from the save.</b>
 *
 * <p><b>Coordinate mapping</b> (mirrors {@code TiledWorldImporter}'s placement rule): marker
 * pixel {@code (x, y)} in a {@code z:<sign><n>} group becomes world tile
 * {@code (CHUNK_SIZE_X + floor(x/16), CHUNK_SIZE_Y + floor(y/16),
 * CHUNK_SIZE_Z + (z - minZ))}, with {@code minZ} scanned from every z-group in the same
 * file — so a lamp lands on exactly the world cell its authored tile was baked to.
 *
 * <p><b>Warmth classing</b> is by marker name: names containing an ember token
 * ({@code brazier}, {@code cauldron}, {@code oven}, {@code torch}, {@code candle},
 * {@code hearth}, {@code fire}) load as fire-warm; everything else (the {@code lamp_*}
 * majority) as lantern-warm.
 *
 * <p>A missing/unreadable file degrades to an empty list (the observer just gets no lamp
 * pools — never a boot failure over a presentation nicety); a malformed file that *does*
 * parse partially yields whatever legal markers preceded the fault.
 */
public final class LampMarkersLoader {

    /** Authored tile size, px (content/maps/README.md map-level rules: 16x16). */
    private static final int TILE_PX = 16;

    /** Legal z-group name (same pattern as {@code TiledWorldImporter}). */
    private static final Pattern Z_NAME = Pattern.compile("z:([+-])(\\d+)");

    /** Marker-name tokens that class a source as ember-warm fire rather than lantern. */
    private static final List<String> FIRE_TOKENS =
            List.of("brazier", "cauldron", "oven", "torch", "candle", "hearth", "fire");

    private LampMarkersLoader() {
    }

    /**
     * Loads every {@code light_source} marker of {@code tmxFile} as world-tile lamps.
     *
     * @param tmxFile the authored source map ({@code content/maps/src/<fixture>.tmx})
     * @return the lamps, in document order; empty if the file is missing or unreadable
     */
    public static List<LampGlowMap.Lamp> load(Path tmxFile) {
        if (!Files.isRegularFile(tmxFile)) {
            return List.of();
        }
        try (InputStream in = Files.newInputStream(tmxFile)) {
            return parse(in);
        } catch (IOException | XMLStreamException | RuntimeException e) {
            System.out.println("observer: lamp markers unreadable (" + tmxFile.getFileName()
                    + "): " + e.getMessage() + " -- night scenes get no lamp pools");
            return List.of();
        }
    }

    /** One marker still in authored (map-local tile, authored z) coordinates. */
    private record AuthoredLamp(int tileX, int tileY, int authoredZ, int luminance,
            boolean fire) {
    }

    private static List<LampGlowMap.Lamp> parse(InputStream in)
            throws XMLStreamException, IOException {
        XMLInputFactory factory = XMLInputFactory.newFactory();
        // Defensive hardening: the map is repo-local, but never resolve external entities.
        factory.setProperty(XMLInputFactory.IS_SUPPORTING_EXTERNAL_ENTITIES, false);
        factory.setProperty(XMLInputFactory.SUPPORT_DTD, false);
        XMLStreamReader reader = factory.createXMLStreamReader(in);
        try {
            return scan(reader);
        } finally {
            reader.close();
        }
    }

    private static List<LampGlowMap.Lamp> scan(XMLStreamReader reader)
            throws XMLStreamException {
        List<AuthoredLamp> authored = new ArrayList<>();
        int minZ = Integer.MAX_VALUE;
        int currentZ = Integer.MIN_VALUE;   // sentinel: not inside a z-group
        boolean inMarkers = false;
        // Pending light_source object, filled while walking its children:
        boolean inLight = false;
        int pendingTileX = 0;
        int pendingTileY = 0;
        boolean pendingFire = false;
        int pendingLuminance = -1;

        while (reader.hasNext()) {
            int event = reader.next();
            if (event == XMLStreamConstants.START_ELEMENT) {
                String name = reader.getLocalName();
                switch (name) {
                    case "group" -> {
                        Matcher m = Z_NAME.matcher(attr(reader, "name", ""));
                        if (m.matches()) {
                            int magnitude = Integer.parseInt(m.group(2));
                            currentZ = "-".equals(m.group(1)) ? -magnitude : magnitude;
                            minZ = Math.min(minZ, currentZ);
                        }
                    }
                    case "objectgroup" ->
                            inMarkers = "markers".equals(attr(reader, "name", ""));
                    case "object" -> {
                        if (inMarkers && currentZ != Integer.MIN_VALUE
                                && "light_source".equals(attr(reader, "type", ""))) {
                            inLight = true;
                            pendingTileX = (int) Math.floor(
                                    Double.parseDouble(attr(reader, "x", "0")) / TILE_PX);
                            pendingTileY = (int) Math.floor(
                                    Double.parseDouble(attr(reader, "y", "0")) / TILE_PX);
                            pendingFire = isFireName(attr(reader, "name", ""));
                            pendingLuminance = -1;
                        }
                    }
                    case "property" -> {
                        if (inLight && "luminance".equals(attr(reader, "name", ""))) {
                            try {
                                pendingLuminance = Integer.parseInt(attr(reader, "value", ""));
                            } catch (NumberFormatException ignored) {
                                pendingLuminance = -1; // validator territory; skip the lamp
                            }
                        }
                    }
                    default -> {
                    }
                }
            } else if (event == XMLStreamConstants.END_ELEMENT) {
                switch (reader.getLocalName()) {
                    case "group" -> currentZ = Integer.MIN_VALUE;
                    case "objectgroup" -> inMarkers = false;
                    case "object" -> {
                        if (inLight && pendingLuminance >= 0 && pendingLuminance <= 31) {
                            authored.add(new AuthoredLamp(pendingTileX, pendingTileY,
                                    currentZ, pendingLuminance, pendingFire));
                        }
                        inLight = false;
                    }
                    default -> {
                    }
                }
            }
        }
        return toWorld(authored, minZ);
    }

    /** Applies the importer's placement rule now that {@code minZ} is known. */
    private static List<LampGlowMap.Lamp> toWorld(List<AuthoredLamp> authored, int minZ) {
        if (authored.isEmpty()) {
            return List.of();
        }
        List<LampGlowMap.Lamp> lamps = new ArrayList<>(authored.size());
        for (AuthoredLamp lamp : authored) {
            lamps.add(new LampGlowMap.Lamp(
                    Coords.CHUNK_SIZE_X + lamp.tileX(),
                    Coords.CHUNK_SIZE_Y + lamp.tileY(),
                    Coords.CHUNK_SIZE_Z + (lamp.authoredZ() - minZ),
                    lamp.luminance(), lamp.fire()));
        }
        return List.copyOf(lamps);
    }

    private static boolean isFireName(String markerName) {
        String lower = markerName.toLowerCase(Locale.ROOT);
        for (String token : FIRE_TOKENS) {
            if (lower.contains(token)) {
                return true;
            }
        }
        return false;
    }

    private static String attr(XMLStreamReader reader, String name, String fallback) {
        String value = reader.getAttributeValue(null, name);
        return value == null ? fallback : value;
    }
}
