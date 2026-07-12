package com.trojia.tools.tmx;

import java.util.List;
import java.util.Objects;

/**
 * A layer group ({@code <group>}); groups nest arbitrarily.
 *
 * <p><strong>Determinism contract:</strong> {@link #layers()} preserves document order
 * of the direct children. Group offsets/tints are out of scope for v0.</p>
 *
 * @param id         Tiled layer id ({@code 0} if absent)
 * @param name       group name, never {@code null}
 * @param layers     direct child layers in document order; defensively copied, immutable
 * @param properties custom properties, never {@code null}
 */
public record TmxLayerGroup(int id, String name, List<TmxLayer> layers, TmxProperties properties)
        implements TmxLayer {

    /**
     * @throws NullPointerException if any component or list element is {@code null}
     */
    public TmxLayerGroup {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(properties, "properties");
        layers = List.copyOf(layers);
    }
}
