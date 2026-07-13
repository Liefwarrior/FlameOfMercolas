package com.trojia.tools.validate;

import java.util.HashSet;
import java.util.Optional;
import java.util.Set;
import java.util.function.Consumer;

import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxProperty;
import com.trojia.tools.tmx.TmxTileLayer;
import com.trojia.tools.tmx.TmxTilesetTile;

import com.trojia.tools.validate.ValidationIssue.Severity;

/**
 * Pass 3: every used tile's binding properties resolve against the raws
 * (content/maps/README.md "Tileset property contract").
 *
 * <p>Walks terrain/floor/fluids tile sublayers of every z-group row-major. For each
 * distinct offending (layer, gid, defect) the <em>first</em> occurrence is reported
 * with its {@code (x,y)} — one error per authoring mistake, not one per painted cell.</p>
 *
 * <ul>
 *   <li>terrain/floor tiles: {@code material=} must resolve against the raws material
 *       id set (treatment-minted derived ids included), with a nearest-name suggestion
 *       on failure; {@code form=} must be a {@code TileForm} name; fluid tiles are
 *       illegal here;</li>
 *   <li>fluids tiles: {@code fluid=} must resolve against the fluid raws;
 *       {@code depth=} must be an int in 1..7 (3 FLUID-lane depth bits); material
 *       tiles are illegal here;</li>
 *   <li>used gids without tileset metadata are errors (a tile with no properties
 *       cannot be imported).</li>
 * </ul>
 *
 * <p>Out-of-range gids are skipped here — the gid-bounds pass owns them.</p>
 */
public final class MaterialResolutionPass implements ValidationPass {

    /** Creates the pass. */
    public MaterialResolutionPass() {
    }

    @Override
    public String id() {
        return "materials";
    }

    @Override
    public void run(MapCheckContext context, Consumer<ValidationIssue> out) {
        for (TmxLayerGroup group : MapStructure.zGroups(context.map())) {
            checkTileLayer(context, group, MapStructure.TERRAIN, false, out);
            checkTileLayer(context, group, MapStructure.FLOOR, false, out);
            checkTileLayer(context, group, MapStructure.FLUIDS, true, out);
        }
    }

    private void checkTileLayer(MapCheckContext context, TmxLayerGroup group, String layerName,
                                boolean fluidLayer, Consumer<ValidationIssue> out) {
        TmxTileLayer layer = MapStructure.tileSublayer(group, layerName);
        if (layer == null) {
            return; // absence is a sublayer-contract finding.
        }
        String path = MapStructure.path(group, layer);
        Set<Long> reported = new HashSet<>(); // (gid) -> first occurrence only
        for (int y = 0; y < layer.height(); y++) {
            for (int x = 0; x < layer.width(); x++) {
                int gid = layer.gidAt(x, y);
                if (gid == 0 || context.tilesetFor(gid).isEmpty() || !reported.add((long) gid)) {
                    continue;
                }
                Optional<TmxTilesetTile> tile = context.tileFor(gid);
                if (tile.isEmpty()) {
                    out.accept(error(context, path, x, y, "gid " + gid
                                    + " has no metadata in its tileset.",
                            "every used tile must declare material+form (or fluid+depth) custom properties in the .tsx."));
                    continue;
                }
                if (fluidLayer) {
                    checkFluidTile(context, path, x, y, gid, tile.get(), out);
                } else {
                    checkMaterialTile(context, path, x, y, gid, tile.get(), out);
                }
            }
        }
    }

    private void checkMaterialTile(MapCheckContext context, String path, int x, int y, int gid,
                                   TmxTilesetTile tile, Consumer<ValidationIssue> out) {
        if (tile.properties().find("fluid").isPresent()) {
            out.accept(error(context, path, x, y, "fluid tile (gid " + gid + ") painted on a "
                            + "terrain/floor sublayer.",
                    "initial pooled fluid is only legal on the fluids sublayer; repaint with a material tile."));
            return;
        }
        Optional<TmxProperty> material = tile.properties().find("material");
        if (material.isEmpty()) {
            out.accept(error(context, path, x, y, "tile (gid " + gid + ") carries no material= property.",
                    "add the string properties material and form to the tileset tile."));
            return;
        }
        String id = material.get().value();
        if (!context.raws().isMaterial(id)) {
            String suggestion = Names.nearest(id, context.raws().materialIds())
                    .map(s -> "did you mean \"" + s + "\"?")
                    .orElse("no similar id exists in content/raws/materials.");
            out.accept(error(context, path, x, y, "unknown material \"" + id + "\" (gid " + gid + ").",
                    suggestion));
        }
        Optional<TmxProperty> form = tile.properties().find("form");
        if (form.isEmpty() || !MapStructure.FORMS.contains(form.get().value())) {
            out.accept(error(context, path, x, y, "tile (gid " + gid + ") has "
                            + (form.isEmpty() ? "no form= property." : "unknown form \"" + form.get().value() + "\"."),
                    "form must be one of WALL, FLOOR, OPEN, RAMP, STAIR_UP, STAIR_DOWN."));
        }
    }

    private void checkFluidTile(MapCheckContext context, String path, int x, int y, int gid,
                                TmxTilesetTile tile, Consumer<ValidationIssue> out) {
        if (tile.properties().find("material").isPresent()) {
            out.accept(error(context, path, x, y, "material tile (gid " + gid + ") painted on the fluids sublayer.",
                    "fluids holds only fluid tiles (fluid= and depth= properties); move the material to terrain or floor."));
            return;
        }
        Optional<TmxProperty> fluid = tile.properties().find("fluid");
        if (fluid.isEmpty()) {
            out.accept(error(context, path, x, y, "tile (gid " + gid + ") carries no fluid= property.",
                    "fluid tiles need string fluid= and int depth= (1..7) properties in the tileset."));
            return;
        }
        String id = fluid.get().value();
        if (!context.raws().isFluid(id)) {
            String suggestion = Names.nearest(id, context.raws().fluidIds())
                    .map(s -> "did you mean \"" + s + "\"?")
                    .orElse("no similar id exists in content/raws/fluids.");
            out.accept(error(context, path, x, y, "unknown fluid \"" + id + "\" (gid " + gid + ").", suggestion));
        }
        Optional<TmxProperty> depth = tile.properties().find("depth");
        Integer depthValue = depth.map(p -> parseIntOrNull(p.value())).orElse(null);
        if (depthValue == null || depthValue < 1 || depthValue > 7) {
            out.accept(error(context, path, x, y, "tile (gid " + gid + ") has "
                            + (depth.isEmpty() ? "no depth= property." : "illegal depth \"" + depth.get().value() + "\"."),
                    "depth must be an int in 1..7 (the FLUID lane stores 3 depth bits)."));
        }
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
