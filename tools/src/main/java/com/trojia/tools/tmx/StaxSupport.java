package com.trojia.tools.tmx;

import java.io.Reader;
import java.util.ArrayList;
import java.util.List;

import javax.xml.stream.Location;
import javax.xml.stream.XMLInputFactory;
import javax.xml.stream.XMLStreamConstants;
import javax.xml.stream.XMLStreamException;
import javax.xml.stream.XMLStreamReader;

/**
 * Package-private StAX plumbing shared by {@link TmxReader} and {@link TsxReader}:
 * hardened factory setup, cursor helpers, attribute parsing, and the common
 * {@code <properties>} block reader.
 *
 * <p>All helpers operate on the cursor invariant: a child parser is entered with the
 * reader positioned on the child's {@code START_ELEMENT} and must consume everything
 * up to and including the matching {@code END_ELEMENT}.</p>
 */
final class StaxSupport {

    private StaxSupport() {
    }

    /** Consumes one child element (cursor on its START_ELEMENT; must eat the END_ELEMENT). */
    @FunctionalInterface
    interface ChildParser {
        void parse(XMLStreamReader r) throws XMLStreamException;
    }

    /**
     * Creates a stream reader with DTD support and external entity resolution disabled.
     */
    static XMLStreamReader newStreamReader(Reader xml) throws XMLStreamException {
        XMLInputFactory factory = XMLInputFactory.newFactory();
        factory.setProperty(XMLInputFactory.SUPPORT_DTD, Boolean.FALSE);
        factory.setProperty(XMLInputFactory.IS_SUPPORTING_EXTERNAL_ENTITIES, Boolean.FALSE);
        factory.setProperty(XMLInputFactory.IS_COALESCING, Boolean.TRUE);
        return factory.createXMLStreamReader(xml);
    }

    /**
     * Advances to the document's root element and verifies its local name.
     *
     * @throws TmxParseException if the root element is not {@code expected}
     */
    static void advanceToRoot(XMLStreamReader r, String expected) throws XMLStreamException {
        while (r.hasNext()) {
            if (r.next() == XMLStreamConstants.START_ELEMENT) {
                if (!expected.equals(r.getLocalName())) {
                    throw new TmxParseException("expected root element <" + expected + "> but found <"
                            + r.getLocalName() + ">" + location(r));
                }
                return;
            }
        }
        throw new TmxParseException("document contains no root element");
    }

    /**
     * Iterates the direct children of the current element (cursor on its
     * START_ELEMENT), invoking {@code child} for each child START_ELEMENT, and returns
     * once the element's own END_ELEMENT is consumed. Text, comments and PIs between
     * children are ignored.
     */
    static void forEachChild(XMLStreamReader r, ChildParser child) throws XMLStreamException {
        while (r.hasNext()) {
            int event = r.next();
            if (event == XMLStreamConstants.START_ELEMENT) {
                child.parse(r);
            } else if (event == XMLStreamConstants.END_ELEMENT) {
                return;
            }
        }
        throw new TmxParseException("unexpected end of document");
    }

    /** Skips the current element and its whole subtree (cursor on its START_ELEMENT). */
    static void skipElement(XMLStreamReader r) throws XMLStreamException {
        int depth = 1;
        while (depth > 0) {
            int event = r.next();
            if (event == XMLStreamConstants.START_ELEMENT) {
                depth++;
            } else if (event == XMLStreamConstants.END_ELEMENT) {
                depth--;
            }
        }
    }

    /** @return the attribute value, or {@code null} if absent (any/no namespace) */
    static String attr(XMLStreamReader r, String name) {
        return r.getAttributeValue(null, name);
    }

    /** @return the attribute value; throws {@link TmxParseException} if absent */
    static String requireAttr(XMLStreamReader r, String name) {
        String value = attr(r, name);
        if (value == null) {
            throw new TmxParseException("<" + r.getLocalName() + "> is missing required attribute \""
                    + name + "\"" + location(r));
        }
        return value;
    }

    /** @return the attribute parsed as int; throws {@link TmxParseException} if absent or malformed */
    static int intAttr(XMLStreamReader r, String name) {
        return parseInt(r, name, requireAttr(r, name));
    }

    /** @return the attribute parsed as int, or {@code fallback} if absent */
    static int intAttrOr(XMLStreamReader r, String name, int fallback) {
        String value = attr(r, name);
        return value == null ? fallback : parseInt(r, name, value);
    }

    /** @return the attribute parsed as double, or {@code fallback} if absent */
    static double doubleAttrOr(XMLStreamReader r, String name, double fallback) {
        String value = attr(r, name);
        if (value == null) {
            return fallback;
        }
        try {
            return Double.parseDouble(value);
        } catch (NumberFormatException e) {
            throw new TmxParseException("attribute \"" + name + "\" of <" + r.getLocalName()
                    + "> is not a number: \"" + value + "\"" + location(r), e);
        }
    }

    private static int parseInt(XMLStreamReader r, String name, String value) {
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException e) {
            throw new TmxParseException("attribute \"" + name + "\" of <" + r.getLocalName()
                    + "> is not an integer: \"" + value + "\"" + location(r), e);
        }
    }

    /**
     * Reads a {@code <properties>} block (cursor on its START_ELEMENT).
     * Values come from the {@code value} attribute or, for multi-line strings, from
     * element text. Nested {@code type="class"} properties are rejected (v0 scope).
     *
     * @return properties in document order
     */
    static TmxProperties readProperties(XMLStreamReader r) throws XMLStreamException {
        List<TmxProperty> out = new ArrayList<>();
        forEachChild(r, child -> {
            if (!"property".equals(child.getLocalName())) {
                throw new TmxParseException("unexpected <" + child.getLocalName()
                        + "> inside <properties>" + location(child));
            }
            out.add(readProperty(child));
        });
        return TmxProperties.of(out);
    }

    private static TmxProperty readProperty(XMLStreamReader r) throws XMLStreamException {
        String name = requireAttr(r, "name");
        TmxPropertyType type;
        try {
            type = TmxPropertyType.fromXml(attr(r, "type"));
        } catch (TmxParseException e) {
            throw new TmxParseException(e.getMessage() + " on property \"" + name + "\"" + location(r), e);
        }
        String value = attr(r, "value");
        StringBuilder text = null;
        while (true) {
            int event = r.next();
            if (event == XMLStreamConstants.CHARACTERS || event == XMLStreamConstants.CDATA
                    || event == XMLStreamConstants.SPACE) {
                if (value == null) {
                    if (text == null) {
                        text = new StringBuilder();
                    }
                    text.append(r.getText());
                }
            } else if (event == XMLStreamConstants.START_ELEMENT) {
                throw new TmxParseException("nested <" + r.getLocalName() + "> inside property \"" + name
                        + "\" is not supported (class properties are out of v0 scope)" + location(r));
            } else if (event == XMLStreamConstants.END_ELEMENT) {
                break;
            }
        }
        if (value == null) {
            value = text == null ? "" : text.toString();
        }
        return new TmxProperty(name, type, value);
    }

    /** @return " at line L, column C" when the cursor has a location, else "" */
    static String location(XMLStreamReader r) {
        Location loc = r.getLocation();
        if (loc == null || loc.getLineNumber() < 0) {
            return "";
        }
        return " at line " + loc.getLineNumber() + ", column " + loc.getColumnNumber();
    }
}
