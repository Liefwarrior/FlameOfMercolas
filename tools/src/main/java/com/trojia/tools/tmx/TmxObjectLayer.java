package com.trojia.tools.tmx;

import java.util.List;
import java.util.Objects;

/**
 * An object layer ({@code <objectgroup>}).
 *
 * <p><strong>Determinism contract:</strong> {@link #objects()} preserves document
 * order. (The importer re-sorts annotations by {@code (z, objectId)} itself —
 * ARCHITECTURE.md section 3 — but the model never reorders.)</p>
 *
 * @param id         Tiled layer id ({@code 0} if absent)
 * @param name       layer name, never {@code null}
 * @param objects    objects in document order; defensively copied, immutable
 * @param properties custom properties, never {@code null}
 */
public record TmxObjectLayer(int id, String name, List<TmxObject> objects, TmxProperties properties)
        implements TmxLayer {

    /**
     * @throws NullPointerException if any component or list element is {@code null}
     */
    public TmxObjectLayer {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(properties, "properties");
        objects = List.copyOf(objects);
    }
}
