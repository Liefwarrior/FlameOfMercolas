package com.trojia.client.sprite;

import java.util.Objects;
import java.util.Set;

/**
 * One validated entry of {@code sprite-index.json} (unified art spec §2.2): a named,
 * tag-queryable rectangle of {@code cellsW}&times;{@code cellsH} 16px cells on the sprite
 * sheet, at cell {@code (col, row)} (top-left origin, same convention as
 * {@link com.trojia.client.atlas.AtlasCellRect}). {@code weight} is consumed only by
 * FaceGen's weighted picks (§2.2) — {@link SpriteIndex#forActor} ignores it. Immutable;
 * {@code tags} is defensively copied to an unmodifiable set.
 *
 * @param id      unique index-wide id, {@code [a-z0-9_]+}
 * @param col     leftmost cell column on the sheet
 * @param row     topmost cell row on the sheet
 * @param cellsW  width in cells, &ge; 1 (sprite is {@code 16*cellsW} px wide)
 * @param cellsH  height in cells, &ge; 1
 * @param weight  FaceGen pick weight, &ge; 1 (default 1 in the JSON)
 * @param tags    non-empty query tags, each {@code [a-z0-9_]+}
 */
public record SpriteRef(String id, int col, int row, int cellsW, int cellsH,
                        int weight, Set<String> tags) {

    public SpriteRef {
        Objects.requireNonNull(id, "id");
        Objects.requireNonNull(tags, "tags");
        tags = Set.copyOf(tags);
    }
}
