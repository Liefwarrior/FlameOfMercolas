package com.trojia.tools.validate;

import java.util.ArrayList;
import java.util.List;
import java.util.OptionalInt;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import com.trojia.tools.tmx.TmxLayer;
import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxMap;
import com.trojia.tools.tmx.TmxObjectLayer;
import com.trojia.tools.tmx.TmxTileLayer;

/**
 * Package-private structural vocabulary of the Tiled authoring convention
 * (content/maps/README.md): z-group names, sublayer names, and lookup helpers
 * shared by the passes.
 *
 * <p>All methods are pure; iteration preserves document order.</p>
 */
final class MapStructure {

    /** Pattern of a legal z-group name: {@code z:} + explicit sign + digits. */
    static final Pattern Z_NAME = Pattern.compile("z:([+-])(\\d+)");

    /** Required tile sublayer holding the cell fill. */
    static final String TERRAIN = "terrain";
    /** Required tile sublayer holding the floor material of unfilled cells. */
    static final String FLOOR = "floor";
    /** Optional tile sublayer holding initial pooled fluid. */
    static final String FLUIDS = "fluids";
    /** Required object sublayer holding markers. */
    static final String MARKERS = "markers";

    /** Every legal sublayer name. */
    static final Set<String> KNOWN_SUBLAYERS = Set.of(TERRAIN, FLOOR, FLUIDS, MARKERS);

    /** Legal {@code form=} property values ({@code TileForm} vocabulary). */
    static final Set<String> FORMS = Set.of("WALL", "FLOOR", "OPEN", "RAMP", "STAIR_UP", "STAIR_DOWN");

    /** Inclusive sane z-level range: the world is at most 64 z-chunks of 8 tiles. */
    static final int MIN_Z = -64;
    static final int MAX_Z = 63;

    private MapStructure() {
    }

    /**
     * Parses a z-group name. {@code z:-0} is rejected (street level is written
     * {@code z:+0}), as is anything not matching {@link #Z_NAME} exactly.
     *
     * @param groupName candidate group name
     * @return the signed z value, or empty when the name is not a legal z-group name
     */
    static OptionalInt zOf(String groupName) {
        Matcher m = Z_NAME.matcher(groupName);
        if (!m.matches()) {
            return OptionalInt.empty();
        }
        int magnitude;
        try {
            magnitude = Integer.parseInt(m.group(2));
        } catch (NumberFormatException e) {
            return OptionalInt.empty(); // digits overflow int: not a sane z anyway
        }
        boolean negative = m.group(1).equals("-");
        if (negative && magnitude == 0) {
            return OptionalInt.empty(); // "z:-0" is banned; street level is z:+0
        }
        return OptionalInt.of(negative ? -magnitude : magnitude);
    }

    /**
     * @param map the parsed map
     * @return top-level groups whose names parse as z-groups, document order
     */
    static List<TmxLayerGroup> zGroups(TmxMap map) {
        List<TmxLayerGroup> groups = new ArrayList<>();
        for (TmxLayer layer : map.layers()) {
            if (layer instanceof TmxLayerGroup group && zOf(group.name()).isPresent()) {
                groups.add(group);
            }
        }
        return groups;
    }

    /**
     * @param group a z-group
     * @param name  sublayer name
     * @return the first direct-child <em>tile</em> layer with that name, or {@code null}
     */
    static TmxTileLayer tileSublayer(TmxLayerGroup group, String name) {
        for (TmxLayer layer : group.layers()) {
            if (layer instanceof TmxTileLayer tiles && tiles.name().equals(name)) {
                return tiles;
            }
        }
        return null;
    }

    /**
     * @param group a z-group
     * @param name  sublayer name
     * @return the first direct-child <em>object</em> layer with that name, or {@code null}
     */
    static TmxObjectLayer objectSublayer(TmxLayerGroup group, String name) {
        for (TmxLayer layer : group.layers()) {
            if (layer instanceof TmxObjectLayer objects && objects.name().equals(name)) {
                return objects;
            }
        }
        return null;
    }

    /**
     * @param group a z-group
     * @param layer a direct child of {@code group}
     * @return the display path {@code "<group>/<layer>"} used in issues
     */
    static String path(TmxLayerGroup group, TmxLayer layer) {
        return group.name() + "/" + layer.name();
    }
}
