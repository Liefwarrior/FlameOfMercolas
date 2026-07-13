package com.trojia.client.camera;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link MapCamera} pure-math contracts: clamping, integer-snapped origin,
 * tile&lt;-&gt;screen roundtrips, Q8 sub-pixel panning, zoom-to-cursor. Headless — the
 * class has no libGDX dependency at all.
 */
class MapCameraTest {

    private static final int TILE = 16;

    /** 100x80 tiles = 1600x1280 world px, larger than the 640x480 viewport. */
    private static MapCamera bigWorld() {
        return new MapCamera(TILE, 100, 80, 640, 480);
    }

    /** 400x400 tiles = 6400x6400 world px; roomy enough that clamps never bite. */
    private static MapCamera hugeWorld() {
        return new MapCamera(TILE, 400, 400, 640, 480);
    }

    @Test
    void constructorRejectsNonPositiveArguments() {
        assertThrows(IllegalArgumentException.class, () -> new MapCamera(0, 10, 10, 640, 480));
        assertThrows(IllegalArgumentException.class, () -> new MapCamera(TILE, 0, 10, 640, 480));
        assertThrows(IllegalArgumentException.class, () -> new MapCamera(TILE, 10, 0, 640, 480));
        assertThrows(IllegalArgumentException.class, () -> new MapCamera(TILE, 10, 10, 0, 480));
        assertThrows(IllegalArgumentException.class, () -> new MapCamera(TILE, 10, 10, 640, 0));
    }

    @Test
    void startsCenteredOnTheWorldMiddle() {
        MapCamera camera = bigWorld();
        // Center (800, 640) world px -> origin (320-800, 240-640).
        assertEquals(-480, camera.renderOriginX());
        assertEquals(-400, camera.renderOriginY());
    }

    @Test
    void tileScreenRoundtripIsExactForEveryColumn() {
        MapCamera camera = bigWorld();
        for (int t = 0; t < camera.worldWidthTiles(); t++) {
            assertEquals(t, camera.screenToTileX(camera.tileToScreenX(t)));
        }
        for (int t = 0; t < camera.worldHeightTiles(); t++) {
            assertEquals(t, camera.screenToTileY(camera.tileToScreenY(t)));
        }
    }

    @Test
    void screenToTileUsesFloorDivisionLeftOfTheWorld() {
        MapCamera camera = bigWorld();
        camera.pan(-100_000, -100_000); // clamp to the top-left edge: origin becomes (0, 0)
        assertEquals(0, camera.renderOriginX());
        assertEquals(0, camera.renderOriginY());
        assertEquals(0, camera.screenToTileX(0));
        assertEquals(-1, camera.screenToTileX(-1));
        assertFalse(camera.isInWorld(camera.screenToTileX(-1), 0));
        assertTrue(camera.isInWorld(0, 0));
    }

    @Test
    void panMovesTheViewAndClampsAtWorldEdges() {
        MapCamera camera = bigWorld();
        camera.pan(100, 0);
        assertEquals(-580, camera.renderOriginX());

        camera.pan(1_000_000, 0);
        // Clamped: centerX = 1600 - 320; the last column's right edge meets the
        // viewport's right edge exactly.
        assertEquals(640, camera.tileToScreenX(99) + camera.tileSpanPx());
        assertEquals(99, camera.visibleTileMaxX());
        assertEquals(60, camera.visibleTileMinX());
    }

    @Test
    void worldsNarrowerThanTheViewportStayCenteredAndIgnorePans() {
        MapCamera camera = new MapCamera(TILE, 10, 10, 640, 480);
        assertEquals(240, camera.renderOriginX()); // 320 - 160/2
        assertEquals(160, camera.renderOriginY()); // 240 - 160/2
        camera.pan(500, -500);
        assertEquals(240, camera.renderOriginX());
        assertEquals(160, camera.renderOriginY());
        assertEquals(0, camera.visibleTileMinX());
        assertEquals(9, camera.visibleTileMaxX());
    }

    @Test
    void zoomIsClampedToTheLegalRange() {
        MapCamera camera = hugeWorld();
        assertEquals(MapCamera.MIN_ZOOM, camera.setZoom(0));
        assertEquals(MapCamera.MIN_ZOOM, camera.setZoom(-3));
        assertEquals(MapCamera.MAX_ZOOM, camera.setZoom(99));
        camera.setZoom(MapCamera.MAX_ZOOM);
        assertEquals(MapCamera.MAX_ZOOM, camera.zoomIn()); // saturates
        camera.setZoom(MapCamera.MIN_ZOOM);
        assertEquals(MapCamera.MIN_ZOOM, camera.zoomOut()); // saturates
    }

    @Test
    void setZoomKeepsTheCameraCenterFixed() {
        MapCamera camera = hugeWorld();
        camera.centerOnTile(100, 100);
        camera.setZoom(2);
        // Tile (100,100)'s center must still sit at the viewport center.
        int tileCenterX = camera.tileToScreenX(100) + camera.tileSpanPx() / 2;
        int tileCenterY = camera.tileToScreenY(100) + camera.tileSpanPx() / 2;
        assertEquals(320, tileCenterX);
        assertEquals(240, tileCenterY);
    }

    @Test
    void zoomAtKeepsTheWorldPointUnderTheCursor() {
        MapCamera camera = hugeWorld();
        int cursorX = 500;
        int cursorY = 100;
        int tileBeforeX = camera.screenToTileX(cursorX);
        int tileBeforeY = camera.screenToTileY(cursorY);
        camera.zoomAt(cursorX, cursorY, 2);
        assertEquals(2, camera.zoom());
        assertEquals(tileBeforeX, camera.screenToTileX(cursorX));
        assertEquals(tileBeforeY, camera.screenToTileY(cursorY));
        camera.zoomAt(cursorX, cursorY, 4);
        assertEquals(tileBeforeX, camera.screenToTileX(cursorX));
        assertEquals(tileBeforeY, camera.screenToTileY(cursorY));
    }

    @Test
    void subPixelPansAccumulateInQ8AtHighZoom() {
        MapCamera camera = hugeWorld();
        camera.setZoom(8);
        int originBefore = camera.renderOriginX();
        for (int i = 0; i < 8; i++) {
            camera.pan(1, 0); // each = 1/8 world px; must not be lost to rounding
        }
        assertEquals(originBefore - 8, camera.renderOriginX());
    }

    @Test
    void tileGridStaysExactlyAlignedAfterFractionalPans() {
        MapCamera camera = hugeWorld();
        camera.setZoom(3);
        camera.pan(1, 1);
        camera.pan(1, 1); // center now off any 1/3 boundary
        int span = camera.tileSpanPx();
        assertEquals(3 * TILE, span);
        for (int t = 50; t < 60; t++) {
            assertEquals(span, camera.tileToScreenX(t + 1) - camera.tileToScreenX(t));
            assertEquals(span, camera.tileToScreenY(t + 1) - camera.tileToScreenY(t));
        }
    }

    @Test
    void centerOnTileClampsIntoBounds() {
        MapCamera camera = bigWorld();
        camera.centerOnTile(0, 0);
        assertEquals(0, camera.renderOriginX());
        assertEquals(0, camera.renderOriginY());
        assertEquals(0, camera.visibleTileMinX());
        assertEquals(39, camera.visibleTileMaxX()); // 640 / 16 = 40 columns visible
    }

    @Test
    void viewportResizeReclamps() {
        MapCamera camera = bigWorld();
        camera.centerOnTile(0, 0); // pinned to the top-left edge
        camera.setViewport(1600, 1280); // viewport now covers the whole world
        // World must be re-centered (here: exactly filling the viewport).
        assertEquals(0, camera.renderOriginX());
        assertEquals(0, camera.renderOriginY());
        assertEquals(99, camera.visibleTileMaxX());
        assertThrows(IllegalArgumentException.class, () -> camera.setViewport(0, 5));
    }

    @Test
    void visibleRangeCoversExactlyTheViewport() {
        MapCamera camera = bigWorld();
        int span = camera.tileSpanPx();
        // Every visible tile's rect intersects the viewport; neighbors outside don't.
        assertTrue(camera.tileToScreenX(camera.visibleTileMinX()) + span > 0);
        assertTrue(camera.tileToScreenX(camera.visibleTileMaxX()) < camera.viewportWidthPx());
        if (camera.visibleTileMinX() > 0) {
            assertTrue(camera.tileToScreenX(camera.visibleTileMinX() - 1) + span <= 0);
        }
        if (camera.visibleTileMaxX() < camera.worldWidthTiles() - 1) {
            assertTrue(camera.tileToScreenX(camera.visibleTileMaxX() + 1)
                    >= camera.viewportWidthPx());
        }
    }
}
