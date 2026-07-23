package com.trojia.tools.validate;

import java.util.OptionalInt;
import java.util.TreeMap;
import java.util.function.Consumer;

import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxTileLayer;

import com.trojia.tools.validate.ValidationIssue.Severity;

/**
 * Pass 7: vertical-connector structure (content/maps/README.md "Forms"; Sprint 4's climb —
 * the baked {@code ZLinkTable} extracts real movement links from exactly these shapes, so a
 * malformed connector silently becomes an unwalkable dead end at runtime).
 *
 * <p>Walks every z-group's terrain sublayer row-major (document order), checking:</p>
 * <ul>
 *   <li><b>STAIR pairing</b> — {@code STAIR_UP} at {@code (x,y,z)} requires
 *       {@code STAIR_DOWN} at {@code (x,y,z+1)} and vice versa (same column, the README's
 *       pairing rule);</li>
 *   <li><b>RAMP head-room</b> — the cell directly above a {@code RAMP} must be OPEN
 *       (nothing authored there: no terrain fill, no floor);</li>
 *   <li><b>RAMP exit</b> — at least one orthogonal neighbor column at {@code z+1} must be
 *       walkable ({@code FLOOR}/{@code RAMP}/{@code STAIR_*}), or the ramp climbs to
 *       nowhere.</li>
 * </ul>
 *
 * <p>Effective form of a cell: the terrain tile's {@code form=} property when the terrain
 * gid is set (an explicit {@code OPEN} terrain tile falls through), else {@code FLOOR}
 * when the floor gid is set (the floor sublayer holds the walkable floor of unfilled
 * cells), else {@code OPEN}. A used gid with missing/unknown metadata is skipped here —
 * the materials pass owns that error, and this pass must not cascade on it.</p>
 */
public final class StairRampPass implements ValidationPass {

    private static final String STAIR_UP = "STAIR_UP";
    private static final String STAIR_DOWN = "STAIR_DOWN";
    private static final String RAMP = "RAMP";
    private static final String OPEN = "OPEN";

    private static final int[] EXIT_DX = {-1, 1, 0, 0};
    private static final int[] EXIT_DY = {0, 0, -1, 1};

    /** Creates the pass. */
    public StairRampPass() {
    }

    @Override
    public String id() {
        return "stairs";
    }

    @Override
    public void run(MapCheckContext context, Consumer<ValidationIssue> out) {
        TreeMap<Integer, TmxLayerGroup> byZ = new TreeMap<>();
        for (TmxLayerGroup group : MapStructure.zGroups(context.map())) {
            OptionalInt z = MapStructure.zOf(group.name());
            if (z.isPresent()) {
                byZ.putIfAbsent(z.getAsInt(), group); // duplicates are the z-groups pass's error
            }
        }
        for (TmxLayerGroup group : MapStructure.zGroups(context.map())) { // document order
            OptionalInt zOpt = MapStructure.zOf(group.name());
            if (zOpt.isEmpty()) {
                continue;
            }
            int z = zOpt.getAsInt();
            TmxTileLayer terrain = MapStructure.tileSublayer(group, MapStructure.TERRAIN);
            if (terrain == null) {
                continue; // absence is a sublayer-contract finding
            }
            String path = MapStructure.path(group, terrain);
            for (int y = 0; y < terrain.height(); y++) {
                for (int x = 0; x < terrain.width(); x++) {
                    String form = terrainForm(context, terrain, x, y);
                    if (STAIR_UP.equals(form)) {
                        checkPairing(context, byZ, path, x, y, z, z + 1, STAIR_DOWN, out);
                    } else if (STAIR_DOWN.equals(form)) {
                        checkPairing(context, byZ, path, x, y, z, z - 1, STAIR_UP, out);
                    } else if (RAMP.equals(form)) {
                        checkRamp(context, byZ, path, x, y, z, out);
                    }
                }
            }
        }
    }

    private void checkPairing(MapCheckContext context, TreeMap<Integer, TmxLayerGroup> byZ,
                              String path, int x, int y, int z, int otherZ, String expected,
                              Consumer<ValidationIssue> out) {
        String other = effectiveForm(context, byZ.get(otherZ), x, y);
        if (other == null) {
            return; // unknown metadata on the counterpart: the materials pass owns it
        }
        if (!expected.equals(other)) {
            String have = STAIR_DOWN.equals(expected) ? STAIR_UP : STAIR_DOWN;
            out.accept(error(context, path, x, y,
                    have + " at z=" + z + " has no " + expected + " at (" + x + "," + y
                            + ") on z=" + otherZ + " (found " + other + ").",
                    "a vertical passage needs STAIR_UP at (x,y,z) and STAIR_DOWN at "
                            + "(x,y,z+1), same column (content/maps/README.md pairing rule)."));
        }
    }

    private void checkRamp(MapCheckContext context, TreeMap<Integer, TmxLayerGroup> byZ,
                           String path, int x, int y, int z, Consumer<ValidationIssue> out) {
        TmxLayerGroup above = byZ.get(z + 1);
        String aboveForm = effectiveForm(context, above, x, y);
        if (aboveForm != null && !OPEN.equals(aboveForm)) {
            out.accept(error(context, path, x, y,
                    "RAMP at z=" + z + " has " + aboveForm + " directly above it on z="
                            + (z + 1) + " (must be OPEN).",
                    "a ramp's own column must stay clear one level up; move the fill or "
                            + "the ramp (content/maps/README.md \"Forms\")."));
        }
        boolean hasExit = false;
        for (int n = 0; n < EXIT_DX.length && !hasExit; n++) {
            String exit = effectiveForm(context, above, x + EXIT_DX[n], y + EXIT_DY[n]);
            hasExit = isWalkable(exit);
        }
        if (!hasExit) {
            out.accept(error(context, path, x, y,
                    "RAMP at z=" + z + " has no walkable exit on z=" + (z + 1)
                            + " (no FLOOR/RAMP/STAIR in any orthogonal neighbor column).",
                    "a ramp must climb TO somewhere: author walkable ground beside its "
                            + "column one level up, or remove the ramp."));
        }
    }

    private static boolean isWalkable(String form) {
        return "FLOOR".equals(form) || RAMP.equals(form)
                || STAIR_UP.equals(form) || STAIR_DOWN.equals(form);
    }

    /**
     * The effective form of {@code (x, y)} in {@code group}: terrain fill first (an
     * explicit OPEN terrain tile falls through), else FLOOR when the floor sublayer is
     * painted, else OPEN. An absent group and out-of-bounds coordinates read OPEN
     * (nothing authored); a used gid with missing metadata reads {@code null} (unknown —
     * the materials pass owns that error).
     */
    private static String effectiveForm(MapCheckContext context, TmxLayerGroup group,
                                        int x, int y) {
        if (group == null) {
            return OPEN;
        }
        TmxTileLayer terrain = MapStructure.tileSublayer(group, MapStructure.TERRAIN);
        if (terrain != null && x >= 0 && y >= 0 && x < terrain.width() && y < terrain.height()) {
            int gid = terrain.gidAt(x, y);
            if (gid != 0) {
                String form = formOf(context, gid);
                if (form == null || !OPEN.equals(form)) {
                    return form; // a real fill (or unknown metadata); OPEN falls through
                }
            }
        }
        TmxTileLayer floor = MapStructure.tileSublayer(group, MapStructure.FLOOR);
        if (floor != null && x >= 0 && y >= 0 && x < floor.width() && y < floor.height()
                && floor.gidAt(x, y) != 0) {
            return "FLOOR";
        }
        return OPEN;
    }

    /** The terrain tile's declared form at {@code (x, y)}, or {@code null} when unknowable. */
    private static String terrainForm(MapCheckContext context, TmxTileLayer terrain,
                                      int x, int y) {
        int gid = terrain.gidAt(x, y);
        return gid == 0 ? null : formOf(context, gid);
    }

    private static String formOf(MapCheckContext context, int gid) {
        return context.tileFor(gid)
                .flatMap(tile -> tile.properties().find("form"))
                .map(property -> property.value())
                .filter(MapStructure.FORMS::contains)
                .orElse(null);
    }

    private ValidationIssue error(MapCheckContext context, String layerPath, int x, int y,
                                  String message, String hint) {
        return new ValidationIssue(Severity.ERROR, id(), context.mapName(), layerPath, x, y,
                message, hint);
    }
}
