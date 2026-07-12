package com.trojia.tools.tmx;

/**
 * Value type of a Tiled custom property, mirroring the {@code type} attribute of a
 * {@code <property>} element. An absent attribute means {@link #STRING} (Tiled's
 * default). Nested class properties ({@code type="class"}) are out of scope for v0
 * and rejected at parse time.
 */
public enum TmxPropertyType {

    /** Plain text; the Tiled default when no {@code type} attribute is present. */
    STRING("string"),
    /** 32-bit signed integer. */
    INT("int"),
    /** Floating-point number (Tiled writes a decimal literal). */
    FLOAT("float"),
    /** Boolean; serialized value is the literal {@code true} or {@code false}. */
    BOOL("bool"),
    /** Color in {@code #AARRGGBB} or {@code #RRGGBB} form; kept as raw text in v0. */
    COLOR("color"),
    /** Path relative to the document; kept as raw text in v0. */
    FILE("file"),
    /** Reference to a Tiled object id (integer). */
    OBJECT("object");

    private final String xmlName;

    TmxPropertyType(String xmlName) {
        this.xmlName = xmlName;
    }

    /** @return the value the {@code type} attribute uses for this type */
    public String xmlName() {
        return xmlName;
    }

    /**
     * Resolves a {@code type} attribute value.
     *
     * @param raw attribute value, or {@code null} when the attribute is absent
     * @return the matching type; {@link #STRING} when {@code raw} is {@code null}
     * @throws TmxParseException if {@code raw} names an unknown or unsupported type
     */
    public static TmxPropertyType fromXml(String raw) {
        if (raw == null) {
            return STRING;
        }
        for (TmxPropertyType t : values()) {
            if (t.xmlName.equals(raw)) {
                return t;
            }
        }
        throw new TmxParseException("unsupported property type \"" + raw + "\"");
    }
}
