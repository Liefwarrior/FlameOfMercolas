package com.trojia.client.face;

import java.util.Locale;

/**
 * Headwear classes for FaceGen's k=0 class draw (FACES-SPEC §3.2, set retained by the
 * unified art spec §4.3). The class is drawn once from the archetype's
 * {@code headwearWeights} and gates the whole composition: {@code BARE} composes the hair
 * pick; every other class composes a {@code face_headwear} part carrying the matching
 * {@code hw_*} tag instead (pixel occlusion replaced the ASCII era's per-band {@code gear:}
 * compatibility lists).
 *
 * <p><b>Declaration order is pinned</b> — the weighted class pick iterates
 * {@link #values()} with integer cumulative weights, so reordering would silently reroll
 * every generated face (the same append-only discipline as the draw-index map).
 */
public enum HeadwearClass {
    BARE,
    HOOD,
    OPEN_HELM,
    CLOSED_HELM,
    COIF,
    COWL;

    /** The sprite tag a non-{@code BARE} class filters {@code face_headwear} pools by. */
    public String hwTag() {
        return "hw_" + name().toLowerCase(Locale.ROOT);
    }
}
