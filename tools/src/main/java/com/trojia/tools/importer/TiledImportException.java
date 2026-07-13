package com.trojia.tools.importer;

/**
 * Thrown when a Tiled map cannot be baked into a world: an unresolved material
 * id, a tile carrying no usable binding, a malformed z-group, or a write the
 * world rejected. The message always names the offending element; unknown-id
 * failures additionally carry a nearest-name suggestion (content/maps/README.md
 * "Tileset property contract").
 *
 * <p>Unchecked: an import failure is a hard authoring error, not a recoverable
 * condition — the caller aborts the bake and surfaces the message.
 */
public final class TiledImportException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param message the human-readable failure, naming the offending element
     */
    public TiledImportException(String message) {
        super(message);
    }
}
