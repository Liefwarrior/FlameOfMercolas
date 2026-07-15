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
import com.badlogic.gdx.utils.JsonReader;
import com.badlogic.gdx.utils.ScreenUtils;
import com.trojia.client.art.JsonTileArtResolver;
import com.trojia.client.atlas.PlaceholderAtlasFactory;
import com.trojia.client.atlas.SheetAtlasSpec;
import com.trojia.client.atlas.SheetTileAtlas;
import com.trojia.client.atlas.TileAtlas;
import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.face.FaceArchetypes;
import com.trojia.client.face.FaceGen;
import com.trojia.client.face.InspectorFaces;
import com.trojia.client.hud.HudPanel;
import com.trojia.client.hud.HudText;
import com.trojia.client.hud.icons.HudToken;
import com.trojia.client.hud.icons.IconAtlas;
import com.trojia.client.hud.icons.IconTextLine;
import com.trojia.client.input.CameraInput;
import com.trojia.client.input.InspectorInput;
import com.trojia.client.input.PlayModeInput;
import com.trojia.client.input.TimeControlInput;
import com.trojia.client.inspect.EventLog;
import com.trojia.client.inspect.EventLogTracker;
import com.trojia.client.inspect.InspectorState;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.render.ActorRenderer;
import com.trojia.client.render.InspectorRenderer;
import com.trojia.client.render.WorldRenderer;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.client.scenario.ScenarioPopulation;
import com.trojia.client.sprite.SpriteIndex;
import com.trojia.client.sprite.SpriteSheet;
import com.trojia.client.time.SimulationDriver;
import com.trojia.client.time.SpeedSetting;
import com.trojia.client.world.ZLevelCursor;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.engine.SimulationSystem;
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
    public enum Fixture { TAVERN, COMPOUND, DOCKS }

    /** Sentinel for {@code --debug-select}: no forced selection (the shipped default). */
    private static final int NO_DEBUG_SELECT = Actor.NONE;

    /** The shipped art pack directory under {@code content/art/} (DECISIONS.md Art
     * register, fourth revision: custom original MERCOLAS-24 art). {@code --art=kenney}
     * and {@code --art=placeholder} are the escape hatches back to the fallback packs. */
    public static final String DEFAULT_ART_DIR = "custom";

    /** Screen-edge margin, px, for the top-left status HUD block (nav + clock lines). */
    private static final float HUD_MARGIN_PX = 8f;

    private final Fixture fixture;
    private final int smokeFrames;
    private final String screenshotPath;
    private final int debugSelectActorId;
    private final String artDir;
    private final int debugStartZ;
    private final int[] debugCenterTile;
    private final int debugZoom;
    private final boolean debugPlayMode;
    private final int debugMoveDx;
    private final int debugMoveDy;
    private final int debugActAsActorId;
    private int framesRendered;
    private float voidR;
    private float voidG;
    private float voidB;

    private MapCamera camera;
    private ZLevelCursor zLevel;
    private TileAtlas atlas;
    private WorldRenderer renderer;
    private SimulationDriver driver;
    private SpriteBatch batch;
    private BitmapFont font;
    private IconAtlas icons;
    private final Matrix4 projection = new Matrix4();

    // Populated fixtures only (null for the tavern):
    private ActorRenderer actorRenderer;
    private SpriteSheet spriteSheet;
    private InspectorFaces inspectorFaces;
    private ScenarioPopulation population;
    private InspectorState inspector;
    private PlayModeState playMode;
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

    public ObserverApp(Fixture fixture, int smokeFrames, String screenshotPath,
            int debugSelectActorId) {
        this(fixture, smokeFrames, screenshotPath, debugSelectActorId, DEFAULT_ART_DIR, -1,
                null, 0);
    }

    /**
     * @param screenshotPath      if non-null, a PNG of the final smoke frame is written
     *                            here right before exit (debug/verification aid only —
     *                            never used on the shipped interactive path).
     * @param debugSelectActorId  if &ge; 0 (and the compound fixture is loaded), force-
     *                            selects this actor at boot, bypassing the mouse — the
     *                            headless proof seam for the selection panel + follow, since
     *                            the {@code --smoke} path has no cursor to click with.
     * @param artDir              art pack directory under {@code content/art/} whose
     *                            {@code art-mapping.json} the boot loads —
     *                            {@link #DEFAULT_ART_DIR} shipped; {@code kenney} /
     *                            {@code placeholder} are the fallback escape hatches.
     * @param debugStartZ         if &ge; 0, the boot z-level (clamped to world bounds) —
     *                            a screenshot/verification aid mirroring the interactive
     *                            PgUp/PgDn scrub; &lt; 0 keeps the fixture's street level.
     * @param debugCenterTile     if non-null, {@code [tileX, tileY]} the camera centers on
     *                            at boot (mirroring interactive WASD panning) — the
     *                            screenshot aid for framing a specific spot (e.g. the
     *                            docks harbor); null keeps the default camera.
     * @param debugZoom           if &gt; 0, the boot camera zoom (clamped; screenshot aid,
     *                            applied after {@code debugSelectActorId}'s zoom-4
     *                            default); &le; 0 keeps the default.
     */
    public ObserverApp(Fixture fixture, int smokeFrames, String screenshotPath,
            int debugSelectActorId, String artDir, int debugStartZ, int[] debugCenterTile,
            int debugZoom) {
        this(fixture, smokeFrames, screenshotPath, debugSelectActorId, artDir, debugStartZ,
                debugCenterTile, debugZoom, false, 0, 0, NO_DEBUG_SELECT);
    }

    /**
     * Play-mode verification aid (PLAY-MODE-SPEC.md §5, the same "bypass the input device,
     * exercise the same code path" convention {@code debugSelectActorId} already established
     * for the mouse): only meaningful when {@code debugSelectActorId} is also set.
     *
     * @param debugPlayMode      if {@code true}, forces Play mode on for
     *                           {@code debugSelectActorId} at boot (bypassing the {@code P}
     *                           key).
     * @param debugMoveDx        signed step direction ({@code -1/0/1}), applied every rendered
     *                           frame while {@code debugPlayMode} is on (bypassing WASD) — the
     *                           held-key movement proof.
     * @param debugMoveDy        the paired vertical signed step direction.
     * @param debugActAsActorId  if &ge; 0, calls {@code Actor.setActAs} once at boot so
     *                           {@code debugSelectActorId} presents as this other actor
     *                           (bypassing the {@code I} key + click) — the disguise proof.
     */
    public ObserverApp(Fixture fixture, int smokeFrames, String screenshotPath,
            int debugSelectActorId, String artDir, int debugStartZ, int[] debugCenterTile,
            int debugZoom, boolean debugPlayMode, int debugMoveDx, int debugMoveDy,
            int debugActAsActorId) {
        this.fixture = fixture;
        this.smokeFrames = smokeFrames;
        this.screenshotPath = screenshotPath;
        this.debugSelectActorId = debugSelectActorId;
        this.artDir = artDir;
        this.debugStartZ = debugStartZ;
        this.debugCenterTile = debugCenterTile == null ? null : debugCenterTile.clone();
        this.debugZoom = debugZoom;
        this.debugPlayMode = debugPlayMode;
        this.debugMoveDx = debugMoveDx;
        this.debugMoveDy = debugMoveDy;
        this.debugActAsActorId = debugActAsActorId;
    }

    @Override
    public void create() {
        boolean populated = fixture != Fixture.TAVERN;
        FixtureWorldLoader.Loaded loaded = switch (fixture) {
            case TAVERN -> FixtureWorldLoader.loadTavern();
            case COMPOUND -> FixtureWorldLoader.loadCompoundBlock();
            case DOCKS -> FixtureWorldLoader.loadDocksSurface();
        };
        TickableWorld world = loaded.world();

        WorldConfig config = world.config();
        int worldWidthTiles = config.chunksX() * Coords.CHUNK_SIZE_X;
        int worldHeightTiles = config.chunksY() * Coords.CHUNK_SIZE_Y;
        int worldZTiles = config.chunksZ() * Coords.CHUNK_SIZE_Z;
        System.out.println("observer: loaded " + fixture.name().toLowerCase(Locale.ROOT)
                + " world " + worldWidthTiles + "x" + worldHeightTiles + "x" + worldZTiles
                + " tiles (chunks " + config.chunksX() + "x" + config.chunksY() + "x"
                + config.chunksZ() + ")");

        String mappingJson = readArtMapping(artDir);
        JsonTileArtResolver artResolver = JsonTileArtResolver.parse(mappingJson);
        // The void clear color comes from the loaded mapping (TILE-ART-SPEC section 5.2)
        // — packs disagree (#0D0B10 custom, #180F14 kenney), so it is read, not hand-kept.
        int voidRgb = artResolver.voidColorRgb();
        this.voidR = ((voidRgb >>> 16) & 0xFF) / 255f;
        this.voidG = ((voidRgb >>> 8) & 0xFF) / 255f;
        this.voidB = (voidRgb & 0xFF) / 255f;
        this.atlas = createAtlas(mappingJson, artResolver);

        this.camera = new MapCamera(JsonTileArtResolver.TILE_PX, worldWidthTiles, worldHeightTiles,
                Gdx.graphics.getWidth(), Gdx.graphics.getHeight());
        int streetLevelZ = switch (fixture) {
            case TAVERN -> FixtureWorldLoader.TAVERN_STREET_LEVEL_Z;
            case COMPOUND -> FixtureWorldLoader.COMPOUND_GROUND_LEVEL_Z;
            case DOCKS -> FixtureWorldLoader.DOCKS_QUAYSIDE_LEVEL_Z;
        };
        this.zLevel = new ZLevelCursor(0, worldZTiles - 1, streetLevelZ);
        if (debugStartZ >= 0) {
            zLevel.to(debugStartZ);   // clamped by the cursor; verification aid only
        }
        if (debugCenterTile != null) {
            camera.centerOnTile(debugCenterTile[0], debugCenterTile[1]);
        }
        if (debugZoom > 0) {
            camera.setZoom(debugZoom);   // clamped by the camera; screenshot aid only
        }
        this.renderer = new WorldRenderer(world, loaded.materials(), loaded.fluids(),
                artResolver, atlas);

        if (populated) {
            this.population = fixture == Fixture.DOCKS
                    ? DocksPopulation.build(loaded.worldSeed(), world)
                    : ScenarioPopulation.of(CompoundBlockPopulation.build(loaded.worldSeed(), world));
            this.driver = new SimulationDriver(world, loaded.worldSeed(),
                    List.<SimulationSystem>of(population.system()));
            // THE unified sprite index (unified art spec §2 / DECISIONS pillar 3+4): one
            // tag-queryable index + one sheet serve actor sprites AND face parts. The
            // GL-free index validates at load — any actorQueries entry resolving to no
            // sprite fails the boot here, loudly.
            SpriteIndex spriteIndex = loadSpriteIndex();
            Path spriteSheetFile = RepoPaths.locate("content").resolve(spriteIndex.sheetPath());
            this.spriteSheet = SpriteSheet.create(spriteIndex,
                    Gdx.files.absolute(spriteSheetFile.toAbsolutePath().toString()));
            this.actorRenderer = new ActorRenderer(population.registry(), spriteIndex,
                    spriteSheet);

            // Inspector: click-to-select panel, all-population event feed, follow-camera.
            this.inspector = new InspectorState();
            this.playMode = new PlayModeState();
            this.eventLog = new EventLog(30);
            this.eventLogTracker = new EventLogTracker(population.registry(), population.homes(),
                    eventLog);
            // The per-tick seam (not per-frame): fires once per executed tick, so the feed
            // never misses a FAST-skipped tick nor double-logs a re-rendered one.
            this.driver.setAfterTick(eventLogTracker::afterTick);
            // FaceGen portraits (unified art spec §4) draw their parts from the SAME
            // unified index + sheet as the actor sprites (face-part pools are just
            // face_* tag queries over it). Archetypes validate at load, and
            // validateCoverage proves every pool the generator can ever consult is
            // non-empty — missing/invalid faces content fails the boot here, loudly.
            FaceArchetypes archetypes = loadFaceArchetypes();
            FaceGen faceGen = new FaceGen(spriteIndex, archetypes);
            faceGen.validateCoverage();
            this.inspectorFaces = new InspectorFaces(faceGen, archetypes, spriteIndex,
                    spriteSheet, loaded.worldSeed());
            this.inspectorRenderer = new InspectorRenderer(population.registry(), population.homes(),
                    population.relationships(), population.jobs(), population.items(), eventLog,
                    inspectorFaces);
            if (debugSelectActorId >= 0 && debugSelectActorId < population.registry().size()) {
                inspector.select(debugSelectActorId);
                inspector.toggleFollow(); // exercise the follow path in the headless proof
                camera.setZoom(4);        // legible sprite + highlight for the screenshot aid
                // Play-mode debug hooks (PLAY-MODE-SPEC.md §5): bypass the P/I keys and the
                // mouse — the same "exercise the real code path without the input device"
                // convention debugSelectActorId itself established, for the headless proof.
                if (debugPlayMode) {
                    population.registry().get(debugSelectActorId)
                            .setStatus(StatusBit.PLAYER_CONTROLLED, true);
                    playMode.enable(debugSelectActorId);
                }
                if (debugActAsActorId >= 0 && debugActAsActorId < population.registry().size()) {
                    population.registry().get(debugSelectActorId).setActAs(debugActAsActorId);
                }
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
        this.icons = IconAtlas.load(RepoPaths.locate(
                "content", "art", "kenney-input-prompts", "Keyboard & Mouse", "Default"));
    }

    @Override
    public void resize(int width, int height) {
        if (camera != null) {
            camera.setViewport(width, height);
        }
    }

    @Override
    public void render() {
        // The loaded mapping's voidColor (TILE-ART-SPEC section 5.2), read at boot.
        ScreenUtils.clear(voidR, voidG, voidB, 1f);

        float deltaSeconds = Gdx.graphics.getDeltaTime();
        boolean playModeActive = playMode != null && playMode.active();
        // Play mode repurposes WASD to drive the played actor (PLAY-MODE-SPEC.md §5.1) —
        // camera panning is suppressed while it is active; zoom/z-scrub still work either way.
        CameraInput.poll(camera, zLevel, deltaSeconds, !playModeActive);
        TimeControlInput.poll(driver);
        if (inspector != null) {
            boolean clickConsumedByPlayMode = PlayModeInput.poll(playMode, inspector, camera,
                    population.registry(), zLevel.z());
            if (!clickConsumedByPlayMode) {
                InspectorInput.poll(inspector, camera, population.registry(), zLevel.z());
            }
            // Screenshot/verification aid only (bypasses WASD, mirrors debugSelectActorId's
            // "bypass the input device" convention): re-applies the same movement-application
            // code PlayModeInput's real WASD poll uses, every rendered frame, so a held key can
            // be proven deterministically without a live keyboard (PLAY-MODE-SPEC.md §5.2).
            if (debugPlayMode && (debugMoveDx != 0 || debugMoveDy != 0)) {
                PlayModeInput.applyMovement(playMode, population.registry(), debugMoveDx, debugMoveDy);
            }
        }
        if (smokeFrames > 0 && population != null) {
            // Smoke/verification runs advance exactly ONE tick per rendered frame instead
            // of the wall-clock accumulator: two runs of the same smoke command execute
            // identical tick counts per frame, so their final screenshots are
            // pixel-identical (the interactive path below is untouched).
            driver.requestStep();
        } else {
            driver.update(deltaSeconds);
        }
        applyFollowCamera();

        projection.setToOrtho2D(0, 0, camera.viewportWidthPx(), camera.viewportHeightPx());
        batch.setProjectionMatrix(projection);
        batch.begin();
        renderer.draw(batch, camera, zLevel.z());
        if (actorRenderer != null) {
            actorRenderer.draw(batch, camera, zLevel.z());
        }

        // DF-style HUD block (Behavior 2 of this pass): a solid black panel behind the nav +
        // clock lines, sized to their actual content so it never clips or over-extends.
        List<HudToken> navTokens = HudText.describeTokens(zLevel.z(), camera.zoom());
        List<HudToken> timeTokens =
                HudText.describeTimeTokens(driver.currentTick(), driver.speed().name());
        float lineHeight = font.getLineHeight();
        float navWidth = IconTextLine.measure(font, navTokens);
        float timeWidth = IconTextLine.measure(font, timeTokens);
        float statusPanelWidth = Math.max(navWidth, timeWidth) + 2 * HudPanel.PADDING;
        float statusPanelHeight = 2 * lineHeight + 2 * HudPanel.PADDING;
        float statusPanelX = HUD_MARGIN_PX - HudPanel.PADDING;
        float statusPanelBottomY = camera.viewportHeightPx() - HUD_MARGIN_PX - 2 * lineHeight
                - HudPanel.PADDING;
        HudPanel.draw(batch, icons.whitePixel(), statusPanelX, statusPanelBottomY,
                statusPanelWidth, statusPanelHeight);
        IconTextLine.draw(batch, font, icons, HUD_MARGIN_PX, camera.viewportHeightPx() - HUD_MARGIN_PX,
                navTokens);
        IconTextLine.draw(batch, font, icons,
                HUD_MARGIN_PX, camera.viewportHeightPx() - HUD_MARGIN_PX - lineHeight, timeTokens);
        if (inspectorRenderer != null) {
            inspectorRenderer.draw(batch, font, icons, camera, inspector, zLevel.z());
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
        Pixmap pixmap = Pixmap.createFromFrameBuffer(0, 0, w, h);
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
        System.out.println("observer[" + fixture.name().toLowerCase(Locale.ROOT)
                + "] frame=" + framesRendered + " tick="
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
        if (spriteSheet != null) {
            spriteSheet.dispose();   // the ONE unified sheet (actors + face parts)
        }
        if (icons != null) {
            icons.dispose();
        }
    }

    private static String readArtMapping(String artDir) {
        try {
            return Files.readString(
                    RepoPaths.locate("content", "art", artDir, "art-mapping.json"),
                    StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new UncheckedIOException(
                    "failed to read content/art/" + artDir + "/art-mapping.json", e);
        }
    }

    /**
     * Builds the tile atlas the mapping calls for: a mapping with a {@code sheet} block
     * loads its PNG sheet ({@code custom}/{@code kenney}); one without falls back to the
     * runtime-rastered placeholder generator ({@code placeholderGen} block). Either way,
     * boot fails loudly if any byAppearance/fluid region name has no cell/raster
     * (TILE-ART-SPEC section 7.2), rather than silently drawing the wrong tile.
     */
    private static TileAtlas createAtlas(String mappingJson, JsonTileArtResolver artResolver) {
        boolean sheetBased = new JsonReader().parse(mappingJson).has("sheet");
        if (sheetBased) {
            SheetAtlasSpec sheetSpec = SheetAtlasSpec.parse(mappingJson);
            sheetSpec.validateReferenced(artResolver.referencedRegionNames());
            Path sheetFile = RepoPaths.locate("content").resolve(sheetSpec.sheetPath());
            return SheetTileAtlas.create(sheetSpec,
                    Gdx.files.absolute(sheetFile.toAbsolutePath().toString()));
        }
        return PlaceholderAtlasFactory.create(PlaceholderAtlasFactory.buildRaster(mappingJson));
    }

    /** Missing/invalid unified index = boot failure (unified art spec §3.3 wiring rule). */
    private static SpriteIndex loadSpriteIndex() {
        Path path = RepoPaths.locate("content", "art", "sprites", "sprite-index.json");
        try (var reader = Files.newBufferedReader(path, StandardCharsets.UTF_8)) {
            return SpriteIndex.load(reader);
        } catch (IOException e) {
            throw new UncheckedIOException("failed to read sprite-index.json", e);
        }
    }

    /** Missing/invalid archetypes = boot failure (unified art spec §3.3 rule, faces). */
    private static FaceArchetypes loadFaceArchetypes() {
        Path archPath = RepoPaths.locate("content", "art", "faces", "face-archetypes.json");
        try (var archReader = Files.newBufferedReader(archPath, StandardCharsets.UTF_8)) {
            return FaceArchetypes.load(archReader);
        } catch (IOException e) {
            throw new UncheckedIOException("failed to read face-archetypes.json", e);
        }
    }
}
