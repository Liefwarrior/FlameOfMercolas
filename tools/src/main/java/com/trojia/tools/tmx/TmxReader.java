package com.trojia.tools.tmx;

import java.io.IOException;
import java.io.Reader;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

import javax.xml.stream.XMLStreamConstants;
import javax.xml.stream.XMLStreamException;
import javax.xml.stream.XMLStreamReader;

/**
 * StAX reader turning a {@code .tmx} document into an immutable {@link TmxMap}.
 *
 * <p><strong>Contract.</strong> Single forward pass; every model collection preserves
 * document order (see {@link com.trojia.tools.tmx package docs}). The reader is
 * stateless between calls and safe to reuse; it is not synchronized, so share across
 * threads only with external synchronization of the {@link TmxWarningListener}.</p>
 *
 * <p><strong>Gid decoding.</strong> Tile-layer data must be CSV-encoded and
 * uncompressed. Each raw gid is parsed as an unsigned 32-bit value; the three Tiled
 * flip bits ({@code 0x80000000} horizontal, {@code 0x40000000} vertical,
 * {@code 0x20000000} diagonal) are masked off and reported once per tile layer (and
 * once per tile object) through the warning listener — tile flips carry no meaning for
 * the simulation import.</p>
 *
 * <p><strong>Out of scope (hard fail):</strong> infinite maps, embedded tilesets,
 * non-CSV encodings, nested class properties. <strong>Skipped with a warning:</strong>
 * image layers and unknown elements. <strong>Skipped silently:</strong>
 * {@code <editorsettings>} and object shape markers
 * ({@code point/ellipse/polygon/polyline/text}).</p>
 */
public final class TmxReader {

    /** Horizontal flip flag (bit 31 of a raw gid). */
    static final long FLIP_HORIZONTAL = 0x8000_0000L;
    /** Vertical flip flag (bit 30 of a raw gid). */
    static final long FLIP_VERTICAL = 0x4000_0000L;
    /** Diagonal (anti-transpose) flip flag (bit 29 of a raw gid). */
    static final long FLIP_DIAGONAL = 0x2000_0000L;
    /** All three flip bits. */
    static final long FLIP_MASK = FLIP_HORIZONTAL | FLIP_VERTICAL | FLIP_DIAGONAL;

    private final TmxWarningListener warnings;

    /** Creates a reader that prints warnings to {@code System.err}. */
    public TmxReader() {
        this(message -> System.err.println("[tmx] warning: " + message));
    }

    /**
     * @param warnings sink for non-fatal diagnostics, never {@code null}
     */
    public TmxReader(TmxWarningListener warnings) {
        this.warnings = Objects.requireNonNull(warnings, "warnings");
    }

    /**
     * Parses a {@code .tmx} document from a character stream. The caller owns and
     * closes {@code xml}.
     *
     * @param xml TMX document source, never {@code null}
     * @return the fully decoded immutable map model
     * @throws TmxParseException on malformed XML or any out-of-scope construct
     */
    public TmxMap read(Reader xml) {
        Objects.requireNonNull(xml, "xml");
        try {
            XMLStreamReader r = StaxSupport.newStreamReader(xml);
            try {
                StaxSupport.advanceToRoot(r, "map");
                return readMap(r);
            } finally {
                r.close();
            }
        } catch (XMLStreamException e) {
            throw new TmxParseException("malformed TMX document: " + e.getMessage(), e);
        }
    }

    /**
     * Parses a {@code .tmx} file (UTF-8).
     *
     * @param file path to the document, never {@code null}
     * @return the fully decoded immutable map model
     * @throws UncheckedIOException on I/O failure
     * @throws TmxParseException    on malformed XML or any out-of-scope construct
     */
    public TmxMap read(Path file) {
        Objects.requireNonNull(file, "file");
        try (Reader in = Files.newBufferedReader(file, StandardCharsets.UTF_8)) {
            return read(in);
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read " + file, e);
        }
    }

    // ------------------------------------------------------------------ map

    private TmxMap readMap(XMLStreamReader r) throws XMLStreamException {
        int width = StaxSupport.intAttr(r, "width");
        int height = StaxSupport.intAttr(r, "height");
        int tileWidth = StaxSupport.intAttr(r, "tilewidth");
        int tileHeight = StaxSupport.intAttr(r, "tileheight");
        String orientation = orDefault(StaxSupport.attr(r, "orientation"), "orthogonal");
        String renderOrder = orDefault(StaxSupport.attr(r, "renderorder"), "right-down");
        if ("1".equals(StaxSupport.attr(r, "infinite"))) {
            throw new TmxParseException("infinite maps are not supported" + StaxSupport.location(r));
        }

        List<TmxTilesetRef> tilesets = new ArrayList<>();
        List<TmxLayer> layers = new ArrayList<>();
        TmxProperties[] props = { TmxProperties.empty() };

        StaxSupport.forEachChild(r, child -> {
            switch (child.getLocalName()) {
                case "properties" -> props[0] = StaxSupport.readProperties(child);
                case "tileset" -> tilesets.add(readTilesetRef(child));
                case "layer" -> layers.add(readTileLayer(child));
                case "objectgroup" -> layers.add(readObjectLayer(child));
                case "group" -> layers.add(readGroup(child));
                case "editorsettings" -> StaxSupport.skipElement(child);
                default -> warnAndSkip(child);
            }
        });
        return new TmxMap(width, height, tileWidth, tileHeight, orientation, renderOrder,
                tilesets, layers, props[0]);
    }

    private TmxTilesetRef readTilesetRef(XMLStreamReader r) throws XMLStreamException {
        int firstGid = StaxSupport.intAttr(r, "firstgid");
        String source = StaxSupport.attr(r, "source");
        if (source == null) {
            throw new TmxParseException("embedded tilesets are not supported; export tileset \""
                    + orDefault(StaxSupport.attr(r, "name"), "?")
                    + "\" as an external .tsx" + StaxSupport.location(r));
        }
        StaxSupport.skipElement(r);
        return new TmxTilesetRef(firstGid, source);
    }

    // ---------------------------------------------------------------- layers

    private TmxLayerGroup readGroup(XMLStreamReader r) throws XMLStreamException {
        int id = StaxSupport.intAttrOr(r, "id", 0);
        String name = orDefault(StaxSupport.attr(r, "name"), "");
        List<TmxLayer> layers = new ArrayList<>();
        TmxProperties[] props = { TmxProperties.empty() };

        StaxSupport.forEachChild(r, child -> {
            switch (child.getLocalName()) {
                case "properties" -> props[0] = StaxSupport.readProperties(child);
                case "layer" -> layers.add(readTileLayer(child));
                case "objectgroup" -> layers.add(readObjectLayer(child));
                case "group" -> layers.add(readGroup(child));
                default -> warnAndSkip(child);
            }
        });
        return new TmxLayerGroup(id, name, layers, props[0]);
    }

    private TmxTileLayer readTileLayer(XMLStreamReader r) throws XMLStreamException {
        int id = StaxSupport.intAttrOr(r, "id", 0);
        String name = orDefault(StaxSupport.attr(r, "name"), "");
        int width = StaxSupport.intAttr(r, "width");
        int height = StaxSupport.intAttr(r, "height");
        int[][] gids = new int[1][];
        TmxProperties[] props = { TmxProperties.empty() };

        StaxSupport.forEachChild(r, child -> {
            switch (child.getLocalName()) {
                case "properties" -> props[0] = StaxSupport.readProperties(child);
                case "data" -> gids[0] = readCsvData(child, width, height, name, id);
                default -> warnAndSkip(child);
            }
        });
        if (gids[0] == null) {
            throw new TmxParseException("tile layer \"" + name + "\" (id " + id + ") has no <data> element");
        }
        return new TmxTileLayer(id, name, width, height, gids[0], props[0]);
    }

    private int[] readCsvData(XMLStreamReader r, int width, int height, String layerName, int layerId)
            throws XMLStreamException {
        String encoding = StaxSupport.attr(r, "encoding");
        if (!"csv".equals(encoding)) {
            throw new TmxParseException("tile layer \"" + layerName + "\" (id " + layerId
                    + ") uses unsupported data encoding \"" + encoding
                    + "\"; only csv is supported" + StaxSupport.location(r));
        }
        String compression = StaxSupport.attr(r, "compression");
        if (compression != null && !compression.isEmpty()) {
            throw new TmxParseException("tile layer \"" + layerName + "\" (id " + layerId
                    + ") uses compression \"" + compression
                    + "\"; compressed data is not supported" + StaxSupport.location(r));
        }

        StringBuilder csv = new StringBuilder();
        while (true) {
            int event = r.next();
            if (event == XMLStreamConstants.CHARACTERS || event == XMLStreamConstants.CDATA
                    || event == XMLStreamConstants.SPACE) {
                csv.append(r.getText());
            } else if (event == XMLStreamConstants.START_ELEMENT) {
                throw new TmxParseException("unexpected <" + r.getLocalName() + "> inside <data> of layer \""
                        + layerName + "\" (chunked/infinite data is not supported)" + StaxSupport.location(r));
            } else if (event == XMLStreamConstants.END_ELEMENT) {
                break;
            }
        }

        String[] tokens = csv.toString().split(",");
        int expected = width * height;
        if (tokens.length != expected) {
            throw new TmxParseException("tile layer \"" + layerName + "\" (id " + layerId + ") csv has "
                    + tokens.length + " entries, expected " + expected + " (" + width + "x" + height + ")");
        }

        int[] out = new int[expected];
        int flipped = 0;
        for (int i = 0; i < expected; i++) {
            String token = tokens[i].trim();
            long raw;
            try {
                raw = Long.parseLong(token);
            } catch (NumberFormatException e) {
                throw new TmxParseException("tile layer \"" + layerName + "\" (id " + layerId
                        + ") csv entry " + i + " is not a number: \"" + token + "\"", e);
            }
            if (raw < 0 || raw > 0xFFFF_FFFFL) {
                throw new TmxParseException("tile layer \"" + layerName + "\" (id " + layerId
                        + ") csv entry " + i + " is out of unsigned 32-bit range: " + raw);
            }
            if ((raw & FLIP_MASK) != 0) {
                flipped++;
            }
            out[i] = (int) (raw & ~FLIP_MASK);
        }
        if (flipped > 0) {
            warnings.warn("tile layer \"" + layerName + "\" (id " + layerId + "): masked flip bits on "
                    + flipped + " of " + expected + " gids; tile flips are not supported and were ignored");
        }
        return out;
    }

    // --------------------------------------------------------------- objects

    private TmxObjectLayer readObjectLayer(XMLStreamReader r) throws XMLStreamException {
        int id = StaxSupport.intAttrOr(r, "id", 0);
        String name = orDefault(StaxSupport.attr(r, "name"), "");
        List<TmxObject> objects = new ArrayList<>();
        TmxProperties[] props = { TmxProperties.empty() };

        StaxSupport.forEachChild(r, child -> {
            switch (child.getLocalName()) {
                case "properties" -> props[0] = StaxSupport.readProperties(child);
                case "object" -> objects.add(readObject(child, name));
                default -> warnAndSkip(child);
            }
        });
        return new TmxObjectLayer(id, name, objects, props[0]);
    }

    private TmxObject readObject(XMLStreamReader r, String layerName) throws XMLStreamException {
        int id = StaxSupport.intAttrOr(r, "id", 0);
        String name = orDefault(StaxSupport.attr(r, "name"), "");
        String typeName = StaxSupport.attr(r, "class");
        if (typeName == null) {
            typeName = orDefault(StaxSupport.attr(r, "type"), "");
        }
        double x = StaxSupport.doubleAttrOr(r, "x", 0);
        double y = StaxSupport.doubleAttrOr(r, "y", 0);
        double width = StaxSupport.doubleAttrOr(r, "width", 0);
        double height = StaxSupport.doubleAttrOr(r, "height", 0);

        int gid = 0;
        String rawGidAttr = StaxSupport.attr(r, "gid");
        if (rawGidAttr != null) {
            long raw;
            try {
                raw = Long.parseLong(rawGidAttr);
            } catch (NumberFormatException e) {
                throw new TmxParseException("object " + id + " in layer \"" + layerName
                        + "\" has non-numeric gid \"" + rawGidAttr + "\"" + StaxSupport.location(r), e);
            }
            if (raw < 0 || raw > 0xFFFF_FFFFL) {
                throw new TmxParseException("object " + id + " in layer \"" + layerName
                        + "\" has gid out of unsigned 32-bit range: " + raw);
            }
            if ((raw & FLIP_MASK) != 0) {
                warnings.warn("object " + id + " in layer \"" + layerName
                        + "\": masked flip bits on gid; tile flips are not supported and were ignored");
            }
            gid = (int) (raw & ~FLIP_MASK);
        }

        TmxProperties[] props = { TmxProperties.empty() };
        StaxSupport.forEachChild(r, child -> {
            switch (child.getLocalName()) {
                case "properties" -> props[0] = StaxSupport.readProperties(child);
                // Shape markers are legal but carry no extra data the importer needs.
                case "point", "ellipse", "polygon", "polyline", "text" -> StaxSupport.skipElement(child);
                default -> warnAndSkip(child);
            }
        });
        return new TmxObject(id, name, typeName, x, y, width, height, gid, props[0]);
    }

    // --------------------------------------------------------------- helpers

    private void warnAndSkip(XMLStreamReader r) throws XMLStreamException {
        warnings.warn("skipping unsupported element <" + r.getLocalName() + ">" + StaxSupport.location(r));
        StaxSupport.skipElement(r);
    }

    private static String orDefault(String value, String fallback) {
        return value == null ? fallback : value;
    }
}
