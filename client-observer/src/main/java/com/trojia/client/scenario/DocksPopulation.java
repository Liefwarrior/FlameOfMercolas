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
import com.trojia.sim.actor.BankLedger;
import com.trojia.sim.actor.BankQueue;
import com.trojia.sim.actor.CivicFixtures;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.FoodEconomy;
import com.trojia.sim.actor.FoodMarket;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.HouseholdFormer;
import com.trojia.sim.actor.HouseholdRaws;
import com.trojia.sim.actor.HouseholdRawsLoader;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.PatrolRouteTable;
import com.trojia.sim.actor.Payroll;
import com.trojia.sim.actor.PrisonCellRegistry;
import com.trojia.sim.actor.RestrictedZone;
import com.trojia.sim.actor.RestrictedZoneTable;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.RooftopTable;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.ZLinkTable;
import com.trojia.sim.actor.faction.FactionDefinition;
import com.trojia.sim.actor.faction.FactionRawsLoader;
import com.trojia.sim.actor.faction.FactionRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobBinder;
import com.trojia.sim.actor.job.JobId;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.quest.QuestBindings;
import com.trojia.sim.actor.quest.QuestRaws;
import com.trojia.sim.actor.quest.QuestRawsLoader;
import com.trojia.sim.actor.quest.QuestRegistry;
import com.trojia.sim.actor.type.AnimalActor;
import com.trojia.sim.actor.type.AnimalKeeper;
import com.trojia.sim.actor.type.CatActor;
import com.trojia.sim.actor.type.DiscipleOfTheFlame;
import com.trojia.sim.actor.type.FeralActor;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.MouseActor;
import com.trojia.sim.actor.type.PriestOfTheFlame;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Shopkeeper;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.Walkability;
import com.trojia.sim.world.World;

import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;

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
    // ---- PASS 5-8 (Phase-1 living-docks): K36 bank, K34 prison cell block, shop guards,
    // farm plots, guard patrol routes. Every coordinate below is read straight from the
    // regenerated docks_surface.tmx markers, is on authored walkable floor (blind-verified),
    // and is single-z. Markers are not runtime-baked, so each anchor needs a constant here. --

    // K36 The Royal Counting-House (bank): banker at the counter + two flanking guard posts;
    // the queue lane + vault chest are markers for the later economy pass (Phase 2 seeds the
    // vault -- it is empty now). All z:+11.
    private static final int[] BANK_COUNTER = {154, 53};            // banker stand (behind the counter)
    private static final int[] GUARD_POST_BANK_WEST = {152, 53};
    private static final int[] GUARD_POST_BANK_EAST = {156, 53};
    private static final int[] BANK_VAULT_CHEST = {152, 57};        // future Royal COIN vault (empty in Phase 1)
    private static final int[][] BANK_QUEUE = {{154, 51}, {154, 50}, {154, 49}};  // front -> back

    // K34 prison cell block: NINE STEEL_WALL cells -- the six originals fronting the corridor's
    // south side (y90) plus the three PASS-9 cells on its north side (y88, anchors
    // cell_k34_07..09), grown so capacity survives the occupancy cap dropping to 1/cell.
    // Filled at arrest time (no actors spawned here). Append-only: PRISON_CELLS_K34[0] stays
    // the Phase-0 scalar arrest-hold escort cell (the retired (103,85) cage-side floor).
    private static final int[][] PRISON_CELLS_K34 =
            {{101, 90}, {103, 90}, {105, 90}, {107, 90}, {109, 90}, {111, 90},
             {101, 88}, {103, 88}, {105, 88}};

    // One militia_watch per retail shop, at the exterior guard post just outside each door
    // (K08/K14/K15/K23/K26/K27/K28). All z:+11 sidewalk cells.
    private static final int[][] SHOP_GUARD_POSTS =
            {{28, 65}, {168, 33}, {126, 51}, {47, 65}, {12, 69}, {35, 69}, {133, 57}};

    // ---- Money-gated market victuallers (economy pass): on-band FOOD vendors that make every
    // INHABITED band reachably fed. On-hull victuallers provision the crewed ships (no mainland
    // counter is A*-reachable across the water); the off-band victuallers provision the z:+12/z:+13
    // terraces (whose residents have no organic dockside z:+11 shop on their OWN band). Each cell is
    // authored walkable floor on its single band (reused work/dwelling anchors), so a victualler is
    // a real manned stall, not a marker. Spread across each band's clusters so every resident routes
    // to a nearby counter within its HUNGER/commute budget. --
    private static final int[][] VICTUALLERS_ZA =
            {SHIP_K30_KESTREL, SHIP_K31_BREGGAS_PROMISE, SHIP_K32_DEEPKEEL};
    private static final int[][] VICTUALLERS_ZB =
            {{100, 98}, {75, 105}, {170, 105}, {152, 111}}; // Terrace Walk, Saltgate, Hovels-B, goat pen
    private static final int[][] VICTUALLERS_ZC =
            {{19, 117}, {73, 119}, {101, 122}, {133, 124}, {49, 112}, {123, 111}};

    // Compound farm plots (markers for the later farm-yield pass). C1/C3 courtyards are Band B
    // (z:+12); C2's is Band A (z:+11). Placed clear of each courtyard's gate spine + anchors.
    private static final int[][] FARM_TILES_C1 = {{40, 105}, {56, 105}, {40, 108}, {56, 108}};
    private static final int[][] FARM_TILES_C2 = {{133, 78}, {147, 78}, {133, 83}, {147, 83}};
    private static final int[][] FARM_TILES_C3 = {{102, 103}, {118, 103}, {102, 106}, {118, 106}};

    // Ordered single-z (z:+11) guard patrol routes (walked in order by the law & order pass's
    // waypoint patrol): the Tarwalk sidewalk, the west quay/berth apron, and Ropewynd's
    // continuous south kerb.
    private static final int[][] PATROL_TARWALK = {{20, 33}, {45, 33}, {70, 33}, {96, 33}, {122, 33}};
    private static final int[][] PATROL_QUAY = {{14, 30}, {28, 30}, {42, 30}, {56, 30}, {68, 30}};
    private static final int[][] PATROL_ROPEWYND =
            {{10, 65}, {35, 65}, {55, 65}, {78, 65}, {100, 65}, {122, 65}, {145, 65}};
    // S4 "the climb": the Saltgate Rise beat — the gazetteer's z11<->z13 climb, finally
    // walkable (the FIRST cross-z patrol route; legs between bands ride the baked brick
    // ramp rows at y96/y116, x72-79, via ZRouter). Waypoint 0 is the K21 watch sergeant's
    // existing PATROL_RISE_TOP anchor, which binds him (and the RISE_TOP-anchored beat
    // watch) to this route via routeContaining — no new actor, roster unchanged. APPENDED
    // as route index 3 so the three single-z routes keep their bindings.
    private static final int[] PATROL_SALTGATE_HEAD = {75, 118};   // z:+13 — == PATROL_RISE_TOP
    private static final int[] PATROL_SALTGATE_MID = {74, 104};    // z:+12 Saltgate roadbed
    private static final int[] PATROL_SALTGATE_FOOT = {75, 36};    // z:+11 east of the Weighhouse

    // Garbage bins (law & order pass, Eli's garbage-can request): one walkable OAK_FLOOR bin
    // cell on the exterior street beside each FOOD business, mirroring gen_docks_surface.py's
    // garbage_bin_* markers EXACTLY (lockstep rule). A daily sim-side scrap drop tops each up
    // to BIN_SCRAP_CAP; the broke's SCAVENGE branch eats off them. The two compound-upper
    // Band-C victuallers deliberately have no bin (the roof decks keep their starvation margin).
    private static final int[][] GARBAGE_BINS_ZA = {{118, 33}, {108, 33}, {66, 65}, {54, 44},
            {52, 52}, {91, 65}, {37, 69}, {89, 32}};
    private static final int[][] GARBAGE_BINS_ZB = {{102, 98}, {77, 105}, {170, 102}, {154, 110}};
    private static final int[][] GARBAGE_BINS_ZC = {{21, 121}, {75, 120}, {99, 122}, {134, 125}};
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
    // Gullet mouse dens (beast food channel pass): OPEN courtyard-trash cells of the decayed
    // C4 quarter, A*-verified reachable from BOTH Gullet gull roosts ({165,80} G3 lane and
    // {176,84} courtyard). Deliberately NOT C4_RUIN — the ruin anchor sits inside a sealed,
    // doorless room (walkable floor, no z:+11 entrance), where a den would be permanently
    // unhuntable and would deadlock the gulls' nearest-prey sense against unroutable chases.
    // SIX dens because the Gullet is isolated (nothing else within the courtyard gull's hunt
    // sense) and the realized per-mouse yield (revive cooldown + real catch latency) runs
    // well under naive theory — the 30k soak starved a Gullet gull with fewer. PASS 9
    // (density revisit): the sixth den moved {185,85} -> {183,84}, c05's exterior DOORSTEP
    // on the open courtyard. Inside c05 it was a LURE: every hunt for it pathed into the
    // condo's interior, where the resident household idle-parks the room to the occupancy
    // cap and seals the predator in (13k-tick roam stalls even after the room gained a
    // circulation loop). On the doorstep the scurry orbit (leash 8) still covers the door
    // mouth and the old wedge cells — a stuck predator still gets adjacency catches — but
    // chases for it now resolve on open courtyard ground.
    private static final int[][] GULLET_MOUSE_DENS =
            {{176, 84}, {176, 84}, {178, 76}, {180, 80}, {170, 95}, {183, 84}};

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

    // Tarry Jek's strand berth (S1-2, the Forty Notables): open Beaching Strand shingle
    // (z:+10 DIRT_FLOOR by the generator's band rule, x130-163 y8-28), 18 tiles east of the
    // strand gull's roost/dens at {132,18} so the beast channel's hunt and roam envelopes
    // stay clear of the new body. Same "known street/floor cells" convention as the stands
    // below; the timber pond's water stops at y9, so y22 is dry shingle.
    private static final int[] STRAND_JEK = {150, 22};           // z:+10

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

    // S3 "The Vanished Clerk": the staff-lodging dwelling four doors west of the K36
    // Counting-House — the rooming house where the vanished clerk kept his rented room, and
    // the spawn-site key that binds the Widow Sedge notable (his landlady and aunt) onto the
    // existing serf who homes there. An EXISTING lodging cell, promoted to a named site: no
    // map regen, no new actor, roster unchanged.
    private static final int[] CLERKS_ROOMING_HOUSE = {146, 53};      // z:+11, STAFF_LODGINGS[19]

    // Where hired shop staff lodge (all Band A, z:+11): the compounds' ground condos, the
    // Band-A hovels, and the Rows flophouse — a rotating cursor so staff spread across the
    // district and every morning fills the streets with commuters.
    private static final int[][] STAFF_LODGINGS = {{131, 70}, {147, 70}, {137, 89}, {157, 70},
            {157, 80}, {157, 89}, {168, 72}, {168, 86}, {173, 70}, {185, 70}, {187, 84},
            {84, 55}, {89, 54}, {95, 56}, {10, 93}, {22, 93}, {184, 63}, {189, 62}, {136, 52},
            CLERKS_ROOMING_HOUSE, {110, 55}};

    // Household size rotation (mean 2.4 — household.json register) applied per dwelling.
    private static final int[] HOUSEHOLD_SIZES = {2, 3, 2, 1, 4};

    // ---- placeholder ItemsLite kind ids (no items-raws system yet, §11.2) ---------------
    // COIN is the shared sim-core vocabulary (ItemKinds.COIN == the legacy KIND_COIN == 1); the
    // rest stay observer-only flavor placeholders on the same short.
    private static final short KIND_COIN = ItemKinds.COIN;
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
    /** Lazily forged identity table (S1) — a memoized pure function of the finished bake. */
    private IdentityRegistry identity;
    /** The S2 authored micro-histories, realized at bake (edges live in {@link #relationships}). */
    private final List<MicroHistoryBake.Bound> authoredHistories;

    private DocksPopulation(ActorsSystem system, ActorTypeStatsTable typeStats,
            JobRegistry jobs, HomeRegistry homes, RelationshipRegistry relationships,
            ItemsLiteRegistry items, ActorRegistry registry, long worldSeed,
            int trackedGroundMoverId, List<Integer> moverIds,
            List<MicroHistoryBake.Bound> authoredHistories) {
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
        this.authoredHistories = authoredHistories;
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

    /** The Royals ledger (Phase-2 economy) — for the money-conservation proof in the run harness. */
    public BankLedger bankAccounts() {
        return system.bankAccounts();
    }

    /**
     * The forged identity table (S1 "Every Soul Has a Name"): every actor's name, epithet
     * and bio, with the Forty Notables' authored identities bound by spawn site. Forged
     * lazily and memoized; because {@link NameForge} reads only bake-immutable state through
     * its own appended {@code identity.names} stream, forging at bake or after any number of
     * ticks yields byte-identical tables and never moves the sim tick hash (both proven by
     * {@code DocksIdentityDeterminismTest}).
     */
    @Override
    public IdentityRegistry identity() {
        if (identity == null) {
            Path namesRoot = RepoPaths.locate("content", "raws").resolve("names");
            identity = NameForge.forge(worldSeed, registry, homes, relationships, jobs,
                    NameRaws.load(namesRoot.resolve("names.json")),
                    NotableRaws.load(namesRoot.resolve("notables.json")),
                    notableSpawnSites(), siteDisplayNames(),
                    MicroHistoryBake.bioAddenda(authoredHistories));
        }
        return identity;
    }

    /**
     * The S2 authored micro-histories (feuds/debts/romances among the notables), realized at
     * bake as relationship edges + bio addenda — bound (historyId, actorA, actorB) rows for
     * tests and headless proofs. Package-private like the bake types it exposes.
     */
    List<MicroHistoryBake.Bound> authoredHistories() {
        return authoredHistories;
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
        BankLedger bank = new BankLedger();

        Builder builder = new Builder(registry, homes, relationships, items, typeStats, jobs,
                householdRaws, worldSeed, world);
        builder.populate();

        // Phase-2 economy bake (Pass 9): open + tier-seed one account per actor, mint ID cards,
        // fund the finite employer pool, and stock the vault chest so totalRoyals() == vault COIN
        // count. Returns the payroll the sim ticks wages from. Closed supply — all minting is here.
        int vaultChestCell = bankVaultChestCell();
        Payroll payroll = CivicAccounts.bake(registry, bank, items, vaultChestCell);

        // Money-gated market bake: build the FOOD market (every shopkeeper is a vendor + the
        // farm-fed compound commons), seed each home-cell larder with the boot ration, and stock
        // every vendor counter to SHOP_STOCK_CAP so the market is provisioned before the first quay
        // import (tick 6000) — no free ration ever reaches homes/commons. All FOOD minting outside
        // the tick loop happens here; its count is recorded for the closed-supply conservation proof.
        FoodMarket foodMarket = buildFoodMarket(registry);
        long foodSeeded = seedLarders(registry, homes, items) + seedVendorStock(foodMarket, items)
                + seedRations(foodMarket, items);

        int arrestHoldCell = worldCell(PRISON_CELLS_K34[0], ZA);
        // Multi-cell prison registry (Pass 10) + bank fixtures (Pass 9), wired from the Phase-1
        // markers; the restricted-zone table (Pass 4) is baked from the same markers just below.
        BankQueue bankQueue = new BankQueue(toIntArray(bankQueue()));
        PrisonCellRegistry prisonCells = new PrisonCellRegistry(toIntArray(prisonCellsK34()));
        // Living-docks Pass 4 (F3 data): bake the restricted-zone side-table (shipyard/ships ->
        // sailor, guardhouse -> guard, bank vault -> guard, shop special-inventory -> trader).
        // Data + accessors only this pass -- no live enforcement reads it yet (the law&order pass
        // does); the gate itself (canAccess) is unit-tested against these zones.
        // S4 "the climb": extract every authored vertical passage (stair pairs + ramp
        // exits) from the baked world's FORM lane — the connector table the opt-in cross-z
        // movers (player, ReturnHome, Held escort, the Saltgate route patrol) plan over.
        // World-less builds get EMPTY: the pre-S4 z-rule no-op, byte-identical.
        ZLinkTable zLinks = world == null ? ZLinkTable.EMPTY : ZLinkTable.extract(world);
        CivicFixtures fixtures = new CivicFixtures(arrestHoldCell, restrictedZoneTable(),
                vaultChestCell, worldCell(BANK_COUNTER, ZA), bankQueue, prisonCells, payroll,
                foodMarket, PatrolRouteTable.of(patrolRoutes()), rooftopTable(), zLinks);
        // Sprint-1 progression + faction wiring: the boot-built skill universe (the committed
        // 16-skill raws) behind the dense per-actor track table, and the 5-faction registry
        // behind the standing ledger. Both are raws-pure boot config (identical every run);
        // membership keys are validated against the bound jobs so a jobs.json rename fails
        // the bake loudly instead of silently orphaning a faction.
        SkillTrackRegistry skillTracks = new SkillTrackRegistry(SkillRawsLoader.load(rawsRoot));
        FactionRegistry factionDefs = FactionRawsLoader.load(rawsRoot);
        validateFactionMembership(factionDefs, jobs);
        FactionStandings factionStandings = new FactionStandings(factionDefs);

        // S2 WORLD "the stories between them": the authored micro-histories (real
        // RelationshipRegistry edges + bio addenda) and per-actor faction leanings, both
        // keyed by notables.json ids and bound by spawn site over the finished bake — pure
        // committed-raws content, draw-free, applied BEFORE the first tick so it is ordinary
        // bake-immutable state inside the persisted triad and the twin-run identity gates.
        Path namesRoot = rawsRoot.resolve("names");
        Map<String, Integer> notableActors = NameForge.bindNotableActors(registry, homes,
                NotableRaws.load(namesRoot.resolve("notables.json")), notableSpawnSites());
        List<MicroHistoryBake.Bound> authoredHistories = MicroHistoryBake.bake(
                HistoryRaws.load(namesRoot.resolve("histories.json")), notableActors,
                relationships);
        FactionLeaningsBake.apply(
                LeaningRaws.load(rawsRoot.resolve("factions").resolve("leanings.json")),
                notableActors, factionStandings);

        // S3 "The Vanished Clerk": bind the authored quest raws against this bake's own
        // notable map / item kinds / zone table / desk cell, and stage the quest's physical
        // props (the leaf in the desk, the key and a payable pocket on Gilt) — all inside
        // the bake's closed-supply baseline, before the first tick.
        QuestRegistry questRegistry = bakeQuests(rawsRoot, notableActors, skillTracks,
                factionDefs, items);

        ActorsSystem system = new ActorsSystem(worldSeed, typeStats, jobs, registry, homes,
                relationships, items, bank, world, fixtures, skillTracks, factionStandings,
                questRegistry);
        system.recordFoodMintedAtBake(foodSeeded);
        return new DocksPopulation(system, typeStats, jobs, homes, relationships, items,
                registry, worldSeed, builder.trackedGroundMoverId, builder.movers,
                authoredHistories);
    }

    /** Royals Gilt's pocket is topped to at bake — funds end_gilt's seize-what-exists pay. */
    static final int VANISHED_CLERK_GILT_POCKET = 40;

    /**
     * S3 quest bake: loads {@code content/raws/quests/quests.json}, binds every quest
     * symbol against THIS bake (parties via the notable map, items via {@link ItemKinds},
     * {@code bank_hall} via {@link #bankHallZoneIndex()}, {@code clerks_desk} via
     * {@link #clerksDeskCell()}, skills/factions via their raws registries), and stages
     * The Vanished Clerk's props: 1 LEDGER_LEAF on the desk, 1 VAULT_KEY carried by Gilt,
     * and Gilt's loose pocket topped to {@link #VANISHED_CLERK_GILT_POCKET} COIN (a bake
     * mint inside the live-COIN baseline — conservation-neutral thereafter; every later
     * quest movement is a MOVE). Returns {@link QuestRegistry#EMPTY} when no quests are
     * authored (pre-quest checkouts stay byte-identical).
     */
    private static QuestRegistry bakeQuests(Path rawsRoot, Map<String, Integer> notableActors,
            SkillTrackRegistry skillTracks, FactionRegistry factionDefs,
            ItemsLiteRegistry items) {
        QuestRaws questRaws = QuestRawsLoader.load(rawsRoot);
        if (questRaws.quests().isEmpty()) {
            return QuestRegistry.EMPTY;
        }
        QuestRegistry bound = QuestRegistry.bind(questRaws, new QuestBindings() {
            @Override
            public int partyActorId(String questId, String partySymbol) {
                Integer id = notableActors.get(partySymbol);
                return id == null ? -1 : id;
            }

            @Override
            public short itemKind(String questId, String itemSymbol) {
                return switch (itemSymbol) {
                    case "vault_key" -> ItemKinds.VAULT_KEY;
                    case "ledger_leaf" -> ItemKinds.LEDGER_LEAF;
                    default -> -1;
                };
            }

            @Override
            public int zoneId(String questId, String zoneSymbol) {
                return zoneSymbol.equals("bank_hall") ? bankHallZoneIndex() : -1;
            }

            @Override
            public int cell(String questId, String cellSymbol) {
                return cellSymbol.equals("clerks_desk") ? clerksDeskCell() : -1;
            }

            @Override
            public int skillRaw(String skillKey) {
                return skillTracks.skills().contains(skillKey)
                        ? skillTracks.skills().id(skillKey).raw() : -1;
            }

            @Override
            public int factionId(String factionKey) {
                return factionDefs.contains(factionKey) ? factionDefs.rawId(factionKey) : -1;
            }
        });
        for (QuestRaws.Quest quest : questRaws.quests()) {
            if (quest.id().equals("vanished-clerk")) {
                int gilt = notableActors.get("gilt"); // bind() above proved it resolves
                items.addOnCell(clerksDeskCell(), ItemKinds.LEDGER_LEAF, 1);
                items.addCarried(gilt, ItemKinds.VAULT_KEY, 1);
                int pocket = items.countCarriedOfKind(gilt, ItemKinds.COIN);
                if (pocket < VANISHED_CLERK_GILT_POCKET) {
                    items.addCarried(gilt, ItemKinds.COIN,
                            VANISHED_CLERK_GILT_POCKET - pocket);
                }
            }
        }
        return bound;
    }

    /** Packs an authored map cell {@code (mapX, mapY)} on z-level {@code mapZ} to its world tile. */
    private static int worldCell(int[] mapXY, int mapZ) {
        return PackedPos.pack(Coords.CHUNK_SIZE_X + mapXY[0], Coords.CHUNK_SIZE_Y + mapXY[1],
                Coords.CHUNK_SIZE_Z + mapZ);
    }

    /** Packs a list of authored map cells on one z-level to world tiles, in order. */
    private static List<Integer> worldCells(int[][] mapXYs, int mapZ) {
        List<Integer> out = new ArrayList<>(mapXYs.length);
        for (int[] xy : mapXYs) {
            out.add(worldCell(xy, mapZ));
        }
        return out;
    }

    /** Unboxes an ordered {@code List<Integer>} of world cells to the {@code int[]} the sim seams take. */
    private static int[] toIntArray(List<Integer> cells) {
        int[] out = new int[cells.size()];
        for (int i = 0; i < cells.size(); i++) {
            out[i] = cells.get(i);
        }
        return out;
    }

    // ---- Phase-1 marker bindings (Passes 5-8). These expose the map anchors that carry no
    // live actor yet -- the prison cells (fill at arrest time, Pass 10), the bank queue/vault
    // (economy, Pass 9), the compound farm plots (Pass 14) and the guard patrol routes
    // (Pass 13) -- as world-packed cells so a later sim pass binds behaviour to real geometry
    // without re-transcribing coordinates. Every cell is single-z (the z rule).

    /** The nine K34 prison-cell floor cells (z:+11), world-packed. */
    public static List<Integer> prisonCellsK34() {
        return worldCells(PRISON_CELLS_K34, ZA);
    }

    /** The bank's ordered waiting-queue slots (front-to-back, z:+11), world-packed. */
    public static List<Integer> bankQueue() {
        return worldCells(BANK_QUEUE, ZA);
    }

    /** The bank vault chest cell (z:+11) -- the future Royal COIN stack; empty in Phase 1. */
    public static int bankVaultChestCell() {
        return worldCell(BANK_VAULT_CHEST, ZA);
    }

    /** Every compound farm plot (C1/C3 on z:+12, C2 on z:+11), world-packed. */
    public static List<Integer> farmPlots() {
        List<Integer> out = new ArrayList<>();
        out.addAll(worldCells(FARM_TILES_C1, ZB));
        out.addAll(worldCells(FARM_TILES_C2, ZA));
        out.addAll(worldCells(FARM_TILES_C3, ZB));
        return out;
    }

    /**
     * The four ordered guard patrol routes, world-packed: the three single-z beats
     * (Tarwalk / quay / Ropewynd, z:+11) plus — S4 "the climb" — the cross-z Saltgate
     * Rise beat (head z:+13 → roadbed z:+12 → foot z:+11, wrapping back up), appended
     * LAST so the existing anchors keep their route bindings.
     */
    public static List<List<Integer>> patrolRoutes() {
        return List.of(worldCells(PATROL_TARWALK, ZA), worldCells(PATROL_QUAY, ZA),
                worldCells(PATROL_ROPEWYND, ZA),
                List.of(worldCell(PATROL_SALTGATE_HEAD, ZC), worldCell(PATROL_SALTGATE_MID, ZB),
                        worldCell(PATROL_SALTGATE_FOOT, ZA)));
    }

    /** The Saltgate Rise route's index in {@link #patrolRoutes()} (S4 — the cross-z beat). */
    public static final int SALTGATE_ROUTE_INDEX = 3;

    /**
     * The authored rooftop planes (Sprint-1 progression wiring: the played actor's
     * skyrunning hook), as world-coordinate boxes. Two boxes, mirroring the map's own roof
     * geometry: the EASTERN ROOFLINE — the C2 Netters' / C4 Gullet roof decks plus the K35
     * Skyrunner's Roost planes at z:+13/+14, x136+ (the exact region the arrest-spec
     * comment above pins every rooftop villain to; y-bounded to 60..99 so Band C's ground
     * streets at y117+ never read as roof) — and the C3 SALTGATE TERRACE roof plane at
     * z:+14 (roofhuts x102-128, y104-116). Baked config, no live state.
     */
    public static RooftopTable rooftopTable() {
        return new RooftopTable(new int[] {
                // eastern roofline: x136..199, y60..99, z 13..14 (world-packed below)
                Coords.CHUNK_SIZE_X + 136, Coords.CHUNK_SIZE_Y + 60, Coords.CHUNK_SIZE_Z + 13,
                Coords.CHUNK_SIZE_X + 199, Coords.CHUNK_SIZE_Y + 99, Coords.CHUNK_SIZE_Z + 14,
                // C3 Saltgate Terrace roof plane: x96..140, y104..116, z:+14 only
                Coords.CHUNK_SIZE_X + 96, Coords.CHUNK_SIZE_Y + 104, Coords.CHUNK_SIZE_Z + 14,
                Coords.CHUNK_SIZE_X + 140, Coords.CHUNK_SIZE_Y + 116, Coords.CHUNK_SIZE_Z + 14,
        });
    }

    /**
     * A fresh wired skill-track table from the committed raws — the exact wiring
     * {@link #build} gives the live system. Load-side reconstruction (a save's loading
     * system must be constructed with the same raws-derived wiring, the typeStats/jobs
     * contract) and tests use this.
     */
    public static SkillTrackRegistry freshSkillTracks() {
        return new SkillTrackRegistry(SkillRawsLoader.load(RepoPaths.locate("content", "raws")));
    }

    /** A fresh wired faction-standing ledger from the committed raws ({@link #freshSkillTracks}'s twin). */
    public static FactionStandings freshFactionStandings() {
        return new FactionStandings(FactionRawsLoader.load(RepoPaths.locate("content", "raws")));
    }

    /**
     * Fail-fast membership audit (Sprint-1 factions): every {@code memberJobs} key in the
     * factions raws must resolve against the bound {@link JobRegistry} — a jobs.json id
     * rename would otherwise silently orphan a faction's whole membership.
     */
    private static void validateFactionMembership(FactionRegistry factions, JobRegistry jobs) {
        for (FactionDefinition def : factions.all()) {
            for (String jobKey : def.memberJobs()) {
                if (jobs.ordinalOf(JobId.of(jobKey)) < 0) {
                    throw new IllegalStateException("factions.json: faction '" + def.key()
                            + "' lists unknown job '" + jobKey + "' (not in jobs.json)");
                }
            }
        }
    }

    // ---- S1 identity pass (NameForge + the Forty Notables): the spawn-site key table and
    // the site display names. Both live HERE, beside the anchor constants they mirror (the
    // lockstep rule): notables.json binds identities by these KEYS — never by raw ActorId —
    // so a map regeneration or roster edit re-resolves every binding instead of orphaning it.

    /**
     * The notable spawn-site key table: {@code notables.json}'s {@code site} keys resolved to
     * world-packed cells. Append-only; a key removal would orphan an authored identity (the
     * binding test fails loudly if it ever happens).
     */
    public static Map<String, Integer> notableSpawnSites() {
        Map<String, Integer> sites = new java.util.LinkedHashMap<>();
        sites.put("K01_WEIGHHOUSE", worldCell(K01_WEIGHHOUSE, ZA));
        sites.put("K02_IMPOUND", worldCell(K02_IMPOUND, ZA));
        sites.put("K03_GILDED_GULL", worldCell(K03_GILDED_GULL, ZA));
        sites.put("K04_BILGE", worldCell(K04_BILGE, ZA));
        sites.put("K05_LANTERN_ROOM", worldCell(K05_LANTERN_ROOM, ZA));
        sites.put("K06_HARLS_YARD", worldCell(K06_HARLS_YARD, ZA));
        sites.put("K07_ROPEWALK", worldCell(K07_ROPEWALK, ZA));
        sites.put("K08_BRANNS", worldCell(K08_BRANNS, ZA));
        sites.put("K09_PITCHFIELD", worldCell(K09_PITCHFIELD, ZA));
        sites.put("K11_SALT_ROW", worldCell(K11_SALT_ROW, ZA));
        sites.put("K12_KINGS_BOND", worldCell(K12_KINGS_BOND, ZA));
        sites.put("K13_DROWNED_HOLD", worldCell(K13_DROWNED_HOLD, ZA));
        sites.put("K14_WRACKHOUSE", worldCell(K14_WRACKHOUSE, ZA));
        sites.put("K15_FENNERS", worldCell(K15_FENNERS, ZA));
        sites.put("K18_BATHHOUSE", worldCell(K18_BATHHOUSE, ZA));
        sites.put("K19_ROWS", worldCell(K19_ROWS, ZA));
        sites.put("K20_MERLES", worldCell(K20_MERLES, ZA));
        sites.put("K22_NETMENDERS", worldCell(K22_NETMENDERS, ZA));
        sites.put("K23_COOPERS", worldCell(K23_COOPERS, ZA));
        sites.put("K25_KENNEL_ROW", worldCell(K25_KENNEL_ROW, ZA));
        sites.put("K26_SAILMAKER", worldCell(SAILMAKER, ZA));
        sites.put("K27_HARDTACK", worldCell(K27_HARDTACK, ZA));
        sites.put("K28_SLOPCHEST", worldCell(K28_SLOPCHEST, ZA));
        sites.put("K29_LONGSTORE", worldCell(K29_LONGSTORE, ZA));
        sites.put("K34_GUARDHOUSE", worldCell(K34_GUARDHOUSE, ZA));
        sites.put("K36_BANK_COUNTER", worldCell(BANK_COUNTER, ZA));
        sites.put("SHIP_K30_KESTREL", worldCell(SHIP_K30_KESTREL, ZA));
        sites.put("SHIP_K31_BREGGAS_PROMISE", worldCell(SHIP_K31_BREGGAS_PROMISE, ZA));
        sites.put("SHIP_K32_DEEPKEEL", worldCell(SHIP_K32_DEEPKEEL, ZA));
        sites.put("MISSION_BUNKS", worldCell(MISSION_BUNKS, ZA));
        sites.put("WATCHPOST_K21", worldCell(WATCHPOST_K21, ZC));
        sites.put("C1_MANSION", worldCell(C1_MANSION, ZB));
        sites.put("C2_MANSION", worldCell(C2_MANSION, ZA));
        sites.put("C3_MANSION", worldCell(C3_MANSION, ZB));
        sites.put("C4_RUIN", worldCell(C4_RUIN, ZA));
        sites.put("PEN_GOATS", worldCell(PEN_GOATS, ZB));
        sites.put("CARTER_STAND", worldCell(CARTER_STAND, ZA));
        sites.put("LAIR_SKYRUNNER", worldCell(LAIR_SKYRUNNER, ZC));
        sites.put("STRAND_JEK", worldCell(STRAND_JEK, 10));
        sites.put("CLERKS_ROOMING_HOUSE", worldCell(CLERKS_ROOMING_HOUSE, ZA));
        return sites;
    }

    /**
     * Site display names for the NameForge's template bios: world-packed cell -&gt; the
     * gazetteer's own register. Insertion order is the deterministic tie-breaker for the
     * forge's nearest-site lookup, so this map is append-only too.
     */
    public static Map<Integer, String> siteDisplayNames() {
        Map<Integer, String> names = new java.util.LinkedHashMap<>();
        names.put(worldCell(K01_WEIGHHOUSE, ZA), "the Weighhouse");
        names.put(worldCell(K02_IMPOUND, ZA), "the Impound Yard");
        names.put(worldCell(K03_GILDED_GULL, ZA), "the Gilded Gull");
        names.put(worldCell(K04_BILGE, ZA), "the Bilge");
        names.put(worldCell(K05_LANTERN_ROOM, ZA), "the Lantern Room");
        names.put(worldCell(K06_HARLS_YARD, ZA), "Harl's Yard");
        names.put(worldCell(K07_ROPEWALK, ZA), "the Ropewalk");
        names.put(worldCell(K08_BRANNS, ZA), "Brann's Chandlery");
        names.put(worldCell(K09_PITCHFIELD, ZA), "Pitchfield");
        names.put(worldCell(K10_DAWNSTALLS, ZA), "the Dawnstalls");
        names.put(worldCell(K11_SALT_ROW, ZA), "Salt Row");
        names.put(worldCell(K12_KINGS_BOND, ZA), "the King's Bond");
        names.put(worldCell(K13_DROWNED_HOLD, ZA), "the Drowned Hold");
        names.put(worldCell(K14_WRACKHOUSE, ZA), "the Wrackhouse");
        names.put(worldCell(K15_FENNERS, ZA), "Fenner's Pawn");
        names.put(worldCell(K17_MISSION, ZA), "the Mission");
        names.put(worldCell(K18_BATHHOUSE, ZA), "the bathhouse");
        names.put(worldCell(K19_ROWS, ZA), "the Rows");
        names.put(worldCell(K20_MERLES, ZA), "Merle's Boats");
        names.put(worldCell(K22_NETMENDERS, ZA), "the Netmenders' Arcade");
        names.put(worldCell(K23_COOPERS, ZA), "the cooperage");
        names.put(worldCell(K25_KENNEL_ROW, ZA), "Kennel Row");
        names.put(worldCell(SAILMAKER, ZA), "the Sailmaker's Loft");
        names.put(worldCell(K27_HARDTACK, ZA), "the Hardtack Oven");
        names.put(worldCell(K28_SLOPCHEST, ZA), "the Slop-Chest");
        names.put(worldCell(K29_LONGSTORE, ZA), "the Long Store");
        names.put(worldCell(K34_GUARDHOUSE, ZA), "the guardhouse");
        names.put(worldCell(BANK_COUNTER, ZA), "the Counting-House");
        names.put(worldCell(CLERKS_DESK, ZA), "the clerk's desk");
        names.put(worldCell(GUARD_POST_BANK_WEST, ZA), "the Counting-House door");
        names.put(worldCell(GUARD_POST_BANK_EAST, ZA), "the Counting-House door");
        names.put(worldCell(SHIP_K30_KESTREL, ZA), "the Kestrel");
        names.put(worldCell(SHIP_K31_BREGGAS_PROMISE, ZA), "Bregga's Promise");
        names.put(worldCell(SHIP_K32_DEEPKEEL, ZA), "the Deep Keel");
        names.put(worldCell(BERTH_01, ZA), "Berth One");
        names.put(worldCell(BERTH_02, ZA), "Berth Two");
        names.put(worldCell(BERTH_03, ZA), "Berth Three");
        names.put(worldCell(CRANE, ZA), "the quay crane");
        names.put(worldCell(MUSTER_QUAY, ZA), "the muster quay");
        names.put(worldCell(PIER_01, ZA), "the finger piers");
        names.put(worldCell(PIER_02, ZA), "the finger piers");
        names.put(worldCell(PIER_03, ZA), "the finger piers");
        names.put(worldCell(PIER_04, ZA), "Pier Row");
        names.put(worldCell(TIMBER_YARD_STAND, ZA), "the timber yard");
        names.put(worldCell(MISSION_BUNKS, ZA), "the Mission bunkroom");
        names.put(worldCell(MISSION_GARDEN, ZA), "the Mission garden");
        names.put(worldCell(TERRACE_WALK_STAND, ZB), "Terrace Walk");
        names.put(worldCell(SALTGATE_PORTERS, ZB), "the Saltgate porters' stand");
        names.put(worldCell(WELL_PLAZA, ZC), "the well plaza");
        names.put(worldCell(NOTICE_BOARD, ZC), "the notice board");
        names.put(worldCell(ABBEY_LANE, ZC), "Abbey Lane");
        for (int[] stall : EELPOT_STALLS) {
            names.put(worldCell(stall, ZA), "the Eel-Pots");
        }
        names.put(worldCell(WATCHPOST_K21, ZC), "the Saltgate watch-post");
        names.put(worldCell(PATROL_RISE_TOP, ZC), "the Rise head");
        names.put(worldCell(PATROL_RISE_FOOT, ZA), "the Rise foot");
        names.put(worldCell(PATROL_TARWALK_WEST, ZA), "the west Tarwalk");
        names.put(worldCell(PATROL_TARWALK_MID, ZA), "the mid Tarwalk");
        names.put(worldCell(WATCH_BOND_POST, ZA), "the Bond post");
        names.put(worldCell(C2_ROOF_WATCHPOST, ZC), "the Netters' roof post");
        names.put(worldCell(PATROL_TARWALK[2], ZA), "the Tarwalk beat");
        names.put(worldCell(PATROL_TARWALK[3], ZA), "the Tarwalk beat");
        names.put(worldCell(PATROL_QUAY[1], ZA), "the quay beat");
        names.put(worldCell(PATROL_ROPEWYND[4], ZA), "the Ropewynd beat");
        // The seven shop guard posts, in SHOP_GUARD_POSTS order (K08/K14/K15/K23/K26/K27/K28).
        String[] shopDoors = {"Brann's door", "the Wrackhouse door", "Fenner's door",
                "the cooperage door", "the Loft door", "the Oven door", "the Slop-Chest door"};
        for (int i = 0; i < SHOP_GUARD_POSTS.length; i++) {
            names.put(worldCell(SHOP_GUARD_POSTS[i], ZA), shopDoors[i]);
        }
        names.put(worldCell(C1_MANSION, ZB), "the Quayward mansion");
        names.put(worldCell(C1_COURTYARD, ZB), "the Quayward courtyard");
        for (int[] condo : C1_CONDOS_GROUND) {
            names.put(worldCell(condo, ZB), "the Quayward Compound");
        }
        for (int[] condo : C1_CONDOS_UPPER) {
            names.put(worldCell(condo, ZC), "the Quayward Compound");
        }
        names.put(worldCell(C2_MANSION, ZA), "the Netters' mansion");
        for (int[] condo : C2_CONDOS_GROUND) {
            names.put(worldCell(condo, ZA), "the Netters' Compound");
        }
        for (int[] condo : C2_CONDOS_UPPER) {
            names.put(worldCell(condo, ZB), "the Netters' Compound");
        }
        for (int[] hut : C2_ROOFHUTS) {
            names.put(worldCell(hut, ZC), "the Netters' roof decks");
        }
        names.put(worldCell(C3_MANSION, ZB), "the Saltgate mansion");
        names.put(worldCell(C3_COURTYARD, ZB), "the Saltgate courtyard");
        for (int[] condo : C3_CONDOS_GROUND) {
            names.put(worldCell(condo, ZB), "Saltgate Terrace");
        }
        for (int[] condo : C3_CONDOS_UPPER) {
            names.put(worldCell(condo, ZC), "Saltgate Terrace");
        }
        for (int[] hut : C3_ROOFHUTS) {
            names.put(worldCell(hut, 14), "the Terrace roofs");
        }
        for (int[] condo : C4_CONDOS_GROUND) {
            names.put(worldCell(condo, ZA), "the Gullet");
        }
        names.put(worldCell(C4_CONDO_UPPER, ZB), "the Gullet");
        for (int[] hut : C4_ROOFHUTS) {
            names.put(worldCell(hut, ZC), "the Gullet roof decks");
        }
        names.put(worldCell(C4_RUIN, ZA), "the Gullet ruin");
        // The Skyrunner's Roost stays COVER-SAFE (K35 is unmarked by design): its anchor
        // resolves to the same register any roof-deck tenant would show.
        names.put(worldCell(LAIR_SKYRUNNER, ZC), "the Gullet roof decks");
        for (int[] hovel : HOVELS_A) {
            names.put(worldCell(hovel, ZA), "a quayside hovel row");
        }
        for (int[] hovel : HOVELS_B) {
            names.put(worldCell(hovel, ZB), "the mid-slope hovel row");
        }
        for (int[] hovel : HOVELS_C) {
            names.put(worldCell(hovel, ZC), "the upper hovel row");
        }
        names.put(worldCell(PEN_GOATS, ZB), "the goat pen");
        names.put(worldCell(CARTER_STAND, ZA), "the carter's stand");
        names.put(worldCell(HITCH_GULL, ZA), "the Gull's hitching post");
        names.put(worldCell(HITCH_BILGE, ZA), "the Bilge's hitching post");
        names.put(worldCell(HITCH_ROWS, ZA), "the Rows' hitching post");
        names.put(worldCell(STRAND_JEK, 10), "the Beaching Strand");
        // S3 append (the append-only rule): the quest's rooming house gets its register name,
        // so its lodgers' template bios read "lodges at Sedge's rooming house".
        names.put(worldCell(CLERKS_ROOMING_HOUSE, ZA), "Sedge's rooming house");
        return names;
    }

    // ---- Living-docks Pass 4: the two new dock trades + the restricted-zone access data. The
    // anchor sets below are the SAME z:+11 work cells the reassignment pass relabels
    // serf.laborer -> Sailor/Trader at (so a Sailor's gate is exactly the site a Sailor works),
    // and the cells the F3 gate tests exercise. Every cell is single-z (the z rule).

    /** Ship-hull + shipyard work anchors: their serf.laborer hands become Sailors; the Sailor gate. */
    static int[] maritimeTradeAnchors() {
        return new int[] {
            worldCell(SHIP_K30_KESTREL, ZA), worldCell(SHIP_K31_BREGGAS_PROMISE, ZA),
            worldCell(SHIP_K32_DEEPKEEL, ZA), worldCell(K06_HARLS_YARD, ZA),
            worldCell(TIMBER_YARD_STAND, ZA)};
    }

    /** Retail-shop counter anchors: their serf.laborer clerks become Traders; the Trader gate. */
    static int[] traderShopAnchors() {
        return new int[] {
            worldCell(K08_BRANNS, ZA), worldCell(K14_WRACKHOUSE, ZA), worldCell(K15_FENNERS, ZA),
            worldCell(K23_COOPERS, ZA), worldCell(SAILMAKER, ZA), worldCell(K27_HARDTACK, ZA),
            worldCell(K28_SLOPCHEST, ZA)};
    }

    /**
     * The K34 holding-cell block (z:+11) — the guard-only zone. PASS 9: the watch-room anchor
     * cell was REMOVED from the zone (it stays the zone's station): a citizen drifting through
     * the widened guardhouse doors and pausing in the watch room must not draw a 1-day unfed
     * custody term. The cell block sits behind the corridor, where only escorts ever stand.
     */
    private static int[] guardhouseInteriorCells() {
        int[] cells = new int[PRISON_CELLS_K34.length];
        for (int i = 0; i < PRISON_CELLS_K34.length; i++) {
            cells[i] = worldCell(PRISON_CELLS_K34[i], ZA);
        }
        return cells;
    }

    // ---- Law & order pass: the POLICED shop zones + the bank vault room. RE-ARMED (density
    // revisit, sim pass): the map pass had shrunk each zone to its solid stock-fixture core
    // because the 1-day loiter sentence was UNFED — any walkable policed cell was a starvation
    // lottery (three animal-keeper deaths traced in the 30k soak) — with the explicit note to
    // re-arm once custody became survivable. Custody now FEEDS from the prisoner's own carried
    // ration ({@code HeldPolicy.eatCarriedRation}, the same provisioning that stocks every
    // citizen's pantry), so the full shell footprints are restored: the Watch polices whole
    // shop interiors again, and a dawdler draws a survivable day in the K34 cells.
    private static final int[][] POLICED_SHOP_RECTS = {   // {x0, y0, x1, y1}, all z:+11
            {24, 66, 31, 74},     // K08 Brann's Chandlery
            {164, 34, 173, 42},   // K14 Wrackhouse
            {122, 52, 128, 58},   // K15 Fenner's Pawn
            {40, 66, 53, 76},     // K23 Cooper & Blockmaker
            {8, 70, 15, 77},      // K26 Sailmaker's Loft
            {32, 70, 38, 78},     // K27 The Hardtack Oven
            {130, 58, 135, 64},   // K28 The Slop-Chest
    };
    // The whole K36 shell again (queue slots stay carved out below) — a hall dawdle is once
    // more the Watch's business now that the sentence it draws is fed.
    private static final int[] BANK_HALL_RECT = {150, 48, 159, 59};   // K36 shell, z:+11

    // S3 "The Vanished Clerk": the clerk's locked desk, inside the hall shell, clear of the
    // queue slots ({154,49..51}), the counter (154,53), the vault chest (152,57) and both
    // guard posts (152/156,53). Bake-seeds one LEDGER_LEAF; the quest's search cell.
    private static final int[] CLERKS_DESK = {151, 50};               // K36 hall, z:+11

    /** A generator shell rect as world-packed cells, minus {@code excluded} cells. */
    private static int[] rectCells(int[] rect, int z, int... excludedWorldCells) {
        List<Integer> cells = new ArrayList<>();
        for (int y = rect[1]; y <= rect[3]; y++) {
            for (int x = rect[0]; x <= rect[2]; x++) {
                int cell = worldCell(new int[] {x, y}, z);
                if (!containsCell(excludedWorldCells, cell)) {
                    cells.add(cell);
                }
            }
        }
        return toIntArray(cells);
    }

    private static boolean containsCell(int[] cells, int cell) {
        for (int c : cells) {
            if (c == cell) {
                return true;
            }
        }
        return false;
    }

    /**
     * The baked restricted-zone side-table. The four Pass-4 access-gate zones (each gated on a
     * PRESENTED job): the shipyard/ships (Sailor), the guardhouse interior + cells (Guard =
     * watch.patrol), the bank vault chest (Guard — there is no distinct banker job this pass, so
     * the Watch protects it), and the shops' special/back-room inventory (Trader) — PLUS, law &amp;
     * order pass (Passes 11-12), the POLICED zones the Watch's APPREHEND enforces live: the seven
     * retail-shop interiors (Trader-gated, so anchored staff and correctly-presenting actors pass)
     * and the bank hall (Guard-gated; the queue slots are carved out as legitimate ground). Zone
     * order is append-only: the four Pass-4 zones keep their indices, and {@code zoneAt}'s
     * lowest-index-wins rule means the original single-cell gates still win their own cells.
     */
    /** The clerk's locked desk cell (S3 "The Vanished Clerk"), world-packed. */
    public static int clerksDeskCell() {
        return worldCell(CLERKS_DESK, ZA);
    }

    /**
     * The restricted-zone INDEX of the K36 bank hall — resolved off the very list
     * {@link #restrictedZoneTable()} builds (no duplicated magic index): the first zone
     * whose cells contain the clerk's desk. The quest bake binds {@code bank_hall} here.
     */
    public static int bankHallZoneIndex() {
        RestrictedZoneTable table = restrictedZoneTable();
        int desk = clerksDeskCell();
        for (int i = 0; i < table.size(); i++) {
            if (table.get(i).contains(desk)) {
                return i;
            }
        }
        throw new IllegalStateException(
                "no restricted zone contains the clerk's desk — the hall shell moved?");
    }

    public static RestrictedZoneTable restrictedZoneTable() {
        List<RestrictedZone> zones = new ArrayList<>();
        zones.add(new RestrictedZone(Job.Maritime.Sailor.ID, Actor.NONE, maritimeTradeAnchors()));
        zones.add(new RestrictedZone(Job.Watch.Patrol.ID, worldCell(K34_GUARDHOUSE, ZA),
                guardhouseInteriorCells()));
        zones.add(new RestrictedZone(Job.Watch.Patrol.ID, worldCell(GUARD_POST_BANK_WEST, ZA),
                new int[] {worldCell(BANK_VAULT_CHEST, ZA)}));
        zones.add(new RestrictedZone(Job.Trade.Trader.ID, Actor.NONE, traderShopAnchors()));
        // Law & order pass: the policed shop interiors, one zone per shop, each stationed with
        // the shop's exterior guard post (same order as SHOP_GUARD_POSTS).
        for (int i = 0; i < POLICED_SHOP_RECTS.length; i++) {
            zones.add(new RestrictedZone(Job.Trade.Trader.ID,
                    worldCell(SHOP_GUARD_POSTS[i], ZA), rectCells(POLICED_SHOP_RECTS[i], ZA)));
        }
        // The bank hall (queue slots carved out): loitering inside the Counting-House is the
        // Watch's business; standing in line is not.
        zones.add(new RestrictedZone(Job.Watch.Patrol.ID, worldCell(GUARD_POST_BANK_EAST, ZA),
                rectCells(BANK_HALL_RECT, ZA, toIntArray(bankQueue()))));
        return new RestrictedZoneTable(zones);
    }

    // ---- Economy-loop pass: FOOD seeding + the distribution market -----------------------------

    /**
     * Seeds every unique CITIZEN home-cell larder with {@link FoodEconomy#LARDER_SEED} FOOD so
     * the first hunger cycles are covered before farm yield / imports ramp. Deterministic:
     * ascending home order, dedup by cell (a bunk crew shares one home cell — seed it once).
     * Returns the total FOOD minted, for the closed-supply conservation proof.
     *
     * <p>Beast-only home cells — feral gull roosts, mouse dens, cat prowl anchors — get NO
     * seed (the beast food channel pass): none of those three types runs {@code SEEK_FOOD}, so
     * their seeds would be permanently-inert live FOOD, and — critically — mouse dens sit ON
     * garbage-bin cells, where a 3-FOOD "larder seed" would pre-stock the bins at t=0 and
     * silently widen the wastrel scavenge margin. {@code animal} homes are NOT skipped:
     * chattel goats/dogs still run SEEK_FOOD and eat their pen seeds today.
     */
    private static long seedLarders(ActorRegistry registry, HomeRegistry homes,
            ItemsLiteRegistry items) {
        HashSet<Integer> citizenHomeCells = new HashSet<>();
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (a.homeId() != Actor.NONE && !isSeedlessBeast(a.typeId().key())) {
                citizenHomeCells.add(homes.get(a.homeId()).homeCell());
            }
        }
        long minted = 0;
        HashSet<Integer> seeded = new HashSet<>();
        for (int i = 0; i < homes.size(); i++) {
            int cell = homes.get(i).homeCell();
            if (citizenHomeCells.contains(cell) && seeded.add(cell)) {
                minted += items.addOnCell(cell, ItemKinds.FOOD, FoodEconomy.LARDER_SEED);
            }
        }
        return minted;
    }

    /** The types whose stacks have no {@code SEEK_FOOD} — their home cells get no larder seed. */
    private static boolean isSeedlessBeast(String typeKey) {
        return typeKey.equals("feral") || typeKey.equals("mouse") || typeKey.equals("cat");
    }

    /**
     * Stocks every vendor counter to {@link FoodEconomy#SHOP_STOCK_CAP} at bake, so the market is
     * already provisioned when the first synchronised hunger wave arrives (before the first quay
     * import at tick {@link FoodEconomy#IMPORT_PERIOD}). Deterministic ascending-index scan; returns
     * the FOOD minted for the conservation proof. This is the same imported supply the quay tops up
     * each period — a citizen still must BUY it with Royals, so the money gate is untouched.
     */
    private static long seedVendorStock(FoodMarket market, ItemsLiteRegistry items) {
        long minted = 0;
        for (int i = 0; i < market.vendorCount(); i++) {
            minted += items.addCarried(market.vendorAt(i), ItemKinds.FOOD, FoodEconomy.SHOP_STOCK_CAP);
        }
        return minted;
    }

    /**
     * Seeds each provisioned citizen with a starting {@link FoodEconomy#CARRY_RATION}-meal pantry at
     * bake, so the population is fed through the first synchronised hunger wave before the first paid
     * provisioning (tick {@link FoodEconomy#IMPORT_PERIOD}) — the food analogue of the seed larder /
     * seed Royals. Deterministic ascending-index scan; returns the FOOD minted for the conservation
     * proof. From tick {@link FoodEconomy#IMPORT_PERIOD} on, every refill of this pantry is BOUGHT.
     */
    private static long seedRations(FoodMarket market, ItemsLiteRegistry items) {
        long minted = 0;
        for (int i = 0; i < market.provisionedCount(); i++) {
            minted += items.addCarried(market.provisionedAt(i), ItemKinds.FOOD, FoodEconomy.CARRY_RATION);
        }
        return minted;
    }

    /**
     * Bakes the FOOD-distribution {@link FoodMarket} (money-gated market pass). Two deterministic
     * ascending-scan lists, respecting the z-rule (every channel a hungry actor uses is same-z):
     * <ul>
     *   <li><b>vendor shops</b> — every {@code shopkeeper}, on any band: the organic dockside
     *       proprietors (z:+11), the on-hull victuallers aboard the crewed ships, the compound
     *       owners who vend to their own courtyard (z:+12), and the off-band victuallers stationed
     *       on the terraces (z:+12/z:+13). This is the market the waged mass BUYS from (solvent
     *       &rArr; eats, broke &rArr; starves — the money lever); the quay import keeps them stocked.
     *       A hungry citizen buys from the nearest reachable same-z counter, so placing a vendor on
     *       every inhabited band is what makes the whole population reachably fed.</li>
     *   <li><b>farm-fed commons</b> — the compound atria (C1/C3 courtyards) and the mission garden:
     *       shared subsistence larders stocked ONLY by the compound's own farmers (never a free
     *       ration), so a same-band compound resident eats the harvest free — legitimately
     *       non-market. A cell exists, and stays stocked, only where a farmer actually works, so no
     *       shop-dependent cohort is fed for free. The roof-slum poor reach neither a vendor they
     *       can afford nor a farm larder — the intended starvation margin.</li>
     * </ul>
     */
    private static FoodMarket buildFoodMarket(ActorRegistry registry) {
        List<Integer> vendors = new ArrayList<>();
        List<Integer> provisioned = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            String type = a.typeId().key();
            if (type.equals("shopkeeper")) {
                vendors.add(a.id()); // every shopkeeper vends food, on whatever band it stands
            }
            if (a.homeId() != Actor.NONE && isProvisionedCitizen(type)) {
                provisioned.add(a.id()); // the serf mass + middle class do their periodic shopping
            }
        }
        // The farm-fed commons: each compound's courtyard/atrium (the farmers' own work anchor,
        // which produceFood tops after the household larder) plus the mission garden. Same-z by
        // construction — the C1/C3 courtyards are Band B (z:+12), the mission garden Band A (z:+11).
        List<Integer> commons = List.of(
                worldCell(C1_COURTYARD, ZB), worldCell(C3_COURTYARD, ZB),
                worldCell(MISSION_GARDEN, ZA));
        // Garbage bins (law & order pass): the walkable bin cells beside each FOOD business the
        // daily scrap drop tops up and the broke's SCAVENGE eats from. Mirrors the generator's
        // garbage_bin_* markers (lockstep).
        List<Integer> bins = new ArrayList<>();
        bins.addAll(worldCells(GARBAGE_BINS_ZA, ZA));
        bins.addAll(worldCells(GARBAGE_BINS_ZB, ZB));
        bins.addAll(worldCells(GARBAGE_BINS_ZC, ZC));
        return new FoodMarket(toIntArray(vendors), toIntArray(commons), toIntArray(provisioned),
                toIntArray(bins));
    }

    /**
     * Whether {@code typeKey} is a provisioned citizen — the working population that must not starve
     * (the serf mass AND the middle class), whose household does its periodic market shopping (a
     * paid ration into carry). Wastrels (the wageless poor + roof decks) and beasts are excluded:
     * they are the intended starvation margin.
     */
    private static boolean isProvisionedCitizen(String typeKey) {
        return switch (typeKey) {
            case "serf", "shopkeeper", "militia_watch", "priest_of_the_flame",
                    "disciple_of_the_flame", "animal_keeper" -> true;
            default -> false;
        };
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
        /** Reused flyweight walkability cursor; {@code null} in the world-less build (all walkable). */
        private final TileCursor cursor;
        /**
         * Running spawn-time occupancy (packedCell -&gt; count) over the WHOLE bake, so every spawn
         * — via {@link #spawnAt} — lands on a cell holding fewer than
         * {@link Actor#MAX_OCCUPANTS_PER_CELL} actors. This is what guarantees no cell begins the
         * sim over the occupancy cap (ONE per square since the density revisit), across every
         * site and any overlap between sites (a
         * proprietor already on an anchor, two nearby dwellings, etc.). Never iterated for output,
         * so its hash-map iteration order is irrelevant to determinism.
         */
        private final Map<Integer, Integer> spawnOccupancy = new HashMap<>();
        private final List<Integer> movers = new ArrayList<>();
        private int trackedGroundMoverId = Actor.NONE;
        private int lodgingCursor;
        private int dockCursor;

        Builder(ActorRegistry registry, HomeRegistry homes, RelationshipRegistry relationships,
                ItemsLiteRegistry items, ActorTypeStatsTable typeStats, JobRegistry jobs,
                HouseholdRaws householdRaws, long seed, World world) {
            this.registry = registry;
            this.homes = homes;
            this.relationships = relationships;
            this.items = items;
            this.typeStats = typeStats;
            this.jobs = jobs;
            this.householdRaws = householdRaws;
            this.seed = seed;
            this.cursor = world == null ? null : world.cursor();
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
            // hatch: Serf.TYPE bunking at the work anchor, home == anchor == the site. That
            // pattern consumes zero household-registry capacity and needs no new dwelling
            // anchor, so it can be extended far more aggressively than the first pass used it --
            // applied here to every genuinely large/storage/industrial site in the district
            // (all anchors below are the site's own already-established business anchor).
            //
            // Occupancy-cap spread (2026-07-15): {@link #bunkAtSite} keeps home + leash anchor
            // pinned to the site, but the SPAWN cell now spreads over the site's nearby walkable
            // floor (deterministic outward BFS, <=2 per cell) so a live-in crew no longer starts
            // stacked dozens-deep on one tile -- the actor-actor occupancy cap
            // (Actor.MAX_OCCUPANTS_PER_CELL) forbids it at spawn as well as in motion.

            // K06 Harl's Yard — 24 shipwrights/caulkers/sawyers bunking across the
            // workshop+timber-yard+slipway complex (the district's biggest multi-part
            // industrial site after the Ropewalk).
            for (int i = 0; i < 24; i++) {
                bunkAtSite(Serf.TYPE, K06_HARLS_YARD, ZA);
            }
            // K07 Ropewalk — 34 more rope-gang hands bunking in the shed itself (576 tiles,
            // the single largest floor in the district; §3.1 "deliberately elongated").
            for (int i = 0; i < 34; i++) {
                bunkAtSite(Serf.TYPE, K07_ROPEWALK, ZA);
            }
            // K12 King's Bond — 20 more porters/night-watch bunking in the sealed, windowless
            // bonded warehouse (bonded goods need round-the-clock security; 221 tiles).
            for (int i = 0; i < 20; i++) {
                bunkAtSite(Serf.TYPE, K12_KINGS_BOND, ZA);
            }
            // K11 Salt Row — 8 seasonal herring-curing hands bunking in the gutting
            // sheds/smokehouse lofts (time-sensitive salt-and-smoke work, dawn starts).
            for (int i = 0; i < 8; i++) {
                bunkAtSite(Serf.TYPE, K11_SALT_ROW, ZA);
            }
            // K14 Wrackhouse — 6 salvage haulers bunking rough among the flotsam (fits the
            // "buys what the sea spits up, no questions" salvage-broker character).
            for (int i = 0; i < 6; i++) {
                bunkAtSite(Serf.TYPE, K14_WRACKHOUSE, ZA);
            }
            // K23 Cooper & Blockmaker — 6 apprentices bunking in the workshop loft (a live-in
            // cooper's apprentice is the historically standard arrangement for the trade).
            for (int i = 0; i < 6; i++) {
                bunkAtSite(Serf.TYPE, K23_COOPERS, ZA);
            }
            // K18 Squall's Bathhouse — 6 boiler stokers bunking by the boilers (the fires
            // can't be left untended overnight).
            for (int i = 0; i < 6; i++) {
                bunkAtSite(Serf.TYPE, K18_BATHHOUSE, ZA);
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
                bunkAtSite(Serf.TYPE, SHIP_K30_KESTREL, ZA);
            }
            for (int i = 0; i < 16; i++) {   // K31 Bregga's Promise (mid, 14x7)
                bunkAtSite(Serf.TYPE, SHIP_K31_BREGGAS_PROMISE, ZA);
            }
            for (int i = 0; i < 22; i++) {   // K32 The Deep Keel (largest, 16x8)
                bunkAtSite(Serf.TYPE, SHIP_K32_DEEPKEEL, ZA);
            }

            // ===================== MARKET VICTUALLERS (money-gated economy pass) ================
            // A FOOD-vending Shopkeeper living/vending in place (home == anchor == cell) on every
            // inhabited band, so a hungry citizen always has a reachable same-z counter to BUY at:
            // on-hull victuallers for the crewed ships (whose crews cannot route to any mainland
            // shop across the water), and off-band victuallers on the z:+12/z:+13 terraces (whose
            // residents have no organic dockside z:+11 shop on their own band). buildFoodMarket
            // picks each up as a vendor; the bake seeds its counter to SHOP_STOCK_CAP.
            for (int[] cell : VICTUALLERS_ZA) {
                soloHomeAtCell(spawn(Shopkeeper.TYPE, cell, ZA));
            }
            for (int[] cell : VICTUALLERS_ZB) {
                soloHomeAtCell(spawn(Shopkeeper.TYPE, cell, ZB));
            }
            for (int[] cell : VICTUALLERS_ZC) {
                soloHomeAtCell(spawn(Shopkeeper.TYPE, cell, ZC));
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
            // Band-A posts -> REAL patrollers (law & order pass, Pass 13): the four
            // non-stationed Band-A watch keep their posts as HOME (they still sleep there) but
            // their work anchor moves onto a route waypoint, which binds each to one of the
            // three ordered single-z routes (PatrolRouteTable.routeContaining reads the anchor).
            // The Rise-foot and Tarwalk-mid watch walk the Tarwalk; the Tarwalk-west watch
            // walks the quay/berth apron; the King's Bond watch walks Ropewynd. All z:+11 —
            // home and every waypoint share one band (the z rule). Stationed shop/bank/roof
            // guards are deliberately NOT bound: they stay at their posts on the square beat.
            int[][] patrollerPosts = {PATROL_RISE_FOOT, PATROL_TARWALK_WEST,
                    PATROL_TARWALK_MID, WATCH_BOND_POST};   // original spawn order kept
            int[][] patrollerWaypoints = {PATROL_TARWALK[2], PATROL_QUAY[1],
                    PATROL_TARWALK[3], PATROL_ROPEWYND[4]};
            for (int i = 0; i < patrollerPosts.length; i++) {
                Actor patroller = spawn(MilitiaWatch.TYPE, patrollerPosts[i], ZA);
                soloHome(patroller);
                patroller.setAnchorCell(worldCell(patrollerWaypoints[i], ZA));
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

            // ===================== K36 THE BANK + SHOP GUARDS (Phase-1 living-docks) ===========
            // The ward's bank (K36): a Shopkeeper banker living/working at the teller counter and
            // two militia_watch stationed at the flanking guard posts INSIDE the hall. Deposit/
            // withdraw verbs + vault seeding are a later economy pass; here the site is physically
            // real and manned (the queue/vault anchors are markers for that pass). All z:+11, so
            // home == work == post is single-band by construction.
            Actor banker = spawn(Shopkeeper.TYPE, BANK_COUNTER, ZA);
            soloHomeAtCell(banker);
            soloHome(spawn(MilitiaWatch.TYPE, GUARD_POST_BANK_WEST, ZA));
            soloHome(spawn(MilitiaWatch.TYPE, GUARD_POST_BANK_EAST, ZA));
            // One militia_watch per retail shop, stationed at its exterior guard post (Eli: "each
            // shop should have one guard"). Enforcement behaviour lands in a later pass.
            for (int[] post : SHOP_GUARD_POSTS) {
                soloHome(spawn(MilitiaWatch.TYPE, post, ZA));
            }

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
            // Gullet gull (beast pass: roost moved off the G3 lane {165,80} onto the open C4
            // courtyard so its roam envelope clears the C2 Netters' compound interior — a
            // wander leg into C2's walled courtyard got trapped behind occupancy-parked
            // residents for 13k+ ticks in the 30k soak and starved the gull to 0).
            // PASS 9 (density revisit): the roost STAYS at {176,84} (a Backwall-corner
            // relocation was tried and failed the roam-bbox floor — the band edge clips any
            // y>=94 roost's envelope to 13 rows). The wedge legs into c02/c05 are fixed
            // STRUCTURALLY instead: both condos' partitions are detached from their west
            // shell wall in the generator, so their rooms form circulation loops and an
            // occupancy-parked household can no longer seal a beast into a partition doorway.
            soloHome(spawn(FeralActor.TYPE, new int[] {176, 84}, ZA));
            soloHome(spawn(FeralActor.TYPE, new int[] {132, 18}, 10));   // Beaching Strand (z:+10)
            // Ropewynd garbage bin (beast pass: WAS the C4 courtyard at {176,84} — the decayed
            // quarter's one-wide condo corridors can box a wandering beast behind occupancy-
            // parked residents for thousands of ticks, which starved this gull through every
            // soak; the Gullet stays hunted by the G3 gull above, whose roost is on the open
            // lane). The bin cell is authored-walkable street with a mouse den on it — a gull
            // living off the Ropewynd trash.
            soloHome(spawn(FeralActor.TYPE, new int[] {91, 65}, ZA));

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

            // ===================== MICE + CATS (beast food channel pass) ======================
            // 30 quay mice at dens on EXISTING authored cells — den = home = anchor, the feral
            // soloHome pattern; spawnAt's free-cell funnel spreads any crowding. EVERY den (and
            // every cat anchor below) is an OPEN STREET cell — bins, stall fronts, quay stands,
            // the mission garden, courtyard trash — NEVER a crewed building interior: the 30k
            // soak proved a warehouse with a work crew (K29/K12-class narrow crate aisles plus
            // a dozen hands parked at the 2/cell cap) crowd-locks any beast that follows prey
            // or a wander draw inside, wedging it until it starves. The Gullet is an ISOLATED
            // hunt cluster (nothing else within hunt sense of the G3 roost), so it holds SIX
            // dens; the three strand mice feed the z:+10 gull (no other prey on its band).
            // Beasts stay outside the citizen economy: no ID card / account (CivicAccounts)
            // and no larder seed (seedLarders skips beast-only home cells).
            // ZA (24): the 8 bin cells, the quay/stall/post/garden street dens, one den on or
            // beside EVERY predator anchor (the orbit-coverage rule above), and the 6 Gullet
            // dens.
            for (int[] den : GARBAGE_BINS_ZA) {
                soloHome(spawn(MouseActor.TYPE, den, ZA));
            }
            // PASS 9 (density revisit): {87,81} is a NEW seam den (a DUPLICATED pair, the
            // GULLET {176,84} precedent) on the 1-wide K17|K29 lane, right at K29's own north
            // door -- the 30k soak wedged the Mission-garden cat at (82,81) in that lane for
            // 6000+ ticks behind occupancy-parked citizens and starved it. The orbit-coverage
            // rule (a den mouse's scurry leash covers the wedge, and adjacency catches need no
            // movement) is the same remedy that saved the C4 corridor gull; TWO mice because a
            // lone den mouse serving a resident wedged predator spends too much of its life
            // DOWNED to keep its own nibble cycle above the hungry bar (the soak measured it
            // at 2999). Reallocated: one from the ZB Saltgate ambience bin (fed no predator),
            // one from the open-strand trio (3 mice for one beach gull; the strand is open
            // ground with no wedge risk) -- the roster stayed exactly 691 (692 since the
            // S1-2 identity pass landed Tarry Jek on the strand, below).
            for (int[] den : new int[][] {MUSTER_QUAY, EELPOT_STALLS[0], EELPOT_STALLS[3],
                    WATCH_BOND_POST, SHOP_GUARD_POSTS[1], MISSION_GARDEN,
                    PATROL_TARWALK_MID, K10_DAWNSTALLS, HITCH_GULL, new int[] {30, 31},
                    new int[] {87, 81}, new int[] {87, 81}}) {
                soloHome(spawn(MouseActor.TYPE, den, ZA));
            }
            for (int[] den : GULLET_MOUSE_DENS) {
                soloHome(spawn(MouseActor.TYPE, den, ZA));
            }
            // ZB (1) / ZC (1): the bins beside the terrace/well cats (orbit coverage); the other
            // terrace bins fed no predator — their mice went to the deficit clusters above (the
            // Saltgate ambience mouse moved to the PASS-9 K17|K29 seam den, see the ZA block).
            soloHome(spawn(MouseActor.TYPE, GARBAGE_BINS_ZB[0], ZB));
            soloHome(spawn(MouseActor.TYPE, GARBAGE_BINS_ZC[2], ZC));
            for (int i = 0; i < 2; i++) {   // PASS 9: was 3 -- one strand mouse moved to the
                soloHome(spawn(MouseActor.TYPE, new int[] {132, 18}, 10));   // K17|K29 seam den
            }
            // 8 wharf cats prowling from open-street anchors (see the interior crowd-lock note
            // above); every anchor has >=1 mouse den inside both its wander envelope and the
            // hunt sense radius.
            for (int[] ground : new int[][] {PATROL_TARWALK_WEST, PATROL_TARWALK_MID,
                    SHOP_GUARD_POSTS[1], K10_DAWNSTALLS, HITCH_GULL, MISSION_GARDEN}) {
                soloHome(spawn(CatActor.TYPE, ground, ZA));
            }
            soloHome(spawn(CatActor.TYPE, TERRACE_WALK_STAND, ZB));
            soloHome(spawn(CatActor.TYPE, WELL_PLAZA, ZC));

            // ===================== TARRY JEK — the strand mudlark (S1-2, Forty Notables) ======
            // Gazetteer §4.4's blessed Wastrel, in the cast since the first draft but never
            // spawned: one mudlark homed on the open strand shingle (home == anchor == the
            // spot, the soloHome pattern — he works the tide-line where he sleeps). Appended
            // LAST among the spawn sections so every pre-existing ActorId (0..690) is
            // untouched; Jek is id 691 by construction and the roster grows 691 -> 692. The
            // civic passes below still cover him (default wastrel.streetlife job, scrap
            // inventory, a seeded account) because they scan the whole registry.
            soloHome(spawn(Wastrel.TYPE, STRAND_JEK, 10));

            // ===================== Civic default jobs + presented-job covers (§10.4) ===========
            for (int i = 0; i < registry.size(); i++) {
                Actor actor = registry.get(i);
                if (actor.jobOrdinal() < 0) {
                    actor.setJobOrdinal((short) jobs.defaultOrdinalFor(actor.typeId()));
                }
            }

            // ===================== Dock trades: cut undifferentiated unemployment (Pass 4) =====
            // Promote the generic serf.laborer hands standing at the ships/shipyard into the new
            // maritime.sailor trade, and the clerks at the retail shops into trade.trader. Runs
            // AFTER the default-job loop (so every idle Serf already reads serf.laborer).
            assignDockTrades();

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

        /**
         * Cut undifferentiated dock unemployment (living-docks Pass 4): relabel the generic
         * {@code serf.laborer} hands already anchored at the ships/shipyard as {@code
         * maritime.sailor}, and those already staffing the retail-shop counters as {@code
         * trade.trader}. This rewrites ONLY the job leaf — every actor keeps its exact existing
         * spawn cell, home and work anchor — so it can introduce no new cross-z commute (the
         * z-rule) and no new cell-occupancy pressure (the 2-per-cell cap): a pure relabel of
         * workers who were already standing at those z:+11 sites. Deterministic ascending-id
         * scan; villains (secret jobs), shopkeepers ({@code trade.stallkeep}), clergy and farmers
         * are never {@code serf.laborer}, so they are left untouched.
         */
        private void assignDockTrades() {
            int laborer = jobs.ordinalOf(Job.Serf.Laborer.ID);
            short sailor = (short) jobs.ordinalOf(Job.Maritime.Sailor.ID);
            short trader = (short) jobs.ordinalOf(Job.Trade.Trader.ID);
            int[] shipyards = maritimeTradeAnchors();
            int[] shops = traderShopAnchors();
            for (int i = 0; i < registry.size(); i++) {
                Actor actor = registry.get(i);
                if (actor.jobOrdinal() != laborer) {
                    continue;
                }
                int anchor = actor.anchorCell();
                if (containsCell(shipyards, anchor)) {
                    actor.setJobOrdinal(sailor);
                } else if (containsCell(shops, anchor)) {
                    actor.setJobOrdinal(trader);
                }
            }
        }

        private static boolean containsCell(int[] cells, int cell) {
            for (int c : cells) {
                if (c == cell) {
                    return true;
                }
            }
            return false;
        }

        /** serf.farmer working a plot: a commuter when the plot differs from home. */
        private void makeFarmer(Actor serf, int[] plot, int z) {
            assignJob(serf, Job.Serf.Farmer.ID);
            serf.setAnchorCell(worldCell(plot, z));
        }

        private Actor spawn(ActorTypeId type, int[] mapXY, int mapZ) {
            return spawnAt(type, worldCell(mapXY, mapZ));
        }

        /**
         * The single spawn funnel: places one actor of {@code type} on the nearest walkable cell
         * to {@code desiredWorldCell} that currently holds fewer than
         * {@link Actor#MAX_OCCUPANTS_PER_CELL} actors (spilling outward only when the desired cell
         * is already full), then records the placement. Because every spawn in this file goes
         * through here, no cell can begin the simulation over the occupancy cap — regardless of
         * how many separate loops or overlapping sites target the same anchor.
         */
        private Actor spawnAt(ActorTypeId type, int desiredWorldCell) {
            int cell = findFreeSpawnCell(desiredWorldCell);
            ActorTypeStats stats = typeStats.get(type);
            Actor actor = registry.spawn(type, stats, cell);
            spawnOccupancy.merge(cell, 1, Integer::sum);
            return actor;
        }

        /**
         * A "bunk at the site" crew member (the ship crews and the K06/K07/K11/K12/K14/K18/K23
         * live-in workforces): its SPAWN cell spreads via {@link #spawnAt} so the site does not
         * start stacked, but its leash anchor and home stay pinned to the SITE anchor, so
         * return-home and the leash still target the workplace exactly as before the spread.
         */
        private Actor bunkAtSite(ActorTypeId type, int[] siteMapXY, int siteZ) {
            int site = worldCell(siteMapXY, siteZ);
            Actor actor = spawnAt(type, site);
            actor.setAnchorCell(site);
            actor.setHomeId(homes.addHome(site));
            return actor;
        }

        // 8-neighborhood, fixed order (E, W, S, N, then the four diagonals) — a deterministic
        // outward ring BFS. Order is arbitrary but FIXED, which is all determinism requires.
        private static final int[] SPREAD_DX = {1, -1, 0, 0, 1, 1, -1, -1};
        private static final int[] SPREAD_DY = {0, 0, 1, -1, 1, -1, 1, -1};
        /** Generous visit cap for the spread BFS — the open docks map never approaches it. */
        private static final int SPREAD_MAX_VISITS = 200_000;

        /**
         * Deterministic outward BFS from {@code desiredWorldCell} over same-z cells, returning the
         * first WALKABLE cell whose running spawn count is below the occupancy cap. The frontier
         * expands geometrically (through walls too) so it can always reach open ground, but only a
         * walkable, under-cap cell is ever returned. Falls back to the desired cell if nothing is
         * found within {@link #SPREAD_MAX_VISITS} (unreachable on the real map — the invariant test
         * would catch it).
         */
        private int findFreeSpawnCell(int desiredWorldCell) {
            int z = PackedPos.z(desiredWorldCell);
            ArrayDeque<Integer> frontier = new ArrayDeque<>();
            HashSet<Integer> visited = new HashSet<>();
            frontier.add(desiredWorldCell);
            visited.add(desiredWorldCell);
            int visits = 0;
            while (!frontier.isEmpty() && visits++ < SPREAD_MAX_VISITS) {
                int cur = frontier.poll();
                if (isWalkableCell(cur)
                        && spawnOccupancy.getOrDefault(cur, 0) < Actor.MAX_OCCUPANTS_PER_CELL) {
                    return cur;
                }
                int cx = PackedPos.x(cur);
                int cy = PackedPos.y(cur);
                for (int d = 0; d < SPREAD_DX.length; d++) {
                    int nx = cx + SPREAD_DX[d];
                    int ny = cy + SPREAD_DY[d];
                    if (nx < 0 || ny < 0 || nx > PackedPos.X_MASK || ny > PackedPos.Y_MASK) {
                        continue;
                    }
                    int ncell = PackedPos.pack(nx, ny, z);
                    if (visited.add(ncell)) {
                        frontier.add(ncell);
                    }
                }
            }
            return desiredWorldCell; // pathological fallback (never hit on the baked docks world)
        }

        /** Whether {@code cell} is walkable (world-less bake: every cell reads walkable). */
        private boolean isWalkableCell(int cell) {
            return cursor == null || Walkability.isWalkable(cursor.moveTo(cell));
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
            int old = actor.cell();
            int x = Math.max(0, Math.min(PackedPos.X_MASK, PackedPos.x(old) + dx));
            int y = Math.max(0, Math.min(PackedPos.Y_MASK, PackedPos.y(old) + dy));
            int desired = PackedPos.pack(x, y, PackedPos.z(old));
            // Keep the displaced cell within the occupancy cap: vacate the old cell, then land on
            // the nearest walkable under-cap cell to the intended displacement, so t=0 stays
            // <=2 per cell even though this teleport bypasses the normal spawn funnel.
            spawnOccupancy.merge(old, -1, Integer::sum);
            int dest = findFreeSpawnCell(desired);
            spawnOccupancy.merge(dest, 1, Integer::sum);
            actor.setCell(dest);
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

        /** Credits {@code quantity} units of {@code kindId} to the actor's carried stack (ItemsLite). */
        private void mint(Actor actor, short kindId, int quantity) {
            items.addCarried(actor.id(), kindId, quantity);
        }
    }
}
