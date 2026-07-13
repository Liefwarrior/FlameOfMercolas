package com.trojia.client;

import com.badlogic.gdx.ApplicationAdapter;
import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.badlogic.gdx.files.FileHandle;
import com.badlogic.gdx.graphics.Pixmap;
import com.badlogic.gdx.graphics.PixmapIO;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.math.Matrix4;
import com.badlogic.gdx.utils.ScreenUtils;
import com.trojia.client.art.JsonTileArtResolver;
import com.trojia.client.atlas.SheetAtlasSpec;
import com.trojia.client.atlas.SheetTileAtlas;
import com.trojia.client.atlas.TileAtlas;
import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.hud.HudText;
import com.trojia.client.input.CameraInput;
import com.trojia.client.input.InspectorInput;
import com.trojia.client.input.TimeControlInput;
import com.trojia.client.inspect.EventLog;
import com.trojia.client.inspect.EventLogTracker;
import com.trojia.client.inspect.InspectorState;
import com.trojia.client.render.ActorRenderer;
import com.trojia.client.render.InspectorRenderer;
import com.trojia.client.render.WorldRenderer;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.time.SimulationDriver;
import com.trojia.client.time.SpeedSetting;
import com.trojia.client.world.ZLevelCursor;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.engine.TickClock;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.WorldConfig;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Locale;

/**
 * The observer application. M0 was an empty window; M1 boots a baked fixture world (see
 * {@link FixtureWorldLoader}) and renders its currently selected z-level as atlas tiles
 * under {@link MapCamera}, navigable via {@link CameraInput}, its time advanced by a
 * {@link SimulationDriver} paced per frame by {@link TimeControlInput}.
 *
 * <p>Two fixtures boot through the same pipeline (selected by {@link Fixture}, wired by
 * {@link ObserverLauncher} from {@code --fixture=}): the system-less {@link Fixture#TAVERN}
 * walk-through, and {@link Fixture#COMPOUND}, which additionally spawns the wealth-stratified
 * {@link CompoundBlockPopulation} into an {@code ActorsSystem} the driver ticks, and draws
 * that population over the tiles via an {@link ActorRenderer} that reads live positions every
 * frame. {@code --smoke=N} renders exactly N frames then exits (see {@link ObserverLauncher});
 * on the compound fixture a smoke run forces {@link SpeedSetting#FAST} so the population
 * visibly advances (the shipped default stays {@link SpeedSetting#PAUSED}).
 */
public final class ObserverApp extends ApplicationAdapter {

    /** Which baked fixture the observer boots. */
    public enum Fixture { TAVERN, COMPOUND }

    /** Sentinel for {@code --debug-select}: no forced selection (the shipped default). */
    private static final int NO_DEBUG_SELECT = Actor.NONE;

    private final Fixture fixture;
    private final int smokeFrames;
    private final String screenshotPath;
    private final int debugSelectActorId;
    private int framesRendered;

    private MapCamera camera;
    private ZLevelCursor zLevel;
    private TileAtlas atlas;
    private WorldRenderer renderer;
    private SimulationDriver driver;
    private SpriteBatch batch;
    private BitmapFont font;
    private final Matrix4 projection = new Matrix4();

    // Compound fixture only (null for the tavern):
    private ActorRenderer actorRenderer;
    private BitmapFont actorFont;
    private CompoundBlockPopulation population;
    private InspectorState inspector;
    private EventLog eventLog;
    private EventLogTracker eventLogTracker;
    private InspectorRenderer inspectorRenderer;

    public ObserverApp(int smokeFrames) {
        this(Fixture.TAVERN, smokeFrames);
    }

    public ObserverApp(Fixture fixture, int smokeFrames) {
        this(fixture, smokeFrames, null);
    }

    public ObserverApp(Fixture fixture, int smokeFrames, String screenshotPath) {
        this(fixture, smokeFrames, screenshotPath, NO_DEBUG_SELECT);
    }

    /**
     * @param screenshotPath      if non-null, a PNG of the final smoke frame is written
     *                            here right before exit (debug/verification aid only —
     *                            never used on the shipped interactive path).
     * @param debugSelectActorId  if &ge; 0 (and the compound fixture is loaded), force-
     *                            selects this actor at boot, bypassing the mouse — the
     *                            headless proof seam for the selection panel + follow, since
     *                            the {@code --smoke} path has no cursor to click with.
     */
    public ObserverApp(Fixture fixture, int smokeFrames, String screenshotPath,
            int debugSelectActorId) {
        this.fixture = fixture;
        this.smokeFrames = smokeFrames;
        this.screenshotPath = screenshotPath;
        this.debugSelectActorId = debugSelectActorId;
    }

    @Override
    public void create() {
        boolean compound = fixture == Fixture.COMPOUND;
        FixtureWorldLoader.Loaded loaded =
                compound ? FixtureWorldLoader.loadCompoundBlock() : FixtureWorldLoader.loadTavern();
        TickableWorld world = loaded.world();

        WorldConfig config = world.config();
        int worldWidthTiles = config.chunksX() * Coords.CHUNK_SIZE_X;
        int worldHeightTiles = config.chunksY() * Coords.CHUNK_SIZE_Y;
        int worldZTiles = config.chunksZ() * Coords.CHUNK_SIZE_Z;
        System.out.println("observer: loaded " + fixture.name().toLowerCase(Locale.ROOT)
                + " world " + worldWidthTiles + "x" + worldHeightTiles + "x" + worldZTiles
                + " tiles (chunks " + config.chunksX() + "x" + config.chunksY() + "x"
                + config.chunksZ() + ")");

        String mappingJson = readArtMapping();
        JsonTileArtResolver artResolver = JsonTileArtResolver.parse(mappingJson);
        SheetAtlasSpec sheetSpec = SheetAtlasSpec.parse(mappingJson);
        // Boot fails loudly if any byAppearance/fluid region name has no cell in the sheet
        // (TILE-ART-SPEC section 7.2), rather than silently drawing the wrong tile.
        sheetSpec.validateReferenced(artResolver.referencedRegionNames());
        Path sheetFile = RepoPaths.locate("content").resolve(sheetSpec.sheetPath());
        this.atlas = SheetTileAtlas.create(sheetSpec,
                Gdx.files.absolute(sheetFile.toAbsolutePath().toString()));

        this.camera = new MapCamera(JsonTileArtResolver.TILE_PX, worldWidthTiles, worldHeightTiles,
                Gdx.graphics.getWidth(), Gdx.graphics.getHeight());
        int streetLevelZ = compound
                ? FixtureWorldLoader.COMPOUND_GROUND_LEVEL_Z : FixtureWorldLoader.TAVERN_STREET_LEVEL_Z;
        this.zLevel = new ZLevelCursor(0, worldZTiles - 1, streetLevelZ);
        this.renderer = new WorldRenderer(world, loaded.materials(), artResolver, atlas);

        if (compound) {
            this.population = CompoundBlockPopulation.build(loaded.worldSeed());
            this.driver = new SimulationDriver(world, loaded.worldSeed(),
                    List.<SimulationSystem>of(population.system()));
            this.actorRenderer = new ActorRenderer(population.registry());
            this.actorFont = new BitmapFont();

            // Inspector: click-to-select panel, all-population event feed, follow-camera.
            this.inspector = new InspectorState();
            this.eventLog = new EventLog(30);
            this.eventLogTracker = new EventLogTracker(population.registry(), population.homes(),
                    eventLog);
            // The per-tick seam (not per-frame): fires once per executed tick, so the feed
            // never misses a FAST-skipped tick nor double-logs a re-rendered one.
            this.driver.setAfterTick(eventLogTracker::afterTick);
            this.inspectorRenderer = new InspectorRenderer(population.registry(), population.homes(),
                    population.relationships(), population.jobs(), population.items(), eventLog);
            if (debugSelectActorId >= 0 && debugSelectActorId < population.registry().size()) {
                inspector.select(debugSelectActorId);
                inspector.toggleFollow(); // exercise the follow path in the headless proof
                camera.setZoom(4);        // legible glyph + highlight for the screenshot aid
            }

            System.out.println("observer: spawned " + population.registry().size()
                    + " actors; homes=" + population.homes().size()
                    + " relationships=" + population.relationships().size()
                    + " items=" + population.items().size());
            if (smokeFrames > 0) {
                // Proof run only: force a non-PAUSED speed so the population advances; the
                // shipped interactive default stays PAUSED (SimulationDriver's own default).
                this.driver.setSpeed(SpeedSetting.FAST);
            }
        } else {
            this.driver = new SimulationDriver(world, loaded.worldSeed());
        }

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

        float deltaSeconds = Gdx.graphics.getDeltaTime();
        CameraInput.poll(camera, zLevel, deltaSeconds);
        TimeControlInput.poll(driver);
        if (inspector != null) {
            InspectorInput.poll(inspector, camera, population.registry(), zLevel.z());
        }
        driver.update(deltaSeconds);
        applyFollowCamera();

        projection.setToOrtho2D(0, 0, camera.viewportWidthPx(), camera.viewportHeightPx());
        batch.setProjectionMatrix(projection);
        batch.begin();
        renderer.draw(batch, camera, zLevel.z());
        if (actorRenderer != null) {
            actorRenderer.draw(batch, actorFont, camera, zLevel.z());
        }
        font.draw(batch, HudText.describe(zLevel.z(), camera.zoom()), 8,
                camera.viewportHeightPx() - 8);
        long simElapsedSeconds = driver.currentTick() * TickClock.MILLIS_PER_TICK / 1000;
        font.draw(batch,
                HudText.describeTime(driver.currentTick(), driver.speed().name(), simElapsedSeconds),
                8, camera.viewportHeightPx() - 8 - font.getLineHeight());
        if (inspectorRenderer != null) {
            inspectorRenderer.draw(batch, font, camera, inspector, zLevel.z());
        }
        batch.end();

        framesRendered++;
        if (population != null && smokeFrames > 0
                && (framesRendered == 1 || framesRendered % 60 == 0 || framesRendered == smokeFrames)) {
            reportTrackedMover();
        }
        boolean smokeDone = smokeFrames > 0 && framesRendered >= smokeFrames;
        if (smokeDone || Gdx.input.isKeyJustPressed(Input.Keys.ESCAPE)) {
            if (smokeDone) {
                System.out.println("observer smoke test: rendered " + framesRendered + " frames OK");
                if (screenshotPath != null) {
                    writeScreenshot();
                }
            }
            Gdx.app.exit();
        }
    }

    /** Debug/verification aid only: dumps the final smoke frame's framebuffer to a PNG. */
    private void writeScreenshot() {
        int w = camera.viewportWidthPx();
        int h = camera.viewportHeightPx();
        Pixmap pixmap = ScreenUtils.getFrameBufferPixmap(0, 0, w, h);
        flipVertically(pixmap);
        FileHandle handle = Gdx.files.absolute(screenshotPath);
        PixmapIO.writePNG(handle, pixmap);
        pixmap.dispose();
        System.out.println("observer: wrote screenshot to " + screenshotPath);
    }

    /** glReadPixels (behind getFrameBufferPixmap) is bottom-up; PNGs are top-down. */
    private static void flipVertically(Pixmap pixmap) {
        int w = pixmap.getWidth();
        int h = pixmap.getHeight();
        for (int y = 0; y < h / 2; y++) {
            for (int x = 0; x < w; x++) {
                int top = pixmap.getPixel(x, y);
                int bottom = pixmap.getPixel(x, h - 1 - y);
                pixmap.drawPixel(x, y, bottom);
                pixmap.drawPixel(x, h - 1 - y, top);
            }
        }
    }

    /**
     * While follow is active, re-center the camera on the selected actor's live tile every
     * frame (read fresh from the registry — no cached position), and snap the viewed z-level
     * to the actor's floor so an actor changing levels stays in view. A no-op with no
     * selection or follow off — free camera then.
     */
    private void applyFollowCamera() {
        if (inspector == null || !inspector.followActive()) {
            return;
        }
        int cell = population.registry().get(inspector.selectedActorId()).cell();
        zLevel.to(PackedPos.z(cell));
        camera.centerOnTile(PackedPos.x(cell), PackedPos.y(cell));
    }

    private void reportTrackedMover() {
        int id = population.trackedGroundMoverId();
        int cell = population.registry().get(id).cell();
        System.out.println("observer[compound] frame=" + framesRendered + " tick="
                + driver.currentTick() + " mover#" + id + " cell=(" + PackedPos.x(cell) + ","
                + PackedPos.y(cell) + "," + PackedPos.z(cell) + ")");
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
        if (actorFont != null) {
            actorFont.dispose();
        }
    }

    private static String readArtMapping() {
        try {
            return Files.readString(
                    RepoPaths.locate("content", "art", "kenney", "art-mapping.json"),
                    StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new UncheckedIOException("failed to read art-mapping.json", e);
        }
    }
}
