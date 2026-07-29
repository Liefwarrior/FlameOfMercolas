package com.trojia.client.fpv;

/**
 * <b>How tall a z-band is, in a world whose simulation has no opinion on the matter.</b>
 *
 * <p>The tile grid is 1 tile wide in x and y and exactly one band tall in z, and that is all
 * the sim knows: a band is an integer index, {@code PackedPos.z}, with no height. The top-down
 * view never had to give it one — it draws a band as a flat sheet. A first-person eye cannot
 * dodge the question, so this class answers it, once, for the whole view.
 *
 * <p><b>The ruling.</b> One tile is one shoulder-width of standing room — the occupancy cap is
 * one actor per cell, so that is what a tile physically <em>is</em> in this world. A storey is
 * about {@value #BAND_HEIGHT} shoulder-widths tall, which is where {@link #BAND_HEIGHT} comes
 * from. Taking a tile as roughly 0.9 m that makes a band about 2.5 m floor-to-floor and eye
 * level about 1.5 m — a room you stand up in, a doorway you walk through, a two-storey
 * warehouse that reads as two storeys. Setting the band to one tile-width instead (the naive
 * "a cube is a cube" reading of the grid) puts the ceiling below your knees and turns the whole
 * ward into a crawlspace; that is the single most common way a first-person view over a tile
 * sim is fudged, so the number is written down here with its reasoning rather than left as a
 * literal in a renderer.
 *
 * <p><b>These are rendering constants and nothing else.</b> Nothing in {@code sim-core} reads
 * them, no pathing or reach or sense radius is expressed in them, and changing
 * {@link #BAND_HEIGHT} changes only how tall the ward looks. A camera is a way of looking at
 * the world, not a fact in it.
 *
 * <p>Heights are measured in <b>world units</b>, the same unit as a tile's width, increasing
 * upward, with height 0 at the floor plane of band 0.
 */
public final class BandGeometry {

    /**
     * Height of one z-band in world units (tile widths) — see the class javadoc's ruling.
     * A band's floor slab sits at its base; the band above's floor slab is this cell's
     * ceiling, which is exactly how the tile data models it (there is no CEILING form).
     */
    public static final float BAND_HEIGHT = 2.75f;

    /** Eye height above the floor slab the actor is standing on, in world units. */
    public static final float EYE_HEIGHT = 1.70f;

    /** Height of a standing actor's billboard, in world units (head just under the eyeline
     * of someone a band up looking down at them; taller than the eye so people read as
     * people, shorter than the band so they fit under a ceiling). */
    public static final float ACTOR_HEIGHT = 1.90f;

    private BandGeometry() {
    }

    /** World height of band {@code z}'s floor slab — the surface an actor there stands on. */
    public static float floorHeight(int z) {
        return z * BAND_HEIGHT;
    }

    /** World height of band {@code z}'s ceiling plane, i.e. band {@code z+1}'s floor slab. */
    public static float ceilingHeight(int z) {
        return (z + 1) * BAND_HEIGHT;
    }

    /**
     * World height of a fluid surface in band {@code z} at pooled {@code depth} (1..7): the
     * band's floor plus {@code depth/8} of the band. Depth-7 harbour water therefore sits
     * just under the lip of the quay one band above it, and the bathhouse's depth-2 pool sits
     * ankle-deep on its own floor, from the same one rule.
     */
    public static float fluidSurfaceHeight(int z, int depth) {
        return floorHeight(z) + BAND_HEIGHT * depth / 8f;
    }
}
