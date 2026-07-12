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

import javax.xml.stream.XMLStreamException;
import javax.xml.stream.XMLStreamReader;

/**
 * StAX reader turning an external tileset ({@code .tsx}) document into an immutable
 * {@link TmxTileset}, including tileset-level and per-tile custom properties (the
 * material binding surface — ARCHITECTURE.md section 3, tools).
 *
 * <p><strong>Contract.</strong> Single forward pass; tiles and properties preserve
 * document order; duplicate tile ids and duplicate property names are hard failures.
 * Stateless between calls and safe to reuse.</p>
 *
 * <p><strong>Skipped silently:</strong> {@code <image>}, {@code <grid>},
 * {@code <tileoffset>}, {@code <transformations>}, {@code <wangsets>},
 * {@code <terraintypes>}, and per-tile {@code <image>}, {@code <animation>},
 * {@code <objectgroup>} — presentation data the importer does not consume.
 * Unknown elements are skipped with a warning.</p>
 */
public final class TsxReader {

    private final TmxWarningListener warnings;

    /** Creates a reader that prints warnings to {@code System.err}. */
    public TsxReader() {
        this(message -> System.err.println("[tsx] warning: " + message));
    }

    /**
     * @param warnings sink for non-fatal diagnostics, never {@code null}
     */
    public TsxReader(TmxWarningListener warnings) {
        this.warnings = Objects.requireNonNull(warnings, "warnings");
    }

    /**
     * Parses a {@code .tsx} document from a character stream. The caller owns and
     * closes {@code xml}.
     *
     * @param xml TSX document source, never {@code null}
     * @return the immutable tileset model
     * @throws TmxParseException on malformed XML or model invariant violations
     */
    public TmxTileset read(Reader xml) {
        Objects.requireNonNull(xml, "xml");
        try {
            XMLStreamReader r = StaxSupport.newStreamReader(xml);
            try {
                StaxSupport.advanceToRoot(r, "tileset");
                return readTileset(r);
            } finally {
                r.close();
            }
        } catch (XMLStreamException e) {
            throw new TmxParseException("malformed TSX document: " + e.getMessage(), e);
        }
    }

    /**
     * Parses a {@code .tsx} file (UTF-8).
     *
     * @param file path to the document, never {@code null}
     * @return the immutable tileset model
     * @throws UncheckedIOException on I/O failure
     * @throws TmxParseException    on malformed XML or model invariant violations
     */
    public TmxTileset read(Path file) {
        Objects.requireNonNull(file, "file");
        try (Reader in = Files.newBufferedReader(file, StandardCharsets.UTF_8)) {
            return read(in);
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read " + file, e);
        }
    }

    private TmxTileset readTileset(XMLStreamReader r) throws XMLStreamException {
        String name = StaxSupport.requireAttr(r, "name");
        int tileWidth = StaxSupport.intAttr(r, "tilewidth");
        int tileHeight = StaxSupport.intAttr(r, "tileheight");
        int tileCount = StaxSupport.intAttrOr(r, "tilecount", 0);
        int columns = StaxSupport.intAttrOr(r, "columns", 0);

        List<TmxTilesetTile> tiles = new ArrayList<>();
        TmxProperties[] props = { TmxProperties.empty() };

        StaxSupport.forEachChild(r, child -> {
            switch (child.getLocalName()) {
                case "properties" -> props[0] = StaxSupport.readProperties(child);
                case "tile" -> tiles.add(readTile(child, name));
                case "image", "grid", "tileoffset", "transformations", "wangsets", "terraintypes" ->
                        StaxSupport.skipElement(child);
                default -> warnAndSkip(child);
            }
        });
        try {
            return new TmxTileset(name, tileWidth, tileHeight, tileCount, columns, props[0], tiles);
        } catch (IllegalArgumentException e) {
            throw new TmxParseException("invalid tileset \"" + name + "\": " + e.getMessage(), e);
        }
    }

    private TmxTilesetTile readTile(XMLStreamReader r, String tilesetName) throws XMLStreamException {
        int localId = StaxSupport.intAttr(r, "id");
        String typeName = StaxSupport.attr(r, "class");
        if (typeName == null) {
            String legacy = StaxSupport.attr(r, "type");
            typeName = legacy == null ? "" : legacy;
        }
        TmxProperties[] props = { TmxProperties.empty() };

        String finalTypeName = typeName;
        StaxSupport.forEachChild(r, child -> {
            switch (child.getLocalName()) {
                case "properties" -> props[0] = StaxSupport.readProperties(child);
                case "image", "animation", "objectgroup" -> StaxSupport.skipElement(child);
                default -> warnAndSkip(child);
            }
        });
        return new TmxTilesetTile(localId, finalTypeName, props[0]);
    }

    private void warnAndSkip(XMLStreamReader r) throws XMLStreamException {
        warnings.warn("skipping unsupported element <" + r.getLocalName() + ">" + StaxSupport.location(r));
        StaxSupport.skipElement(r);
    }
}
