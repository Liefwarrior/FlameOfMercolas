package com.trojia.tools.validate;

import java.util.OptionalInt;
import java.util.TreeMap;
import java.util.function.Consumer;

import com.trojia.tools.tmx.TmxLayer;
import com.trojia.tools.tmx.TmxLayerGroup;

import com.trojia.tools.validate.ValidationIssue.Severity;

/**
 * Pass 1: z-group naming and contiguity (content/maps/README.md "Z-level groups").
 *
 * <p>Checks, in document order:</p>
 * <ul>
 *   <li>every top-level layer is a {@code <group>} named {@code z:[+-]N} (sign always
 *       written, {@code z:+0} for street level, {@code z:-0} banned);</li>
 *   <li>no two groups claim the same z;</li>
 *   <li>the claimed z values form a gap-free range;</li>
 *   <li>zOrigin is sane: {@code z:+0} exists and every z lies in
 *       [{@value MapStructure#MIN_Z}, {@value MapStructure#MAX_Z}] (64 z-chunk world cap).</li>
 * </ul>
 */
public final class ZGroupContiguityPass implements ValidationPass {

    /** Creates the pass. */
    public ZGroupContiguityPass() {
    }

    @Override
    public String id() {
        return "z-groups";
    }

    @Override
    public void run(MapCheckContext context, Consumer<ValidationIssue> out) {
        TreeMap<Integer, String> byZ = new TreeMap<>();
        boolean anyGroup = false;

        for (TmxLayer layer : context.map().layers()) {
            if (!(layer instanceof TmxLayerGroup group)) {
                out.accept(error(context, "top-level layer \"" + layer.name() + "\" is not a z-group.",
                        "wrap every layer in a <group> named z:+N or z:-N (e.g. z:+0)."));
                continue;
            }
            anyGroup = true;
            OptionalInt z = MapStructure.zOf(group.name());
            if (z.isEmpty()) {
                out.accept(error(context, "group \"" + group.name() + "\" violates the z:[+-]N naming convention.",
                        "rename it z:+N or z:-N with the sign always written; street level is z:+0 (never z:-0)."));
                continue;
            }
            int value = z.getAsInt();
            if (value < MapStructure.MIN_Z || value > MapStructure.MAX_Z) {
                out.accept(error(context, "z-group \"" + group.name() + "\" is outside the sane z range "
                                + MapStructure.MIN_Z + ".." + MapStructure.MAX_Z + ".",
                        "the world caps at 64 z-chunks; re-origin the map around z:+0."));
                continue;
            }
            String previous = byZ.putIfAbsent(value, group.name());
            if (previous != null) {
                out.accept(error(context, "duplicate z-level: groups \"" + previous + "\" and \""
                                + group.name() + "\" both claim z=" + value + ".",
                        "merge the two groups or renumber one of them."));
            }
        }

        if (!anyGroup) {
            out.accept(error(context, "map has no z-groups.",
                    "add at least the street-level group z:+0 with terrain/floor/markers sublayers."));
            return;
        }
        if (byZ.isEmpty()) {
            return; // every group name was bad; already reported.
        }
        if (!byZ.containsKey(0)) {
            out.accept(error(context, "street-level group z:+0 is missing (zOrigin).",
                    "every map anchors at z:+0; add the group even if it is nearly empty."));
        }
        int previous = byZ.firstKey();
        for (int z : byZ.keySet()) {
            if (z > previous + 1) {
                out.accept(error(context, "z-levels are not contiguous: gap between z=" + previous
                                + " and z=" + z + ".",
                        "every z-level the map touches needs a group, even if nearly empty; add z:"
                                + (previous + 1 >= 0 ? "+" : "") + (previous + 1) + "."));
            }
            previous = z;
        }
    }

    private ValidationIssue error(MapCheckContext context, String message, String hint) {
        return new ValidationIssue(Severity.ERROR, id(), context.mapName(), "",
                ValidationIssue.NO_COORD, ValidationIssue.NO_COORD, message, hint);
    }
}
