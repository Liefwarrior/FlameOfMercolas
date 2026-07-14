package com.trojia.client.face;

import java.util.Locale;

/**
 * Hair-color classes for FaceGen's k=8 global class draw (unified art spec §4.3). A part
 * carrying any {@code hair_*} tag is eligible iff it matches the drawn class; untagged
 * parts are always eligible. Applies to {@code face_hair}, {@code face_brow} and bearded
 * {@code face_mouth} variants.
 *
 * <p><b>Declaration order and weights are pinned</b> (placeholder pending bless; RED rare —
 * canon: red hair is remarkable, Gabri hides his). The pick iterates {@link #values()} with
 * integer cumulative weights, so both the order and the weights are part of every
 * generated face's identity.
 */
public enum HairColor {
    BLACK(5),
    BROWN(6),
    GREY(4),
    WHITE(2),
    RED(1);

    private final int weight;

    HairColor(int weight) {
        this.weight = weight;
    }

    /** This class's slice of the global weight table. */
    public int weight() {
        return weight;
    }

    /** The sprite tag this class filters color-gated pools by. */
    public String hairTag() {
        return "hair_" + name().toLowerCase(Locale.ROOT);
    }
}
