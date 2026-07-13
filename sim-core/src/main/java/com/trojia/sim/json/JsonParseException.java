package com.trojia.sim.json;

/**
 * Thrown by {@link MiniJson} when input text is not valid JSON (or violates a strict
 * parse-mode rule such as {@link JsonNumberMode#INTEGER_ONLY} or the duplicate-key ban).
 *
 * <p><strong>Position contract:</strong> every instance carries a 1-based line and
 * column pinpointing the offending input. Lines are counted by {@code '\n'} only
 * (a {@code "\r\n"} pair counts once, via its {@code '\n'}; a lone {@code '\r'} does
 * not start a new line). Columns count UTF-16 code units from the start of the line,
 * starting at 1; a tab counts as one column. For unexpected end-of-input the position
 * is one past the last character. The message always ends with
 * {@code " at line <line>, column <column>"}.</p>
 */
public final class JsonParseException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    /** 1-based line of the offending input. */
    private final int line;

    /** 1-based column (UTF-16 code units) of the offending input. */
    private final int column;

    /**
     * Creates a positioned parse error.
     *
     * @param message description of the defect, without position suffix
     * @param line    1-based line number, {@code >= 1}
     * @param column  1-based column number, {@code >= 1}
     */
    public JsonParseException(String message, int line, int column) {
        super(message + " at line " + line + ", column " + column);
        this.line = line;
        this.column = column;
    }

    /**
     * Returns the 1-based line number of the error.
     *
     * @return the line, {@code >= 1}
     */
    public int getLine() {
        return line;
    }

    /**
     * Returns the 1-based column number of the error.
     *
     * @return the column, {@code >= 1}
     */
    public int getColumn() {
        return column;
    }
}
