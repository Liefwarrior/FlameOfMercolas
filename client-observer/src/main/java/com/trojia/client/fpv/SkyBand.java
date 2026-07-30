package com.trojia.client.fpv;

import com.trojia.client.render.AmbientLight;

/**
 * What is overhead when nothing is overhead.
 *
 * <p>The tile view never had to answer this. An unresolved cell there is painted the art
 * pack's {@code voidColor} — true black in the shipped Kenney pack — and that reads correctly
 * from above, as "nothing authored here". Read the same colour along the top half of a
 * first-person frame and it reads as a rendering bug: you are standing in a street looking at
 * a wall of black where the sky should be.
 *
 * <p>So the first-person view declares a sky: a flat band above the horizon, in a muted slate
 * that carries the frame's own {@link AmbientLight} so it darkens with the ward at dusk and
 * warms with it at dawn. Flat, not a gradient — this is a 16px pixel-art pack and a smooth
 * ramp would be the only smooth thing on screen.
 *
 * <h2>And what is underfoot when nothing is underfoot</h2>
 *
 * <p>Leaving the below-horizon half to the pack's void black was wrong for exactly the same
 * reason, and it showed. The plan reaches {@link FirstPersonPlanner#DRAW_RADIUS_TILES} tiles
 * and no farther; a ground plane below the eye projects nearer and nearer the horizon line the
 * farther out it goes and never reaches it, so between the last drawn tile and the horizon
 * there is always a band of frame with no surface in it. Standing on the quay looking out over
 * the harbour — the view this whole feature exists for — that band was a stripe of pure black
 * sitting at eye level right where the water should meet the sky.
 *
 * <p>So there is a {@link #groundHazeAt ground haze} too: the same slate, taken down and
 * slightly warmed, painted below the horizon before anything else. It is the honest colour for
 * "the ward keeps going and this view stops here", it is what a translucent water surface at
 * the edge of the draw radius now composites against instead of black, and between the two
 * bands <b>every pixel of a first-person frame is painted before a single quad is drawn</b>.
 *
 * <p>Indoors neither band is seen, and neither needs a special case: the ceiling — which in
 * this world is the floor slab of the band above — and the floor are drawn over them.
 *
 * <p>Pure colour arithmetic; unit-testable, no GL.
 */
public final class SkyBand {

    /**
     * Daylight sky, {@code 0xRRGGBB}. Deliberately desaturated and a little dirty: this is a
     * working harbour under the DF-black art pack, and a bright blue would be the loudest
     * thing in the frame.
     */
    public static final int DAY_SKY_RGB = 0x6B7784;

    /**
     * How much of the sky's brightness the ground haze keeps. Distance haze takes the colour
     * of the sky it is under; ground is darker than sky because it is lit by it rather than
     * being it, and this is enough darker that the horizon line reads as a horizon rather than
     * as one flat field.
     */
    public static final float GROUND_HAZE_LEVEL = 0.62f;

    /** Warm bias on the haze's red, so the far ward is earth-under-slate, not more slate. */
    public static final float GROUND_HAZE_WARMTH = 1.14f;

    /** One resolved sky colour, channels in {@code [0, 1]}. */
    public record Rgb(float r, float g, float b) {
    }

    private SkyBand() {
    }

    /**
     * The sky under a frame's ambient light: the daylight slate multiplied by the ambient,
     * which is exactly how every other scene draw in this client takes the day/night cycle.
     * At {@link AmbientLight#NEUTRAL} it is the flat daylight colour.
     */
    public static Rgb colorAt(AmbientLight ambient) {
        float r = ((DAY_SKY_RGB >> 16) & 0xFF) / 255f;
        float g = ((DAY_SKY_RGB >> 8) & 0xFF) / 255f;
        float b = (DAY_SKY_RGB & 0xFF) / 255f;
        return new Rgb(r * ambient.r(), g * ambient.g(), b * ambient.b());
    }

    /**
     * The backdrop for everything below the horizon: {@link #colorAt} taken down to
     * {@link #GROUND_HAZE_LEVEL} and warmed. Darker than the sky at every hour and never
     * black, so the seam where the draw radius runs out reads as distance rather than as a
     * hole in the renderer.
     */
    public static Rgb groundHazeAt(AmbientLight ambient) {
        Rgb sky = colorAt(ambient);
        float r = Math.min(1f, sky.r() * GROUND_HAZE_LEVEL * GROUND_HAZE_WARMTH);
        return new Rgb(r, sky.g() * GROUND_HAZE_LEVEL, sky.b() * GROUND_HAZE_LEVEL);
    }
}
