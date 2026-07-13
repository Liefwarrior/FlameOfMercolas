package com.trojia.tools.validate;

import java.util.HashSet;
import java.util.Set;
import java.util.function.Consumer;

import com.trojia.tools.tmx.TmxLayer;
import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxObjectLayer;
import com.trojia.tools.tmx.TmxTileLayer;

import com.trojia.tools.validate.ValidationIssue.Severity;

/**
 * Pass 2: fixed sublayers inside each z-group (content/maps/README.md table).
 *
 * <p>Per z-group (groups with illegal names are pass-1 findings and skipped here):</p>
 * <ul>
 *   <li>{@code terrain} and {@code floor} tile layers are required; a {@code markers}
 *       object layer is required (may be empty); {@code fluids} is an optional tile layer;</li>
 *   <li>no other sublayer names are legal, and no sublayer may appear twice;</li>
 *   <li>a known name with the wrong layer kind (e.g. {@code terrain} as an object
 *       layer) is an error;</li>
 *   <li>tile sublayers must span exactly the map's dimensions.</li>
 * </ul>
 */
public final class SublayerContractPass implements ValidationPass {

    /** Creates the pass. */
    public SublayerContractPass() {
    }

    @Override
    public String id() {
        return "sublayers";
    }

    @Override
    public void run(MapCheckContext context, Consumer<ValidationIssue> out) {
        for (TmxLayerGroup group : MapStructure.zGroups(context.map())) {
            Set<String> seen = new HashSet<>();
            for (TmxLayer layer : group.layers()) {
                String path = MapStructure.path(group, layer);
                if (!MapStructure.KNOWN_SUBLAYERS.contains(layer.name())) {
                    out.accept(error(context, path, "unknown sublayer \"" + layer.name() + "\".",
                            "only terrain, floor, fluids (tile layers) and markers (object layer) are legal inside a z-group."));
                    continue;
                }
                if (!seen.add(layer.name())) {
                    out.accept(error(context, path, "duplicate sublayer \"" + layer.name() + "\".",
                            "each sublayer may appear at most once per z-group; merge the duplicates."));
                    continue;
                }
                boolean wantsObjects = layer.name().equals(MapStructure.MARKERS);
                if (wantsObjects && !(layer instanceof TmxObjectLayer)) {
                    out.accept(error(context, path, "sublayer \"markers\" must be an object layer.",
                            "recreate markers as an <objectgroup>; tiles do not carry marker annotations."));
                    continue;
                }
                if (!wantsObjects && !(layer instanceof TmxTileLayer)) {
                    out.accept(error(context, path, "sublayer \"" + layer.name() + "\" must be a tile layer.",
                            "recreate it as a CSV tile <layer>; only markers is an object layer."));
                    continue;
                }
                if (layer instanceof TmxTileLayer tiles
                        && (tiles.width() != context.map().width() || tiles.height() != context.map().height())) {
                    out.accept(error(context, path, "sublayer is " + tiles.width() + "x" + tiles.height()
                                    + " but the map is " + context.map().width() + "x" + context.map().height() + ".",
                            "resize the layer to the full map extent (Tiled does this automatically on map resize)."));
                }
            }
            for (String required : new String[] { MapStructure.TERRAIN, MapStructure.FLOOR, MapStructure.MARKERS }) {
                if (!seen.contains(required)) {
                    out.accept(error(context, group.name(), "required sublayer \"" + required + "\" is missing.",
                            required.equals(MapStructure.MARKERS)
                                    ? "add an empty <objectgroup> named markers."
                                    : "add the tile layer, even if it is all-empty (floor may be empty; terrain holds the fill)."));
                }
            }
        }
    }

    private ValidationIssue error(MapCheckContext context, String layerPath, String message, String hint) {
        return new ValidationIssue(Severity.ERROR, id(), context.mapName(), layerPath,
                ValidationIssue.NO_COORD, ValidationIssue.NO_COORD, message, hint);
    }
}
