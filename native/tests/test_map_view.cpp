// CORE ACTION #13 -- THE WARD MAP.
//
// The owner's ask, verbatim: "It's too difficult to locate places like the
// mission, let's give the player a map that they can press M to see." What a
// headless case can prove of that page: the plan's cell classification reads
// the REAL baked Docks honestly (walls where movement collides, floor where
// bodies walk, the harbour as water, the VOID border as nothing), and the
// palette is DERIVED from the atlas rather than invented beside it.
//
// THE MAP PASS rewrote everything after that, because the owner played the
// build and said: "Names are stacking up on the map view. Makes it hard to
// figure out where the place you're looking for is." The floating-nameplate
// declutter solver those cases used to pin is GONE, and what replaced it is
// what they pin now: ONE label rule that sets a name INSIDE the shape that owns
// it or answers nothing at all, a deduped place table with the mapper's own
// footprints in it, a viewport that fits the whole ward and then follows the
// cursor as it zooms, a cursor that steps PLACES rather than tiles, and a
// People view that answers who is standing in the selection right now.
//
// What a case cannot prove -- legibility -- is the mandatory screenshot's job.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/map_view.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/docks_signs_generated.hpp"
#include "granadad/sim/stealth.hpp"
#include "granadad/sim/tile_query.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

namespace {

/// The one world every case here reads. Loaded once; TileQuery borrows it.
const content::World& bakedDocks() {
    static content::World world =
        content::loadWorldFile(content::bakedMap(sim::docks::kWorldName));
    return world;
}

}  // namespace

TEST_CASE("the plan's cells read the baked Docks the way movement does") {
    const sim::TileQuery tiles(bakedDocks());

    // The authored spawn is a walked street: FLOOR at the player's own band.
    CHECK(mapCellAt(tiles, sim::docks::kSpawnTileX, sim::docks::kSpawnTileY,
                    sim::docks::kBandQuayside) == MapCell::Floor);

    // The VOID border ring is NOTHING, never a wall -- solid() answers true
    // for VOID and a classifier that used it would draw a rampart around the
    // district. (5,5) is deep inside the one-chunk border.
    CHECK(mapCellAt(tiles, 5, 5, sim::docks::kBandQuayside) == MapCell::Void);

    // The harbour reads as WATER from the quayside band -- the surface sits
    // at the top of z18, one level below the deck -- and from the roof-slum
    // plane too, which is what "the harbour water is always shown" means.
    // Counted rather than pinned to one coordinate, because the harbour is
    // thousands of authored cells and any one of them could gain a pier.
    const MapBounds bounds = mapContentBounds(tiles);
    int waterFromQuay = 0;
    int waterFromRoofs = 0;
    int wallsAtQuay = 0;
    for (std::int32_t y = bounds.minY; y <= bounds.maxY; ++y) {
        for (std::int32_t x = bounds.minX; x <= bounds.maxX; ++x) {
            const MapCell atQuay = mapCellAt(tiles, x, y, sim::docks::kBandQuayside);
            if (atQuay == MapCell::Water) {
                ++waterFromQuay;
            }
            if (atQuay == MapCell::Wall) {
                ++wallsAtQuay;
                // A wall on the plan is a wall a body collides with.
                CHECK(tiles.solid(x, y, sim::docks::kBandQuayside));
            }
            if (mapCellAt(tiles, x, y, sim::docks::kBandRoofs) == MapCell::Water) {
                ++waterFromRoofs;
            }
        }
    }
    CHECK(waterFromQuay > 2000);
    CHECK(waterFromRoofs > 2000);
    CHECK(wallsAtQuay > 500);

    // And the bounds themselves crop the border: the survey's own sign table
    // runs x 32..223, y 32..159, and the plan's extent has to hold all of it.
    CHECK(bounds.minX >= 30);
    CHECK(bounds.minY >= 30);
    CHECK(bounds.maxX <= 225);
    CHECK(bounds.maxY <= 161);
    for (std::size_t i = 0; i < sim::docks::kSignCount; ++i) {
        const sim::docks::Sign& sign = sim::docks::kSigns[i];
        CHECK(sign.anchorX >= static_cast<float>(bounds.minX));
        CHECK(sign.anchorX <= static_cast<float>(bounds.maxX + 1));
        CHECK(sign.anchorY >= static_cast<float>(bounds.minY));
        CHECK(sign.anchorY <= static_cast<float>(bounds.maxY + 1));
    }
}

TEST_CASE("the plan's palette is derived from the atlas, walls low and floors lifted") {
    // The procedural atlas, so this case owes nothing to the art tree -- the
    // derivation rule is the claim, not any one pack's colours.
    const TileAtlas atlas = TileAtlas::procedural();
    const MapPalette palette = MapPalette::fromAtlas(atlas);
    const std::size_t materials = materialIds().size();
    REQUIRE(palette.wall.size() == materials);
    REQUIRE(palette.floor.size() == materials);

    const auto luma = [](const Rgb& c) { return 0.299F * c.r + 0.587F * c.g + 0.114F * c.b; };
    for (std::size_t m = 0; m < materials; ++m) {
        INFO("material ", m);
        // DERIVED: the wall tone is the material's own side-face average,
        // scaled -- not a constant somebody typed beside the atlas.
        const Rgb side = atlas.averageOf(atlas.tileFor(static_cast<std::uint16_t>(m),
                                                       FaceKind::Side, 0));
        CHECK(std::abs(palette.wall[m].r - side.r * 0.52F) < 0.001F);
        CHECK(std::abs(palette.wall[m].g - side.g * 0.52F) < 0.001F);
        CHECK(std::abs(palette.wall[m].b - side.b * 0.52F) < 0.001F);
        // And the register holds: walkable ground reads lighter than masonry
        // of the same material, which is the whole legibility rule.
        CHECK(luma(palette.floor[m]) > luma(palette.wall[m]));
    }
    // Water is its own tone, not any material's wall or floor.
    CHECK(luma(palette.water) > 0.0F);
    CHECK(palette.water.b > palette.water.r);
}

TEST_CASE("the one label rule: shout, drop the article, wrap into the shape, "
          "or answer nothing at all") {
    // THE MAP PASS. The old page carried a twenty-four-seat label-AVOIDANCE
    // solver that hung floating plates around a small map -- plates, clearance
    // rings, corner-leaning variants, displacement rows, and a drop rule for
    // anything that still could not find a hole. This one function replaced all
    // of it, and it is applied identically to a sixty-four-tile rope shed and a
    // seven-tile pawnshop.
    const int scale = 1;
    const int glyph = 5 * scale;
    const int rowPx = 7 * scale;

    // SHOUTED, and the leading article DROPPED -- four glyphs that name nothing
    // on a map face. It is the ONLY word the rule ever removes.
    const std::vector<std::string> gull =
        fitFootprintLabel("The Gilded Gull", 12 * glyph, 4 * rowPx, scale);
    REQUIRE(gull.size() == 1);
    CHECK(gull[0] == "GILDED GULL");

    // WRAPPED ON WORD BOUNDARIES when the shape is narrow and tall.
    const std::vector<std::string> mission =
        fitFootprintLabel("Mission of the Flame", 8 * glyph, 4 * rowPx, scale);
    REQUIRE(mission.size() >= 2);
    for (const std::string& line : mission) {
        CHECK(static_cast<int>(line.size()) <= 8);
    }
    std::string rejoined;
    for (const std::string& line : mission) {
        if (!rejoined.empty()) {
            rejoined += ' ';
        }
        rejoined += line;
    }
    // NEVER A WORD CUT IN HALF: the wrap is a rearrangement of the whole name,
    // never a truncation of it. "THE SLOP-CH" names nothing.
    CHECK(rejoined == "MISSION OF THE FLAME");

    // THE FALLBACK, AND IT IS DELIBERATE: a shape that cannot hold the name at
    // any wrapping answers NOTHING, and the page draws no floating plate for
    // it. A word wider than the shape refuses the whole name...
    CHECK(fitFootprintLabel("Wrackhouse", 6 * glyph, 40 * rowPx, scale).empty());
    // ...and so does a shape too short for the rows the wrap needs.
    CHECK(fitFootprintLabel("Cooper and Blockmaker", 7 * glyph, 1 * rowPx, scale).empty());
    // The Drowned-Name Wall is a 3x3 shrine and is the hardest case in the
    // district: at the whole-ward scale it gets a door dot and the cursor.
    CHECK(fitFootprintLabel("The Drowned-Name Wall", 3 * glyph, 3 * rowPx, scale).empty());
}

TEST_CASE("the named places: deduped by name, largest footprint wins, alphabetical") {
    const std::vector<MapPlace>& places = mapPlaces();
    REQUIRE(!places.empty());

    // DEDUPED. Tarwalk is signed nine times along its reach and Saltgate Rise
    // six; the plan says each name once. This is the one rule the old declutter
    // pass had that was worth keeping.
    const auto count = [&places](const char* name) {
        int n = 0;
        for (const MapPlace& place : places) {
            if (place.name == name) {
                ++n;
            }
        }
        return n;
    };
    CHECK(count("Tarwalk") == 1);
    CHECK(count("Ropewynd") == 1);
    CHECK(count("Saltgate Rise") == 1);
    CHECK(count("Flame ground") == 1);

    // ALPHABETICAL, so the Index reads as an index and a captured frame is
    // reproducible.
    for (std::size_t i = 1; i < places.size(); ++i) {
        CHECK(places[i - 1].name <= places[i].name);
    }

    // THE ACCEPTANCE SENTENCE. The place the owner could not find is a place
    // with a SHAPE, and the shape is the mapper's own -- which is what this
    // whole pass turns on and what the old page never once read.
    const int mission = mapPlaceIndex("Mission of the Flame");
    REQUIRE(mission >= 0);
    CHECK(places[static_cast<std::size_t>(mission)].tilesX() == 17);
    CHECK(places[static_cast<std::size_t>(mission)].tilesY() == 15);
    CHECK_FALSE(places[static_cast<std::size_t>(mission)].way);

    // LARGEST SEGMENT WINS for a deduped street -- the label has to fit INSIDE
    // the shape now, so the segment with the most room in it carries the name.
    // (The old page picked the segment nearest the cluster centroid, which was
    // right when the label floated and is wrong now that it does not.)
    const int rise = mapPlaceIndex("Saltgate Rise");
    REQUIRE(rise >= 0);
    std::int64_t widest = 0;
    for (std::size_t i = 0; i < sim::docks::kSignCount; ++i) {
        const sim::docks::Sign& sign = sim::docks::kSigns[i];
        if (std::string(sign.place) != "Saltgate Rise") {
            continue;
        }
        widest = std::max<std::int64_t>(
            widest, static_cast<std::int64_t>(sign.x1 - sign.x0 + 1) *
                        static_cast<std::int64_t>(sign.y1 - sign.y0 + 1));
    }
    CHECK(places[static_cast<std::size_t>(rise)].area() == widest);

    // ...AND EVERY OTHER SEGMENT IS KEPT rather than thrown away, so the cursor
    // can light the whole street and the page can answer "is the body on the
    // Tarwalk" about all nine blocks of it. Only the NAME is deduped.
    const int tarwalk = mapPlaceIndex("Tarwalk");
    REQUIRE(tarwalk >= 0);
    const MapPlace& street = places[static_cast<std::size_t>(tarwalk)];
    CHECK(street.segments.size() == 9);
    // The authored spawn stands on the Tarwalk, in a block that is NOT the one
    // the label went to -- which is the exact case that made the first capture
    // draw its cursor fifty tiles from the body.
    CHECK(street.contains(156, 63));
    const bool inTheNamedBlock = MapRect{street.x0, street.y0, street.x1, street.y1}
                                     .contains(156, 63);
    CHECK_FALSE(inTheNamedBlock);
    // A door has exactly one segment and nothing changes for it.
    CHECK(places[static_cast<std::size_t>(mission)].segments.size() == 1);

    // NESTED FOOTPRINTS RESOLVE INWARD. The Netter house sits inside The
    // Netters' Compound, and a finger on that ground means the house.
    const int house = mapPlaceIndex("The Netter house");
    REQUIRE(house >= 0);
    const MapPlace& inner = places[static_cast<std::size_t>(house)];
    CHECK(mapPlaceUnder((inner.x0 + inner.x1) / 2, (inner.y0 + inner.y1) / 2) == house);

    // AND A DOOR KNOWS ITS STREET. "Go to the Gilded Gull" means "walk the
    // Tarwalk", and the authored way footprints already knew it -- so the
    // Overview says it instead of leaving the reader to work it out off the
    // picture.
    //
    // THIS IS WHY mapWayUnder WALKS THE RAW SIGN TABLE. The gate caught the
    // first version of it, which asked mapPlaces(): that table keeps ONE
    // segment per street name (the roomiest, because a label has to fit inside
    // the shape), and the Tarwalk it keeps is the worn eastern stretch. The
    // Gull's own door stands on the Tarwalk in a segment the deduped table does
    // not hold, so the deduped answer was -1 for a door that is plainly on the
    // street. A street's identity is its NAME and every segment carries it.
    const int gull = mapPlaceIndex("The Gilded Gull");
    REQUIRE(gull >= 0);
    const MapPlace& tavern = places[static_cast<std::size_t>(gull)];
    const int onWay = mapWayUnder(static_cast<std::int32_t>(tavern.anchorX),
                                  static_cast<std::int32_t>(tavern.anchorY), 6);
    REQUIRE(onWay >= 0);
    CHECK(places[static_cast<std::size_t>(onWay)].way);
    CHECK(places[static_cast<std::size_t>(onWay)].name == "Tarwalk");
    // Out in the harbour there is no signed way at all, and the answer is -1
    // rather than the nearest street half the district away.
    CHECK(mapWayUnder(60, 40, 6) == -1);
}

TEST_CASE("names go INSIDE their shapes, and enough of them do to be worth having") {
    // The number the scale ruling in map_view.hpp's header rests on, pinned: at
    // the whole-ward fit scale of the 960x540 composition, a majority of the
    // forty authored doors name themselves inside their own footprint. If this
    // ever drops, the page has quietly gone back to being unreadable and the
    // ruling that justified defaulting to the whole ward stops holding.
    DistrictMapState state;
    state.bounds = mapContentBounds(sim::TileQuery(bakedDocks()));
    const MapPageLayout layout = mapPageLayout(960, 540, state);
    REQUIRE(layout.usable);
    REQUIRE(layout.split);
    const int labelScale = mapLabelScale(540);

    int named = 0;
    int doors = 0;
    for (const MapPlace& place : mapPlaces()) {
        if (place.way) {
            continue;
        }
        ++doors;
        if (!fitFootprintLabel(place.name, place.tilesX() * layout.viewport.scale,
                               place.tilesY() * layout.viewport.scale, labelScale)
                 .empty()) {
            ++named;
        }
    }
    CHECK(doors == 40);
    CHECK(named >= 20);

    // AND ZOOMING IN NAMES MORE OF THEM, which is the whole reason the ladder
    // exists -- his "buildings are large and use more screen" delivered on
    // demand rather than at the cost of orientation.
    DistrictMapState zoomed = state;
    zoomed.zoom = mapZoomSteps() - 1;
    const MapPageLayout far = mapPageLayout(960, 540, zoomed);
    int namedFar = 0;
    for (const MapPlace& place : mapPlaces()) {
        if (place.way) {
            continue;
        }
        if (!fitFootprintLabel(place.name, place.tilesX() * far.viewport.scale,
                               place.tilesY() * far.viewport.scale, labelScale)
                 .empty()) {
            ++namedFar;
        }
    }
    CHECK(far.viewport.scale > layout.viewport.scale);
    CHECK(namedFar > named);
}

TEST_CASE("the viewport: fits the ward at step 0, follows the cursor beyond it") {
    DistrictMapState state;
    state.bounds = mapContentBounds(sim::TileQuery(bakedDocks()));
    const MapBounds bounds = state.bounds;

    const MapPageLayout fit = mapPageLayout(960, 540, state);
    REQUIRE(fit.usable);
    // STEP 0 IS THE WHOLE WARD: every corner of the district is inside the pane.
    CHECK(fit.viewport.pxOfX(static_cast<float>(bounds.minX)) >= fit.viewport.pane.x);
    CHECK(fit.viewport.pxOfY(static_cast<float>(bounds.minY)) >= fit.viewport.pane.y);
    CHECK(fit.viewport.pxOfX(static_cast<float>(bounds.maxX) + 1.0F) <=
          fit.viewport.pane.right());
    CHECK(fit.viewport.pxOfY(static_cast<float>(bounds.maxY) + 1.0F) <=
          fit.viewport.pane.bottom());

    // THE VIEW FOLLOWS THE CURSOR. Zoomed in, the selected place is on screen
    // wherever it stands in the ward -- there is no pan state that can disagree
    // with the selection, because there is no pan state at all.
    for (int step = 1; step < mapZoomSteps(); ++step) {
        for (const char* name : {"The Weighhouse", "The Gullet Compound", "Pitchfield",
                                 "Saltgate Watch-Post"}) {
            DistrictMapState at = state;
            at.zoom = step;
            at.selected = mapPlaceIndex(name);
            REQUIRE(at.selected >= 0);
            const MapPageLayout layout = mapPageLayout(960, 540, at);
            const MapPlace& place = mapPlaces()[static_cast<std::size_t>(at.selected)];
            const int cx = layout.viewport.pxOfX(
                (static_cast<float>(place.x0) + static_cast<float>(place.x1) + 1.0F) * 0.5F);
            const int cy = layout.viewport.pxOfY(
                (static_cast<float>(place.y0) + static_cast<float>(place.y1) + 1.0F) * 0.5F);
            INFO(name, " at zoom ", step);
            CHECK(cx >= layout.viewport.pane.x);
            CHECK(cx < layout.viewport.pane.right());
            CHECK(cy >= layout.viewport.pane.y);
            CHECK(cy < layout.viewport.pane.bottom());
        }
    }

    // THE MOUSE IS THE INVERSE OF WHAT WAS DRAWN rather than a second
    // description of it: a pixel inside a footprint hit-tests to that
    // footprint, and a pixel off the pane hits nothing at all.
    const int gull = mapPlaceIndex("The Gilded Gull");
    REQUIRE(gull >= 0);
    const MapPlace& place = mapPlaces()[static_cast<std::size_t>(gull)];
    const int px = fit.viewport.pxOfX(static_cast<float>(place.x0) + 0.5F);
    const int py = fit.viewport.pxOfY(static_cast<float>(place.y0) + 0.5F);
    CHECK(mapPlaceAtPixel(fit.viewport, px, py) == gull);
    CHECK(mapPlaceAtPixel(fit.viewport, fit.viewport.pane.x - 4, py) == -1);
}

TEST_CASE("the cursor walks places, and a handful of presses crosses the ward") {
    const std::vector<MapPlace>& places = mapPlaces();
    const auto centreX = [&places](int i) {
        return (places[static_cast<std::size_t>(i)].x0 + places[static_cast<std::size_t>(i)].x1) /
               2;
    };

    const int gull = mapPlaceIndex("The Gilded Gull");
    REQUIRE(gull >= 0);
    // A step in a direction lands on something ELSE, and on the same thing
    // every run -- the cursor cannot drift between captures.
    const int west = mapPlaceToward(gull, MapStep::West);
    CHECK(west != gull);
    CHECK(mapPlaceToward(gull, MapStep::West) == west);
    // And it really is westward, which is the only thing the arrow promised.
    CHECK(centreX(west) < centreX(gull));

    // THE WARD IS CROSSABLE. Walking west from its eastern edge reaches the
    // western edge in a handful of presses, which is the point of a cursor that
    // steps PLACES rather than tiles: a tile cursor would want a hundred and
    // ninety of them.
    int at = mapPlaceIndex("The Gullet Compound");
    REQUIRE(at >= 0);
    int presses = 0;
    while (presses < 40) {
        const int next = mapPlaceToward(at, MapStep::West);
        if (next == at) {
            break;
        }
        at = next;
        ++presses;
    }
    CHECK(centreX(at) < 60);
    CHECK(presses < 25);
}

TEST_CASE("the page's own cursor: opens where you stand, tabs cycle, zoom clamps") {
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 960;
    config.height = 540;
    Session session(config);

    session.toggleDistrictMap();
    REQUIRE(session.districtMapOpen());
    // OPENS WHERE YOU STAND. The first thing the page says is true of the
    // ground under the body's feet, so the plan is already panned to the
    // player's own quarter -- "the map opened somewhere else" is the exact
    // disorientation this pass exists to remove.
    const int here = mapPlaceUnder(session.body().tileX(), session.body().tileY());
    if (here >= 0) {
        CHECK(session.districtMapSelected() == here);
    } else {
        CHECK(session.districtMapSelected() >= 0);
    }

    // The tabs cycle and wrap, and the printed digits select directly.
    CHECK(session.districtMapTab() == MapTab::Overview);
    session.cycleDistrictMapTab(1);
    CHECK(session.districtMapTab() == MapTab::People);
    session.cycleDistrictMapTab(-1);
    CHECK(session.districtMapTab() == MapTab::Overview);
    session.cycleDistrictMapTab(-1);
    CHECK(session.districtMapTab() == MapTab::Legend);
    session.setDistrictMapTab(2);
    CHECK(session.districtMapTab() == MapTab::Index);

    // The zoom ladder CLAMPS at both ends rather than wrapping -- a map that
    // jumped from the closest rung back to the whole ward on one more press
    // would lose the reader's place.
    CHECK(session.districtMapZoom() == 0);
    session.adjustDistrictMapZoom(-1);
    CHECK(session.districtMapZoom() == 0);
    for (int i = 0; i < 12; ++i) {
        session.adjustDistrictMapZoom(1);
    }
    CHECK(session.districtMapZoom() == mapZoomSteps() - 1);

    // Selecting by name is what an Index row and the capture flag both call,
    // and a name matching nothing changes nothing.
    CHECK(session.selectDistrictMapPlace("The Weighhouse"));
    CHECK(session.districtMapSelected() == mapPlaceIndex("The Weighhouse"));
    CHECK_FALSE(session.selectDistrictMapPlace("The House That Is Not There"));
    CHECK(session.districtMapSelected() == mapPlaceIndex("The Weighhouse"));

    // THE COMMIT VERB. ENTER turns the body to face the selection and puts the
    // page away -- the honest first half of "going to a place", since you
    // cannot walk somewhere you cannot face.
    const MapPlace& want =
        mapPlaces()[static_cast<std::size_t>(mapPlaceIndex("The Weighhouse"))];
    session.faceDistrictMapSelection();
    CHECK_FALSE(session.districtMapOpen());
    std::int32_t aimX = 0;
    std::int32_t aimY = 0;
    mapAimPoint(want, session.body().tileX(), session.body().tileY(), aimX, aimY);
    CHECK(session.body().yaw() ==
          sim::bearingTo(session.body().tileX(), session.body().tileY(), aimX, aimY));
    // A door has ONE signed point and the aim IS that point -- the door you
    // knock on, which is also the dot the plan draws.
    CHECK(aimX == static_cast<std::int32_t>(want.anchorX));
    CHECK(aimY == static_cast<std::int32_t>(want.anchorY));

    // A STREET HAS NO SINGLE POINT, and the aim is the nearest bit of it. The
    // Tarwalk is signed at its eastern end and runs the whole width of the
    // ward; standing on it outside the Gilded Gull, the nearest bit of it is
    // underfoot, not fifty tiles east where the post stands.
    const MapPlace& street = mapPlaces()[static_cast<std::size_t>(mapPlaceIndex("Tarwalk"))];
    mapAimPoint(street, 156, 63, aimX, aimY);
    CHECK(aimX == 156);
    CHECK(aimY == 63);
    CHECK(aimX != static_cast<std::int32_t>(street.anchorX));
}

TEST_CASE("the People view answers 'who is in there', off the live roster") {
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 960;
    config.height = 540;
    // Mid-morning, when the ward is at work and the roster is in its places.
    config.timeOfDay = 10 * 3600;
    config.timeOfDayGiven = true;
    Session session(config);
    session.stepMany(sim::MoveInput{}, 8);
    session.toggleDistrictMap();
    REQUIRE(session.districtMapOpen());

    int withPeople = 0;
    for (const char* name : {"The Gilded Gull", "The Weighhouse", "Mission of the Flame",
                             "The Rows", "The Ropewalk", "Pitchfield", "The Long Store"}) {
        REQUIRE(session.selectDistrictMapPlace(name));
        const DistrictMapState state = session.districtMapState();
        const MapPlace& place =
            mapPlaces()[static_cast<std::size_t>(session.districtMapSelected())];
        // Whoever the page reports as being inside the selection really is
        // inside it, by the same rectangle the plan drew.
        for (const MapPersonRow& row : state.people) {
            INFO(name, " reports ", row.name);
            CHECK(place.contains(row.x, row.y));
            CHECK_FALSE(row.name.empty());
            CHECK_FALSE(row.what.empty());
        }
        if (!state.people.empty()) {
            ++withPeople;
        }
        // NAME ORDER, so a page turn is stable and a capture reproducible. The
        // roster's own index order is BAKE order, which means a body walking
        // out of a room would reshuffle every row under it.
        for (std::size_t i = 1; i < state.people.size(); ++i) {
            CHECK(state.people[i - 1].name <= state.people[i].name);
        }
    }
    // The ward has six hundred people in it at ten in the morning. If NONE of
    // seven of its busiest addresses had anybody in them, the view would be
    // answering the wrong question.
    CHECK(withPeople >= 1);
}

TEST_CASE("the ward map page: toggles, exclusivity, ESC, and the frame it draws") {
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 320;
    config.height = 180;
    Session session(config);

    // M's own call: opens, and the same call closes.
    session.toggleDistrictMap();
    CHECK(session.districtMapOpen());
    CHECK(session.menuOpen());  // the movement keys stand down under it
    session.toggleDistrictMap();
    CHECK_FALSE(session.districtMapOpen());

    // Every other overlay stands down when it opens, and it stands down for
    // them -- the one-page-owns-the-keyboard rule every overlay keeps.
    session.togglePause();
    REQUIRE(session.pauseOpen());
    session.toggleDistrictMap();
    CHECK(session.districtMapOpen());
    CHECK_FALSE(session.pauseOpen());
    session.toggleCasebook();
    CHECK_FALSE(session.districtMapOpen());
    CHECK(session.casebookOpen());
    session.toggleDistrictMap();
    CHECK(session.districtMapOpen());
    CHECK_FALSE(session.casebookOpen());

    // ESC's own path closes it.
    session.closeConversation();
    CHECK_FALSE(session.districtMapOpen());

    // AND THE PAGE ACTUALLY DRAWS. The same scene with and without the map
    // differs -- the plan, the labels and the wedge are real pixels -- and
    // drawing it mutates nothing: the body stands exactly where it stood.
    Framebuffer plain(config.width, config.height);
    (void)session.drawFrame(plain);
    session.toggleDistrictMap();
    // A few steps so the page's own EasedToggle is genuinely open, the same
    // settle a screenshot capture runs.
    session.stepMany(sim::MoveInput{}, 16);
    const std::int32_t bodyX = session.body().x();
    const std::int32_t bodyY = session.body().y();
    Framebuffer mapped(config.width, config.height);
    (void)session.drawFrame(mapped);
    CHECK(session.body().x() == bodyX);
    CHECK(session.body().y() == bodyY);
    std::size_t differing = 0;
    for (std::size_t i = 0; i < plain.pixels().size(); ++i) {
        if (plain.pixels()[i] != mapped.pixels()[i]) {
            ++differing;
        }
    }
    // Most of the frame: the page dims the world and draws the plan over it.
    CHECK(differing > plain.pixels().size() / 2);
}

TEST_CASE("FAST TRAVEL's page fields default empty and gate the foot they add") {
    // THE DEFAULTS-ARE-OLD-LITERALS RULE, pinned. The TRAVEL verb added
    // travelKey/travelCost/travelRefusal to DistrictMapState; a live Session
    // always fills travelKey, but every hand-built state -- the capture
    // harness's, test_map_view's own, the ones written before the field
    // existed -- leaves it empty, and an empty travelKey must draw the exact
    // four-row detail foot the page has always drawn. Proven by taking a
    // wired live state, CLEARING its travel fields (which is the historical
    // page), and showing the cleared frame is stable and DIFFERS from the
    // filled one -- so the field is the thing that changes the picture, and a
    // committed frame moves only where a Session fills it.
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 640;
    config.height = 360;
    config.timeOfDay = 20 * 3600;
    config.timeOfDayGiven = true;
    Session session(config);
    session.stepMany(sim::MoveInput{}, 1);
    session.toggleDistrictMap();
    REQUIRE(session.selectDistrictMapPlace("The Weighhouse"));
    const DistrictMapState live = session.districtMapState();
    REQUIRE_FALSE(live.travelKey.empty());  // a live Session offers the verb

    // The historical page: the same state with the travel fields at their
    // empty defaults.
    DistrictMapState old = live;
    old.travelKey.clear();
    old.travelCost.clear();
    old.travelRefusal.clear();

    Framebuffer oldFoot(config.width, config.height);
    drawDistrictMap(oldFoot, old);
    Framebuffer again(config.width, config.height);
    drawDistrictMap(again, old);
    std::size_t drift = 0;
    for (std::size_t i = 0; i < oldFoot.pixels().size(); ++i) {
        if (oldFoot.pixels()[i] != again.pixels()[i]) {
            ++drift;
        }
    }
    CHECK(drift == 0);  // the empty-defaulted foot is deterministic

    // The live page draws the verb, so the frame is no longer identical -- the
    // field is what changed it, not an accident of the composition.
    Framebuffer verbFoot(config.width, config.height);
    drawDistrictMap(verbFoot, live);
    std::size_t moved = 0;
    for (std::size_t i = 0; i < oldFoot.pixels().size(); ++i) {
        if (oldFoot.pixels()[i] != verbFoot.pixels()[i]) {
            ++moved;
        }
    }
    CHECK(moved > 0);
}

TEST_CASE("--map-overlay's own beat: runSmoke opens the ward map for the shutter") {
    SmokeRunConfig config;
    config.session.contentDir = content::contentDir();
    config.session.width = 320;
    config.session.height = 180;
    config.steps = 0;
    config.walk = false;
    config.mapOverlay = true;
    const SmokeRunResult result = runSmoke(config);
    CHECK(result.ok);
    CHECK(result.scriptedWanted == 1);
    CHECK(result.scriptedLanded == 1);
}

TEST_CASE("violation #8: the map's tab row answers a pixel the way it was drawn") {
    // FLOW lane, UI-EA-SPEC sec. 4. mapTabAtPixel is the inverse of the tab
    // row drawDistrictMap prints -- same band, same four tabs, same readout
    // -- through panel.hpp's tabRowTabAt, the casebook's own proven pattern.
    // Scan the row's own scanline: all four tabs must be findable, in order,
    // and a pixel in the map pane must answer -1 rather than a tab.
    DistrictMapState state;
    state.bounds = mapContentBounds(sim::TileQuery(bakedDocks()));
    state.readout = "08:00   BAND 1";
    const MapPageLayout layout = mapPageLayout(960, 540, state);
    REQUIRE(layout.usable);

    const int rowY = layout.interior.y + layout.metric.heightOf(layout.tabRow) +
                     layout.metric.cellH() / 2;
    std::vector<int> seen;
    for (int px = layout.interior.x; px < layout.interior.x + layout.interior.w; ++px) {
        const int tab = mapTabAtPixel(state, 960, 540, px, rowY);
        if (tab >= 0 && (seen.empty() || seen.back() != tab)) {
            seen.push_back(tab);
        }
    }
    CHECK(seen == std::vector<int>{0, 1, 2, 3});

    // Off the row: the middle of the map pane is nobody's tab.
    const int paneX = layout.mapPane.x + layout.mapPane.w / 2;
    const int paneY = layout.mapPane.y + layout.mapPane.h / 2;
    CHECK(mapTabAtPixel(state, 960, 540, paneX, paneY) == -1);

    // And an unusably small frame answers -1 rather than reading garbage.
    CHECK(mapTabAtPixel(state, 8, 8, 4, 4) == -1);
}
