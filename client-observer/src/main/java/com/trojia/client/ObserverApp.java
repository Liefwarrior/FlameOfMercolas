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
import com.trojia.client.boot.LampMarkersLoader;
import com.trojia.client.boot.PlaceSignsLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.camera.FollowZSnap;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.face.FaceArchetypes;
import com.trojia.client.fpv.CellActorIndex;
import com.trojia.client.fpv.CompassRenderer;
import com.trojia.client.fpv.EyeProjection;
import com.trojia.client.fpv.FacingWedge;
import com.trojia.client.fpv.FirstPersonCamera;
import com.trojia.client.fpv.FirstPersonPlanner;
import com.trojia.client.fpv.FirstPersonRenderer;
import com.trojia.client.fpv.ViewFacing;
import com.trojia.client.fpv.ViewModeState;
import com.trojia.client.fpv.ViewQuad;
import com.trojia.client.fpv.WorldCellSight;
import com.trojia.client.face.FaceGen;
import com.trojia.client.face.InspectorFaces;
import com.trojia.client.hud.HudPanel;
import com.trojia.client.hud.HudText;
import com.trojia.client.hud.icons.HudToken;
import com.trojia.client.hud.icons.IconAtlas;
import com.trojia.client.hud.icons.IconTextLine;
import com.trojia.client.input.CameraInput;
import com.trojia.client.input.ClimbInput;
import com.trojia.client.input.EatInput;
import com.trojia.client.input.FirstPersonInput;
import com.trojia.client.input.CullInput;
import com.trojia.client.input.SellInput;
import com.trojia.client.input.SpellInput;
import com.trojia.client.input.FishInput;
import com.trojia.client.input.InspectorInput;
import com.trojia.client.input.ObserverScript;
import com.trojia.client.input.PlayModeInput;
import com.trojia.client.input.TalkInput;
import com.trojia.client.input.TheftInput;
import com.trojia.client.input.TimeControlInput;
import com.trojia.client.inspect.CrimeFeedTracker;
import com.trojia.client.inspect.DeathFeedTracker;
import com.trojia.client.inspect.EatFeedbackTracker;
import com.trojia.client.inspect.EventLog;
import com.trojia.client.inspect.EventLogTracker;
import com.trojia.client.inspect.CullFeedbackTracker;
import com.trojia.client.inspect.SpellBar;
import com.trojia.client.inspect.SpellFeedbackTracker;
import com.trojia.client.inspect.SellFeedbackTracker;
import com.trojia.client.inspect.FishFeedbackTracker;
import com.trojia.client.inspect.InspectorState;
import com.trojia.client.inspect.JournalText;
import com.trojia.client.inspect.LenienceFeedbackTracker;
import com.trojia.client.inspect.MastersBoardSnapshot;
import com.trojia.client.inspect.MastersBoardText;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.QuestFeedTracker;
import com.trojia.client.inspect.QuitGuard;
import com.trojia.client.inspect.SkillUpTracker;
import com.trojia.client.inspect.TalkState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.render.ActorRenderer;
import com.trojia.client.render.AmbientLight;
import com.trojia.client.render.DepthVision;
import com.trojia.client.render.FishingSpotRenderer;
import com.trojia.client.render.InspectorRenderer;
import com.trojia.client.render.SpellBarRenderer;
import com.trojia.client.render.JournalRenderer;
import com.trojia.client.render.LampGlowMap;
import com.trojia.client.render.NameplateRenderer;
import com.trojia.client.render.PlaceSign;
import com.trojia.client.render.PlaceSignArt;
import com.trojia.client.render.PlaceSignRenderer;
import com.trojia.client.render.TalkPanelRenderer;
import com.trojia.client.render.ToastRenderer;
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
import com.trojia.sim.bark.BarkRawsLoader;
import com.trojia.sim.bark.BarkTableRegistry;
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
     * register, FIFTH revision, 2026-07-15: DF-translated Kenney, true-black void,
     * Roman-pillared civic facades — supersedes the fourth revision's custom
     * MERCOLAS-24 pack). {@code --art=custom} and {@code --art=placeholder} are the
     * escape hatches back to the superseded/fallback packs. */
    public static final String DEFAULT_ART_DIR = "kenney";

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
    /** The scripted-playtest tape ({@code --script=}), or {@code null} (live input only). */
    private final ObserverScript script;
    /** Scripted held-movement state (the {@code hold=dx,dy} verb), applied every frame. */
    private int scriptMoveDx;
    private int scriptMoveDy;
    /** Scripted held first-person walk (the {@code step=forward,strafe} verb): the same
     * held-key semantics as {@code hold}, but look-relative, so a tape can walk a street. */
    private int scriptForward;
    private int scriptStrafe;
    /** Scripted hold-N nameplates toggle (the {@code plates} verb). */
    private boolean scriptPlatesHeld;
    /** A {@code shot=path} scheduled for the current frame; captured after the batch ends. */
    private String pendingShotPath;
    private int framesRendered;
    private float voidR;
    private float voidG;
    private float voidB;

    private MapCamera camera;
    private ZLevelCursor zLevel;
    // THE SECOND VIEW (2026-07-29). A camera is a way of looking at the world, not a fact in
    // it: everything below reads the same deterministic tick stream the tile view reads, and
    // writes nothing. The tile view stays the verification source of truth and stays running
    // underneath — the switch is a cross-fade between two readings of one world, not a mode
    // the simulation knows about.
    private final ViewModeState viewMode = new ViewModeState();
    private final FirstPersonCamera eye = new FirstPersonCamera();
    private final CompassRenderer compass = new CompassRenderer();
    private FirstPersonPlanner fpvPlanner;
    private FirstPersonRenderer fpvRenderer;
    private WorldCellSight cellSight;
    private CellActorIndex fpvActors;
    private final FollowZSnap followZSnap = new FollowZSnap();
    private final QuitGuard quitGuard = new QuitGuard();
    private TileAtlas atlas;
    private WorldRenderer renderer;
    private SimulationDriver driver;
    private SpriteBatch batch;
    private BitmapFont font;
    private IconAtlas icons;
    private final Matrix4 projection = new Matrix4();

    // Populated fixtures only (null for the tavern):
    private DepthVision depthVision;
    private ActorRenderer actorRenderer;
    private FishingSpotRenderer fishingSpotRenderer;
    private PlaceSignRenderer placeSignRenderer;
    private SpriteSheet spriteSheet;
    private InspectorFaces inspectorFaces;
    private ScenarioPopulation population;
    private InspectorState inspector;
    private PlayModeState playMode;
    private EventLog eventLog;
    private EventLogTracker eventLogTracker;
    private InspectorRenderer inspectorRenderer;
    private NameplateRenderer nameplateRenderer;
    private ToastQueue toasts;
    private SkillUpTracker skillUpTracker;
    private EatFeedbackTracker eatFeedbackTracker;
    private FishFeedbackTracker fishFeedbackTracker;
    private CullFeedbackTracker cullFeedbackTracker;
    /** Simple Magic: the craftings bar's outcome narration + its one-slot X memory. */
    private SpellFeedbackTracker spellFeedbackTracker;
    private final SpellInput.LastCast lastCast = new SpellInput.LastCast();
    private final SpellBarRenderer spellBarRenderer = new SpellBarRenderer();
    /** This frame's craftings-bar layout, recomputed every frame from the viewport. */
    private java.util.List<SpellBar.Button> spellButtons = java.util.List.of();
    private SellFeedbackTracker sellFeedbackTracker;
    private ToastRenderer toastRenderer;
    // Sprint 2 "walk up and talk": the speech panel + the theft feedback loop.
    private TalkState talk;
    private TalkPanelRenderer talkPanelRenderer;
    private CrimeFeedTracker crimeFeedTracker;
    private BarkTableRegistry barkTables;
    private long bootWorldSeed;
    // Sprint 3 "The Vanished Clerk": the journal pane + the quest feed.
    private QuestFeedTracker questFeedTracker;
    private JournalRenderer journalRenderer;
    private boolean journalOpen;
    // Sprint 5 "the masters board": the ward's best per craft + climbers since dawn.
    private MastersBoardSnapshot mastersSnapshot;
    private boolean mastersOpen;

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
        this(fixture, smokeFrames, screenshotPath, debugSelectActorId, artDir, debugStartZ,
                debugCenterTile, debugZoom, debugPlayMode, debugMoveDx, debugMoveDy,
                debugActAsActorId, null);
    }

    /**
     * The scripted-playtest constructor (Sprint 4 CLIENT): {@code script} replays a whole
     * session — selection, Play mode, movement, verbs, screenshots — through the same
     * deterministic {@code apply*} seams the live keyboard wrappers call, one action list
     * per rendered frame (see {@link ObserverScript}). Meaningful only with
     * {@code smokeFrames > 0} (the one-tick-per-frame determinism rule).
     */
    public ObserverApp(Fixture fixture, int smokeFrames, String screenshotPath,
            int debugSelectActorId, String artDir, int debugStartZ, int[] debugCenterTile,
            int debugZoom, boolean debugPlayMode, int debugMoveDx, int debugMoveDy,
            int debugActAsActorId, ObserverScript script) {
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
        this.script = script;
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
        // — packs disagree (#0D0B10 custom, #000000 kenney/DF-black), so it is read, not
        // hand-kept.
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
        // Day/night lighting: static lamp/brazier markers -> precomputed glow pools.
        // Markers are not baked into the TROJSAV yet (importer defers them), so they are
        // read straight from the fixture's authored source map — see LampMarkersLoader's
        // javadoc for the contract exception. Missing source map = no pools, never a
        // boot failure.
        LampGlowMap lampGlow = loadLampGlow(fixture, worldWidthTiles, worldHeightTiles);
        this.renderer = new WorldRenderer(world, loaded.materials(), loaded.fluids(),
                artResolver, atlas, lampGlow);
        // Depth vision (S4 EPIC): the look-down column resolver that lets the actor pass,
        // the hover plates, click-to-inspect and the place signs see through empty air to
        // the bands below. Presentation-only — reads tiles, never feeds the hasher. Built
        // before the populated branch: an EMPTY ward still gets labelled buildings.
        this.depthVision = new DepthVision(world);
        // Building labels (2026-07-28, Eli: "we also need to label buildings"): the ward's
        // authored place_sign markers, read from the same source map the lamps come from
        // (marker baking is still deferred — see PlaceSignsLoader's javadoc). Missing
        // source map = an unlabelled ward, never a boot failure.
        this.placeSignRenderer = new PlaceSignRenderer(
                loadPlaceSigns(fixture), depthVision);
        // The first-person view's GL-free half: its OWN tile cursor (borrowing the renderer's
        // or DepthVision's would move their position out from under them mid-frame) and the
        // shared TilePlan art chain, so a cell resolves to the same region, the same cosmetic
        // variant and the same tint whichever camera asks.
        this.cellSight = new WorldCellSight(world);
        this.fpvPlanner = new FirstPersonPlanner(loaded.materials(), loaded.fluids(),
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
                    spriteSheet, lampGlow, depthVision);
            // Same atlas, same sprite sheet, same lamp map as the tile view — the first-person
            // frame is a second reading of one world, not a second world.
            this.fpvRenderer = new FirstPersonRenderer(atlas, spriteSheet, lampGlow);
            this.fpvActors = new CellActorIndex(population.registry(), spriteIndex);
            // Fishing-spot overlay (S6): the sim-side registry drawn in world space,
            // z-order terrain -> water -> SPOTS -> actors. Omniscient while observing;
            // in Play mode only the played soul's sim-confirmed-perceived spots draw.
            this.fishingSpotRenderer = new FishingSpotRenderer(
                    population.system().fishingSpots(), depthVision,
                    population.system().skillTracks(), loaded.worldSeed(),
                    () -> playMode.active() ? playMode.playedActorId() : Actor.NONE);

            // Inspector: click-to-select panel, all-population event feed, follow-camera.
            this.inspector = new InspectorState();
            this.playMode = new PlayModeState();
            // 120 entries (Sprint 5 "the torrent": 30 was sized for a trickle — with the
            // growth digest live the mixed feed still needs room for a filtered lane to
            // reach back a meaningful stretch).
            this.eventLog = new EventLog(120);
            this.eventLogTracker = new EventLogTracker(population.registry(), population.homes(),
                    eventLog, population.identity());
            // Skill-up narration (S1 item 3): the played actor's level-ups toast
            // bottom-center; everyone else's land in the event feed as people. Reads the
            // Sim team's SkillLevelLog seam — zero sim writes.
            this.toasts = new ToastQueue();
            this.skillUpTracker = new SkillUpTracker(population.system().skillTracks(),
                    population.registry(), population.identity(), eventLog, toasts,
                    () -> playMode.playedActorId());
            // The TALK surface (S2 item 1): World's bark tables behind the sim's selector;
            // a missing barks.json degrades to EMPTY (the panel says nothing, never fails).
            this.talk = new TalkState();
            this.barkTables = BarkRawsLoader.load(RepoPaths.locate("content", "raws"));
            this.bootWorldSeed = loaded.worldSeed();
            // Theft narration (S2 items 1+4): every crime row lands in the feed as named
            // people; the played actor's own attempts additionally toast with the
            // CRPG check line (and land it on the open talk panel). Zero sim writes.
            this.crimeFeedTracker = new CrimeFeedTracker(population.system().crimeLog(),
                    population.system().skillTracks(), population.registry(),
                    population.identity(), eventLog, toasts,
                    () -> playMode.playedActorId(), talk);
            // Death narration (S6, Eli's bug 7): every DeathLog row lands in the feed BY
            // NAME ("Tatter Deepnet has died -- starvation"); the played soul's own death
            // toasts. Zero sim writes.
            DeathFeedTracker deathFeedTracker = new DeathFeedTracker(
                    population.system().deathLog(), population.registry(),
                    population.identity(), eventLog, toasts,
                    () -> playMode.playedActorId());
            // Quest narration (S3): stage advances land in the feed as the journal's own
            // prose; the owner's advances toast; failed drawer pries toast the check line.
            this.questFeedTracker = new QuestFeedTracker(population.questRegistry(),
                    population.system().questLog(), population.system().skillTracks(),
                    eventLog, toasts, () -> playMode.playedActorId());
            this.journalRenderer = new JournalRenderer();
            // Eat-outcome narration (S4 item 3, S5 barter decomposition): the played
            // actor's E press resolves sim-side next tick; this tracker toasts the outcome
            // reason — and, on a counter buy, the haggle-decomposed personal quote.
            this.eatFeedbackTracker = new EatFeedbackTracker(population.registry(), toasts,
                    () -> playMode.playedActorId(), population.system().skillTracks(),
                    population.system().factionStandings());
            // Fish-outcome narration (S6): the played soul's R cast resolves sim-side
            // next tick; this tracker toasts the outcome reason + the catch-check line.
            this.fishFeedbackTracker = new FishFeedbackTracker(population.registry(), toasts,
                    () -> playMode.playedActorId(), population.system().skillTracks(),
                    population.system().fishingSpots());
            // Cull-outcome narration (S8): the played soul's K cull resolves sim-side next
            // tick; this tracker toasts the outcome reason + the cull-check line.
            this.cullFeedbackTracker = new CullFeedbackTracker(population.registry(), toasts,
                    () -> playMode.playedActorId(), population.system().skillTracks());
            // Crafting narration (Simple Magic): a press on the craftings bar resolves
            // sim-side next tick; this tracker toasts the outcome, what it did, and the
            // linkcraft check line.
            this.spellFeedbackTracker = new SpellFeedbackTracker(population.registry(), toasts,
                    () -> playMode.playedActorId(), population.system().skillTracks(),
                    population.system().spells());
            // Counter-sale narration (S8): the played soul's B sale resolves sim-side next
            // tick; this tracker toasts sold / nobody-buying.
            this.sellFeedbackTracker = new SellFeedbackTracker(population.registry(), toasts,
                    () -> playMode.playedActorId(), population.items(),
                    population.system().bankAccounts());
            // Watch-lenience narration (S5 check lines): the played soul's warn/fine
            // transitions toast the exact inputs the lenience draw read. Zero sim writes.
            LenienceFeedbackTracker lenienceFeedbackTracker = new LenienceFeedbackTracker(
                    population.registry(), population.system().skillTracks(),
                    population.system().factionStandings(), toasts,
                    () -> playMode.playedActorId());
            // The masters board's dawn baseline (S5 item 3): construction snapshots the
            // bake's seeded masters; each day boundary re-baselines the climbers.
            this.mastersSnapshot = new MastersBoardSnapshot(
                    population.system().skillTracks(), population.registry().size());
            // The per-tick seam (not per-frame): fires once per executed tick, so no
            // tracker misses a FAST-skipped tick nor double-logs a re-rendered one.
            this.driver.setAfterTick(tick -> {
                eventLogTracker.afterTick(tick);
                skillUpTracker.afterTick(tick);
                crimeFeedTracker.afterTick(tick);
                deathFeedTracker.afterTick(tick);
                questFeedTracker.afterTick(tick);
                eatFeedbackTracker.afterTick(tick);
                fishFeedbackTracker.afterTick(tick);
                cullFeedbackTracker.afterTick(tick);
                spellFeedbackTracker.afterTick(tick);
                sellFeedbackTracker.afterTick(tick);
                lenienceFeedbackTracker.afterTick(tick);
                mastersSnapshot.afterTick(tick);
            });
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
                    inspectorFaces, population.identity(), population.system().skillTracks(),
                    population.system().factionStandings(), () -> playMode.playedActorId(),
                    population.system().bankAccounts());
            // Hover nameplates (S1 item 2): PRESENTED identity always; hold N to plate
            // every on-screen actor. While an actor is played, plates tint by how each
            // soul regards the played actor's presented face (S2 item 2).
            this.nameplateRenderer = new NameplateRenderer(population.registry(),
                    population.jobs(), population.identity(),
                    population.system().factionStandings(), population.relationships(),
                    () -> playMode.playedActorId(), depthVision);
            this.toastRenderer = new ToastRenderer();
            this.talkPanelRenderer = new TalkPanelRenderer(population.registry(), inspectorFaces);
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
        // THE SWITCH (V). First person needs eyes to look through, so it is only available
        // while an actor is driven; asking for it without one says so rather than doing
        // nothing. Toggling never touches the simulation — it changes which of two readings
        // of the same tick stream is on screen.
        if (population != null && Gdx.input.isKeyJustPressed(Input.Keys.V)) {
            applyViewToggle();
        }
        boolean firstPerson = viewMode.firstPersonVisible();
        // Play mode repurposes WASD to drive the played actor (PLAY-MODE-SPEC.md §5.1) and —
        // Sprint 4 "the climb" — Up/Down to climb stairs, so camera panning AND z-scrub are
        // suppressed while it is active (the follow-camera owns the viewed floor); zoom
        // still works either way.
        CameraInput.poll(camera, zLevel, deltaSeconds, !playModeActive, !playModeActive);
        TimeControlInput.poll(driver);
        boolean escConsumedByTalk = false;
        if (inspector != null) {
            // The CRAFTINGS BAR (Simple Magic) polls FIRST: a click that lands on a button
            // must never also reselect the tile behind it. The layout is this frame's, so the
            // hit rectangles are exactly the ones drawn.
            spellButtons = SpellBar.layout(population.system().spells(),
                    camera.viewportWidthPx(), camera.viewportHeightPx(),
                    inspector.hasSelection());
            boolean clickConsumedBySpellBar = SpellInput.poll(playMode, population.registry(),
                    population.identity(),
                    population.system().spells(), population.system().skillTracks(),
                    spellButtons, toasts, spellFeedbackTracker, driver.currentTick(),
                    camera.viewportHeightPx(), lastCast);
            // In first person WASD means forward/strafe relative to the look direction, so the
            // world-axis read is suppressed here and FirstPersonInput does the rotating — both
            // ending at PlayModeInput.applyMovement, the one route into the sim's move intent.
            boolean clickConsumedByPlayMode = PlayModeInput.poll(playMode, inspector, camera,
                    population.registry(), zLevel.z(), !firstPerson);
            if (!clickConsumedByPlayMode && !clickConsumedBySpellBar) {
                // Depth-aware click-to-inspect (S4 EPIC): same-z actor wins; an empty tile
                // falls through to the visible below-z actor. Play-mode verb picks above
                // stay same-z (reach realism — the lead's ruling).
                InspectorInput.poll(inspector, camera, population.registry(), zLevel.z(),
                        depthVision);
            }
            // The Sprint-2 adjacency verbs (T talk / G pickpocket). Talk owns ESC while its
            // panel is up — closing a conversation must not also close the observer.
            escConsumedByTalk = TalkInput.poll(talk, playMode, population.registry(),
                    population.jobs(), population.identity(),
                    population.system().factionStandings(), population.relationships(),
                    barkTables, toasts, population.questRegistry(),
                    population.system().questLog(), bootWorldSeed, driver.currentTick(),
                    population::askTopicsOf, population.topicCatalog());
            TheftInput.poll(playMode, population.registry(), population.identity(), toasts);
            // The CLIMB verb (S4): Up/Down while driving a soul takes the baked stair/ramp
            // under its feet. Polled after movement so a held climb key wins the frame's
            // move intent (one intent slot; the climb is the deliberate act).
            ClimbInput.poll(playMode, population.registry(), population.zLinks(), toasts);
            // The EAT verb (S4 item 3): E feeds the played soul through the sim's own
            // eat-in-reach chain; the outcome toast lands via EatFeedbackTracker.
            EatInput.poll(playMode, population.registry(), toasts, eatFeedbackTracker);
            // The FISH verb (S6): R casts at a perceived spot within reach through the
            // sim's shared cast attempt; outcome + check line via FishFeedbackTracker.
            FishInput.poll(playMode, population.registry(), toasts, fishFeedbackTracker);
            // The CULL verb (S8): K takes a scalp off a downed vermin body within knife reach
            // through the sim's shared cull verb; outcome + check line via CullFeedbackTracker.
            CullInput.poll(playMode, population.registry(), toasts, cullFeedbackTracker,
                    driver.currentTick());
            // The SELL verb (S8): B turns carried materials into Royals at a counter in reach
            // through the sim's shared exchange; outcome toast via SellFeedbackTracker.
            SellInput.poll(playMode, population.registry(), toasts, sellFeedbackTracker);
            // First-person look and step. Left/Right turn the client-side view yaw (never
            // Actor.setFacing — facing is serialized sim state the twin-run gate compares);
            // PageUp/PageDown shear the horizon; WASD steps relative to where you are looking.
            if (firstPerson) {
                FirstPersonInput.poll(eye, playMode, population.registry(),
                        deltaSeconds, camera.viewportHeightPx());
            } else {
                // The turn keys stay live on the map, so the facing wedge is something you aim
                // before you press V rather than a read-out you discover afterwards. Arrow-key
                // panning is already suppressed while an actor is driven, so nothing collides.
                FirstPersonInput.pollTurn(eye, playMode, deltaSeconds);
            }
            // The JOURNAL toggle (S3): J opens/closes the quest pane (J was unbound; the
            // design's verify-free-then-bind rule). Shares the pane with the masters board.
            if (Gdx.input.isKeyJustPressed(Input.Keys.J)) {
                journalOpen = !journalOpen;
                if (journalOpen) {
                    mastersOpen = false;
                }
            }
            // The FEED FILTER cycle (S5 "the torrent"): L walks ALL/GROWTH/CRIME/QUESTS
            // (L was unbound; the same verify-free-then-bind rule).
            if (Gdx.input.isKeyJustPressed(Input.Keys.L)) {
                inspector.cycleFeedFilter();
            }
            // The MASTERS BOARD toggle (S5 item 3): M opens/closes the ward's craft rolls
            // (M was unbound). The board and the journal share the centered pane, so
            // opening one closes the other.
            if (Gdx.input.isKeyJustPressed(Input.Keys.M)) {
                mastersOpen = !mastersOpen;
                if (mastersOpen) {
                    journalOpen = false;
                }
            }
            // Screenshot/verification aid only (bypasses WASD, mirrors debugSelectActorId's
            // "bypass the input device" convention): re-applies the same movement-application
            // code PlayModeInput's real WASD poll uses, every rendered frame, so a held key can
            // be proven deterministically without a live keyboard (PLAY-MODE-SPEC.md §5.2).
            if (debugPlayMode && (debugMoveDx != 0 || debugMoveDy != 0)) {
                PlayModeInput.applyMovement(playMode, population.registry(), debugMoveDx, debugMoveDy);
            }
            // Scripted playtest tape (--script=): any held scripted movement first, then
            // this frame's actions (so a frame's deliberate verb — a climb, a talk — wins
            // the frame's one intent slot) — both through the exact apply* seams the live
            // keys use.
            if (script != null) {
                if (scriptMoveDx != 0 || scriptMoveDy != 0) {
                    PlayModeInput.applyMovement(playMode, population.registry(),
                            scriptMoveDx, scriptMoveDy);
                }
                if (scriptForward != 0 || scriptStrafe != 0) {
                    FirstPersonInput.applyMove(eye, playMode, population.registry(),
                            scriptForward, scriptStrafe);
                }
                applyScriptFrame(framesRendered);
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
        trackEye(deltaSeconds);
        // The transition dollies the tile camera toward max zoom on the way in and back out
        // again on the way out, so the frame the cross-fade lands on is already showing the
        // patch of ground you are about to be standing on.
        camera.setZoom(viewMode.dollyZoom(camera.zoom()));

        projection.setToOrtho2D(0, 0, camera.viewportWidthPx(), camera.viewportHeightPx());
        batch.setProjectionMatrix(projection);
        batch.begin();
        // Day/night cycle: one ambient per frame, a pure function of the tick, anchored on
        // the same DayPhase boundaries as the HUD clock tag. Scene draws only — the HUD
        // below stays untinted (both renderers restore the batch to white).
        AmbientLight ambient = AmbientLight.at(driver.currentTick());
        boolean drawTopDown = viewMode.topDownVisible();
        if (drawTopDown) {
            renderer.draw(batch, camera, zLevel.z(), ambient);
            if (fishingSpotRenderer != null) {
                // Between water and actors (spec z-order): a fisher stands IN the ripple.
                fishingSpotRenderer.draw(batch, camera, zLevel.z(), icons.whitePixel());
            }
            if (actorRenderer != null) {
                actorRenderer.draw(batch, camera, zLevel.z(), ambient);
            }
            drawFacingWedge();
        }
        // Place labels: the door plaques and the street fingerposts are world furniture,
        // drawn just after the actors (a figure in a doorway stands UNDER their shop's sign)
        // and lit by the same day/night ambient as everything else in the scene. This call
        // also plans the frame's single pop-up, which the HUD pass below draws.
        //
        // WHAT THE VIEWER IS ATTENDING TO, and why it differs by mode. Observing: the tile
        // under the cursor — the ward's existing hover idiom (NameplateRenderer's own), so
        // pointing at a building asks "what is that?" and a cursor off the world asks
        // nothing. Playing: the driven actor's OWN tile, so the sign speaks when you walk up
        // to the door instead of whenever the mouse drifts. The overlay applies the matching
        // reach (3 tiles free-camera, 1 tile doorway).
        if (drawTopDown && placeSignRenderer != null && !placeSignRenderer.isEmpty()) {
            int attentionX;
            int attentionY;
            boolean attentionLive;
            if (playModeActive) {
                int cell = population.registry().get(playMode.playedActorId()).cell();
                attentionX = PackedPos.x(cell);
                attentionY = PackedPos.y(cell);
                attentionLive = true;
            } else {
                attentionX = camera.screenToTileX(Gdx.input.getX());
                attentionY = camera.screenToTileY(Gdx.input.getY());
                attentionLive = camera.isInWorld(attentionX, attentionY);
            }
            placeSignRenderer.drawMarks(batch, camera, zLevel.z(), icons.whitePixel(),
                    attentionX, attentionY, attentionLive, playModeActive, ambient);
        }

        if (viewMode.firstPersonVisible() && fpvRenderer != null) {
            drawFirstPerson(ambient);
        }

        // DF-style HUD block (Behavior 2 of this pass): a solid black panel behind the nav +
        // clock lines — plus, on populated fixtures, the VERB legend line (Sprint 4 playtest
        // fix: the social-verb surface was undiscoverable), the district pulse, and, while an
        // actor is driven, that actor's PURSE (S8: the money the loop pays out was on no
        // screen at all). One list of lines, drawn top down, sized to its actual content so
        // the panel never clips or over-extends.
        List<List<HudToken>> hudLines = new java.util.ArrayList<>();
        hudLines.add(HudText.describeTokens(zLevel.z(), camera.zoom()));
        hudLines.add(HudText.describeTimeTokens(driver.currentTick(), driver.speed().name()));
        if (population != null) {
            // While driving, the verb line; while driving in first person, the line that says
            // which keys just changed meaning (WASD is now look-relative, the arrows turn).
            if (!playModeActive) {
                hudLines.add(HudText.observerVerbKeybindingTokens());
            } else if (viewMode.settledFirstPerson()) {
                hudLines.add(HudText.firstPersonKeybindingTokens());
            } else {
                hudLines.add(HudText.playModeKeybindingTokens());
            }
            // S8 payoff legibility: Royals (banked) and Coins (carried) are different money
            // and a player has to be able to tell a sale from a pickpocketing.
            if (playModeActive) {
                hudLines.add(HudText.purseTokens(
                        com.trojia.client.inspect.Purse.royalsOf(playMode.playedActorId(),
                                population.items(), population.system().bankAccounts()),
                        com.trojia.client.inspect.Purse.coinsOf(playMode.playedActorId(),
                                population.items())));
            }
            // Orientation, in both modes: the band under the eye and the bearing it is
            // looking along. The band is the whole point of the Z-axis being legible —
            // "z=19" on the quay and "z=22" on a roof is how a climb reads as a climb in
            // text as well as in the picture.
            if (playModeActive && eye.isPlaced()) {
                hudLines.add(List.of(HudToken.dimText(HudText.eyeLine(eye.band(),
                        ViewFacing.compassPoint(eye.yaw()),
                        Math.round(ViewFacing.compassDegrees(eye.yaw()))))));
            }
            // S6 motivation legibility: the district pulse — a one-line living census
            // (working / duty-out / starving / held / confined / dead), recomputed live.
            hudLines.add(List.of(HudToken.dimText(
                    com.trojia.client.inspect.DistrictPulse.line(population.registry()))));
        }
        float lineHeight = font.getLineHeight();
        float widestLine = 0f;
        for (List<HudToken> line : hudLines) {
            widestLine = Math.max(widestLine, IconTextLine.measure(font, line));
        }
        float statusPanelWidth = widestLine + 2 * HudPanel.PADDING;
        float statusPanelHeight = hudLines.size() * lineHeight + 2 * HudPanel.PADDING;
        float statusPanelX = HUD_MARGIN_PX - HudPanel.PADDING;
        float statusPanelBottomY = camera.viewportHeightPx() - HUD_MARGIN_PX
                - hudLines.size() * lineHeight - HudPanel.PADDING;
        HudPanel.draw(batch, icons.whitePixel(), statusPanelX, statusPanelBottomY,
                statusPanelWidth, statusPanelHeight);
        for (int i = 0; i < hudLines.size(); i++) {
            IconTextLine.draw(batch, font, icons, HUD_MARGIN_PX,
                    camera.viewportHeightPx() - HUD_MARGIN_PX - i * lineHeight, hudLines.get(i));
        }
        if (inspectorRenderer != null) {
            // The sheet and the feed belong on screen in both modes; the world-space tile
            // highlight belongs only to the camera that has tiles on screen.
            inspectorRenderer.draw(batch, font, icons, camera, inspector, zLevel.z(),
                    drawTopDown);
        }
        if (population != null && !spellButtons.isEmpty()) {
            // The craftings bar, drawn after the sheet so it can dock against its edge. It is a
            // screen-space column, so it survives the first-person switch: a driven actor can
            // still work a crafting through its own eyes.
            spellBarRenderer.draw(batch, font, icons, population.system().spells(),
                    population.system().skillTracks(), population.registry(),
                    population.system().activeEffects(),
                    playMode != null && playMode.active() ? playMode.playedActorId()
                            : com.trojia.sim.actor.Actor.NONE,
                    spellButtons, camera.viewportWidthPx(), camera.viewportHeightPx(),
                    inspector != null && inspector.hasSelection(), driver.currentTick());
        }
        if (drawTopDown && nameplateRenderer != null) {
            // Hover nameplate at the live cursor; hold N to plate every on-screen actor
            // (or the script's `plates` toggle — the tape has no keys to hold).
            nameplateRenderer.draw(batch, font, icons, camera, zLevel.z(),
                    Gdx.input.getX(), Gdx.input.getY(),
                    Gdx.input.isKeyPressed(Input.Keys.N) || scriptPlatesHeld);
        }
        if (drawTopDown && placeSignRenderer != null && !placeSignRenderer.isEmpty()) {
            // The one NES pop-up: HUD, not scene — hard-edged, opaque, blocky-lettered, never
            // dimmed and never faded. It snaps on with the plan and snaps off with it.
            //
            // ZONE AWARENESS (S8 round 2): drawn last, but no longer drawn ON TOP OF whatever
            // is under it. It is handed everything the frame already committed to the screen —
            // the status/pulse/purse block, the inspector's sheet and event feed, and the hover
            // nameplate the box belongs to — and it MOVES rather than covering any of them.
            List<PlaceSignArt.Rect> hudZones = new java.util.ArrayList<>();
            hudZones.add(new PlaceSignArt.Rect(statusPanelX, statusPanelBottomY,
                    statusPanelWidth, statusPanelHeight));
            if (inspectorRenderer != null) {
                hudZones.addAll(inspectorRenderer.panelBounds());
            }
            if (nameplateRenderer != null && nameplateRenderer.hoverPlateBounds() != null) {
                hudZones.add(nameplateRenderer.hoverPlateBounds());
            }
            placeSignRenderer.drawBox(batch, camera, icons.whitePixel(), hudZones);
        }
        if (talkPanelRenderer != null) {
            // The speech exchange (a no-op while closed) — over the plates, under toasts.
            talkPanelRenderer.draw(batch, font, icons, camera, talk);
        }
        if (journalRenderer != null) {
            // The quest journal (a no-op while closed): content recomputed live each frame
            // off the GL-free JournalText (the no-staleness contract), drawn under toasts.
            journalRenderer.draw(batch, font, icons, camera, journalOpen,
                    journalOpen ? JournalText.lines(population.questRegistry(),
                            population.system().questLog(), population.registry(),
                            population.identity()) : List.of());
            // The WARD MASTERS board (S5 item 3) shares the same pane: the ward's best
            // per craft + climbers since dawn, recomputed live off the skill table.
            journalRenderer.draw(batch, font, icons, camera, mastersOpen,
                    mastersOpen ? MastersBoardText.lines(population.system().skillTracks(),
                            population.registry(), population.identity(), mastersSnapshot)
                            : List.of(), "(M close)");
        }
        if (playModeActive && eye.isPlaced()) {
            // Drawn identically in both modes and through the whole cross-fade: the one
            // element that provably does not move when the picture changes.
            compass.draw(batch, font, icons.whitePixel(), eye.yaw(),
                    camera.viewportWidthPx(), camera.viewportHeightPx());
        }
        if (toastRenderer != null) {
            // Toasts age by rendered wall-clock seconds (readable at any sim speed).
            toasts.update(deltaSeconds);
            toastRenderer.draw(batch, font, icons, camera, toasts);
        }
        batch.end();

        // A scripted `shot=` scheduled for this frame: the batch is flushed, so the
        // framebuffer now holds exactly what this frame drew.
        if (pendingShotPath != null) {
            writeScreenshot(pendingShotPath);
            pendingShotPath = null;
        }

        framesRendered++;
        if (population != null && smokeFrames > 0
                && (framesRendered == 1 || framesRendered % 60 == 0 || framesRendered == smokeFrames)) {
            reportTrackedMover();
        }
        boolean smokeDone = smokeFrames > 0 && framesRendered >= smokeFrames;
        if (smokeDone) {
            System.out.println("observer smoke test: rendered " + framesRendered + " frames OK");
            if (screenshotPath != null) {
                writeScreenshot(screenshotPath);
            }
            Gdx.app.exit();
            return;
        }
        // ESC quits behind a two-press confirmation on populated fixtures (Sprint 4
        // playtest fix: one keystroke used to end a session, and no save verb exists).
        // The tavern walkthrough keeps the old instant exit — it is a dev fixture with
        // nothing to lose and no toast surface to confirm on.
        quitGuard.update(deltaSeconds);
        if (Gdx.input.isKeyJustPressed(Input.Keys.ESCAPE) && !escConsumedByTalk) {
            if (population == null || quitGuard.press()) {
                Gdx.app.exit();
            } else {
                toasts.add(QuitGuard.CONFIRM_TOAST);
            }
        }
    }

    /**
     * Dispatches one frame's scripted actions ({@link ObserverScript}) through the same
     * deterministic seams the live input wrappers call. Populated fixtures only (the
     * tavern has no inspector to script against); camera verbs work everywhere.
     */
    private void applyScriptFrame(int frame) {
        for (ObserverScript.Action action : script.at(frame)) {
            switch (action.verb()) {
                case SELECT -> inspector.select(action.intArgs()[0]);
                case FOLLOW -> inspector.toggleFollow();
                case PLAY -> PlayModeInput.applyPlayToggle(playMode, inspector,
                        population.registry());
                case HOLD -> {
                    int[] d = action.intArgs();
                    scriptMoveDx = d[0];
                    scriptMoveDy = d[1];
                }
                case TALK -> TalkInput.applyTalk(talk, playMode, population.registry(),
                        population.jobs(), population.identity(),
                        population.system().factionStandings(), population.relationships(),
                        barkTables, toasts, population.questRegistry(),
                        population.system().questLog(), bootWorldSeed, driver.currentTick(),
                        population::askTopicsOf, population.topicCatalog());
                case TOPIC -> TalkInput.applyAsk(talk, playMode, population.registry(),
                        population.jobs(), population.identity(),
                        population.system().factionStandings(), population.relationships(),
                        barkTables, population.questRegistry(),
                        population.system().questLog(), bootWorldSeed, driver.currentTick(),
                        com.trojia.client.inspect.TalkTopics.indexOfKeyNumber(
                                action.intArgs()[0]));
                case PICKPOCKET -> TheftInput.applyPickpocket(playMode,
                        population.registry(), population.identity(), toasts);
                case JOURNAL -> {
                    journalOpen = !journalOpen;
                    if (journalOpen) {
                        mastersOpen = false;
                    }
                }
                case MASTERS -> {
                    mastersOpen = !mastersOpen;
                    if (mastersOpen) {
                        journalOpen = false;
                    }
                }
                case FILTER -> inspector.cycleFeedFilter();
                case PLATES -> scriptPlatesHeld = !scriptPlatesHeld;
                case ZOOM -> camera.setZoom(action.intArgs()[0]);
                case CENTER -> {
                    int[] c = action.intArgs();
                    camera.centerOnTile(c[0], c[1]);
                }
                case Z -> zLevel.to(action.intArgs()[0]);
                case SHOT -> pendingShotPath = action.args();
                case CLIMB -> {
                    // args: "up"/"down", optionally ",quiet" (probe mode: no refusal toast
                    // — a tape sweeping a street for the connector under its feet).
                    String[] parts = action.args().split(",");
                    ClimbInput.applyClimb(playMode, population.registry(),
                            population.zLinks(), toasts,
                            "down".equalsIgnoreCase(parts[0].trim()) ? -1 : +1,
                            parts.length < 2 || !"quiet".equalsIgnoreCase(parts[1].trim()));
                }
                case EAT -> EatInput.applyEat(playMode, population.registry(), toasts,
                        eatFeedbackTracker);
                case FISH -> FishInput.applyFish(playMode, population.registry(), toasts,
                        fishFeedbackTracker);
                case CULL -> CullInput.applyCull(playMode, population.registry(), toasts,
                        cullFeedbackTracker, driver.currentTick());
                case SELL -> SellInput.applySell(playMode, population.registry(), toasts,
                        sellFeedbackTracker);
                case CAST -> SpellInput.applyCast(playMode, population.registry(),
                        population.identity(),
                        population.system().spells(), population.system().skillTracks(),
                        population.system().spells().rawOf(action.args().trim()),
                        toasts, spellFeedbackTracker, driver.currentTick());
                case FPV -> applyViewToggle();
                case TURN -> FirstPersonInput.applyTurn(eye, action.intArgs()[0]);
                case STEP -> {
                    int[] fs = action.intArgs();
                    scriptForward = fs[0];
                    scriptStrafe = fs[1];
                }
                case LOOK -> eye.setLookShear(action.intArgs()[0], camera.viewportHeightPx());
                case FACE -> eye.setYaw((float) Math.toRadians(action.intArgs()[0] - 90));
            }
        }
    }

    /** Refused when nothing is being driven: first person needs eyes to look through. */
    static final String FIRST_PERSON_NEEDS_ACTOR =
            "Select an actor and press P before switching to first person.";

    /** Which actor the eye is currently seated behind, so a hand-over reseeds the yaw once. */
    private int eyeActorId = Actor.NONE;

    /**
     * The V toggle. A no-op-with-an-explanation while nothing is driven; otherwise it starts
     * the cross-fade. It does not seed the eye — {@link #trackEye} has been doing that since
     * the moment the actor was taken over, which is why the facing wedge on the map already
     * showed the direction the first-person frame is about to open on.
     *
     * <p>What it does do on the way IN is make the frame open on what the wedge promised:
     * level the horizon (a shear left over from the last time you were in first person is not
     * an aim you took, and it re-opens the view staring at the floor) and stop any travel-yaw
     * swing still running (that swing is the tile view's, and the camera's own contract says
     * the yaw in first person is the player's alone).
     */
    private void applyViewToggle() {
        if (playMode == null || !playMode.active()) {
            if (toasts != null) {
                toasts.add(FIRST_PERSON_NEEDS_ACTOR);
            }
            return;
        }
        if (viewMode.toggle(camera.zoom()) == ViewModeState.Mode.FIRST_PERSON) {
            eye.levelTheHorizon();
            eye.cancelTravelSwing();
        }
    }

    /**
     * THE DECOUPLING, once per frame: hand the eye the cell the sim has actually committed the
     * driven actor to, and let it ease. The actor is a tile-stepper with an occupancy cap and a
     * speed gate and none of that changes; the eye is a continuous position that happens to be
     * on its way there.
     *
     * <p>Taking over a new actor seats the eye instantly and seeds the view yaw from that
     * actor's {@code facing()} — the only time the sim's four-way facing is read into the
     * continuous yaw, and it is never written back. See {@link FirstPersonCamera}.
     *
     * <p>While the tile view is the one on screen the step also aims the yaw, so the facing
     * wedge drawn on the driven actor turns as it walks. In first person it does not: there the
     * movement keys are already relative to the yaw, and adopting the travelled direction would
     * snap the player's aim to the nearest of eight every time they took a step.
     */
    private void trackEye(float deltaSeconds) {
        viewMode.advance(deltaSeconds);
        if (playMode == null || !playMode.active()) {
            // Not a hard cut: if the first-person frame is up when the body is let go, it fades
            // out the way it faded in. Every other mode change in this client is a transition.
            // The eye keeps hold of the released actor's id until the fade lands, so the body
            // you were just looking out of does not pop into the middle of its own last frame.
            viewMode.releaseToTopDown();
            if (!viewMode.firstPersonVisible()) {
                eyeActorId = Actor.NONE;
            } else {
                // The fade is still showing the first-person frame, so the eye still has to
                // walk: it was mid-stride when the body was let go, and skipping this froze
                // the picture solid for the whole 0.34 s while it faded. It reads nothing new
                // from the sim — it is finishing the ease it already had.
                eye.advance(deltaSeconds);
            }
            return;
        }
        int playedId = playMode.playedActorId();
        Actor played = population.registry().get(playedId);
        int cell = played.cell();
        if (playedId != eyeActorId || !eye.isPlaced()) {
            eyeActorId = playedId;
            eye.snapTo(PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell),
                    ViewFacing.yawOfFacing(played.facing()));
        } else {
            eye.followCell(PackedPos.x(cell), PackedPos.y(cell), PackedPos.z(cell),
                    !viewMode.firstPersonVisible());
        }
        eye.advance(deltaSeconds);
    }

    /**
     * The arrowhead on the driven actor in the tile view, pointing along the view yaw — half
     * of what makes the switch survivable (see {@link FacingWedge}). Drawn over the actor
     * sprite, under every HUD surface.
     */
    private void drawFacingWedge() {
        if (playMode == null || !playMode.active() || !eye.isPlaced()) {
            return;
        }
        int cell = population.registry().get(playMode.playedActorId()).cell();
        if (PackedPos.z(cell) != zLevel.z()) {
            return; // the driven actor is on another floor; the wedge belongs with the body
        }
        int span = camera.tileSpanPx();
        float centreX = camera.tileToScreenX(PackedPos.x(cell)) + span / 2f;
        float centreY = camera.viewportHeightPx()
                - (camera.tileToScreenY(PackedPos.y(cell)) + span / 2f);
        FacingWedge.draw(batch, icons.whitePixel(),
                FacingWedge.corners(centreX, centreY, span, eye.yaw()),
                com.badlogic.gdx.graphics.Color.toFloatBits(1f, 0.85f, 0.35f, 0.92f));
    }

    /**
     * Plans and draws the first-person frame. The plan is a pure function of the tile lanes
     * and the eye; the renderer below it only looks up textures. Actor positions are re-bucketed
     * every frame — nothing is cached across a tick, the same no-staleness contract the tile
     * renderers keep.
     *
     * <p>{@link #eyeActorId} is handed to the planner as the actor to hide: it is the body this
     * frame is being seen out of, and without that the driven actor's own sprite stands in the
     * middle of its own view for most of every stride (the eye slides between cells while the
     * sim has already committed the body to the one ahead).
     */
    private void drawFirstPerson(AmbientLight ambient) {
        fpvActors.refresh();
        EyeProjection projection = EyeProjection.of(eye.eyeX(), eye.eyeY(), eye.eyeHeight(),
                eye.yaw(), camera.viewportWidthPx(), camera.viewportHeightPx(),
                EyeProjection.DEFAULT_FOV_DEGREES, eye.lookShearPx());
        List<ViewQuad> plan = fpvPlanner.plan(projection, eye.band(), cellSight, fpvActors,
                eyeActorId);
        fpvRenderer.draw(batch, plan, projection, icons.whitePixel(), ambient, viewMode.blend());
    }

    /** Debug/verification aid only: dumps the current framebuffer to a PNG at {@code path}. */
    private void writeScreenshot(String path) {
        int w = camera.viewportWidthPx();
        int h = camera.viewportHeightPx();
        Pixmap pixmap = Pixmap.createFromFrameBuffer(0, 0, w, h);
        flipVertically(pixmap);
        FileHandle handle = Gdx.files.absolute(path);
        PixmapIO.writePNG(handle, pixmap);
        pixmap.dispose();
        System.out.println("observer: wrote screenshot to " + path);
    }

    /** glReadPixels (behind getFrameBufferPixmap) is bottom-up; PNGs are top-down. */
    private static void flipVertically(Pixmap pixmap) {
        int w = pixmap.getWidth();
        int h = pixmap.getHeight();
        // Blending OFF: drawPixel composites by default, so any pixel the frame left with
        // alpha below 1 — pooled water, a mid-cross-fade first-person frame — would blend a
        // mirror image of the screen into itself and read as a rendering bug in the evidence
        // rather than as the screenshot artifact it is.
        Pixmap.Blending previous = pixmap.getBlending();
        pixmap.setBlending(Pixmap.Blending.None);
        for (int y = 0; y < h / 2; y++) {
            for (int x = 0; x < w; x++) {
                int top = pixmap.getPixel(x, y);
                int bottom = pixmap.getPixel(x, h - 1 - y);
                pixmap.drawPixel(x, y, bottom);
                pixmap.drawPixel(x, h - 1 - y, top);
            }
        }
        pixmap.setBlending(previous);
    }

    /**
     * While follow is active, re-center the camera on the selected actor's live tile every
     * frame (read fresh from the registry — no cached position). The viewed z-level snaps
     * to the actor's floor only on an EDGE — target change or the actor changing bands
     * ({@link FollowZSnap}, the Sprint-4 playtest fix) — so a manual z-scrub peek survives
     * until the followed soul actually takes a stair. A no-op with no selection or follow
     * off — free camera then.
     */
    private void applyFollowCamera() {
        if (inspector == null || !inspector.followActive()) {
            followZSnap.reset();
            return;
        }
        int actorId = inspector.selectedActorId();
        int cell = population.registry().get(actorId).cell();
        if (followZSnap.shouldSnap(actorId, PackedPos.z(cell))) {
            zLevel.to(PackedPos.z(cell));
        }
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

    /**
     * Builds the fixture's static lamp-influence map from its authored source map's
     * {@code light_source} markers (the importer does not bake markers yet — see
     * {@link LampMarkersLoader}). Degrades to {@link LampGlowMap#EMPTY} when the source
     * map is not present (e.g. a checkout without {@code content/maps/src}): the night
     * still darkens, there are just no lamp pools.
     */
    private static LampGlowMap loadLampGlow(Fixture fixture, int worldWidthTiles,
            int worldHeightTiles) {
        String tmxName = switch (fixture) {
            case TAVERN -> "tavern_fixture.tmx";
            case COMPOUND -> "compound_block.tmx";
            case DOCKS -> "docks_surface.tmx";
        };
        List<LampGlowMap.Lamp> lampMarkers;
        try {
            lampMarkers = LampMarkersLoader.load(
                    RepoPaths.locate("content", "maps", "src", tmxName));
        } catch (IllegalStateException e) {
            lampMarkers = List.of(); // no content/maps/src in this checkout
        }
        System.out.println("observer: lamp light sources loaded: " + lampMarkers.size());
        return lampMarkers.isEmpty() ? LampGlowMap.EMPTY
                : new LampGlowMap(lampMarkers, worldWidthTiles, worldHeightTiles);
    }

    /**
     * Reads the fixture's authored {@code place_sign} markers — the ward's names — from the
     * same source map the lamps come from (marker baking is still deferred; see
     * {@link PlaceSignsLoader}). Degrades to an empty list when {@code content/maps/src} is
     * not in this checkout: the ward simply goes unlabelled.
     */
    private static List<PlaceSign> loadPlaceSigns(Fixture fixture) {
        String tmxName = switch (fixture) {
            case TAVERN -> "tavern_fixture.tmx";
            case COMPOUND -> "compound_block.tmx";
            case DOCKS -> "docks_surface.tmx";
        };
        List<PlaceSign> signs;
        try {
            signs = PlaceSignsLoader.load(RepoPaths.locate("content", "maps", "src", tmxName));
        } catch (IllegalStateException e) {
            signs = List.of(); // no content/maps/src in this checkout
        }
        System.out.println("observer: named places loaded: " + signs.size());
        return signs;
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
