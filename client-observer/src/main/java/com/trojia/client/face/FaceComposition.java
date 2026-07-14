package com.trojia.client.face;

import com.trojia.client.sprite.SpriteRef;

import java.util.List;

/**
 * A composed face: the ordered part list FaceGen resolves for one actor (unified art spec
 * §4.8). Pure value — layer order is list order (bottom &rarr; top), anchors are pixels
 * from the top-left of the {@value #CANVAS_PX}&times;{@value #CANVAS_PX} canvas. The GL
 * side just draws the list in order — no offscreen compositing, no projection features.
 * Never serialized: a face is a pure function of {@code (worldSeed, actorId)}, so TROJSAV
 * carries nothing.
 *
 * @param parts placed parts, bottom-most first; never empty
 */
public record FaceComposition(List<PlacedPart> parts) {

    /** Face canvas edge, px: 3&times;3 grid of 16 px cells (spec §4.1). */
    public static final int CANVAS_PX = 48;

    public FaceComposition {
        parts = List.copyOf(parts);
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("a face composition has at least a base part");
        }
    }

    /**
     * One placed part.
     *
     * @param part the sprite entry (cells resolve px size: {@code cellsW*tilePx} wide)
     * @param x    anchor px from the canvas left edge
     * @param y    anchor px from the canvas top edge
     */
    public record PlacedPart(SpriteRef part, int x, int y) {
    }
}
