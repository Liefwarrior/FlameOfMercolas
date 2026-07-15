package com.trojia.client.scenario;

import com.trojia.client.boot.RepoPaths;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRawsLoader;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.ActorTypeStatsTable;
import com.trojia.sim.actor.ActorTypes;
import com.trojia.sim.actor.ActorsSystem;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.HouseholdFormer;
import com.trojia.sim.actor.HouseholdRaws;
import com.trojia.sim.actor.HouseholdRawsLoader;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobBinder;
import com.trojia.sim.actor.job.JobId;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.AnimalActor;
import com.trojia.sim.actor.type.AnimalKeeper;
import com.trojia.sim.actor.type.DiscipleOfTheFlame;
import com.trojia.sim.actor.type.FeralActor;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.PriestOfTheFlame;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Shopkeeper;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.World;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * The deterministic Docks-ward population (DOCKS-GAZETTEER.md §3/§4, ACTORS-SPEC.md §11.4):
 * a whole-district roster (~350+ actors) spawned onto the baked {@code docks_surface} world —
 * every keyed establishment staffed (proprietor + hired staff with EMPLOYER edges), all four
 * residential Compounds and all 45 hovels occupied by {@link HouseholdFormer} households,
 * rooftop slums packed with Wastrels (villain jobs hidden under streetlife covers), Watch
 * patrols at the gazetteer's posts, Animal Keepers with beasts, Ferals scavenging, clergy at
 * the Mission, and a dockworker Serf mass whose job anchors are the berths/piers/shipyard so
 * the waterfront visibly works during the day (JobBehaviors {@code pursueAtAnchor} commuting).
 * Shared by the observer's docks boot path ({@code ObserverApp --fixture=docks}) and the
 * headless proof listing ({@code DocksActorsMain}).
 *
 * <p><b>Scale vs. the lore target.</b> The ward's lore-derived target is ~2,750 people /
 * ~1,100 households (gazetteer §2.5, canon troop-number derivation — see
 * {@link CompoundBlockPopulation}'s Javadoc). This fixture realizes the surface geometry's
 * densest sustainable slice (~13% of the ward); the head-count split per dwelling/business is
 * <b>(placeholder)</b> — canon fixes no per-site staffing.
 *
 * <p><b>Placement.</b> Actors live in <em>world-tile</em> coordinates, matching the
 * {@code TiledWorldImporter} placement rule: authored map cell {@code (x, y)} on z-level
 * {@code z} lands at world tile {@code (CHUNK_SIZE_X + x, CHUNK_SIZE_Y + y,
 * CHUNK_SIZE_Z + z)} (this map's authored z-range is {@code +0..+15}, so {@code minZ = 0}).
 * All map-cell anchors below are read from the committed {@code docks_surface.tmx}
 * {@code markers} object layers (pixel/16, floor-divided); the few work-stand cells with no
 * marker (Terrace Walk, Saltgate porters' stand, eel-pot stall fronts) are derived from
 * known street coordinates on authored walkable floor, the same convention
 * {@code CompoundBlockPopulation} uses for its courtyard stall.
 *
 * <p><b>The z rule (binding).</b> {@code Actor#stepToward} never crosses z-levels, so every
 * actor's home and work anchor sit on the SAME authored z: Band A residents (z:+11) work the
 * waterfront/Band-A shops, Band B residents (z:+12) work Terrace Walk/Saltgate/the goat pen,
 * Band C residents (z:+13) work the well plaza/Gallows Row, and roof-slum Wastrels drift on
 * their own roof plane. Cross-band movement is a later pathfinding milestone.
 *
 * <p><b>Determinism.</b> Actors spawn in one fixed order (ascending ActorId by construction);
 * the only randomness — household grouping and employer hiring — flows through
 * {@link HouseholdFormer}'s named {@code household.*} RNG streams, never
 * {@code java.util.Random}.
 */
public final class DocksPopulation implements ScenarioPopulation {

    // ---- authored z planes (docks_surface.tmx bands, gazetteer §2.1) --------------------
    private static final int ZA = 11;   // Band A quayside walk plane
    private static final int ZB = 12;   // Band B mid-slope walk plane
    private static final int ZC = 13;   // Band C upper walk plane

    // ---- waterfront work anchors (markers, z:+11) ----------------------------------------
    private static final int[] BERTH_01 = {21, 27};
    private static final int[] BERTH_02 = {41, 28};
    private static final int[] BERTH_03 = {61, 27};
    private static final int[] CRANE = {41, 29};
    private static final int[] MUSTER_QUAY = {45, 30};
    private static final int[] PIER_01 = {99, 8};
    private static final int[] PIER_02 = {107, 8};
    private static final int[] PIER_03 = {115, 10};
    private static final int[] PIER_04 = {123, 24};

    // ---- ship-crew hull anchors (markers `ship_k3*_anchor`, z:+11 — 2026-07-14 crew pass) ----
    private static final int[] SHIP_K30_KESTREL = {66, 20};
    private static final int[] SHIP_K31_BREGGAS_PROMISE = {64, 13};
    private static final int[] SHIP_K32_DEEPKEEL = {63, 4};

    // ---- establishment anchors (markers `business_k*`, z:+11) ---------------------------
    // DEV (Eli 2026-07-13 sizing pass, DOCKS-GAZETTEER.md 3.1): every K01-K25 anchor
    // below moved to match the resized footprints in gen_docks_surface.py; K26-K29/K34
    // are new. Values are read straight from the regenerated docks_surface.tmx markers.
    private static final int[] K01_WEIGHHOUSE = {64, 41};
    private static final int[] K02_IMPOUND = {63, 56};
    private static final int[] K03_GILDED_GULL = {120, 41};
    private static final int[] K04_BILGE = {106, 38};
    private static final int[] K05_LANTERN_ROOM = {64, 71};
    private static final int[] K06_HARLS_YARD = {141, 41};
    private static final int[] K07_ROPEWALK = {36, 85};
    private static final int[] K08_BRANNS = {27, 69};
    private static final int[] K09_PITCHFIELD = {10, 46};
    private static final int[] K10_DAWNSTALLS = {46, 41};
    private static final int[] K11_SALT_ROW = {45, 51};
    private static final int[] K12_KINGS_BOND = {90, 40};
    private static final int[] K13_DROWNED_HOLD = {184, 47};   // clue_c3_drowned_hold (squat)
    private static final int[] K14_WRACKHOUSE = {168, 37};
    private static final int[] K15_FENNERS = {125, 56};
    private static final int[] K17_MISSION = {88, 71};
    private static final int[] K18_BATHHOUSE = {107, 73};
    private static final int[] K19_ROWS = {109, 55};
    private static final int[] K20_MERLES = {88, 18};
    private static final int[] K22_NETMENDERS = {45, 34};
    private static final int[] K23_COOPERS = {47, 70};
    private static final int[] K25_KENNEL_ROW = {167, 54};
    private static final int[] SAILMAKER = {11, 73};           // K26 Sailmaker's Loft
    private static final int[] K27_HARDTACK = {35, 74};
    // K28/K29/K34 moved again (overlap-audit pass, Eli 2026-07-13): the sizing
    // pass's lots physically overlapped pre-existing hovels (and, for K28/K34,
    // each other) in real map coordinates -- relocated in gen_docks_surface.py;
    // anchors below match the regenerated docks_surface.tmx markers.
    private static final int[] K28_SLOPCHEST = {133, 62};
    private static final int[] K29_LONGSTORE = {88, 87};
    private static final int[] K34_GUARDHOUSE = {106, 85};
    // The arrest holding cell (ARREST-SPEC addendum): gen_docks_surface.py's K34 cage bars are
    // two STEEL_WALL tiles at (102,84)/(103,84) -- not walkable floor by construction -- so the
    // actual escort target is the granite floor immediately beside them. No dedicated marker
    // was authored for this cell; derived the same way this file's other unmarked work stands
    // are (TERRACE_WALK_STAND et al., "no marker; known street/floor cells, precedent
    // convention"), pinned against the committed gen_docks_surface.py cage geometry.
    private static final int[] HOLDING_CELL_K34 = {103, 85};
    private static final int[] LAIR_SKYRUNNER = {189, 88};     // K35, z:+13, unmarked
    private static final int[] MISSION_BUNKS = {85, 78};
    private static final int[] MISSION_GARDEN = {90, 88};
    private static final int[] IMPOUND_DOG = {60, 57};
    private static final int[][] KENNEL_DOGS = {{165, 54}, {171, 50}, {171, 53}};
    private static final int[] PEN_GOATS = {152, 111};         // z:+12

    // Eel-pot stall fronts: the Tarwalk cells beside the four authored stall counters
    // (lamp_eelpot_01..04 sit on these) — derived work stands, no dedicated marker.
    private static final int[][] EELPOT_STALLS = {{85, 32}, {97, 32}, {105, 32}, {113, 32}};

    // ---- tavern patron seat anchors (markers `patron_seat_*`, 2026-07-15 interior-detail
    // pass, design §4) — seated, home-elsewhere residents, not staff.
    private static final int[][] PATRON_SEATS_GULL =
            {{117, 37}, {118, 37}, {117, 42}, {118, 42}, {119, 40}, {121, 40}};
    private static final int[][] PATRON_SEATS_BILGE = {{102, 40}, {101, 39}, {108, 40}, {110, 41}};
    private static final int[][] PATRON_SEATS_LANTERN = {{59, 71}, {61, 72}, {62, 72}, {65, 73}};

    // ---- the carter's stand + hitching posts (markers, 2026-07-15 interior-detail pass,
    // design §6 — completes the gazetteer §4.2 Animal Keeper roster: kennelmaster, impound
    // keeper, goatherd, and now the carter, blessed in the gazetteer but never spawned) --------
    private static final int[] CARTER_STAND = {50, 30};
    private static final int[] HITCH_GULL = {125, 33};
    private static final int[] HITCH_BILGE = {102, 33};
    private static final int[] HITCH_ROWS = {109, 51};

    // ---- Watch posts (markers; K21 + gazetteer §4.2 beat) --------------------------------
    private static final int[] WATCHPOST_K21 = {67, 122};      // z:+13
    private static final int[] PATROL_RISE_TOP = {75, 118};    // z:+13
    private static final int[] NOTICE_BOARD = {73, 119};       // z:+13
    private static final int[] PATROL_RISE_FOOT = {75, 34};
    private static final int[] PATROL_TARWALK_WEST = {30, 30};
    private static final int[] PATROL_TARWALK_MID = {100, 30};
    private static final int[] WATCH_BOND_POST = {90, 33};
    // ARREST-SPEC addendum: every post above sits on Band-A ground (z:+11) or the Rise
    // (z:+13, x62-80) -- none is within the same-z + chebyshev-8 detection window of ANY
    // Job.Villain actor (all six live on rooftop planes z:+13/z:+14, x136+). Without a post
    // actually up there, the new arrest mechanic could never trigger in this fixture. This
    // one reaches C2's roof deck (open REMAN_FLOOR between roofhut_10/11, gen_docks_surface.py
    // C2_HUTS) -- within chebyshev 5 of both the Cutpurse (roofhut_10) and the Skyrunner
    // (roofhut_11) hiding there under their wastrel.streetlife covers.
    private static final int[] C2_ROOF_WATCHPOST = {159, 74};

    // ---- Compound C1 Quayward (grand, Band B: ground z:+12, upper z:+13) -----------------
    private static final int[] C1_MANSION = {19, 106};
    private static final int[][] C1_CONDOS_GROUND =
            {{37, 100}, {49, 100}, {61, 100}, {37, 112}, {49, 112}, {61, 112}};
    private static final int[][] C1_CONDOS_UPPER =
            {{37, 100}, {49, 100}, {61, 100}, {37, 112}, {49, 112}, {61, 112}};
    private static final int[] C1_COURTYARD = {43, 107};

    // ---- Compound C2 Netters' (mid, Band A: ground z:+11, upper z:+12, roof z:+13) -------
    private static final int[] C2_MANSION = {121, 79};
    private static final int[][] C2_CONDOS_GROUND =
            {{131, 70}, {147, 70}, {137, 89}, {157, 70}, {157, 80}, {157, 89}};
    private static final int[][] C2_CONDOS_UPPER = {{157, 70}, {157, 80}, {157, 89}};
    private static final int[][] C2_ROOFHUTS = {{155, 70}, {158, 79}, {155, 88}};

    // ---- Compound C3 Saltgate Terrace (cramped, ground z:+12, upper z:+13, roof z:+14) ---
    private static final int[] C3_MANSION = {89, 108};
    private static final int[][] C3_CONDOS_GROUND =
            {{127, 104}, {135, 104}, {101, 111}, {112, 111}, {123, 111}, {134, 111}};
    private static final int[][] C3_CONDOS_UPPER = {{101, 111}, {112, 111}, {123, 111}, {134, 111}};
    private static final int[][] C3_ROOFHUTS = {{102, 112}, {128, 111}};
    private static final int[] C3_COURTYARD = {111, 104};

    // ---- Compound C4 Gullet (decayed, ground z:+11, upper z:+12, roof z:+13) -------------
    private static final int[][] C4_CONDOS_GROUND =
            {{168, 72}, {168, 86}, {173, 70}, {185, 70}, {187, 84}};
    private static final int[] C4_CONDO_UPPER = {187, 84};
    private static final int[][] C4_ROOFHUTS = {{185, 79}, {188, 83}, {185, 88}, {189, 92}};
    private static final int[] C4_RUIN = {177, 89};

    // ---- hovels (markers `hovel_NN_anchor`, grouped by band) ------------------------------
    private static final int[][] HOVELS_A = {{84, 55}, {89, 54}, {95, 56}, {10, 93}, {22, 93},
            {184, 63}, {189, 62}, {136, 52}, {146, 53}};
    private static final int[][] HOVELS_B =
            {{146, 103}, {153, 104}, {161, 103}, {170, 105}, {180, 103}, {186, 108}};
    private static final int[][] HOVELS_C = {{12, 117}, {19, 117}, {28, 117}, {36, 117},
            {46, 117}, {54, 117}, {86, 117}, {94, 117}, {110, 117}, {120, 117}, {130, 117},
            {140, 117}, {150, 117}, {168, 117}, {178, 117}, {10, 124}, {20, 124}, {30, 124},
            {42, 124}, {52, 124}, {86, 124}, {94, 124}, {108, 124}, {118, 124}, {128, 124},
            {138, 124}, {148, 124}, {158, 124}, {168, 124}, {178, 124}};

    // ---- derived work stands (no marker; known street/floor cells, precedent convention) --
    private static final int[] TERRACE_WALK_STAND = {100, 98};   // z:+12 brick Terrace Walk
    private static final int[] SALTGATE_PORTERS = {75, 105};     // z:+12 Saltgate roadbed
    private static final int[] WELL_PLAZA = {101, 122};          // z:+13 well_gallows_row_anchor
    private static final int[] ABBEY_LANE = {133, 124};          // z:+13 abbey lane dirt
    // K06 Harl's Yard timber store yard aisle (border/trect-derived, gen_docks_surface.py
    // border(11,150,36,159,46) fence + trect log-stack rows at y39/42/45) — sits in the open
    // aisle between the y39 and y42 stack rows, same "no marker; known street/floor cells"
    // convention as the rest of this section (2026-07-14 warehouse-crew pass).
    private static final int[] TIMBER_YARD_STAND = {155, 40};    // z:+11

    // The waterfront job cycle: where Band-A dockworker households commute (all z:+11).
    private static final int[][] DOCK_WORK = {BERTH_01, PIER_02, BERTH_02, PIER_01, BERTH_03,
            PIER_03, CRANE, PIER_04, MUSTER_QUAY, K06_HARLS_YARD};
    // Band-A non-dock day work mixed into the hovel rotation.
    private static final int[][] BAND_A_WORK = {BERTH_01, PIER_02, K10_DAWNSTALLS, BERTH_02,
            K11_SALT_ROW, PIER_01, K07_ROPEWALK, BERTH_03, MUSTER_QUAY, PIER_03, CRANE, PIER_04};
    private static final int[][] BAND_B_WORK = {TERRACE_WALK_STAND, SALTGATE_PORTERS};
    private static final int[][] BAND_C_WORK = {WELL_PLAZA, NOTICE_BOARD, ABBEY_LANE};

    // Where hired shop staff lodge (all Band A, z:+11): the compounds' ground condos, the
    // Band-A hovels, and the Rows flophouse — a rotating cursor so staff spread across the
    // district and every morning fills the streets with commuters.
    private static final int[][] STAFF_LODGINGS = {{131, 70}, {147, 70}, {137, 89}, {157, 70},
            {157, 80}, {157, 89}, {168, 72}, {168, 86}, {173, 70}, {185, 70}, {187, 84},
            {84, 55}, {89, 54}, {95, 56}, {10, 93}, {22, 93}, {184, 63}, {189, 62}, {136, 52},
            {146, 53}, {110, 55}};

    // Household size rotation (mean 2.4 — household.json register) applied per dwelling.
    private static final int[] HOUSEHOLD_SIZES = {2, 3, 2, 1, 4};

    // ---- placeholder ItemsLite kind ids (no items-raws system yet, §11.2) ---------------
    private static final short KIND_COIN = 1;
    private static final short KIND_STOCK = 2;
    private static final short KIND_ALMS_TOKEN = 3;
    private static final short KIND_SCRAP = 4;
    private static final short KIND_LOCKPICK = 5;
    private static final short KIND_FEED = 6;
    private static final short KIND_CUDGEL = 7;

    private final ActorsSystem system;
    private final ActorTypeStatsTable typeStats;
    private final JobRegistry jobs;
    private final HomeRegistry homes;
    private final RelationshipRegistry relationships;
    private final ItemsLiteRegistry items;
    private final ActorRegistry registry;
    private final long worldSeed;
    private final int trackedGroundMoverId;
    private final List<Integer> moverIds;

    private DocksPopulation(ActorsSystem system, ActorTypeStatsTable typeStats,
            JobRegistry jobs, HomeRegistry homes, RelationshipRegistry relationships,
            ItemsLiteRegistry items, ActorRegistry registry, long worldSeed,
            int trackedGroundMoverId, List<Integer> moverIds) {
        this.system = system;
        this.typeStats = typeStats;
        this.jobs = jobs;
        this.homes = homes;
        this.relationships = relationships;
        this.items = items;
        this.registry = registry;
        this.worldSeed = worldSeed;
        this.trackedGroundMoverId = trackedGroundMoverId;
        this.moverIds = List.copyOf(moverIds);
    }

    @Override
    public ActorsSystem system() {
        return system;
    }

    @Override
    public ActorRegistry registry() {
        return registry;
    }

    @Override
    public HomeRegistry homes() {
        return homes;
    }

    @Override
    public RelationshipRegistry relationships() {
        return relationships;
    }

    @Override
    public ItemsLiteRegistry items() {
        return items;
    }

    @Override
    public JobRegistry jobs() {
        return jobs;
    }

    public ActorTypeStatsTable typeStats() {
        return typeStats;
    }

    public long worldSeed() {
        return worldSeed;
    }

    /**
     * A Band-A actor deliberately spawned displaced from its home with a depleted REST need
     * so its {@code RETURN_HOME} policy fires immediately and it visibly walks home as the
     * world ticks — the movement the render/smoke proof tracks.
     */
    @Override
    public int trackedGroundMoverId() {
        return trackedGroundMoverId;
    }

    /**
     * The ids of every actor deliberately displaced from its home at spawn (movers), in
     * ascending id order — one per walk-plane band (z:+11 / z:+12 / roof z:+13).
     */
    public List<Integer> moverIds() {
        return moverIds;
    }

    /**
     * Builds the full population over a fresh set of registries, keyed to {@code worldSeed}
     * (the baked world's own seed). Loads the committed actor/job/household raws from
     * {@code content/raws}. World-less convenience (no collision checking — every cell reads
     * as walkable); prefer {@link #build(long, World)} wherever a baked world is in scope.
     */
    public static DocksPopulation build(long worldSeed) {
        return build(worldSeed, null);
    }

    /**
     * Builds the full population exactly as {@link #build(long)}, but wires {@code world} into
     * the {@link ActorsSystem} so {@code JobBehaviors}' patrol/wander/commute movement respects
     * real walls, doors and water (§2.5's walkability check). {@code world} may be {@code null}
     * (equivalent to {@link #build(long)}).
     */
    public static DocksPopulation build(long worldSeed, World world) {
        Path rawsRoot = RepoPaths.locate("content", "raws");
        ActorTypeStatsTable typeStats = ActorRawsLoader.load(rawsRoot.resolve("actors"));
        HouseholdRaws householdRaws = HouseholdRawsLoader.load(
                rawsRoot.resolve("actors").resolve("household.json"));
        JobRegistry jobs = JobBinder.bind(rawsRoot.resolve("jobs").resolve("jobs.json"),
                ActorTypes.allTypeIds());

        ActorRegistry registry = new ActorRegistry();
        HomeRegistry homes = new HomeRegistry();
        RelationshipRegistry relationships = new RelationshipRegistry();
        ItemsLiteRegistry items = new ItemsLiteRegistry();

        Builder builder = new Builder(registry, homes, relationships, items, typeStats, jobs,
                householdRaws, worldSeed);
        builder.populate();

        int arrestHoldCell = worldCell(HOLDING_CELL_K34, ZA);
        ActorsSystem system = new ActorsSystem(worldSeed, typeStats, jobs, registry, homes,
                relationships, items, world, arrestHoldCell);
        return new DocksPopulation(system, typeStats, jobs, homes, relationships, items,
                registry, worldSeed, builder.trackedGroundMoverId, builder.movers);
    }

    /** Packs an authored map cell {@code (mapX, mapY)} on z-level {@code mapZ} to its world tile. */
    private static int worldCell(int[] mapXY, int mapZ) {
        return PackedPos.pack(Coords.CHUNK_SIZE_X + mapXY[0], Coords.CHUNK_SIZE_Y + mapXY[1],
                Coords.CHUNK_SIZE_Z + mapZ);
    }

    /** The mutable spawn walker — all wiring lives here so the outer type stays an immutable handle. */
    private static final class Builder {
        private final ActorRegistry registry;
        private final HomeRegistry homes;
        private final RelationshipRegistry relationships;
        private final ItemsLiteRegistry items;
        private final ActorTypeStatsTable typeStats;
        private final JobRegistry jobs;
        private final HouseholdRaws householdRaws;
        private final long seed;
        private final List<Integer> movers = new ArrayList<>();
        private int trackedGroundMoverId = Actor.NONE;
        private int lodgingCursor;
        private int dockCursor;

        Builder(ActorRegistry registry, HomeRegistry homes, RelationshipRegistry relationships,
                ItemsLiteRegistry items, ActorTypeStatsTable typeStats, JobRegistry jobs,
                HouseholdRaws householdRaws, long seed) {
            this.registry = registry;
            this.homes = homes;
            this.relationships = relationships;
            this.items = items;
            this.typeStats = typeStats;
            this.jobs = jobs;
            this.householdRaws = householdRaws;
            this.seed = seed;
        }

        void populate() {
            // Spawn order is fixed => ActorIds deterministic; the only RNG is HouseholdFormer's
            // named draws. Sections run in gazetteer order: establishments, Compounds, hovels,
            // Watch, beasts, ferals, then the cross-cutting wiring passes.

            // ===================== K-ESTABLISHMENTS (proprietor + hired staff) =================
            // Every staffed business: a Shopkeeper living over the shop (home == anchor) plus
            // hired Serf staff lodging across the district's Band-A dwellings and commuting to
            // the counter every shift (pursueAtAnchor round trips + EMPLOYER edges).
            Actor weighmaster = business(K01_WEIGHHOUSE, 4);   // K01 tariff clerks (2026-07-14: +2)
            Actor gullHost = business(K03_GILDED_GULL, 6);     // K03 large tavern (+3)
            business(K04_BILGE, 4);                            // K04 mid tavern (+2)
            Actor lanternLandlady = business(K05_LANTERN_ROOM, 4);   // (+2)

            // ===================== TAVERN PATRONS (2026-07-15 interior-detail pass, design §4) ==
            // Bunk-at-seat (see spawnPatrons' Javadoc for why a nextLodging()-homed commuter
            // never reliably arrives): a patron is a real district resident, not an employee, so
            // there's no hire() EMPLOYER edge -- just always seated. Type mix matched to each
            // tavern's own gazetteer character (14 patrons total).
            spawnPatrons(PATRON_SEATS_GULL, Serf.TYPE, Serf.TYPE, Serf.TYPE, Serf.TYPE,
                    Wastrel.TYPE, Wastrel.TYPE);          // K03: rumor-tier eavesdropping texture
            spawnPatrons(PATRON_SEATS_BILGE, Serf.TYPE, Serf.TYPE, Wastrel.TYPE, Wastrel.TYPE);
            //                                                    // K04: "Wastrel/Serf mixing bowl"
            spawnPatrons(PATRON_SEATS_LANTERN, Serf.TYPE, Serf.TYPE, Serf.TYPE, Wastrel.TYPE);
            //                                          // K05: neutral ground, calmer mixed crowd
            Actor harl = business(K06_HARLS_YARD, 8);          // K06 shipyard wrights (+4)
            // K07 Ropewalk: staffCount left at the original 4 -- the +10 warehouse-crew
            // hands are hired explicitly below via the captured foreman (2026-07-14 pass).
            Actor ropewalkForeman = business(K07_ROPEWALK, 4);
            business(K08_BRANNS, 2);                           // K08 chandlery (+1)
            business(K09_PITCHFIELD, 2);                       // K09 tar yard (fire-risk, left lean)
            business(K11_SALT_ROW, 6);                         // K11 gutting sheds (+3)
            // K12 King's Bond: staffCount left at the original 2 -- the +6 warehouse-crew
            // porters are hired explicitly below via the captured foreman (2026-07-14 pass).
            Actor bondForeman = business(K12_KINGS_BOND, 2);
            business(K14_WRACKHOUSE, 4);                       // K14 salvage (+2)
            business(K15_FENNERS, 1);                          // K15 pawn cage (deliberately cramped, left untouched)
            business(K18_BATHHOUSE, 4);                        // K18 boilers (+2)
            business(K20_MERLES, 2);                           // K20 boathouse (+1)
            business(K22_NETMENDERS, 4);                       // K22 colonnade (+2)
            business(K23_COOPERS, 4);                          // K23 barrel shop (+2)
            business(SAILMAKER, 4);                            // K26 Sailmaker's Loft (+2)
            business(K27_HARDTACK, 2);                         // K27 the Hardtack Oven (+1)
            business(K28_SLOPCHEST, 2);                        // K28 the Slop-Chest (+1)

            // K06 Harl's Yard timber store yard — 6 new warehouse-flavored log-stack hands,
            // hired under Harl (the existing K06 proprietor), anchored in the yard's own
            // fenced aisle (2026-07-14 warehouse-crew pass, design §5).
            for (int i = 0; i < 6; i++) {
                Actor timberHand = spawn(Serf.TYPE, nextLodging(), ZA);
                timberHand.setHomeId(homes.addHome(timberHand.cell()));
                timberHand.setAnchorCell(worldCell(TIMBER_YARD_STAND, ZA));
                hire(harl, timberHand);
            }
            // K07 Ropewalk — 10 additional rope-gang hands under the captured foreman
            // (2026-07-14 warehouse-crew pass, design §5: 4 -> 14).
            for (int i = 0; i < 10; i++) {
                Actor ropeHand = spawn(Serf.TYPE, nextLodging(), ZA);
                ropeHand.setHomeId(homes.addHome(ropeHand.cell()));
                ropeHand.setAnchorCell(worldCell(K07_ROPEWALK, ZA));
                hire(ropewalkForeman, ropeHand);
            }
            // K12 King's Bond — 6 additional porters under the captured foreman
            // (2026-07-14 warehouse-crew pass, design §5: 2 -> 8).
            for (int i = 0; i < 6; i++) {
                Actor bondHand = spawn(Serf.TYPE, nextLodging(), ZA);
                bondHand.setHomeId(homes.addHome(bondHand.cell()));
                bondHand.setAnchorCell(worldCell(K12_KINGS_BOND, ZA));
                hire(bondForeman, bondHand);
            }

            // ===== BUNK-AT-WORKPLACE CREWS (2026-07-14 serf-doubling pass, round 2) ===========
            // Eli's directive: double the ward's serf count (252 -> 504). Reaching that
            // through ordinary hired staff would require either breaking household.json's
            // mean-2.4 distribution, overstaffing small/cramped sites past their established
            // character, or authoring new dwelling anchors (a real map/tmx change) -- all
            // three off-limits. The K30-K32 ship crews above already used a cleaner escape
            // hatch: Serf.TYPE spawned directly AT the work anchor, home == anchor == spawn
            // cell (soloHomeAtCell, no setAnchorCell -- Actor's constructor already sets
            // anchorCell = spawn cell). That pattern consumes zero household-registry
            // capacity and needs no new dwelling anchor, so it can be extended far more
            // aggressively than the first pass used it -- applied here to every genuinely
            // large/storage/industrial site in the district (all anchors below are the
            // site's own already-established, already-walkable business anchor constant).

            // K06 Harl's Yard — 24 shipwrights/caulkers/sawyers bunking across the
            // workshop+timber-yard+slipway complex (the district's biggest multi-part
            // industrial site after the Ropewalk).
            for (int i = 0; i < 24; i++) {
                Actor yardHand = spawn(Serf.TYPE, K06_HARLS_YARD, ZA);
                soloHomeAtCell(yardHand);
            }
            // K07 Ropewalk — 34 more rope-gang hands bunking in the shed itself (576 tiles,
            // the single largest floor in the district; §3.1 "deliberately elongated").
            for (int i = 0; i < 34; i++) {
                Actor ropeGangHand = spawn(Serf.TYPE, K07_ROPEWALK, ZA);
                soloHomeAtCell(ropeGangHand);
            }
            // K12 King's Bond — 20 more porters/night-watch bunking in the sealed, windowless
            // bonded warehouse (bonded goods need round-the-clock security; 221 tiles).
            for (int i = 0; i < 20; i++) {
                Actor bondBunk = spawn(Serf.TYPE, K12_KINGS_BOND, ZA);
                soloHomeAtCell(bondBunk);
            }
            // K11 Salt Row — 8 seasonal herring-curing hands bunking in the gutting
            // sheds/smokehouse lofts (time-sensitive salt-and-smoke work, dawn starts).
            for (int i = 0; i < 8; i++) {
                Actor curingHand = spawn(Serf.TYPE, K11_SALT_ROW, ZA);
                soloHomeAtCell(curingHand);
            }
            // K14 Wrackhouse — 6 salvage haulers bunking rough among the flotsam (fits the
            // "buys what the sea spits up, no questions" salvage-broker character).
            for (int i = 0; i < 6; i++) {
                Actor salvageHand = spawn(Serf.TYPE, K14_WRACKHOUSE, ZA);
                soloHomeAtCell(salvageHand);
            }
            // K23 Cooper & Blockmaker — 6 apprentices bunking in the workshop loft (a live-in
            // cooper's apprentice is the historically standard arrangement for the trade).
            for (int i = 0; i < 6; i++) {
                Actor coopersHand = spawn(Serf.TYPE, K23_COOPERS, ZA);
                soloHomeAtCell(coopersHand);
            }
            // K18 Squall's Bathhouse — 6 boiler stokers bunking by the boilers (the fires
            // can't be left untended overnight).
            for (int i = 0; i < 6; i++) {
                Actor stoker = spawn(Serf.TYPE, K18_BATHHOUSE, ZA);
                soloHomeAtCell(stoker);
            }

            // K10 Dawnstalls — three self-employed stallholders commuting to the market.
            for (int i = 0; i < 3; i++) {
                Actor stallkeep = spawn(Shopkeeper.TYPE, nextLodging(), ZA);
                soloHomeAtCell(stallkeep);
                stallkeep.setAnchorCell(worldCell(K10_DAWNSTALLS, ZA));
            }
            // K24 The Eel-Pots — four night-stall keepers, one per authored stall front.
            for (int[] stall : EELPOT_STALLS) {
                Actor eelKeeper = spawn(Shopkeeper.TYPE, nextLodging(), ZA);
                soloHomeAtCell(eelKeeper);
                eelKeeper.setAnchorCell(worldCell(stall, ZA));
            }
            // K19 The Rows — the landlord plus ten hammock lodgers who work the waterfront.
            Actor rowsLandlord = spawn(Shopkeeper.TYPE, K19_ROWS, ZA);
            soloHomeAtCell(rowsLandlord);
            for (int i = 0; i < 18; i++) {   // 10 -> 18 (2026-07-14 serf-doubling pass, +8)
                Actor lodger = spawn(Serf.TYPE, K19_ROWS, ZA);
                lodger.setHomeId(homes.addHome(lodger.cell()));
                lodger.setAnchorCell(worldCell(nextDockWork(), ZA));
            }
            // K29 The Long Store — warehouse, no bed: the foreman lodges elsewhere and
            // commutes in with one hand (per the design note: no canon night-watchman here).
            Actor longStoreForeman = spawn(Shopkeeper.TYPE, nextLodging(), ZA);
            longStoreForeman.setHomeId(homes.addHome(longStoreForeman.cell()));
            longStoreForeman.setAnchorCell(worldCell(K29_LONGSTORE, ZA));
            Actor longStoreHand = spawn(Serf.TYPE, nextLodging(), ZA);
            longStoreHand.setHomeId(homes.addHome(longStoreHand.cell()));
            longStoreHand.setAnchorCell(worldCell(K29_LONGSTORE, ZA));
            hire(longStoreForeman, longStoreHand);
            // 10 additional Long Store hands (2026-07-14 warehouse-crew pass, design §5:
            // 1 -> 11; round 2 bumped 6 -> 10). Still commuting, NOT bunk-at-workplace: the
            // gazetteer explicitly states "no canon night-watchman posted there" for K29,
            // unlike every other site in this file's bunk-crew section above -- respected.
            for (int i = 0; i < 10; i++) {
                Actor extraHand = spawn(Serf.TYPE, nextLodging(), ZA);
                extraHand.setHomeId(homes.addHome(extraHand.cell()));
                extraHand.setAnchorCell(worldCell(K29_LONGSTORE, ZA));
                hire(longStoreForeman, extraHand);
            }

            // ===================== SHIP CREWS (K30-K32 hulls, 2026-07-14 crew pass) ===========
            // Crew bunk aboard: Serf spawned directly at the hull anchor, home == anchor ==
            // deck cell (soloHomeAtCell), no setAnchorCell call -- Actor's constructor already
            // sets anchorCell = spawn cell, so these are non-commuters by construction (design
            // §4). Counts scale with hull size. K33 Widow's Grief (condemned derelict): no crew.
            // Round-2 bump (2026-07-14 serf-doubling pass): these hulls have "no interior
            // simulation" (§3.1) -- below-decks berths aren't tile-modeled at all, so crew
            // count isn't actually constrained by the walkable deck footprint the way a real
            // building's floor space is. Bumped up while keeping the same increasing-scale
            // ordering (4/6/8 -> 10/16/22).
            for (int i = 0; i < 10; i++) {   // K30 The Kestrel (smallest, 10x5)
                Actor crew = spawn(Serf.TYPE, SHIP_K30_KESTREL, ZA);
                soloHomeAtCell(crew);
            }
            for (int i = 0; i < 16; i++) {   // K31 Bregga's Promise (mid, 14x7)
                Actor crew = spawn(Serf.TYPE, SHIP_K31_BREGGAS_PROMISE, ZA);
                soloHomeAtCell(crew);
            }
            for (int i = 0; i < 22; i++) {   // K32 The Deep Keel (largest, 16x8)
                Actor crew = spawn(Serf.TYPE, SHIP_K32_DEEPKEEL, ZA);
                soloHomeAtCell(crew);
            }

            // K13 The Drowned Hold — condemned; two Wastrel squatters, no lamps, no trade.
            Actor holdSquatter = spawn(Wastrel.TYPE, K13_DROWNED_HOLD, ZA);
            household(List.of(holdSquatter, spawn(Wastrel.TYPE, K13_DROWNED_HOLD, ZA)));

            // K17 Mission of the Flame — the ward's one Priest, three Disciples, six
            // destitute in the bunks, two garden hands (clergy.* jobs are anchor commutes).
            Actor priest = spawn(PriestOfTheFlame.TYPE, MISSION_BUNKS, ZA);
            Actor disciple1 = spawn(DiscipleOfTheFlame.TYPE, MISSION_BUNKS, ZA);
            Actor disciple2 = spawn(DiscipleOfTheFlame.TYPE, MISSION_BUNKS, ZA);
            Actor disciple3 = spawn(DiscipleOfTheFlame.TYPE, MISSION_BUNKS, ZA);
            household(List.of(priest, disciple1, disciple2, disciple3));
            for (Actor cleric : List.of(priest, disciple1, disciple2, disciple3)) {
                cleric.setAnchorCell(worldCell(K17_MISSION, ZA));
            }
            HouseholdFormer.bindMentorPairFree(priest, disciple1, relationships);
            Actor bunkWastrel = spawn(Wastrel.TYPE, MISSION_BUNKS, ZA);
            household(List.of(bunkWastrel, spawn(Wastrel.TYPE, MISSION_BUNKS, ZA),
                    spawn(Wastrel.TYPE, MISSION_BUNKS, ZA)));
            Actor bunkSerf = spawn(Serf.TYPE, MISSION_BUNKS, ZA);
            household(List.of(bunkSerf, spawn(Serf.TYPE, MISSION_BUNKS, ZA),
                    spawn(Serf.TYPE, MISSION_BUNKS, ZA)));
            Actor gardener1 = spawn(Serf.TYPE, MISSION_BUNKS, ZA);
            Actor gardener2 = spawn(Serf.TYPE, MISSION_BUNKS, ZA);
            household(List.of(gardener1, gardener2));
            makeFarmer(gardener1, MISSION_GARDEN, ZA);
            makeFarmer(gardener2, MISSION_GARDEN, ZA);
            // 30 additional almshouse lodgers (2026-07-14 serf-doubling pass, bunk-crew
            // round 2): the Mission's whole canon purpose is housing the ward's destitute
            // (DOCKS-GAZETTEER.md K17 -- "soup, bunks, a disciple always awake"), and this
            // fixture only realizes ~13% of the ward's lore-scale population (class
            // Javadoc) -- there is real headroom here before it reads as implausibly
            // overcrowded. Typed Serf (not Wastrel): indigent laborers taken in between
            // casual dock work, not beggars/villains-under-cover. Ten cohesive 3-person
            // households at the same bunks anchor, no separate dwelling authored (same
            // bunk-at-workplace escape hatch as the crews above).
            for (int i = 0; i < 10; i++) {
                Actor almsLodger1 = spawn(Serf.TYPE, MISSION_BUNKS, ZA);
                Actor almsLodger2 = spawn(Serf.TYPE, MISSION_BUNKS, ZA);
                Actor almsLodger3 = spawn(Serf.TYPE, MISSION_BUNKS, ZA);
                household(List.of(almsLodger1, almsLodger2, almsLodger3));
            }

            // ===================== THE WATCH (gazetteer §4.2 — never a Gullet anchor) ==========
            // K21 watch-post: three Watch quartered at the post, beats at the Rise head.
            Actor watchSergeant = spawn(MilitiaWatch.TYPE, WATCHPOST_K21, ZC);
            watchSergeant.setHomeId(homes.addHome(watchSergeant.cell()));
            watchSergeant.setAnchorCell(worldCell(PATROL_RISE_TOP, ZC));
            for (int[] beat : new int[][] {PATROL_RISE_TOP, NOTICE_BOARD}) {
                Actor watch = spawn(MilitiaWatch.TYPE, WATCHPOST_K21, ZC);
                watch.setHomeId(homes.addHome(watch.cell()));
                watch.setAnchorCell(worldCell(beat, ZC));
            }
            // Band-A posts: the Rise foot, the two Tarwalk beats, the King's Bond door.
            for (int[] post : new int[][] {PATROL_RISE_FOOT, PATROL_TARWALK_WEST,
                    PATROL_TARWALK_MID, WATCH_BOND_POST}) {
                soloHome(spawn(MilitiaWatch.TYPE, post, ZA));
            }
            // K34 Guardhouse — the Rise's FOOT garrison (pairs with K21 at the head, per
            // gazetteer 2.4's own stated intent): two Watch quartered at the new post.
            Actor guardhouseSergeant = spawn(MilitiaWatch.TYPE, K34_GUARDHOUSE, ZA);
            guardhouseSergeant.setHomeId(homes.addHome(guardhouseSergeant.cell()));
            soloHome(spawn(MilitiaWatch.TYPE, K34_GUARDHOUSE, ZA));
            // C2 roof deck post (ARREST-SPEC addendum, see the constant's own comment above):
            // the one Watch actually positioned to exercise the new arrest/hold/escalation
            // mechanic against a real Cutpurse and Skyrunner in this fixture.
            Actor roofWatch = spawn(MilitiaWatch.TYPE, C2_ROOF_WATCHPOST, ZC);
            soloHome(roofWatch);
            roofWatch.setAnchorCell(worldCell(C2_ROOF_WATCHPOST, ZC));

            // ===================== ANIMAL KEEPERS + BEASTS (§4.8 Keeper<->Animal) =============
            // K25 Kennel Row: the kennelmaster and three dogs at their authored cage anchors.
            Actor kennelmaster = spawn(AnimalKeeper.TYPE, K25_KENNEL_ROW, ZA);
            int kennelHome = homes.addHome(kennelmaster.cell());
            kennelmaster.setHomeId(kennelHome);
            for (int[] cage : KENNEL_DOGS) {
                Actor dog = spawn(AnimalActor.TYPE, cage, ZA);
                dog.setOwnerId(kennelmaster.id());
                dog.setHomeId(kennelHome);
            }
            // K02 Impound Yard: the watchman-keeper and the impound dog.
            Actor impoundKeeper = spawn(AnimalKeeper.TYPE, K02_IMPOUND, ZA);
            int impoundHome = homes.addHome(impoundKeeper.cell());
            impoundKeeper.setHomeId(impoundHome);
            Actor impoundDog = spawn(AnimalActor.TYPE, IMPOUND_DOG, ZA);
            impoundDog.setOwnerId(impoundKeeper.id());
            impoundDog.setHomeId(impoundHome);
            // Band-B goat pen east of the compounds.
            Actor goatherd = spawn(AnimalKeeper.TYPE, PEN_GOATS, ZB);
            int penHome = homes.addHome(goatherd.cell());
            goatherd.setHomeId(penHome);
            for (int i = 0; i < 4; i++) {
                Actor goat = spawn(AnimalActor.TYPE, PEN_GOATS, ZB);
                goat.setOwnerId(goatherd.id());
                goat.setHomeId(penHome);
            }
            // The carter (2026-07-15 interior-detail pass, design §6): DOCKS-GAZETTEER.md §4.2
            // names a carter (dray horse) among the ward's Animal Keepers, but only 3 of the 4
            // were ever spawned in this fixture — completing that gap, not new scope. A "horse"
            // flavor of the existing generic AnimalActor (same raws, same policy stack as the
            // kennel dogs/pen goats above — no rider-coupling mechanic; see the field comments
            // on HITCH_GULL et al. for the full scope reasoning).
            Actor carter = spawn(AnimalKeeper.TYPE, CARTER_STAND, ZA);
            int carterHome = homes.addHome(carter.cell());
            carter.setHomeId(carterHome);
            for (int[] hitch : new int[][] {HITCH_GULL, HITCH_BILGE, HITCH_ROWS}) {
                Actor horse = spawn(AnimalActor.TYPE, hitch, ZA);
                horse.setOwnerId(carter.id());
                horse.setHomeId(carterHome);
            }

            // ===================== FERALS (scavengers; ownerless, roost at spawn) ==============
            soloHome(spawn(FeralActor.TYPE, new int[] {30, 31}, ZA));    // Tarwalk west gutter
            soloHome(spawn(FeralActor.TYPE, new int[] {120, 30}, ZA));   // east storm grate
            soloHome(spawn(FeralActor.TYPE, new int[] {165, 80}, ZA));   // the Gullet G3
            soloHome(spawn(FeralActor.TYPE, new int[] {132, 18}, 10));   // Beaching Strand (z:+10)
            soloHome(spawn(FeralActor.TYPE, new int[] {176, 84}, ZA));   // C4 courtyard trash

            // ===================== COMPOUND C1 — Quayward (grand, Band B) =====================
            // The owning family's Shopkeeper head runs the courtyard market; three condo
            // households farm the courtyard plot; the rest keep house.
            Actor c1Owner = spawn(Shopkeeper.TYPE, C1_MANSION, ZB);
            Actor c1Heir = spawn(Serf.TYPE, C1_MANSION, ZB);
            Actor c1MoverKin = spawn(Serf.TYPE, C1_MANSION, ZB);
            household(List.of(c1Owner, c1Heir, c1MoverKin,
                    spawn(Serf.TYPE, C1_MANSION, ZB), spawn(Serf.TYPE, C1_MANSION, ZB)));
            c1Owner.setAnchorCell(worldCell(C1_COURTYARD, ZB));
            relationships.addDirected(c1Owner.id(), c1Heir.id(), RelationshipKind.MENTOR);
            Actor c1FirstHead = null;
            for (int i = 0; i < C1_CONDOS_GROUND.length; i++) {
                int size = HOUSEHOLD_SIZES[(i + 1) % HOUSEHOLD_SIZES.length];
                int[] work = i < 3 ? C1_COURTYARD : null;   // three farming households
                Actor head = dwelling(Serf.TYPE, C1_CONDOS_GROUND[i], ZB, size, work, ZB,
                        work != null ? Job.Serf.Farmer.ID : null);
                if (i == 0) {
                    c1FirstHead = head;
                }
            }
            for (int i = 0; i < C1_CONDOS_UPPER.length; i++) {
                int size = HOUSEHOLD_SIZES[(i + 3) % HOUSEHOLD_SIZES.length];
                int[] work = i < 2 ? WELL_PLAZA : null;     // two water-carrier households
                dwelling(Serf.TYPE, C1_CONDOS_UPPER[i], ZC, size, work, ZC, null);
            }
            relationships.addSymmetric(c1Owner.id(), c1FirstHead.id(), RelationshipKind.NEIGHBOR);

            // ===================== COMPOUND C2 — Netters' (mid, Band A) =======================
            // The dockworker compound: ground households man the berths and piers by day.
            Actor c2Owner = spawn(Shopkeeper.TYPE, C2_MANSION, ZA);
            Actor c2Mover = spawn(Serf.TYPE, C2_MANSION, ZA);   // the tracked Band-A mover
            household(List.of(c2Owner, c2Mover, spawn(Serf.TYPE, C2_MANSION, ZA),
                    spawn(Serf.TYPE, C2_MANSION, ZA)));
            c2Owner.setAnchorCell(worldCell(K10_DAWNSTALLS, ZA));
            Actor c2FirstHead = null;
            for (int i = 0; i < C2_CONDOS_GROUND.length; i++) {
                int size = HOUSEHOLD_SIZES[i % HOUSEHOLD_SIZES.length] + 1;   // packed units
                Actor head = dwelling(Serf.TYPE, C2_CONDOS_GROUND[i], ZA, size,
                        nextDockWork(), ZA, null);
                if (i == 0) {
                    c2FirstHead = head;
                }
            }
            for (int i = 0; i < C2_CONDOS_UPPER.length; i++) {
                int size = HOUSEHOLD_SIZES[(i + 2) % HOUSEHOLD_SIZES.length];
                dwelling(Serf.TYPE, C2_CONDOS_UPPER[i], ZB, size,
                        BAND_B_WORK[i % BAND_B_WORK.length], ZB, null);
            }
            // Roof slum: Wastrel households; the skyrunner and a cutpurse hide under
            // streetlife covers (presented job) among ordinary rooftop tenants.
            Actor c2Cutpurse = spawn(Wastrel.TYPE, C2_ROOFHUTS[0], ZC);
            household(List.of(c2Cutpurse, spawn(Wastrel.TYPE, C2_ROOFHUTS[0], ZC)));
            assignJob(c2Cutpurse, Job.Villain.Cutpurse.ID);
            Actor skyrunner = spawn(Wastrel.TYPE, C2_ROOFHUTS[1], ZC);
            Actor skyPartner = spawn(Wastrel.TYPE, C2_ROOFHUTS[1], ZC);
            household(List.of(skyrunner, skyPartner));
            assignJob(skyrunner, Job.Villain.Skyrunner.ID);
            Actor c2RoofFamily = spawn(Wastrel.TYPE, C2_ROOFHUTS[2], ZC);
            household(List.of(c2RoofFamily, spawn(Wastrel.TYPE, C2_ROOFHUTS[2], ZC),
                    spawn(Wastrel.TYPE, C2_ROOFHUTS[2], ZC)));
            relationships.addSymmetric(c2Owner.id(), c2FirstHead.id(), RelationshipKind.NEIGHBOR);
            relationships.addSymmetric(c2Cutpurse.id(), c2RoofFamily.id(),
                    RelationshipKind.NEIGHBOR);

            // ===================== COMPOUND C3 — Saltgate Terrace (cramped, Band B) ===========
            Actor c3Owner = spawn(Shopkeeper.TYPE, C3_MANSION, ZB);
            household(List.of(c3Owner, spawn(Serf.TYPE, C3_MANSION, ZB),
                    spawn(Serf.TYPE, C3_MANSION, ZB), spawn(Serf.TYPE, C3_MANSION, ZB)));
            c3Owner.setAnchorCell(worldCell(C3_COURTYARD, ZB));
            for (int i = 0; i < C3_CONDOS_GROUND.length; i++) {
                int size = HOUSEHOLD_SIZES[(i + 4) % HOUSEHOLD_SIZES.length];
                int[] work = i < 4 ? C3_COURTYARD : null;   // four farming households
                dwelling(Serf.TYPE, C3_CONDOS_GROUND[i], ZB, size, work, ZB,
                        work != null ? Job.Serf.Farmer.ID : null);
            }
            for (int i = 0; i < C3_CONDOS_UPPER.length; i++) {
                int size = HOUSEHOLD_SIZES[(i + 1) % HOUSEHOLD_SIZES.length];
                dwelling(Serf.TYPE, C3_CONDOS_UPPER[i], ZC, size,
                        BAND_C_WORK[i % BAND_C_WORK.length], ZC, null);
            }
            // z:+14 roof: the highest slum in the ward — a cutpurse works the rooftops.
            Actor c3RoofCutpurse = spawn(Wastrel.TYPE, C3_ROOFHUTS[0], 14);
            household(List.of(c3RoofCutpurse, spawn(Wastrel.TYPE, C3_ROOFHUTS[0], 14)));
            assignJob(c3RoofCutpurse, Job.Villain.Cutpurse.ID);
            Actor c3RoofPair = spawn(Wastrel.TYPE, C3_ROOFHUTS[1], 14);
            household(List.of(c3RoofPair, spawn(Wastrel.TYPE, C3_ROOFHUTS[1], 14)));

            // ===================== COMPOUND C4 — Gullet (decayed, Band A east) =================
            // No mansion, absentee landlord: dockworker households, a squatted ruin, and the
            // roof slum that adjoins the Drowned Hold's roofline (the skyrunner highway).
            for (int i = 0; i < C4_CONDOS_GROUND.length; i++) {
                int size = HOUSEHOLD_SIZES[(i + 2) % HOUSEHOLD_SIZES.length] + 1;
                dwelling(Serf.TYPE, C4_CONDOS_GROUND[i], ZA, size, nextDockWork(), ZA, null);
            }
            dwelling(Serf.TYPE, C4_CONDO_UPPER, ZB, 2, null, ZB, null);
            Actor c4Robber = spawn(Wastrel.TYPE, C4_ROOFHUTS[0], ZC);
            household(List.of(c4Robber, spawn(Wastrel.TYPE, C4_ROOFHUTS[0], ZC),
                    spawn(Wastrel.TYPE, C4_ROOFHUTS[0], ZC)));
            assignJob(c4Robber, Job.Villain.Robber.ID);
            Actor c4RoofPairA = spawn(Wastrel.TYPE, C4_ROOFHUTS[1], ZC);
            household(List.of(c4RoofPairA, spawn(Wastrel.TYPE, C4_ROOFHUTS[1], ZC)));
            // roofhut_09 (C4_ROOFHUTS[2]) is the hut beside K35, the Skyrunner's Roost: its
            // second tenant presents as an ordinary rooftop dweller (home = the hut) but
            // works the concealed lair through the hut's own wall — never a business
            // anchor, never lit, never on any discoverable establishments list.
            Actor c4Cutpurse = spawn(Wastrel.TYPE, C4_ROOFHUTS[2], ZC);
            Actor c4Skyrunner = spawn(Wastrel.TYPE, C4_ROOFHUTS[2], ZC);
            household(List.of(c4Cutpurse, c4Skyrunner));
            assignJob(c4Cutpurse, Job.Villain.Cutpurse.ID);
            assignJob(c4Skyrunner, Job.Villain.Skyrunner.ID);
            c4Skyrunner.setAnchorCell(worldCell(LAIR_SKYRUNNER, ZC));
            Actor c4LeanTo = spawn(Wastrel.TYPE, C4_ROOFHUTS[3], ZC);
            household(List.of(c4LeanTo, spawn(Wastrel.TYPE, C4_ROOFHUTS[3], ZC)));
            Actor ruinSquatter = spawn(Wastrel.TYPE, C4_RUIN, ZA);
            household(List.of(ruinSquatter, spawn(Wastrel.TYPE, C4_RUIN, ZA),
                    spawn(Wastrel.TYPE, C4_RUIN, ZA)));
            relationships.addSymmetric(c4Robber.id(), c4Cutpurse.id(), RelationshipKind.FRIEND);

            // ===================== HOVELS (45 — the squalor mass, all three bands) =============
            spawnHovelRow(HOVELS_A, ZA, BAND_A_WORK);
            spawnHovelRow(HOVELS_B, ZB, BAND_B_WORK);
            spawnHovelRow(HOVELS_C, ZC, BAND_C_WORK);

            // ===================== FLAVOUR EDGES (deterministic, no RNG) ======================
            relationships.addSymmetric(harl.id(), weighmaster.id(), RelationshipKind.FRIEND);
            relationships.addSymmetric(gullHost.id(), lanternLandlady.id(),
                    RelationshipKind.FRIEND);
            relationships.addSymmetric(rowsLandlord.id(), kennelmaster.id(),
                    RelationshipKind.FRIEND);
            relationships.addDirected(harl.id(), c2FirstHead.id(), RelationshipKind.MENTOR);
            relationships.addSymmetric(holdSquatter.id(), ruinSquatter.id(),
                    RelationshipKind.FRIEND);
            relationships.addSymmetric(bunkWastrel.id(), bunkSerf.id(), RelationshipKind.NEIGHBOR);
            relationships.addSymmetric(watchSergeant.id(), goatherd.id(), RelationshipKind.FRIEND);

            // ===================== Civic default jobs + presented-job covers (§10.4) ===========
            for (int i = 0; i < registry.size(); i++) {
                Actor actor = registry.get(i);
                if (actor.jobOrdinal() < 0) {
                    actor.setJobOrdinal((short) jobs.defaultOrdinalFor(actor.typeId()));
                }
            }

            // ===================== Starting inventory (placeholder ids, §11.2) =================
            for (int i = 0; i < registry.size(); i++) {
                giveStartingInventory(registry.get(i));
            }

            // ===================== Movers: displace + deplete REST -> RETURN_HOME ==============
            // One demo mover per walk plane so all three bands visibly self-correct at boot.
            trackedGroundMoverId = c2Mover.id();
            makeMover(c2Mover, 6, 4);        // Band A (z:+11), tracked
            makeMover(c1MoverKin, 5, 3);     // Band B (z:+12)
            makeMover(skyPartner, -3, 4);    // C2 roof slum (z:+13)
        }

        // ------------------------------------------------------------------ section helpers

        /**
         * One staffed establishment: a Shopkeeper proprietor living over the shop plus
         * {@code staffCount} hired Serfs lodging across the district (rotating cursor) who
         * commute to the shop anchor — each with a guaranteed EMPLOYER edge.
         */
        private Actor business(int[] anchor, int staffCount) {
            Actor proprietor = spawn(Shopkeeper.TYPE, anchor, ZA);
            soloHomeAtCell(proprietor);
            for (int i = 0; i < staffCount; i++) {
                Actor staff = spawn(Serf.TYPE, nextLodging(), ZA);
                staff.setHomeId(homes.addHome(staff.cell()));
                staff.setAnchorCell(worldCell(anchor, ZA));
                hire(proprietor, staff);
            }
            return proprietor;
        }

        /**
         * Tavern patrons (design §4, reworked 2026-07-15 -- see the bunk-crew pattern this
         * section already uses for K06/K07/K12/etc. and the K30-K32 ship crews): each seat gets
         * one actor of the matching type spawned <em>directly at the seat</em>, home == anchor
         * == seat cell ({@link #soloHomeAtCell}, no {@link #setAnchorCell} call needed since
         * {@code Actor}'s constructor already sets anchorCell = spawn cell). A patron homed via
         * {@link #nextLodging()} at a distant dwelling and merely anchored at the seat was found
         * to never reliably arrive: {@code stepToward}'s greedy Chebyshev-reduce + single-level
         * wall-slide is not a real pathfinder, and the cross-district walk from a rotating
         * lodging cell into a tavern's furnished interior can walk an actor into a concave wall
         * pocket it can never route around (confirmed by tracing the exact walk: patron actors
         * parked permanently a dozen-plus tiles short of their seat, stuck against a wall corner,
         * across every sampled tick from 2,000 to 40,000). Spawning at the seat removes the
         * commute (and the pathing hazard) entirely -- a patron is simply always there, exactly
         * like a ship's crew never actually walks aboard. {@code types.length} must equal
         * {@code seats.length}.
         */
        private void spawnPatrons(int[][] seats, ActorTypeId... types) {
            for (int i = 0; i < seats.length; i++) {
                Actor patron = spawn(types[i], seats[i], ZA);
                soloHomeAtCell(patron);
            }
        }

        /**
         * One occupied dwelling unit: a household of {@code size} actors of {@code type} at
         * the unit anchor. When {@code work} is non-null the head (and the second member, if
         * any) commute there; {@code jobOverride} (e.g. serf.farmer) replaces the type default.
         * Returns the household head.
         */
        private Actor dwelling(ActorTypeId type, int[] unit, int z, int size, int[] work,
                int workZ, JobId jobOverride) {
            List<Actor> group = new ArrayList<>(size);
            for (int i = 0; i < size; i++) {
                group.add(spawn(type, unit, z));
            }
            household(group);
            if (work != null) {
                int workers = Math.min(2, size);
                for (int i = 0; i < workers; i++) {
                    Actor worker = group.get(i);
                    worker.setAnchorCell(worldCell(work, workZ));
                    if (jobOverride != null) {
                        assignJob(worker, jobOverride);
                    }
                }
            }
            return group.get(0);
        }

        /**
         * One band of hovels: rotating household sizes (mean 2.4), every fourth hovel a
         * Wastrel household begging in place, the rest Serf households whose head (and
         * second member) commute to the band's work cycle — Band A's cycle is the
         * waterfront, so the hovel mass is what makes the quay crowded by day.
         */
        private void spawnHovelRow(int[][] hovels, int z, int[][] workCycle) {
            for (int i = 0; i < hovels.length; i++) {
                int size = HOUSEHOLD_SIZES[i % HOUSEHOLD_SIZES.length];
                if (i % 4 == 3) {
                    dwelling(Wastrel.TYPE, hovels[i], z, size, null, z, null);
                } else {
                    dwelling(Serf.TYPE, hovels[i], z, size,
                            workCycle[i % workCycle.length], z, null);
                }
            }
        }

        /** The next Band-A staff lodging cell (rotating cursor over condos/hovels/the Rows). */
        private int[] nextLodging() {
            return STAFF_LODGINGS[lodgingCursor++ % STAFF_LODGINGS.length];
        }

        /** The next waterfront work anchor (rotating cursor over berths/piers/crane/yard). */
        private int[] nextDockWork() {
            return DOCK_WORK[dockCursor++ % DOCK_WORK.length];
        }

        // ------------------------------------------------------------------ shared plumbing

        /** Sets one explicit (non-default) job by its raws id — the villain/farmer overrides. */
        private void assignJob(Actor actor, JobId jobId) {
            actor.setJobOrdinal((short) jobs.ordinalOf(jobId));
        }

        /** serf.farmer working a plot: a commuter when the plot differs from home. */
        private void makeFarmer(Actor serf, int[] plot, int z) {
            assignJob(serf, Job.Serf.Farmer.ID);
            serf.setAnchorCell(worldCell(plot, z));
        }

        private Actor spawn(ActorTypeId type, int[] mapXY, int mapZ) {
            ActorTypeStats stats = typeStats.get(type);
            return registry.spawn(type, stats, worldCell(mapXY, mapZ));
        }

        /** One shared Home at the group leader's cell + a HOUSEHOLD clique, via the real former. */
        private void household(List<Actor> group) {
            HouseholdFormer.formHouseholds(group, homes, relationships, seed,
                    cohesive(group.size()));
        }

        /** A single-occupant home at the actor's spawn cell — a Home, no household edges. */
        private void soloHome(Actor actor) {
            actor.setHomeId(homes.addHome(actor.cell()));
        }

        /** Same as {@link #soloHome} (named for the live-over-the-shop proprietors). */
        private void soloHomeAtCell(Actor actor) {
            actor.setHomeId(homes.addHome(actor.cell()));
        }

        /** One guaranteed EMPLOYER edge employer -> hire (staffCount forced to 1, §11.4 step 3). */
        private void hire(Actor employer, Actor staff) {
            HouseholdFormer.formEmployment(employer, List.of(staff), relationships, seed,
                    new HouseholdRaws(householdRaws.householdSizeWeights(), 1, 1, 0, 0, 0, 0));
        }

        /**
         * A household-size weight vector that forces {@link HouseholdFormer} to group all
         * {@code size} members into one Home (all weight on the exact size) — one authored
         * dwelling unit = exactly one household, deterministically, while still running
         * through the real former's shared-Home + HOUSEHOLD-clique machinery.
         */
        private HouseholdRaws cohesive(int size) {
            int[] weights = new int[size];
            weights[size - 1] = 1;
            return new HouseholdRaws(weights, 1, 1, 0, 0, 0, 0);
        }

        private void makeMover(Actor actor, int dx, int dy) {
            int x = PackedPos.x(actor.cell());
            int y = PackedPos.y(actor.cell());
            int z = PackedPos.z(actor.cell());
            actor.setCell(PackedPos.pack(x + dx, y + dy, z));
            // Deplete REST well below LOW (3000) so RETURN_HOME scores above every JOB policy.
            actor.applyNeedDelta(Need.REST, -(actor.need(Need.REST) - 400));
            movers.add(actor.id());
        }

        private void giveStartingInventory(Actor actor) {
            String type = actor.typeId().key();
            switch (type) {
                case "serf" -> mint(actor, KIND_COIN, 2);
                case "shopkeeper" -> {
                    mint(actor, KIND_STOCK, 5);
                    mint(actor, KIND_COIN, 3);
                }
                case "priest_of_the_flame", "disciple_of_the_flame" -> mint(actor, KIND_ALMS_TOKEN, 4);
                case "wastrel" -> {
                    if (isSkyrunner(actor)) {
                        mint(actor, KIND_LOCKPICK, 1);
                        mint(actor, KIND_SCRAP, 1);
                    } else {
                        mint(actor, KIND_SCRAP, 1);
                    }
                }
                case "militia_watch" -> mint(actor, KIND_CUDGEL, 1);
                case "animal_keeper" -> mint(actor, KIND_FEED, 3);
                default -> { /* animals/ferals carry nothing */ }
            }
        }

        private boolean isSkyrunner(Actor actor) {
            return actor.jobOrdinal() >= 0
                    && jobs.get(actor.jobOrdinal()).id().equals(Job.Villain.Skyrunner.ID);
        }

        /** Mints a carried ItemsLite entry and links its id into the actor's inventory-lite. */
        private void mint(Actor actor, short kindId, int quantity) {
            int itemId = items.mint(kindId, actor.id(), actor.id(), Actor.NONE, (short) quantity);
            actor.addInventoryItem((short) itemId);
        }
    }
}
