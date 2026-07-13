package com.trojia.tools.validate;

import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.function.Consumer;

import com.trojia.tools.tmx.TmxLayer;
import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxObject;
import com.trojia.tools.tmx.TmxObjectLayer;
import com.trojia.tools.tmx.TmxTileLayer;

import com.trojia.tools.validate.ValidationIssue.Severity;

/**
 * Pass 4: gid bounds versus tileset tilecount, plus flip-bit rejection.
 *
 * <p>Every non-zero gid in every tile layer (and every tile object) must fall inside
 * a declared tileset range {@code firstGid <= gid < firstGid + tileCount}. A tileset
 * that omits {@code tilecount} cannot be bound-checked and is itself an error.
 * Per (layer, gid) the first out-of-range occurrence is reported with its (x,y).</p>
 *
 * <p><strong>Flip bits.</strong> {@link com.trojia.tools.tmx.TmxReader} masks the
 * three Tiled flip bits and reports them through its warning listener; the model
 * therefore never carries them. This pass promotes each such reader warning
 * (collected in {@link MapCheckContext#readerWarnings()}) to an error — flipped or
 * rotated tiles carry no meaning for the import and are rejected outright.</p>
 */
public final class GidBoundsPass implements ValidationPass {

    private static final String FLIP_MARKER = "masked flip bits";

    /** Creates the pass. */
    public GidBoundsPass() {
    }

    @Override
    public String id() {
        return "gid-bounds";
    }

    @Override
    public void run(MapCheckContext context, Consumer<ValidationIssue> out) {
        for (String warning : context.readerWarnings()) {
            if (warning.contains(FLIP_MARKER)) {
                out.accept(new ValidationIssue(Severity.ERROR, id(), context.mapName(), "",
                        ValidationIssue.NO_COORD, ValidationIssue.NO_COORD,
                        "flipped/rotated tiles are not allowed: " + warning + ".",
                        "clear the flip/rotation in Tiled (highlight the tiles and press X/Y/Z to reset); flips carry no meaning for the import."));
            }
        }
        for (ResolvedTileset tileset : context.tilesets()) {
            if (tileset.tileset().tileCount() <= 0) {
                out.accept(new ValidationIssue(Severity.ERROR, id(), context.mapName(), "",
                        ValidationIssue.NO_COORD, ValidationIssue.NO_COORD,
                        "tileset \"" + tileset.tileset().name() + "\" declares no tilecount; gids cannot be bound-checked.",
                        "re-save the .tsx with Tiled 1.9+ so the tilecount attribute is written."));
            }
        }
        walk(context, context.map().layers(), null, out);
    }

    private void walk(MapCheckContext context, List<TmxLayer> layers, TmxLayerGroup parent,
                      Consumer<ValidationIssue> out) {
        for (TmxLayer layer : layers) {
            String path = parent == null ? layer.name() : MapStructure.path(parent, layer);
            switch (layer) {
                case TmxLayerGroup group -> walk(context, group.layers(), group, out);
                case TmxTileLayer tiles -> checkTileLayer(context, tiles, path, out);
                case TmxObjectLayer objects -> checkObjectLayer(context, objects, path, out);
            }
        }
    }

    private void checkTileLayer(MapCheckContext context, TmxTileLayer layer, String path,
                                Consumer<ValidationIssue> out) {
        Set<Integer> reported = new HashSet<>();
        for (int y = 0; y < layer.height(); y++) {
            for (int x = 0; x < layer.width(); x++) {
                int gid = layer.gidAt(x, y);
                if (gid != 0 && context.tilesetFor(gid).isEmpty() && reported.add(gid)) {
                    out.accept(new ValidationIssue(Severity.ERROR, id(), context.mapName(), path, x, y,
                            "gid " + gid + " is outside every declared tileset range.",
                            "the map references a tile that does not exist; " + rangeHint(context)));
                }
            }
        }
    }

    private void checkObjectLayer(MapCheckContext context, TmxObjectLayer layer, String path,
                                  Consumer<ValidationIssue> out) {
        for (TmxObject object : layer.objects()) {
            if (object.gid() != 0 && context.tilesetFor(object.gid()).isEmpty()) {
                int tx = (int) Math.floor(object.x() / context.map().tileWidth());
                int ty = (int) Math.floor(object.y() / context.map().tileHeight());
                out.accept(new ValidationIssue(Severity.ERROR, id(), context.mapName(), path, tx, ty,
                        "object " + object.id() + " (\"" + object.name() + "\") carries gid " + object.gid()
                                + ", outside every declared tileset range.",
                        "the object references a tile that does not exist; " + rangeHint(context)));
            }
        }
    }

    private static String rangeHint(MapCheckContext context) {
        if (context.tilesets().isEmpty()) {
            return "the map declares no tilesets at all — add the materials.tsx reference.";
        }
        StringBuilder sb = new StringBuilder("valid ranges:");
        for (ResolvedTileset t : context.tilesets()) {
            if (t.tileset().tileCount() > 0) {
                sb.append(' ').append(t.firstGid()).append("..")
                        .append(t.firstGid() + t.tileset().tileCount() - 1)
                        .append(" (").append(t.tileset().name()).append(')');
            }
        }
        return sb.append('.').toString();
    }
}
