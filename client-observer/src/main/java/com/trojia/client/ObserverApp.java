package com.trojia.client;

import com.badlogic.gdx.ApplicationAdapter;
import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.math.Matrix4;
import com.badlogic.gdx.utils.ScreenUtils;
import com.trojia.client.art.JsonTileArtResolver;
import com.trojia.client.atlas.PlaceholderAtlas;
import com.trojia.client.atlas.PlaceholderAtlasFactory;
import com.trojia.client.atlas.PlaceholderSheetRaster;
import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.hud.HudText;
import com.trojia.client.input.CameraInput;
import com.trojia.client.render.WorldRenderer;
import com.trojia.client.world.ZLevelCursor;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.WorldConfig;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * The observer application. M0 was an empty window; M1 boots the baked tavern fixture
 * world (see {@link FixtureWorldLoader}) and renders its currently selected z-level as
 * atlas tiles under {@link MapCamera}, navigable via {@link CameraInput}. {@code --smoke=N}
 * still renders exactly N frames then exits (see {@link ObserverLauncher}).
 */
public final class ObserverApp extends ApplicationAdapter {

    private final int smokeFrames;
    private int framesRendered;

    private MapCamera camera;
    private ZLevelCursor zLevel;
    private PlaceholderAtlas atlas;
    private WorldRenderer renderer;
    private SpriteBatch batch;
    private BitmapFont font;
    private final Matrix4 projection = new Matrix4();

    public ObserverApp(int smokeFrames) {
        this.smokeFrames = smokeFrames;
    }

    @Override
    public void create() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadTavern();
        TickableWorld world = loaded.world();

        WorldConfig config = world.config();
        int worldWidthTiles = config.chunksX() * Coords.CHUNK_SIZE_X;
        int worldHeightTiles = config.chunksY() * Coords.CHUNK_SIZE_Y;
        int worldZTiles = config.chunksZ() * Coords.CHUNK_SIZE_Z;
        System.out.println("observer: loaded tavern world " + worldWidthTiles + "x"
                + worldHeightTiles + "x" + worldZTiles + " tiles (chunks "
                + config.chunksX() + "x" + config.chunksY() + "x" + config.chunksZ() + ")");

        String mappingJson = readArtMapping();
        JsonTileArtResolver artResolver = JsonTileArtResolver.parse(mappingJson);
        PlaceholderSheetRaster raster = PlaceholderAtlasFactory.buildRaster(mappingJson);
        this.atlas = PlaceholderAtlasFactory.create(raster);

        this.camera = new MapCamera(JsonTileArtResolver.TILE_PX, worldWidthTiles, worldHeightTiles,
                Gdx.graphics.getWidth(), Gdx.graphics.getHeight());
        this.zLevel = new ZLevelCursor(0, worldZTiles - 1, FixtureWorldLoader.TAVERN_STREET_LEVEL_Z);
        this.renderer = new WorldRenderer(world, loaded.materials(), artResolver, atlas);

        this.batch = new SpriteBatch();
        this.font = new BitmapFont();
    }

    @Override
    public void resize(int width, int height) {
        if (camera != null) {
            camera.setViewport(width, height);
        }
    }

    @Override
    public void render() {
        ScreenUtils.clear(0.055f, 0.05f, 0.08f, 1f);

        CameraInput.poll(camera, zLevel, Gdx.graphics.getDeltaTime());

        projection.setToOrtho2D(0, 0, camera.viewportWidthPx(), camera.viewportHeightPx());
        batch.setProjectionMatrix(projection);
        batch.begin();
        renderer.draw(batch, camera, zLevel.z());
        font.draw(batch, HudText.describe(zLevel.z(), camera.zoom()), 8,
                camera.viewportHeightPx() - 8);
        batch.end();

        framesRendered++;
        boolean smokeDone = smokeFrames > 0 && framesRendered >= smokeFrames;
        if (smokeDone || Gdx.input.isKeyJustPressed(Input.Keys.ESCAPE)) {
            if (smokeDone) {
                System.out.println("observer smoke test: rendered " + framesRendered + " frames OK");
            }
            Gdx.app.exit();
        }
    }

    @Override
    public void dispose() {
        if (atlas != null) {
            atlas.dispose();
        }
        if (batch != null) {
            batch.dispose();
        }
        if (font != null) {
            font.dispose();
        }
    }

    private static String readArtMapping() {
        try {
            return Files.readString(
                    RepoPaths.locate("content", "art", "placeholder", "art-mapping.json"),
                    StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new UncheckedIOException("failed to read art-mapping.json", e);
        }
    }
}
