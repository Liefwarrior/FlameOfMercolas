package com.trojia.client.render;

/**
 * The depth-aware "what does this empty-air column show?" seam (Sprint 4 EPIC — depth
 * vision for actors, Eli: "have layers below in the z axis be visible with scaled
 * darkening ... also make it work for actors"): given a tile column and the camera's
 * viewed z-level, answers which lower z-level (if any) the air-depth look-down pass
 * resolves that column to. {@link DepthVision} is the world-backed implementation; the
 * interface exists so the consumers — {@code ActorRenderer}'s depth pass,
 * {@code ActorPicker}'s depth fallback, {@code NameplateRenderer}'s hover-through-air —
 * unit-test headless against a synthetic column with no world and no GL.
 *
 * <p>Presentation-only, exactly like the terrain look-down it mirrors: a pure function of
 * the world's tile state at the asked cell, never read by sim-core or the
 * {@code WorldHasher}.
 */
@FunctionalInterface
public interface DepthSight {

    /** Sentinel: the column shows nothing below (occluded at the view z, or all air/too
     * deep) — mirrors {@code WorldRenderer.LOOKDOWN_NONE}. */
    int NONE = -1;

    /**
     * The z' the air column at {@code (tileX, tileY)} resolves to when viewed from
     * {@code viewZ} — the exact cell the terrain look-down pass draws there — or
     * {@link #NONE} when the view-z cell itself draws something (occluded: no look-down
     * happens) or nothing is within {@code WorldRenderer.MAX_LOOKDOWN} levels.
     */
    int visibleBelowZ(int viewZ, int tileX, int tileY);
}
