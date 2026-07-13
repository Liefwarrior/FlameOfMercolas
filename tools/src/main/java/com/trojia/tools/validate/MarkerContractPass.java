package com.trojia.tools.validate;

import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.function.Consumer;

import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxObject;
import com.trojia.tools.tmx.TmxObjectLayer;
import com.trojia.tools.tmx.TmxProperty;
import com.trojia.tools.tmx.TmxTileLayer;
import com.trojia.tools.tmx.TmxTilesetTile;

import com.trojia.tools.validate.ValidationIssue.Severity;

/**
 * Pass 5: marker/object contract (content/maps/README.md "Markers").
 *
 * <p>Per marker object, in document order per z-group:</p>
 * <ul>
 *   <li>every object must be named;</li>
 *   <li>the object class must be a known marker kind ({@code light_source} or
 *       {@code script_anchor});</li>
 *   <li>{@code light_source} needs an int {@code luminance} property in 0..31;</li>
 *   <li>{@code script_anchor} names must be unique per map;</li>
 *   <li>ignition anchors (script anchors whose name starts with {@code ignition})
 *       must sit on a cell whose collapsed material (terrain fill, else floor —
 *       ruling section 1.1 #17) is flammable, and inside the map bounds.</li>
 * </ul>
 *
 * <p>Cells whose material does not resolve at all are skipped here — the materials
 * pass owns that defect.</p>
 */
public final class MarkerContractPass implements ValidationPass {

    private static final Set<String> KNOWN_CLASSES = Set.of("light_source", "script_anchor");
    private static final String IGNITION_PREFIX = "ignition";

    /** Creates the pass. */
    public MarkerContractPass() {
    }

    @Override
    public String id() {
        return "markers";
    }

    @Override
    public void run(MapCheckContext context, Consumer<ValidationIssue> out) {
        Map<String, String> anchorSeenAt = new HashMap<>(); // anchor name -> layer path of first sighting
        for (TmxLayerGroup group : MapStructure.zGroups(context.map())) {
            TmxObjectLayer markers = MapStructure.objectSublayer(group, MapStructure.MARKERS);
            if (markers == null) {
                continue; // absence/kind mismatch is a sublayer-contract finding.
            }
            String path = MapStructure.path(group, markers);
            for (TmxObject object : markers.objects()) {
                int tx = (int) Math.floor(object.x() / context.map().tileWidth());
                int ty = (int) Math.floor(object.y() / context.map().tileHeight());
                if (object.name().isBlank()) {
                    out.accept(error(context, path, tx, ty, "object " + object.id() + " has no name.",
                            "every marker must be named (names are how scripts and scenarios address them)."));
                    continue;
                }
                if (!KNOWN_CLASSES.contains(object.typeName())) {
                    out.accept(error(context, path, tx, ty, "marker \"" + object.name()
                                    + "\" has unknown class \"" + object.typeName() + "\".",
                            "set the object class to light_source or script_anchor."));
                    continue;
                }
                switch (object.typeName()) {
                    case "light_source" -> checkLightSource(context, path, tx, ty, object, out);
                    case "script_anchor" -> checkScriptAnchor(context, group, path, tx, ty, object, anchorSeenAt, out);
                    default -> throw new AssertionError("unreachable: " + object.typeName());
                }
            }
        }
    }

    private void checkLightSource(MapCheckContext context, String path, int tx, int ty,
                                  TmxObject object, Consumer<ValidationIssue> out) {
        Optional<TmxProperty> luminance = object.properties().find("luminance");
        Integer value = luminance.map(p -> parseIntOrNull(p.value())).orElse(null);
        if (value == null || value < 0 || value > 31) {
            out.accept(error(context, path, tx, ty, "light_source \"" + object.name() + "\" has "
                            + (luminance.isEmpty() ? "no luminance property."
                            : "illegal luminance \"" + luminance.get().value() + "\"."),
                    "add an int luminance property in 0..31 (light levels are 5-bit)."));
        }
    }

    private void checkScriptAnchor(MapCheckContext context, TmxLayerGroup group, String path, int tx, int ty,
                                   TmxObject object, Map<String, String> anchorSeenAt,
                                   Consumer<ValidationIssue> out) {
        String firstPath = anchorSeenAt.putIfAbsent(object.name(), path);
        if (firstPath != null) {
            out.accept(error(context, path, tx, ty, "duplicate script_anchor name \"" + object.name()
                            + "\" (first seen in " + firstPath + ").",
                    "anchor names must be unique per map; rename one of them."));
            return;
        }
        if (!object.name().startsWith(IGNITION_PREFIX)) {
            return;
        }
        if (tx < 0 || tx >= context.map().width() || ty < 0 || ty >= context.map().height()) {
            out.accept(error(context, path, tx, ty, "ignition anchor \"" + object.name()
                    + "\" lies outside the map bounds.", "move the point onto the map."));
            return;
        }
        String material = collapsedMaterial(context, group, tx, ty);
        if (material == null) {
            out.accept(error(context, path, tx, ty, "ignition anchor \"" + object.name()
                            + "\" targets an OPEN cell (no fill, no floor).",
                    "place the anchor on a flammable tile — the Tavern Fire script ignites the material under it."));
            return;
        }
        context.raws().flammability(material).ifPresent(flammability -> {
            if (flammability <= 0) {
                out.accept(error(context, path, tx, ty, "ignition anchor \"" + object.name()
                                + "\" sits on \"" + material + "\", which is not flammable.",
                        "move the anchor onto a flammable material (e.g. oak, thatch) or change the tile."));
            }
        });
        // Unknown material: the materials pass already reported it; stay silent here.
    }

    /**
     * Collapse rule of ruling section 1.1 #17: fill (terrain) wins, else floor, else OPEN.
     *
     * @return the material id at (tx, ty), or {@code null} for an OPEN cell or when the
     *         cell's gid does not resolve to a metadata tile
     */
    private String collapsedMaterial(MapCheckContext context, TmxLayerGroup group, int tx, int ty) {
        for (String layerName : new String[] { MapStructure.TERRAIN, MapStructure.FLOOR }) {
            TmxTileLayer layer = MapStructure.tileSublayer(group, layerName);
            if (layer == null || tx >= layer.width() || ty >= layer.height()) {
                continue;
            }
            int gid = layer.gidAt(tx, ty);
            if (gid == 0) {
                continue;
            }
            Optional<TmxTilesetTile> tile = context.tileFor(gid);
            return tile.flatMap(t -> t.properties().find("material"))
                    .map(TmxProperty::value)
                    .orElse(null);
        }
        return null;
    }

    private static Integer parseIntOrNull(String raw) {
        try {
            return Integer.parseInt(raw);
        } catch (NumberFormatException e) {
            return null;
        }
    }

    private ValidationIssue error(MapCheckContext context, String layerPath, int x, int y,
                                  String message, String hint) {
        return new ValidationIssue(Severity.ERROR, id(), context.mapName(), layerPath, x, y, message, hint);
    }
}
