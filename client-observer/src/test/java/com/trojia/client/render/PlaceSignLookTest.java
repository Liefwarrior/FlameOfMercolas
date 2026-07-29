package com.trojia.client.render;

import com.trojia.client.atlas.PlaceholderPngWriter;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The LOOK, proved headlessly (Eli's art ruling is the acceptance criterion, not a nicety):
 * {@link PlaceSignArt}'s quads are painted by {@link PlaceSignRaster} — the same lists the GL
 * renderer hands the batch — and the pixels are asserted directly.
 *
 * <p>What "NES / Zelda II / early Final Fantasy" means, checked one property at a time: the
 * field is pure {@code #000000}; the border is a solid bone ring of exactly
 * {@link PlaceSignArt#BORDER_PX} px on all four sides; the corners are SQUARE (a rounded box
 * would leave the backdrop showing at the corner pixel); the edge is HARD (the pixel one step
 * outside the box is untouched backdrop — no shadow, no glow, no bleed); and every pixel is
 * fully opaque (no translucency anywhere).
 *
 * <p>Each case also writes its canvas to {@code build/place-sign-proof/} so the look can be
 * eyeballed without a GL context.
 */
class PlaceSignLookTest {

    private static final int BLACK = 0xFF000000;
    private static final int BONE = argb(PlaceSignArt.INK_BONE);
    private static final int GOLD = argb(PlaceSignArt.INK_GOLD);

    private static final String PLACE = "The Weighhouse";
    private static final String WHAT = "harbormaster and customs house";

    /** Renders the pop-up as the HUD pass does, at {@code (x, y)} on a 320x140 canvas. */
    private static PlaceSignRaster popUp(float x, float y) {
        PlaceSignRaster raster = new PlaceSignRaster(320, 140);
        float lineHeight = 10f;
        float widest = Math.max(PlaceSignRaster.textWidth(PLACE), PlaceSignRaster.textWidth(WHAT));
        float w = PlaceSignArt.boxWidth(widest);
        float h = PlaceSignArt.boxHeight(lineHeight);
        raster.paint(PlaceSignArt.box(x, y, w, h, PlaceSignArt.INK_BONE));
        float textX = PlaceSignArt.textLeftX(x);
        float lineTop = PlaceSignArt.firstLineTopY(y, h);
        raster.text(PLACE, textX, lineTop, BONE);
        raster.text(WHAT, textX, lineTop - lineHeight, 0xFFA8A490);
        return raster;
    }

    private static float boxW() {
        return PlaceSignArt.boxWidth(
                Math.max(PlaceSignRaster.textWidth(PLACE), PlaceSignRaster.textWidth(WHAT)));
    }

    private static float boxH() {
        return PlaceSignArt.boxHeight(10f);
    }

    @Test
    void theFieldIsTrueBlackAndTheBorderIsACrispBoneRing() throws IOException {
        float x = 20f;
        float y = 20f;
        PlaceSignRaster raster = popUp(x, y);
        write(raster, "box-weighhouse");

        int left = Math.round(x);
        int bottom = Math.round(y);
        int w = Math.round(boxW());
        int h = Math.round(boxH());

        // Border: exactly BORDER_PX of bone on every side, then black.
        for (int i = 0; i < PlaceSignArt.BORDER_PX; i++) {
            assertEquals(BONE, raster.atBatch(left + w / 2, bottom + i), "bottom rail row " + i);
            assertEquals(BONE, raster.atBatch(left + w / 2, bottom + h - 1 - i), "top rail row " + i);
            assertEquals(BONE, raster.atBatch(left + i, bottom + h / 2), "left rail col " + i);
            assertEquals(BONE, raster.atBatch(left + w - 1 - i, bottom + h / 2), "right rail col " + i);
        }
        assertEquals(BLACK, raster.atBatch(left + w / 2, bottom + PlaceSignArt.BORDER_PX),
                "one step inside the bottom rail is the void, not a bevel");
        assertEquals(BLACK, raster.atBatch(left + PlaceSignArt.BORDER_PX, bottom + h / 2),
                "one step inside the left rail is the void");

        // The field between the text lines is genuinely #000000, not a dark grey panel.
        assertEquals(BLACK, raster.atBatch(left + w - PlaceSignArt.BORDER_PX - 2, bottom + h / 2),
                "the right inner margin is pure black");
    }

    @Test
    void theCornersAreSquareAndTheEdgeIsHard() throws IOException {
        float x = 20f;
        float y = 20f;
        PlaceSignRaster raster = popUp(x, y);
        int left = Math.round(x);
        int bottom = Math.round(y);
        int w = Math.round(boxW());
        int h = Math.round(boxH());

        // Square corners: all four corner pixels are border ink. A rounded box would show
        // the backdrop here.
        assertEquals(BONE, raster.atBatch(left, bottom), "SW corner");
        assertEquals(BONE, raster.atBatch(left + w - 1, bottom), "SE corner");
        assertEquals(BONE, raster.atBatch(left, bottom + h - 1), "NW corner");
        assertEquals(BONE, raster.atBatch(left + w - 1, bottom + h - 1), "NE corner");

        // Hard edge: one pixel out on every side is untouched backdrop — no drop shadow, no
        // glow, no soft falloff. This is the whole point of the ruling.
        assertEquals(PlaceSignRaster.BACKDROP, raster.atBatch(left - 1, bottom + h / 2));
        assertEquals(PlaceSignRaster.BACKDROP, raster.atBatch(left + w, bottom + h / 2));
        assertEquals(PlaceSignRaster.BACKDROP, raster.atBatch(left + w / 2, bottom - 1));
        assertEquals(PlaceSignRaster.BACKDROP, raster.atBatch(left + w / 2, bottom + h));
        // Including diagonally out of the corner, where a shadow would land first.
        assertEquals(PlaceSignRaster.BACKDROP, raster.atBatch(left + w, bottom - 1));

        write(raster, "box-edges");
    }

    @Test
    void nothingInTheLabelIsEverTranslucent() {
        PlaceSignRaster raster = popUp(20f, 20f);
        for (int y = 0; y < raster.height(); y++) {
            for (int x = 0; x < raster.width(); x++) {
                assertEquals(0xFF, raster.at(x, y) >>> 24,
                        "pixel (" + x + "," + y + ") must be fully opaque");
            }
        }
    }

    @Test
    void theWordsAreInkedInsideTheField() {
        PlaceSignRaster raster = popUp(20f, 20f);
        int inked = 0;
        int left = Math.round(20f) + PlaceSignArt.BORDER_PX;
        int right = left + Math.round(boxW()) - 2 * PlaceSignArt.BORDER_PX;
        for (int y = 0; y < raster.height(); y++) {
            for (int x = left; x < right; x++) {
                if (raster.at(x, y) == BONE) {
                    inked++;
                }
            }
        }
        assertTrue(inked > 200, "the place line must actually be legible ink, got " + inked);
    }

    @Test
    void aBoxAtTheScreenEdgeIsClampedFullyOnScreen() {
        float w = boxW();
        float h = boxH();
        // A place at the far right of a 320-wide viewport wants to draw off the edge.
        float[] at = PlaceSignArt.clampToViewport(300f, 4f, w, h, 320f, 140f);
        assertTrue(at[0] + w <= 320f - PlaceSignArt.SCREEN_MARGIN_PX + 0.001f,
                "clamped right edge: " + (at[0] + w));
        assertTrue(at[1] >= PlaceSignArt.SCREEN_MARGIN_PX, "clamped bottom: " + at[1]);
        // A box wider than the viewport pins to the margin rather than jumping off-screen.
        float[] tiny = PlaceSignArt.clampToViewport(-50f, -50f, 400f, 400f, 320f, 140f);
        assertEquals(PlaceSignArt.SCREEN_MARGIN_PX, tiny[0]);
        assertEquals(PlaceSignArt.SCREEN_MARGIN_PX, tiny[1]);
    }

    // ------------------------------------------------------------------ the hanging signs

    /** One door tile at canvas (8,8) with the sign hung over it, at the given tile span. */
    private static PlaceSignRaster door(int span, boolean named, int depth) {
        PlaceSignRaster raster = new PlaceSignRaster(span + 16, span + 16);
        float[] ink = PlaceSignArt.INK_BONE;
        if (depth > 0) {
            DepthVision.Shade shade = DepthVision.shade(depth);
            ink = new float[] {ink[0] * shade.r(), ink[1] * shade.g(), ink[2] * shade.b()};
        }
        raster.paint(PlaceSignArt.hangingSign(8f, 8f, span, named, ink));
        return raster;
    }

    @Test
    void theHangingSignIsAPlaqueOfTheSameInkAsTheBox() throws IOException {
        PlaceSignRaster raster = door(48, false, 0);
        write(raster, "sign-plaque-x3");
        assertTrue(countOf(raster, BONE) > 0, "a bone plaque edge and bracket");
        assertTrue(countOf(raster, BLACK) > 0, "a black plaque field — the box in miniature");
        // The plaque hangs BELOW the bracket arm: the arm row carries ink, the row above it
        // is untouched sky.
        assertEquals(PlaceSignRaster.BACKDROP, raster.atBatch(8 + 24, 8f + 48 * 0.95f),
                "nothing is drawn above the bracket");
    }

    @Test
    void theSignStillResolvesAtTheWidestZoom() throws IOException {
        // MapCamera.MIN_ZOOM is 1 -> 16px per tile. The sign must not collapse to a smear.
        PlaceSignRaster raster = door(16, false, 0);
        write(raster, "sign-plaque-x1");
        assertTrue(countOf(raster, BONE) >= 12,
                "at 16px the plaque edge and bracket must still be drawn: "
                        + countOf(raster, BONE));
        assertTrue(countOf(raster, BLACK) >= 4, "and still have a readable black field");
    }

    @Test
    void theNamedSignLightsGoldSoTheReaderKnowsWhichDoor() throws IOException {
        PlaceSignRaster lit = door(48, true, 0);
        write(lit, "sign-plaque-named");
        assertTrue(countOf(lit, GOLD) > 0, "the described door's sign is gold");
        assertEquals(0, countOf(lit, BONE), "and nothing of it stays bone");
        assertEquals(0, countOf(door(48, false, 0), GOLD), "an unnamed sign never lights");
    }

    @Test
    void aSignReadThroughDepthRecedesExactlyLikeTheTerrainUnderIt() throws IOException {
        PlaceSignRaster surface = door(48, false, 0);
        PlaceSignRaster twoDown = door(48, false, 2);
        write(twoDown, "sign-plaque-depth2");
        assertEquals(0, countOf(twoDown, BONE), "no pixel of a below-plane sign is full bone");
        assertTrue(countOf(surface, BONE) > 0);
        // The dim ink is genuinely the shared DepthVision curve, not an ad-hoc alpha.
        DepthVision.Shade shade = DepthVision.shade(2);
        int expected = argb(new float[] {PlaceSignArt.INK_BONE[0] * shade.r(),
                PlaceSignArt.INK_BONE[1] * shade.g(), PlaceSignArt.INK_BONE[2] * shade.b()});
        assertTrue(countOf(twoDown, expected) > 0, "the shared depth shade, applied verbatim");
        assertNotEquals(BONE, expected);
    }

    // ------------------------------------------------------------------------ scaffolding

    private static int countOf(PlaceSignRaster raster, int argb) {
        int count = 0;
        for (int y = 0; y < raster.height(); y++) {
            for (int x = 0; x < raster.width(); x++) {
                if (raster.at(x, y) == argb) {
                    count++;
                }
            }
        }
        return count;
    }

    private static int argb(float[] ink) {
        return 0xFF000000
                | (channel(ink[0]) << 16) | (channel(ink[1]) << 8) | channel(ink[2]);
    }

    private static int channel(float value) {
        return Math.max(0, Math.min(255, Math.round(value * 255f)));
    }

    /** Drops the canvas into {@code build/place-sign-proof/} for eyeballing. */
    private static void write(PlaceSignRaster raster, String name) throws IOException {
        Path dir = Path.of("build", "place-sign-proof");
        Files.createDirectories(dir);
        Files.write(dir.resolve(name + ".png"), PlaceholderPngWriter.encodeArgb(
                raster.pixelsArgb(), raster.width(), raster.height()));
    }

    /** The quad list is stable input: painting it twice paints the same pixels. */
    @Test
    void theLookIsDeterministic() {
        List<PlaceSignArt.Quad> first = PlaceSignArt.box(4f, 4f, 60f, 30f, PlaceSignArt.INK_BONE);
        List<PlaceSignArt.Quad> second = PlaceSignArt.box(4f, 4f, 60f, 30f, PlaceSignArt.INK_BONE);
        assertEquals(first, second);
        PlaceSignRaster a = popUp(20f, 20f);
        PlaceSignRaster b = popUp(20f, 20f);
        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                assertEquals(a.at(x, y), b.at(x, y));
            }
        }
    }
}
