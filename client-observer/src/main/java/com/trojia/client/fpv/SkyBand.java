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
 * ramp would be the only smooth thing on screen. Nothing below the horizon changes: the pack's
 * void colour still shows through wherever no surface resolves, which is the honest answer for
 * "there is nothing there", and it is the same answer the tile view gives.
 *
 * <p>Indoors the sky is simply never seen, and needs no special case: the ceiling — which in
 * this world is the floor slab of the band above — is drawn over it.
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
}
