package com.trojia.client.atlas;

/**
 * One cell of the placeholder atlas: a pixel rectangle in sheet coordinates,
 * origin top-left (the shared orientation of the raster buffer, libGDX
 * {@code Pixmap}, PNG rows, and the {@code .atlas} text format's {@code xy} line).
 *
 * @param x      left edge in pixels, {@code >= 0}
 * @param y      top edge in pixels, {@code >= 0}
 * @param width  width in pixels, {@code > 0}
 * @param height height in pixels, {@code > 0}
 */
public record AtlasCellRect(int x, int y, int width, int height) {

    /**
     * Validates the rectangle.
     *
     * @throws IllegalArgumentException if {@code x} or {@code y} is negative, or
     *                                  {@code width} or {@code height} is not positive
     */
    public AtlasCellRect {
        if (x < 0 || y < 0) {
            throw new IllegalArgumentException("cell origin (" + x + ", " + y + ") negative");
        }
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException(
                    "cell size " + width + "x" + height + " not positive");
        }
    }
}
