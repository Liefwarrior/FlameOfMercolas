// CORE ACTION #13 -- THE WARD MAP.
//
// The owner's ask, verbatim: "It's too difficult to locate places like the
// mission, let's give the player a map that they can press M to see." What a
// headless case can prove of that page: the plan's cell classification reads
// the REAL baked Docks honestly (walls where movement collides, floor where
// bodies walk, the harbour as water, the VOID border as nothing), the palette
// is DERIVED from the atlas rather than invented beside it, the label
// declutter is deterministic and never overprints -- and the acceptance
// sentence itself: standing on the authored spawn, the Mission of the Flame's
// name is ON the placed-label list. What a case cannot prove -- legibility --
// is the mandatory screenshot's job.

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

TEST_CASE("label declutter: deterministic, never overprinting, ways deduped -- "
          "and the Mission of the Flame places from the authored spawn") {
    const sim::TileQuery tiles(bakedDocks());
    const MapBounds bounds = mapContentBounds(tiles);
    const MapFrame frame = mapFrameFor(640, 360, bounds);
    const float playerX = static_cast<float>(sim::docks::kSpawnTileX) + 0.5F;
    const float playerY = static_cast<float>(sim::docks::kSpawnTileY) + 0.5F;
    const int textScale = std::max(1, hudMinorScale(360));

    const std::vector<PlacedMapLabel> labels =
        placeMapLabels(frame, playerX, playerY, textScale);
    REQUIRE(!labels.empty());

    // DETERMINISTIC: the same stand produces the identical list, in order.
    const std::vector<PlacedMapLabel> again =
        placeMapLabels(frame, playerX, playerY, textScale);
    REQUIRE(labels.size() == again.size());
    for (std::size_t i = 0; i < labels.size(); ++i) {
        CHECK(labels[i].text == again[i].text);
        CHECK(labels[i].x == again[i].x);
        CHECK(labels[i].y == again[i].y);
        CHECK(labels[i].way == again[i].way);
    }

    // NEVER OVERPRINTING: no two placed glyph boxes intersect. The plates may
    // abut (each carries its own border); the text itself may not collide.
    for (std::size_t a = 0; a < labels.size(); ++a) {
        const int aw = textWidth(labels[a].text, textScale);
        const int ah = 6 * textScale;
        for (std::size_t b = a + 1; b < labels.size(); ++b) {
            const int bw = textWidth(labels[b].text, textScale);
            const int bh = 6 * textScale;
            const bool apart = labels[a].x + aw <= labels[b].x ||
                               labels[b].x + bw <= labels[a].x ||
                               labels[a].y + ah <= labels[b].y ||
                               labels[b].y + bh <= labels[a].y;
            INFO("labels '", labels[a].text, "' and '", labels[b].text, "'");
            CHECK(apart);
        }
    }

    // WAYS DEDUPED: a street signed along its whole reach gets ONE label.
    // Tarwalk has nine authored posts; the plan says the name once.
    const auto count = [&labels](const char* name) {
        int n = 0;
        for (const PlacedMapLabel& label : labels) {
            if (label.text == name) {
                ++n;
            }
        }
        return n;
    };
    CHECK(count("Tarwalk") == 1);
    CHECK(count("Ropewynd") == 1);
    CHECK(count("Saltgate Rise") == 1);

    // THE ACCEPTANCE SENTENCE. The owner's own example of what could not be
    // located is the label this page exists to place: standing on the
    // authored spawn, "Mission of the Flame" is on the map.
    CHECK(count("Mission of the Flame") == 1);

    // And the near doors a spawn-stand reads first are all named too --
    // nearest-first is the tie rule, so the Gull (four seconds away) is
    // never the label that got dropped.
    CHECK(count("The Gilded Gull") == 1);

    // A healthy fraction of the 40 door names lands at this scale. Not all
    // 83 signs fit 640x360 and the rule is to drop rather than overprint --
    // but a map that placed fewer than half its doors would be decluttered
    // into uselessness.
    int doorLabels = 0;
    for (const PlacedMapLabel& label : labels) {
        if (!label.way) {
            ++doorLabels;
        }
    }
    CHECK(doorLabels >= 20);
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
