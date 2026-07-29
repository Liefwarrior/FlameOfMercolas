package com.trojia.client.boot;

import com.trojia.client.render.PlaceSign;
import com.trojia.sim.world.Coords;

import javax.xml.stream.XMLInputFactory;
import javax.xml.stream.XMLStreamConstants;
import javax.xml.stream.XMLStreamException;
import javax.xml.stream.XMLStreamReader;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Reads a fixture's {@code place_sign} markers straight from its authored Tiled source
 * ({@code content/maps/src/*.tmx}) into world-tile {@link PlaceSign}s — the building-label
 * pass's client-side path to the ward's names (2026-07-28, Eli: "we also need to label
 * buildings").
 *
 * <p><b>Why the client reads a .tmx here</b> — the same deliberate, presentation-only
 * exception {@link LampMarkersLoader} documents: {@code TiledWorldImporter} parses the
 * {@code markers} object layer but does not bake it, so markers simply do not exist in the
 * TROJSAV the client loads. A place name is a way of LOOKING at the ward, not a fact in it,
 * so it has no business in the save format or in sim-core at all. <b>When marker baking
 * lands in the TROJSAV, delete this class and read them from the save</b> — the same note
 * the lamp loader carries.
 *
 * <p><b>Coordinate mapping</b> mirrors {@code TiledWorldImporter}'s placement rule exactly
 * (and {@link LampMarkersLoader}'s): marker pixel {@code (x, y)} in a {@code z:<sign><n>}
 * group becomes world tile {@code (CHUNK_SIZE_X + floor(x/16), CHUNK_SIZE_Y + floor(y/16),
 * CHUNK_SIZE_Z + (z - minZ))}, with {@code minZ} scanned from every z-group in the file.
 * The authored footprint bounds ride the same x/y offsets, so a sign and its rect can never
 * disagree about which lot they describe.
 *
 * <p><b>Order.</b> The returned list is sorted by (z, y, x, name) — a deterministic
 * ascending scan, never document order, so the overlay's tie-breaks are stable across
 * machines and across a map regen that renumbers object ids.
 *
 * <p>A missing/unreadable file degrades to an empty list (the observer just gets an
 * unlabelled ward — never a boot failure over a presentation nicety); a malformed file that
 * parses partially yields whatever legal signs preceded the fault. A sign missing any of its
 * six required attributes is skipped: that is validator territory
 * ({@code MarkerContractPass}), not a render-time crash.
 */
public final class PlaceSignsLoader {

    /** Authored tile size, px (content/maps/README.md map-level rules: 16x16). */
    private static final int TILE_PX = 16;

    /** Legal z-group name (same pattern as {@code TiledWorldImporter}). */
    private static final Pattern Z_NAME = Pattern.compile("z:([+-])(\\d+)");

    private PlaceSignsLoader() {
    }

    /**
     * Loads every {@code place_sign} marker of {@code tmxFile} as a world-tile place.
     *
     * @param tmxFile the authored source map ({@code content/maps/src/<fixture>.tmx})
     * @return the places, ascending by (z, y, x, name); empty if the file is unreadable
     */
    public static List<PlaceSign> load(Path tmxFile) {
        if (!Files.isRegularFile(tmxFile)) {
            return List.of();
        }
        try (InputStream in = Files.newInputStream(tmxFile)) {
            return parse(in);
        } catch (IOException | XMLStreamException | RuntimeException e) {
            System.out.println("observer: place signs unreadable (" + tmxFile.getFileName()
                    + "): " + e.getMessage() + " -- the ward goes unlabelled");
            return List.of();
        }
    }

    /** One sign still in authored (map-local tile, authored z) coordinates. */
    private record AuthoredSign(String name, String place, String what, int tileX, int tileY,
            int authoredZ, int x0, int y0, int x1, int y1) {

        boolean complete() {
            return place != null && what != null && x0 >= 0 && y0 >= 0 && x1 >= 0 && y1 >= 0;
        }
    }

    private static List<PlaceSign> parse(InputStream in) throws XMLStreamException, IOException {
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

    private static List<PlaceSign> scan(XMLStreamReader reader) throws XMLStreamException {
        List<AuthoredSign> authored = new ArrayList<>();
        int minZ = Integer.MAX_VALUE;
        int currentZ = Integer.MIN_VALUE;   // sentinel: not inside a z-group
        boolean inMarkers = false;
        boolean inSign = false;
        String pendingName = "";
        String pendingPlace = null;
        String pendingWhat = null;
        int pendingTileX = 0;
        int pendingTileY = 0;
        int[] pendingBounds = {-1, -1, -1, -1};   // x0, y0, x1, y1

        while (reader.hasNext()) {
            int event = reader.next();
            if (event == XMLStreamConstants.START_ELEMENT) {
                switch (reader.getLocalName()) {
                    case "group" -> {
                        Matcher m = Z_NAME.matcher(attr(reader, "name", ""));
                        if (m.matches()) {
                            int magnitude = Integer.parseInt(m.group(2));
                            currentZ = "-".equals(m.group(1)) ? -magnitude : magnitude;
                            minZ = Math.min(minZ, currentZ);
                        }
                    }
                    case "objectgroup" -> inMarkers = "markers".equals(attr(reader, "name", ""));
                    case "object" -> {
                        if (inMarkers && currentZ != Integer.MIN_VALUE
                                && "place_sign".equals(attr(reader, "type", ""))) {
                            inSign = true;
                            pendingName = attr(reader, "name", "");
                            pendingPlace = null;
                            pendingWhat = null;
                            pendingTileX = (int) Math.floor(
                                    Double.parseDouble(attr(reader, "x", "0")) / TILE_PX);
                            pendingTileY = (int) Math.floor(
                                    Double.parseDouble(attr(reader, "y", "0")) / TILE_PX);
                            pendingBounds = new int[] {-1, -1, -1, -1};
                        }
                    }
                    case "property" -> {
                        if (inSign) {
                            String key = attr(reader, "name", "");
                            String value = attr(reader, "value", "");
                            switch (key) {
                                case "place" -> pendingPlace = value;
                                case "what" -> pendingWhat = value;
                                case "x0" -> pendingBounds[0] = parseTile(value);
                                case "y0" -> pendingBounds[1] = parseTile(value);
                                case "x1" -> pendingBounds[2] = parseTile(value);
                                case "y1" -> pendingBounds[3] = parseTile(value);
                                default -> {
                                }
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
                        if (inSign) {
                            AuthoredSign sign = new AuthoredSign(pendingName, pendingPlace,
                                    pendingWhat, pendingTileX, pendingTileY, currentZ,
                                    pendingBounds[0], pendingBounds[1],
                                    pendingBounds[2], pendingBounds[3]);
                            if (sign.complete()) {
                                authored.add(sign);
                            }
                        }
                        inSign = false;
                    }
                    default -> {
                    }
                }
            }
        }
        return toWorld(authored, minZ);
    }

    /** Applies the importer's placement rule now that {@code minZ} is known, then sorts. */
    private static List<PlaceSign> toWorld(List<AuthoredSign> authored, int minZ) {
        if (authored.isEmpty()) {
            return List.of();
        }
        authored.sort(Comparator.comparingInt(AuthoredSign::authoredZ)
                .thenComparingInt(AuthoredSign::tileY)
                .thenComparingInt(AuthoredSign::tileX)
                .thenComparing(AuthoredSign::name));
        List<PlaceSign> signs = new ArrayList<>(authored.size());
        for (AuthoredSign sign : authored) {
            signs.add(new PlaceSign(sign.place(), sign.what(),
                    Coords.CHUNK_SIZE_X + sign.tileX(),
                    Coords.CHUNK_SIZE_Y + sign.tileY(),
                    Coords.CHUNK_SIZE_Z + (sign.authoredZ() - minZ),
                    Coords.CHUNK_SIZE_X + sign.x0(), Coords.CHUNK_SIZE_Y + sign.y0(),
                    Coords.CHUNK_SIZE_X + sign.x1(), Coords.CHUNK_SIZE_Y + sign.y1()));
        }
        return List.copyOf(signs);
    }

    private static int parseTile(String raw) {
        try {
            int value = Integer.parseInt(raw);
            return value < 0 ? -1 : value;   // negatives are validator territory; drop the sign
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private static String attr(XMLStreamReader reader, String name, String fallback) {
        String value = reader.getAttributeValue(null, name);
        return value == null ? fallback : value;
    }
}
