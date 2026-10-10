#pragma once

// THE WARD MAP -- the district plan the player presses M to see, rebuilt as a
// composed terminal panel with names set INSIDE the shapes that own them.
//
// ---------------------------------------------------------------------------
// THE COMPLAINT THIS PASS ANSWERS, VERBATIM
// ---------------------------------------------------------------------------
// The owner played the build and said: "Names are stacking up on the map view.
// Makes it hard to figure out where the place you're looking for is. Update
// this to feel like Daggerfall Unity where the buildings are large and use more
// screen with the names being inside the building's shape."
//
// The page he was looking at is docs/frames/map/before-ward-960.png. What the
// old code did was ELABORATE and aimed at the wrong target: placeMapLabels()
// was a label-AVOIDANCE solver -- plates, one-pixel clearance rings,
// corner-leaning variants, three rings of twenty-four candidate seats, a
// doorstep-clearance rule so a street name would not wall off a door name, and
// a DROP rule for anything that still could not find a hole. Sophisticated
// machinery for fitting eighty-three floating nameplates around a small dense
// map, when the actual answer is to make the map big enough to hold its own
// names. Every one of those mechanisms is gone from this file. Nothing floats.
//
// ---------------------------------------------------------------------------
// TWO REFERENCES COMBINE HERE, AND docs/design/UI-REFERENCE-TERMINAL.md SAYS SO
// ---------------------------------------------------------------------------
// 1. HIS DAGGERFALL UNITY NOTE -- large legible footprints, each place's name
//    set inside its own shape. Answered by drawing every named door's authored
//    footprint rectangle as a solid BLOCK with a border, and setting the name
//    inside that block (fitFootprintLabel below). A building is a shape on this
//    plan, not a dot with a tag pointing at it.
//
// 2. THE CULTGAME MAP FRAME (the spec's "map frame, specifically" section) -- a
//    moveable cursor on the map, the selection NAMED in prose directly beneath
//    it, a TAB ROW of views over that selection, and an aligned key/value
//    detail panel. Answered by MapTab and the composition drawDistrictMap
//    resolves. This is what answers his OTHER sentence -- "finding the person or
//    thing I want at that place" -- because MapTab::People over the selected
//    place is exactly that question, asked and answered.
//
// The two are not alternatives. The footprint labels answer WHERE AM I LOOKING;
// the cursor and the panels answer WHAT IS THERE AND WHO IS IN IT.
//
// ---------------------------------------------------------------------------
// THE SCALE RULING, AND WHY IT WENT THIS WAY
// ---------------------------------------------------------------------------
// "Buildings are large" and "the whole ward fits on one screen" are in direct
// tension, and the numbers say so. The district is 192x127 world tiles. At
// 960x540 the composed map pane is about 620x392 pixels, which fits the whole
// ward at THREE pixels per tile. Measured against the authored footprints and
// the label rule below, three pixels per tile names 24 of the 40 doors inside
// their own shapes; four names 28; six names 35; nine names 39. The last one
// (The Drowned-Name Wall, a 3x3 shrine) would need twenty.
//
// SO THE PAGE ZOOMS, AND ITS DEFAULT IS THE WHOLE WARD. The zoom ladder is
// integer multiples of the fit scale (fit, 2x, 3x, 4x) so it is proportional at
// every window size, and the viewport pans to keep the selection centred.
//
// The default is the whole ward and not a zoomed quarter, deliberately: the
// question the map is opened to answer is "where is the Mission FROM HERE",
// which is a question about relationships between places and needs both of them
// on screen. A default that opened zoomed would answer "what is this block"
// -- a real question, but the second one. Zooming in is one key, and at 2x the
// footprints are large enough that nearly every place on screen is named in
// its own shape, which is the Daggerfall note honoured on demand rather than
// at the cost of orientation.
//
// The sixteen doors too small to hold a name AT FIT DO NOT GET A FLOATING
// PLATE, and that is the deliberate fallback rather than a quiet return to
// stacking: they keep their door dot, they are named the instant the cursor
// selects them (the selection line under the map, always), they are named in
// full in MapTab::Index, and they name themselves the moment you zoom.
//
// ---------------------------------------------------------------------------
// ONE LABEL RULE, NOT PER-BUILDING SPECIAL CASES
// ---------------------------------------------------------------------------
// fitFootprintLabel() is the whole of it and it is applied identically to a
// sixty-four-tile rope shed and a seven-tile pawnshop:
//
//   shout the name -> drop a leading "THE " -> wrap on WORD boundaries to the
//   footprint's own pixel width -> centre the block in the footprint -> and
//   answer EMPTY if the shape cannot hold the result at any wrapping.
//
// Empty is the fallback firing. There is no abbreviation table, no leader line,
// no per-building override, and no clipping mid-word: "THE SLOP-CH" names
// nothing.
//
// ---------------------------------------------------------------------------
// WHAT IS KEPT
// ---------------------------------------------------------------------------
// mapCellAt's band and water rules, MapPalette's atlas derivation, the player's
// facing wedge, the door dots, way names a step greyer than door names, and the
// dedup rule that gives a street signed nine times along its reach exactly one
// name. NO MAP DATA MOVES: gen_docks_surface.py and the .tmx are untouched and
// this file cannot reach them.
//
// NOT MODULAR. The map is a PANE of a FIXED composition -- see panel.hpp's own
// header and the owner's ruling. It is width-aware for the WINDOW (the pane,
// the fit scale, the column counts and the detail split all resolve against the
// frame size) and there is no drag, no resize and no saved arrangement.
//
// PURE RENDER-LAYER READS. Everything here is a const walk over TileQuery, the
// atlas, the generated sign table and already-public body state -- no sim state
// moves, no hash can move, and the whole page is drawable headless, which is
// how its label rule and its geometry get unit tests instead of adjectives.
//
// Floats are legal in this file. Nothing in it may be read by the simulation.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/atlas.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/panel.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render {

// ---------------------------------------------------------------------------
// what one tile of the plan is
// ---------------------------------------------------------------------------

/// How one (x, y) column reads on the plan at the player's own band. The
/// z-band emphasis rule the design settled for v1: the player's OWN band is
/// what the plan is a plan OF, one band down still draws (dimmer -- the quay
/// seen from the mid-slope), and the harbour's water is always shown, because
/// a dock district's map with no waterline is not a map of a dock district.
enum class MapCell : std::uint8_t {
    /// Nothing authored anywhere useful -- the true-black field.
    Void = 0,
    /// Solid at the player's band: masonry, drawn dark in the material's own
    /// tone so warehouse brick and granite frontage read apart.
    Wall,
    /// Walkable at the player's band -- the ground underfoot, drawn light.
    Floor,
    /// Walkable one band DOWN from the player -- lower ground, the same
    /// material tone dimmed, so a mid-slope reader still sees the quay.
    LowerFloor,
    /// The harbour (or any water column): FLUID-lane depth at the surface
    /// band, always shown whatever band the player stands on.
    Water,
};

/// Classifies one column for a plan drawn at `band`. Pure; the whole page's
/// geometry honesty rests on this being a plain read of the same TileQuery
/// answers movement already lives by, so it is public and tested against the
/// real baked Docks.
[[nodiscard]] MapCell mapCellAt(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                                std::int32_t band) noexcept;

// ---------------------------------------------------------------------------
// the palette -- derived from the atlas, never invented beside it
// ---------------------------------------------------------------------------

/// Per-material plan tones, DERIVED from the shipped atlas's own tile
/// averages -- walls from the material's side face darkened, floors from its
/// floor face lifted -- so the plan reads as THIS game's own district and a
/// future art-pack swap recolours the map by the act of swapping. Water and
/// void come from the atlas's own water tile and voidColour().
struct MapPalette {
    /// Indexed by MATERIAL-lane id, same as the atlas.
    std::vector<Rgb> wall;
    std::vector<Rgb> floor;
    Rgb water{0.16F, 0.28F, 0.38F};
    Rgb voidTone{0.02F, 0.02F, 0.03F};

    [[nodiscard]] static MapPalette fromAtlas(const TileAtlas& atlas);

    [[nodiscard]] Rgb wallTone(std::uint16_t material) const noexcept;
    [[nodiscard]] Rgb floorTone(std::uint16_t material) const noexcept;
};

// ---------------------------------------------------------------------------
// frame geometry
// ---------------------------------------------------------------------------

/// The authored extent of a world, in world tiles -- the VOID border ring
/// cropped away so the plan spends its pixels on the district. Scanned once
/// off the column contents; Session caches it at boot.
struct MapBounds {
    std::int32_t minX = 0;
    std::int32_t minY = 0;
    std::int32_t maxX = 0;  // inclusive
    std::int32_t maxY = 0;  // inclusive

    [[nodiscard]] std::int32_t tilesX() const noexcept { return maxX - minX + 1; }
    [[nodiscard]] std::int32_t tilesY() const noexcept { return maxY - minY + 1; }
};

[[nodiscard]] MapBounds mapContentBounds(const sim::TileQuery& tiles);

// ---------------------------------------------------------------------------
// the named places
// ---------------------------------------------------------------------------

/// One rectangle of world tiles, inclusive on both ends.
struct MapRect {
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;

    [[nodiscard]] bool contains(std::int32_t x, std::int32_t y) const noexcept {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1;
    }
};

/// One named place on the plan, with the FOOTPRINT the mapper authored for it.
///
/// The footprint is the whole point of this pass. The generated sign table has
/// carried `x0,y0,x1,y1` since it was first baked and the old page never once
/// looked at it -- it read `anchorX/anchorY` and hung a plate near the point.
/// A place is a SHAPE.
struct MapPlace {
    /// The sign's own id, for a stable tie-break and for debugging.
    std::string id;
    /// The display name, exactly as the .tmx authored it.
    std::string name;
    /// The one line of flavour the sign carries.
    std::string what;
    /// A street or a quay rather than a door. Ways are drawn a step greyer and
    /// are never filled as blocks -- a street is ground, not a building.
    bool way = false;
    /// The footprint, world tiles, INCLUSIVE on both ends.
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    /// The sign itself: the door you knock on, or the post the street is
    /// signed at. Where the door dot goes.
    float anchorX = 0.0F;
    float anchorY = 0.0F;
    std::int32_t band = 0;

    /// EVERY authored rectangle carrying this name, the one above included.
    ///
    /// A door has one. A STREET has as many as it has posts -- Tarwalk has
    /// nine, laid end to end round two bends -- and the difference matters at
    /// three separate places on this page, which is why they are kept rather
    /// than thrown away by the dedup:
    ///
    ///   * THE CURSOR draws a box round every one of them, so selecting a
    ///     street lights the WHOLE street. The first capture of this page had
    ///     the player standing on the Tarwalk with the cursor box fifty tiles
    ///     east of him, on the one segment the dedup had kept -- a box that
    ///     said "here" about somewhere else.
    ///   * `contains` answers for the whole street, so standing anywhere along
    ///     it selects it, and the People view over it answers "who is on the
    ///     Tarwalk" rather than "who is on this one block of it".
    ///   * the NAME is still set in exactly one of them (the roomiest, which is
    ///     x0..y1 above), because a plan that says a street's name nine times
    ///     is the plan this pass was written to replace.
    std::vector<MapRect> segments;

    [[nodiscard]] std::int32_t tilesX() const noexcept { return x1 - x0 + 1; }
    [[nodiscard]] std::int32_t tilesY() const noexcept { return y1 - y0 + 1; }
    /// The area of the NAMED segment -- what the label has to fit inside, and
    /// what the nesting rules sort by. Not the sum of the segments.
    [[nodiscard]] std::int64_t area() const noexcept {
        return static_cast<std::int64_t>(tilesX()) * static_cast<std::int64_t>(tilesY());
    }
    [[nodiscard]] bool contains(std::int32_t x, std::int32_t y) const noexcept {
        for (const MapRect& rect : segments) {
            if (rect.contains(x, y)) {
                return true;
            }
        }
        return x >= x0 && x <= x1 && y >= y0 && y <= y1;
    }
};

/// Every DISTINCT named place in the district, alphabetical by name.
///
/// DEDUPED BY NAME, which is the one rule the old page had that was worth
/// keeping: Tarwalk is signed nine times along its reach and Saltgate Rise six,
/// and a plan that says a street's name six times is a plan nobody can read.
/// The surviving entry is the SEGMENT WITH THE LARGEST FOOTPRINT (ties by id),
/// which is a change from the old centroid pick and a deliberate one: the label
/// now has to fit INSIDE the shape, so the shape with the most room in it is
/// the one that should carry the name.
///
/// Alphabetical because this list is also the INDEX the player reads
/// (MapTab::Index), and because a stable order means a captured frame is
/// reproducible and a cursor index means the same thing between runs.
///
/// Built once, on first call, off the generated table. No world state.
[[nodiscard]] const std::vector<MapPlace>& mapPlaces();

/// The index of the place with this exact authored name, or -1.
[[nodiscard]] int mapPlaceIndex(std::string_view name);

/// The SMALLEST-footprint place whose shape contains this world tile, or -1.
/// Smallest because footprints nest -- The Netter house sits inside The
/// Netters' Compound -- and the inner one is what a player pointing at that
/// ground means.
[[nodiscard]] int mapPlaceUnder(std::int32_t x, std::int32_t y);

/// The STREET a point stands on: the smallest WAY footprint containing it, or
/// the nearest way anchor within `reachTiles` when it stands on none. -1 for
/// neither.
///
/// This is what puts "STANDS ON  TARWALK" in the detail pane, and it is a real
/// piece of navigation rather than trivia: a player told to go to the Gilded
/// Gull is being told to walk the Tarwalk, and the street is the thing they can
/// actually follow. The authored way footprints already know it.
[[nodiscard]] int mapWayUnder(std::int32_t x, std::int32_t y, std::int32_t reachTiles);

/// WHERE YOU WOULD ACTUALLY HEAD FOR, given where you are standing.
///
/// A place with ONE segment answers with its own anchor -- the door you knock
/// on, the post the street is signed at -- because that single point is the way
/// in and is what the door dot on the plan marks.
///
/// A place with MANY answers with the nearest point of the nearest segment, and
/// that is not fussiness. The Tarwalk is signed nine times across the whole
/// width of the ward and its anchor is the eastern post; a body standing on the
/// Tarwalk outside the Gilded Gull was being told "E, 54 PACES" by a pane whose
/// next line said "YOU ARE STANDING IN IT". The bearing has to mean the street,
/// not the sign.
///
/// The same point is used for the distance, for the compass bearing, and by
/// ENTER's own turn -- one answer, so the pane and the body cannot disagree.
void mapAimPoint(const MapPlace& place, std::int32_t fromX, std::int32_t fromY,
                 std::int32_t& outX, std::int32_t& outY);

/// Compass directions the cursor moves in. Same convention as angle.hpp: north
/// is -Y.
enum class MapStep : std::uint8_t { North, East, South, West };

/// THE CURSOR STEPS FROM PLACE TO PLACE, NOT FROM TILE TO TILE.
///
/// The reference's cursor walks a tile grid because in that game a tile IS the
/// unit of choice. Here the unit of choice is a PLACE -- the owner's sentence
/// is "where the place you're looking for is", not "what is at (127,59)" -- so
/// an arrow press moves to the nearest OTHER place whose centre lies in that
/// half-plane from the current one, scored by distance along the direction of
/// travel plus a penalty for sideways drift. Ties break by id.
///
/// That means the selection line under the map always names something, the
/// detail pane is never blank, and four arrow presses cross the ward instead of
/// a hundred and ninety.
///
/// Returns the new index, or `from` unchanged when nothing lies that way.
[[nodiscard]] int mapPlaceToward(int from, MapStep step);

// ---------------------------------------------------------------------------
// THE ONE LABEL RULE
// ---------------------------------------------------------------------------

/// The text scale footprint labels are set at: one glyph per 5 pixels at the
/// 960x540 baseline, and proportional above it, so a name occupies the same
/// FRACTION of the plan at every window size. Deliberately a step under the
/// panel body's own metric -- the labels sit on top of the district's own
/// colours and the plan needs the pixels more than the type does.
[[nodiscard]] int mapLabelScale(int frameHeight) noexcept;

/// SHOUT, DROP A LEADING "THE ", WRAP ON WORD BOUNDARIES, CENTRE -- and answer
/// EMPTY when the shape cannot hold the result at any wrapping.
///
/// This is the ONLY label rule on this page and it does not know what building
/// it is looking at. See the header: no abbreviation table, no leader lines, no
/// per-building overrides, and never a word cut in half.
///
/// `widthPx`/`heightPx` are the footprint's own pixel size at the current zoom.
[[nodiscard]] std::vector<std::string> fitFootprintLabel(std::string_view name, int widthPx,
                                                         int heightPx, int labelScale);

// ---------------------------------------------------------------------------
// the viewport -- world tiles into the map pane
// ---------------------------------------------------------------------------

/// The window into the ward the map pane is currently showing. Integer pixels
/// per tile (the chunkiness is the art direction), the view centred on a world
/// point and clamped so the plan never floats off its own pane.
struct MapViewport {
    int scale = 1;
    /// Screen pixel of world tile column `bounds.minX`'s left edge.
    int originX = 0;
    int originY = 0;
    PanelRect pane{};
    MapBounds bounds{};

    [[nodiscard]] int pxOfX(float worldX) const noexcept {
        return originX + static_cast<int>((worldX - static_cast<float>(bounds.minX)) *
                                          static_cast<float>(scale));
    }
    [[nodiscard]] int pxOfY(float worldY) const noexcept {
        return originY + static_cast<int>((worldY - static_cast<float>(bounds.minY)) *
                                          static_cast<float>(scale));
    }
    /// The inverse, for a mouse.
    [[nodiscard]] std::int32_t tileOfPx(int px) const noexcept;
    [[nodiscard]] std::int32_t tileOfPy(int py) const noexcept;
};

/// The largest whole pixels-per-tile at which the WHOLE ward fits `pane`. Never
/// below 1 -- a window too small to fit the district at one pixel per tile pans
/// instead, which is the honest answer at 320x180.
[[nodiscard]] int mapFitScale(const PanelRect& pane, const MapBounds& bounds) noexcept;

/// How many zoom steps the ladder has. Step 0 is the whole ward.
[[nodiscard]] int mapZoomSteps() noexcept;

/// Pixels per tile at zoom `step`: INTEGER MULTIPLES of the fit scale, so the
/// ladder is proportional at every window size and every step lands on the tile
/// grid. Clamped to the ladder.
[[nodiscard]] int mapZoomScale(int step, const PanelRect& pane, const MapBounds& bounds) noexcept;

/// The viewport centred on (centreX, centreY) at `scale`, clamped so the plan's
/// own edges never pull inside the pane while there is district left to show.
/// A ward smaller than the pane is centred in it.
[[nodiscard]] MapViewport mapViewport(const PanelRect& pane, const MapBounds& bounds, int scale,
                                      float centreX, float centreY) noexcept;

/// Which named place a pixel of the map pane lands on, or -1. The inverse of
/// what was drawn, so a mouse cannot fall out of register with the picture --
/// panel.hpp's optionListAt makes the same argument for lists.
[[nodiscard]] int mapPlaceAtPixel(const MapViewport& view, int px, int py);

// ---------------------------------------------------------------------------
// the page
// ---------------------------------------------------------------------------

/// The views over the current selection. The reference's own four, translated:
/// Overview / People / Infrastructure / Legend becomes Overview / People /
/// Index / Legend, because a district of eighty-three signs has no
/// infrastructure to report and DOES need a way to be told "go to the
/// Weighhouse" and find it.
enum class MapTab : std::uint8_t {
    /// Aligned key/value facts about the selection: what it is, where it is,
    /// how far and which way from where you stand.
    Overview,
    /// WHO IS IN THERE RIGHT NOW. The direct answer to "finding the person or
    /// thing I want at that place".
    People,
    /// Every named place in the ward, alphabetical. Selecting one moves the
    /// cursor to it and pans the plan there.
    Index,
    /// What the colours on the plan mean.
    Legend,
};

inline constexpr int kMapTabCount = 4;

/// One body standing inside the selected footprint, as the People view prints
/// it. Gathered by Session off the live roster; this file never touches sim.
struct MapPersonRow {
    std::string name;
    /// What they are -- "SHOPKEEPER", "MILITIA WATCH".
    std::string what;
    std::int32_t x = 0;
    std::int32_t y = 0;
};

/// Everything drawDistrictMap needs, gathered by Session::drawFrame.
struct DistrictMapState {
    const sim::TileQuery* tiles = nullptr;
    const MapPalette* palette = nullptr;
    MapBounds bounds{};
    /// Player position in world-tile units, and facing.
    float playerX = 0.0F;
    float playerY = 0.0F;
    std::int32_t band = 0;
    std::int32_t yawBam = 0;
    /// Where the player is standing, in the HUD's own words -- the breadcrumb's
    /// middle crumb.
    std::string title;
    /// A bouncer's warning routed onto this page while it is up, the same
    /// contract the tiled Menu's journal tile carries. Empty draws nothing.
    /// It outranks the header row while it lasts -- there is no separate
    /// breadcrumb line to take any more (UI-EA-SPEC sec. 5, breadcrumb law).
    std::string alert;
    /// 0 (closed) .. 1 (open) -- the page's own EasedToggle, per the settled
    /// UI convention (DECISIONS.md rule 1).
    float openAmount = 1.0F;

    // --- UI-EA-SPEC sec. 2: the Law of Earned Text -------------------------
    /// TUTOR tier, 0 (rest) .. 1 (raised). At rest the nav band prints bare
    /// keycaps and no verb words; raised, the verb words ride beside the caps
    /// at this strength. Raised on page open, device change, unrecognized
    /// press and idle -- the countdown helper is LANE HUD's, the wake signals
    /// LANE FLOW's; this page only renders the value. Default 0: the at-rest
    /// diet is what a hand-built state draws.
    float tutor = 0.0F;
    /// Contract (b): the commit beat. Armed (set toward 1) by the routing's
    /// ImpactPulse at a commit press; this page renders drawCommitPulse over
    /// its commit foot while it decays. Default 0 draws nothing.
    float commitPulse = 0.0F;

    // --- the page's own cursor ---------------------------------------------
    /// Index into mapPlaces(). The cursor, and the subject of every tab.
    int selected = 0;
    /// Index into the zoom ladder. 0 is the whole ward.
    int zoom = 0;
    MapTab tab = MapTab::Overview;
    /// First visible row of a scrolling tab (People, Index). Whole screenfuls.
    int detailFirst = 0;

    /// Who is standing inside the selection, gathered by the caller.
    std::vector<MapPersonRow> people;
    /// Right-aligned in the tab row. The clock and the band -- the two facts a
    /// map reader wants permanently on screen.
    std::string readout;

    // --- ship note move 3: the prompts name the device holding them --------
    //
    // The nav band's keys in the vocabulary of whichever device last spoke,
    // assembled by Session off the live binding table (promptLabel and
    // friends in controls.hpp). THE DEFAULTS ARE THE EXACT LITERALS THIS
    // PAGE ALWAYS PRINTED, so a hand-built state -- every test written
    // before these fields existed -- draws byte-identical frames.
    /// "ARROWS", or "D-PAD" with a pad in hand.
    std::string navMoveKeys = "ARROWS";
    /// "TAB", or "LT RT" -- the sub-tab grammar (controls.hpp's tabStep;
    /// main.cpp routes it to cycleDistrictMapTab). Nine and the sticks put
    /// the views on the triggers, Oblivion's own sub-tab pair.
    std::string navTabKeys = "TAB";
    /// "+ -", or "RS" -- the right stick carries the zoom ladder on a pad
    /// (main.cpp's stickNav routes it as the raw `=` and `-`).
    std::string navZoomKeys = "+ -";
    /// "< >", or "LB RB" -- the PAGE grammar (pageStep): the map is a page
    /// of NOTES and the bumpers leave it for its neighbours. EMPTY DRAWS
    /// NOTHING, so a hand-built state keeps the old four-slot band.
    std::string navPageKeys;
    /// The key that shuts the page: "M" (Action::Map's keyboard half), or
    /// the universal back "B" on a pad, which has no map button of its own.
    std::string navCloseKey = "M";
    /// Whether the tab row prints its digits (`1 - OVERVIEW`). A keyboard's
    /// digits pick a view outright; a pad has none and steps views on the
    /// triggers, so its tab row prints the names alone.
    bool showDigits = true;
    /// The commit verb's key: "ENTER", or "A".
    std::string commitKey = "ENTER";

    // --- FAST TRAVEL (TRAVEL lane): the page's second commit ---------------
    //
    // THE DEFAULTS ARE EMPTY AND EMPTY DRAWS THE OLD PAGE EXACTLY -- the same
    // defaults-are-old-literals rule the nav keys above follow, so every
    // hand-built state (every test written before these fields existed) keeps
    // its four-row detail chrome and draws byte-identical frames. A live
    // Session always fills travelKey, so the live page always holds the row.

    /// The TRAVEL verb's key: "T" on a keyboard (a raw map-page key, Tab/+/-'s
    /// own precedent), "X" on a pad -- the grammar's one second-commit key
    /// (controls.hpp's promptAltCommitKey).
    std::string travelKey;
    /// The verb's cost restatement -- "4 MIN" -- when the walk is honest.
    /// Drawn in the number ink beside the verb, the commit-foot grammar's
    /// "e - Establish (Cost: 200*)".
    std::string travelCost;
    /// The one-line reason travel is refused, in the city register, or empty.
    /// It takes the verb's own row: state changes the verb -- a state label
    /// where the action would be, never a greyed-out key.
    std::string travelRefusal;
};

/// Where every part of the page landed. Exposed because a mouse, a test and the
/// scrolling arithmetic all need the same answer the drawing used, and deriving
/// it twice is how a hit-test drifts out of register with a picture.
struct MapPageLayout {
    PanelMetric metric{};
    PanelRect bounds{};
    PanelRect interior{};
    /// THE ONE HEADER LINE (UI-EA-SPEC sec. 5, breadcrumb law): the tab row --
    /// tabs, resource readout right -- IS the breadcrumb. The old separate
    /// breadcrumb band and the SELECTED line under the plan are gone; their
    /// rows went to the plan, and the selection is named ONCE, on the detail
    /// pane's title row (`TARWALK - NE 12`).
    PanelRect headerBand{};
    PanelRect bodyBand{};
    PanelRect mapPane{};
    PanelRect detailPane{};
    PanelRect navBand{};
    int tabRow = 0;
    int headerRow = 0;
    int bodyRow = 0;
    int bodyRows = 0;
    int navRow = 0;
    int dividerCell = 0;
    std::vector<int> ruleRows;
    MapViewport viewport{};
    /// False when the window is too narrow to hold a detail pane beside the
    /// map: the plan takes the whole body and the selection line under it is
    /// the only detail there is. The honest answer at 320x180, and a caller
    /// composes off this flag rather than guessing a pixel breakpoint.
    bool split = true;
    bool usable = false;
};

[[nodiscard]] MapPageLayout mapPageLayout(int frameWidth, int frameHeight,
                                          const DistrictMapState& state);

/// Which view tab sits under framebuffer pixel (px,py), or -1. The inverse of
/// the tab row drawDistrictMap draws -- same band, same title, same four tabs,
/// same readout, through panel.hpp's tabRowTabAt -- exactly the casebook's own
/// casebookTabAtPixel pattern. FLOW lane, UI-EA-SPEC sec. 4 violation #8: the
/// map's tab row took clicks and dropped them while the casebook's answered.
[[nodiscard]] int mapTabAtPixel(const DistrictMapState& state, int frameWidth, int frameHeight,
                                int px, int py);

/// How many rows the scrolling tabs (People, Index) show at once at this size,
/// and which screenful `state.selected`/`state.detailFirst` puts you on.
struct MapDetailScroll {
    int perScreen = 1;
    int screens = 1;
    int screen = 0;
    int firstRow = 0;
};

[[nodiscard]] MapDetailScroll mapDetailScroll(const DistrictMapState& state, int frameWidth,
                                              int frameHeight);

/// Draws the whole page: the composed frame, the plan with its footprint
/// blocks and their names, the cursor, the player's wedge, the selection line,
/// the tabbed detail pane and the global nav.
void drawDistrictMap(Framebuffer& target, const DistrictMapState& state);

}  // namespace granadad::render
