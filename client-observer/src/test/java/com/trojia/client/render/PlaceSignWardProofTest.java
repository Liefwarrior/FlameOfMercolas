package com.trojia.client.render;

import com.trojia.client.atlas.PlaceholderPngWriter;
import com.trojia.client.boot.PlaceSignsLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.camera.MapCamera;
import com.trojia.sim.world.Coords;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * District-scale proof of the whole label path, headlessly: the REAL places loaded from the
 * committed {@code docks_surface.tmx}, projected through a REAL {@link MapCamera}, planned by
 * the REAL {@link PlaceSignOverlay}, drawn as the REAL {@link PlaceSignArt} quads. The only
 * thing missing versus the shipped frame is the {@code SpriteBatch} and the tile art.
 *
 * <p>Buildings are drawn here as flat masonry blocks straight from each place's own AUTHORED
 * FOOTPRINT — so the geometry under the signs is the ward's real geometry, not a mock-up, but
 * this is a schematic of the district, not a screenshot of it.
 *
 * <p>The canvases written to {@code build/place-sign-proof/} are Eli's own traverse — the quay,
 * mid-Tarwalk, the Mission, the whole district at the widest zoom, and the open harbour. They
 * answer the acceptance question: pan across the Docks having read nothing, and see whether the
 * ward says what it is without burying it.
 */
class PlaceSignWardProofTest {

    /**
     * The quayside walking plane (gazetteer 2.1 band A, authored z:+11) in WORLD z: the
     * loader applies the importer's own {@code CHUNK_SIZE_Z + (z - minZ)} offset, and the
     * district's minZ is 0.
     */
    private static final int VIEW_Z = Coords.CHUNK_SIZE_Z + 11;
    /** The loader's world x/y offsets, likewise the importer's own. */
    private static final int OX = Coords.CHUNK_SIZE_X;
    private static final int OY = Coords.CHUNK_SIZE_Y;
    private static final int VIEWPORT_W = 900;
    private static final int VIEWPORT_H = 560;
    private static final float[] TEXT_PLACE = {0.96f, 0.94f, 0.84f};
    private static final float[] TEXT_WHAT = {0.66f, 0.64f, 0.56f};
    private static final int MASONRY = 0xFF1B2026;
    private static final int MASONRY_EDGE = 0xFF39434E;
    private static final int VOID = 0xFF000000;

    /** Every column is solid at the view z — the whole ward's Band A reads on its own plane. */
    private static final DepthSight SAME_PLANE = (viewZ, x, y) -> DepthSight.NONE;

    private static List<PlaceSign> wardSigns() {
        return PlaceSignsLoader.load(
                RepoPaths.locate("content", "maps", "src", "docks_surface.tmx"));
    }

    /**
     * One frame: camera centred on {@code (centreX, centreY)} at {@code zoom}, cursor parked
     * on {@code (cursorX, cursorY)}.
     */
    private static PlaceSignRaster frame(List<PlaceSign> signs, int centreX, int centreY,
            int zoom, int cursorX, int cursorY, PlaceSignOverlay.Plan[] planOut) {
        return frame(signs, centreX, centreY, zoom, cursorX, cursorY, planOut,
                VIEWPORT_W, VIEWPORT_H);
    }

    private static PlaceSignRaster frame(List<PlaceSign> signs, int centreX, int centreY,
            int zoom, int cursorX, int cursorY, PlaceSignOverlay.Plan[] planOut,
            int viewW, int viewH) {
        MapCamera camera = new MapCamera(16, OX + 192, OY + 128, viewW, viewH);
        camera.setZoom(zoom);
        camera.centerOnTile(centreX, centreY);

        PlaceSignOverlay.Plan plan = PlaceSignOverlay.plan(signs, VIEW_Z,
                camera.visibleTileMinX(), camera.visibleTileMaxX(),
                camera.visibleTileMinY(), camera.visibleTileMaxY(), SAME_PLANE,
                cursorX, cursorY, true, false);
        planOut[0] = plan;

        PlaceSignRaster raster = new PlaceSignRaster(viewW, viewH);
        fill(raster, VOID);
        int span = camera.tileSpanPx();
        // The masonry under the labels: each place's own authored footprint rect.
        for (PlaceSign sign : signs) {
            if (sign.z() != VIEW_Z) {
                continue;
            }
            for (int ty = sign.y0(); ty <= sign.y1(); ty++) {
                for (int tx = sign.x0(); tx <= sign.x1(); tx++) {
                    boolean edge = tx == sign.x0() || tx == sign.x1()
                            || ty == sign.y0() || ty == sign.y1();
                    blockAt(raster, camera, tx, ty, span, edge ? MASONRY_EDGE : MASONRY);
                }
            }
        }
        // The hanging signs, exactly as the scene pass draws them.
        for (PlaceSignOverlay.Marker marker : plan.markers()) {
            float left = camera.tileToScreenX(marker.tileX());
            float bottom = viewH - camera.tileToScreenY(marker.tileY()) - span;
            raster.paint(PlaceSignArt.mark(marker.way(), left, bottom, span, marker.named(),
                    PlaceSignArt.INK_BONE));
        }
        // The one pop-up, exactly as the HUD pass draws it.
        PlaceSignOverlay.Box box = plan.box();
        if (box != null) {
            float widest = Math.max(PlaceSignRaster.textWidth(box.place()),
                    PlaceSignRaster.textWidth(box.what()));
            float w = PlaceSignArt.boxWidth(widest);
            float h = PlaceSignArt.boxHeight();
            float anchorLeft = camera.tileToScreenX(box.anchorTileX());
            float anchorBottom = viewH - camera.tileToScreenY(box.anchorTileY()) - span;
            // The shipped placement, keep-out zones and all: this proof stands in a HUD block
            // top-left and an inspector column right, so what the PNG shows is a real frame.
            float[] at = PlaceSignArt.placeAvoiding(anchorLeft + span / 2f,
                    anchorBottom + span, anchorBottom, w, h, viewW, viewH, hudZones(viewW, viewH));
            raster.paint(PlaceSignArt.box(at[0], at[1], w, h, PlaceSignArt.INK_BONE));
            float textX = PlaceSignArt.textLeftX(at[0]);
            raster.text(box.place(), textX, PlaceSignArt.firstLineTopY(at[1], h), TEXT_PLACE);
            raster.text(box.what(), textX, PlaceSignArt.secondLineTopY(at[1], h), TEXT_WHAT);
        }
        return raster;
    }

    @Test
    void theQuayNamesItsCivicCluster() throws IOException {
        List<PlaceSign> signs = wardSigns();
        PlaceSignOverlay.Plan[] plan = new PlaceSignOverlay.Plan[1];
        // Map tile (64,40) is the Weighhouse's own frontage plaza on the quay.
        PlaceSignRaster raster = frame(signs, OX + 64, OY + 42, 2, OX + 64, OY + 40, plan);
        write(raster, "ward-quay");
        assertNotNull(plan[0].box(), "the cursor is on the Weighhouse frontage");
        assertEquals("The Weighhouse", plan[0].box().place());
        assertTrue(plan[0].markers().size() >= 1,
                "the Weighhouse's own door is marked: " + plan[0].markers().size());
    }

    @Test
    void midTarwalkNamesTheTavernYouArePointingAt() throws IOException {
        List<PlaceSign> signs = wardSigns();
        PlaceSignOverlay.Plan[] plan = new PlaceSignOverlay.Plan[1];
        PlaceSignRaster raster = frame(signs, OX + 105, OY + 40, 2, OX + 105, OY + 38, plan);
        write(raster, "ward-tarwalk");
        assertEquals("The Bilge", plan[0].box().place());
        assertEquals("sailor dive on the Tarwalk", plan[0].box().what());
    }

    @Test
    void theMissionNamesItselfAndOnlyItself() throws IOException {
        List<PlaceSign> signs = wardSigns();
        PlaceSignOverlay.Plan[] plan = new PlaceSignOverlay.Plan[1];
        PlaceSignRaster raster = frame(signs, OX + 90, OY + 73, 2, OX + 88, OY + 71, plan);
        write(raster, "ward-mission");
        assertEquals("Mission of the Flame", plan[0].box().place());
        assertEquals("Divine Light almshouse", plan[0].box().what());
        assertEquals(1, plan[0].markers().stream().filter(PlaceSignOverlay.Marker::named).count(),
                "one lit door, and only one");
    }

    @Test
    void theWidestZoomOverTheWholeWardStillShowsExactlyOneBox() throws IOException {
        List<PlaceSign> signs = wardSigns();
        PlaceSignOverlay.Plan[] plan = new PlaceSignOverlay.Plan[1];
        // Zoom 1 on a canvas wide enough for most of the 192x128 district at once: the
        // densest thing the clutter rule ever faces.
        PlaceSignRaster raster = frame(signs, OX + 96, OY + 64, 1, OX + 88, OY + 71, plan,
                1600, 1100);
        write(raster, "ward-widest-zoom");
        assertTrue(plan[0].markers().size() >= 15,
                "most of the ward's doors are on screen at once: " + plan[0].markers().size());
        assertNotNull(plan[0].box());
        assertEquals("Mission of the Flame", plan[0].box().place(),
                "still exactly one box, and it is the one under the cursor");
    }

    @Test
    void theOpenHarbourSaysNothing() throws IOException {
        List<PlaceSign> signs = wardSigns();
        PlaceSignOverlay.Plan[] plan = new PlaceSignOverlay.Plan[1];
        // Out on the water east of the fishbone, clear of every footprint.
        PlaceSignRaster raster = frame(signs, OX + 150, OY + 8, 2, OX + 150, OY + 4, plan);
        write(raster, "ward-open-water");
        assertEquals(null, plan[0].box(), "pointing at open water names nothing");
    }


    // ------------------------------------------------------------ the per-place sweep

    /**
     * THE regression pin (S8 round 2, Eli): "POINTING AT A PLACE'S OWN SIGN NAMES A DIFFERENT
     * PLACE ... the one gesture a player will certainly make - point at the sign - returns a
     * lie." Six of round 1's thirty-nine plaques did exactly that, and round 1's ward proof
     * sampled points that narrowly missed it. So this walks EVERY signed place in the
     * committed map, points at its own mark cell in BOTH modes, and asserts the box names it.
     * A single misnamed place fails the build with the neighbour it lied about.
     */
    @Test
    void everySignedPlaceNamesItselfWhenYouPointAtItsOwnMark() {
        List<PlaceSign> signs = wardSigns();
        assertTrue(signs.size() >= 79, "the whole ward roster: " + signs.size());
        List<String> lies = new java.util.ArrayList<>();
        for (PlaceSign sign : signs) {
            for (boolean playing : new boolean[] {false, true}) {
                PlaceSignOverlay.Plan plan = PlaceSignOverlay.plan(signs, sign.z(),
                        0, 4096, 0, 4096, SAME_PLANE,
                        sign.doorX(), sign.doorY(), true, playing);
                String said = plan.box() == null ? "(silence)" : plan.box().place();
                if (!sign.place().equals(said)) {
                    lies.add((playing ? "playing" : "observing") + " at ("
                            + sign.doorX() + "," + sign.doorY() + ",z" + sign.z() + ") "
                            + sign.place() + " -> " + said);
                }
            }
        }
        assertEquals(List.of(), lies, "a place's own mark must name that place");
    }

    /**
     * The same sweep one step further out: standing ON a building's footprint (its door cell
     * stepped inside the threshold) must never name a neighbour either. Observer mode, where
     * the floor is what a building speaks from.
     */
    @Test
    void everyBuildingNamesItselfFromInsideItsOwnFootprint() {
        List<PlaceSign> signs = wardSigns();
        List<String> lies = new java.util.ArrayList<>();
        for (PlaceSign sign : signs) {
            if (sign.kind() != PlaceSign.Kind.DOOR) {
                continue;   // a way's run is deliberately shared with what stands on it
            }
            int cx = (sign.x0() + sign.x1()) / 2;
            int cy = (sign.y0() + sign.y1()) / 2;
            PlaceSignOverlay.Plan plan = PlaceSignOverlay.plan(signs, sign.z(),
                    0, 4096, 0, 4096, SAME_PLANE, cx, cy, true, false);
            String said = plan.box() == null ? "(silence)" : plan.box().place();
            if (!sign.place().equals(said)) {
                lies.add(sign.place() + " at its own centre (" + cx + "," + cy + ") -> " + said);
            }
        }
        assertEquals(List.of(), lies, "a building must name itself from its own middle");
    }

    /**
     * The crossing Eli asked about, walked as an ACTOR from the quay to the Mission: every
     * step down Saltgate Rise names the road, the doorways name themselves, and nothing in
     * between is anonymous. Play mode throughout - this is a walk, not a pan.
     */
    @Test
    void walkingFromTheQuayToTheMissionTheWardNamesItselfTheWholeWay() throws IOException {
        List<PlaceSign> signs = wardSigns();
        record Step(int x, int y, String expected) {
        }
        List<Step> crossing = List.of(
                new Step(30, 27, "The Long Quay"),
                new Step(64, 30, "Tarwalk"),
                new Step(75, 30, "Saltgate Rise"),
                new Step(75, 45, "Saltgate Rise"),
                new Step(75, 62, "Saltgate Rise"),
                new Step(88, 65, "Mission of the Flame"));
        List<String> said = new java.util.ArrayList<>();
        for (Step step : crossing) {
            PlaceSignOverlay.Plan plan = PlaceSignOverlay.plan(signs, VIEW_Z,
                    0, 4096, 0, 4096, SAME_PLANE,
                    OX + step.x(), OY + step.y(), true, true);
            said.add(plan.box() == null ? "(silence)" : plan.box().place());
        }
        assertEquals(crossing.stream().map(Step::expected).toList(), said,
                "the crossing from the quay to the Mission");

        PlaceSignOverlay.Plan[] plan = new PlaceSignOverlay.Plan[1];
        write(frame(signs, OX + 75, OY + 40, 2, OX + 75, OY + 45, plan), "ward-saltgate-rise");
        assertEquals("Saltgate Rise", plan[0].box().place());
    }

    /** A pan of the waterfront: the piers say what they are, fingers included. */
    @Test
    void theFishbonePierAndItsFingersNameThemselves() throws IOException {
        List<PlaceSign> signs = wardSigns();
        PlaceSignOverlay.Plan[] plan = new PlaceSignOverlay.Plan[1];
        PlaceSignRaster raster = frame(signs, OX + 75, OY + 14, 2, OX + 75, OY + 12, plan);
        write(raster, "ward-fishbone");
        assertEquals("The Fishbone Pier", plan[0].box().place());
        assertTrue(plan[0].markers().stream().anyMatch(PlaceSignOverlay.Marker::way),
                "the ward's fingerposts are on screen");

        PlaceSignOverlay.Plan[] finger = new PlaceSignOverlay.Plan[1];
        frame(signs, OX + 70, OY + 17, 2, OX + 66, OY + 17, finger);
        assertEquals("Second Finger", finger[0].box().place());
    }

    // ------------------------------------------------------------------------ scaffolding

    /**
     * The UI a real frame already has on screen when the pop-up lands: the status/pulse/purse
     * block top-left and the inspector's sheet column right. The box must dodge both.
     */
    private static List<PlaceSignArt.Rect> hudZones(int viewW, int viewH) {
        return List.of(new PlaceSignArt.Rect(2f, viewH - 90f, 260f, 88f),
                new PlaceSignArt.Rect(viewW - 442f, viewH - 320f, 442f, 318f));
    }

    private static void blockAt(PlaceSignRaster raster, MapCamera camera, int tileX, int tileY,
            int span, int argb) {
        int left = camera.tileToScreenX(tileX);
        int top = camera.tileToScreenY(tileY);
        for (int y = top; y < top + span; y++) {
            for (int x = left; x < left + span; x++) {
                if (x >= 0 && x < raster.width() && y >= 0 && y < raster.height()) {
                    raster.setAt(x, y, argb);
                }
            }
        }
    }

    private static void fill(PlaceSignRaster raster, int argb) {
        for (int y = 0; y < raster.height(); y++) {
            for (int x = 0; x < raster.width(); x++) {
                raster.setAt(x, y, argb);
            }
        }
    }

    private static void write(PlaceSignRaster raster, String name) throws IOException {
        Path dir = Path.of("build", "place-sign-proof");
        Files.createDirectories(dir);
        Files.write(dir.resolve(name + ".png"), PlaceholderPngWriter.encodeArgb(
                raster.pixelsArgb(), raster.width(), raster.height()));
    }
}
