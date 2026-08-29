// Docks signage: the generated table, the name-resolution priority order,
// and the world-space label pass that draws it.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/signage_renderer.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/docks_signs.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace docks = granadad::sim::docks;

namespace {

SessionConfig docksAt(int hour) {
    SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.world = docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.width = 320;
    config.height = 180;
    return config;
}

/// True when at least one pixel of `frame` is exactly the sign ink colour --
/// full-alpha text, which is what a legible label this close always draws.
bool hasSignInk(const Framebuffer& frame) {
    constexpr Rgb kSignInk{0.93F, 0.87F, 0.68F};
    const std::uint32_t inkPacked = packRgb(kSignInk);
    for (const std::uint32_t pixel : frame.pixels()) {
        if (pixel == inkPacked) {
            return true;
        }
    }
    return false;
}

/// The bounding box of every sign-ink pixel in the frame, or an empty rect.
/// Sign ink is one exact colour (see hasSignInk), so this needs no threshold.
CentreRect signInkBox(const Framebuffer& frame) {
    constexpr Rgb kSignInk{0.93F, 0.87F, 0.68F};
    const std::uint32_t inkPacked = packRgb(kSignInk);
    CentreRect box{frame.width(), frame.height(), 0, 0};
    bool any = false;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            if (frame.pixels()[frame.index(x, y)] != inkPacked) {
                continue;
            }
            any = true;
            box.x0 = std::min(box.x0, x);
            box.y0 = std::min(box.y0, y);
            box.x1 = std::max(box.x1, x + 1);
            box.y1 = std::max(box.y1, y + 1);
        }
    }
    return any ? box : CentreRect{0, 0, 0, 0};
}

int signInkCount(const Framebuffer& frame) {
    constexpr Rgb kSignInk{0.93F, 0.87F, 0.68F};
    const std::uint32_t inkPacked = packRgb(kSignInk);
    int count = 0;
    for (const std::uint32_t pixel : frame.pixels()) {
        count += pixel == inkPacked ? 1 : 0;
    }
    return count;
}

const docks::Sign* findSign(const char* id) {
    for (const docks::Sign& sign : docks::kSigns) {
        if (std::strcmp(sign.id, id) == 0) {
            return &sign;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("the generated sign table is the survey's own count, and every door has a real name") {
    // 83 markers total: the exact number the signage survey's own tmx parse
    // found (Part 2), not a round number somebody guessed.
    CHECK(docks::kSignCount == 83);

    std::size_t doors = 0;
    std::size_t ways = 0;
    for (const docks::Sign& sign : docks::kSigns) {
        // The K-numbering skips k35 on purpose (Part 2's own finding: an
        // unused number, not a missed building) -- so no id should ever
        // reference it.
        CHECK(std::strstr(sign.id, "k35") == nullptr);
        if (sign.kind == docks::SignKind::Door) {
            ++doors;
            // Part 3's finding, made a standing check: of the 40 doors the
            // survey found, ZERO needed a synthesized name -- every one
            // already carries a real `place` string recovered from the tmx.
            CHECK(sign.place != nullptr);
            CHECK(sign.place[0] != '\0');
        } else {
            ++ways;
        }
    }
    CHECK(doors == 40);
    CHECK(ways == 43);
}

TEST_CASE("a building already in docks::kPlaces wins over its own tmx sign") {
    const docks::Sign* gull = findSign("sign_k03_gilded_gull");
    REQUIRE(gull != nullptr);
    // Verifies the survey's own coordinate-transform proof: the generated
    // world rect for this sign is an EXACT match for kPlaces' own entry.
    CHECK(gull->x0 == 146);
    CHECK(gull->y0 == 66);
    CHECK(gull->x1 == 160);
    CHECK(gull->y1 == 79);
    CHECK(gull->band == sim::docks::kBandQuayside);

    const docks::ResolvedSignLabel resolved = docks::resolveSignLabel(*gull);
    CHECK(resolved.tier == docks::SignSourceTier::KnownPlace);
    CHECK(resolved.text == "THE GILDED GULL");
}

TEST_CASE("every one of the 40 door signs resolves to its own authored name, not a street's") {
    // The regression guard the priority-order bug shipped without: checking
    // ONLY k03 and k01 (the two cases below) missed the 8 doors whose
    // footprint centre happened to fall inside one of kPlaces' broad
    // street/area rects and had their real tmx name overwritten by the
    // street name instead. This walks every door the survey found, not a
    // sample of two.
    std::size_t doorsChecked = 0;
    for (const docks::Sign& sign : docks::kSigns) {
        if (sign.kind != docks::SignKind::Door) {
            continue;
        }
        ++doorsChecked;
        const docks::ResolvedSignLabel resolved = docks::resolveSignLabel(sign);
        INFO("door id: ", sign.id);
        if (std::strcmp(sign.id, "sign_k03_gilded_gull") == 0) {
            // The one door where docks::kPlaces genuinely names the same
            // building as the tmx sign -- see the dedicated test case above.
            // The two tables spell it with different casing, so this is the
            // one door NOT expected to echo sign.place verbatim.
            CHECK(resolved.tier == docks::SignSourceTier::KnownPlace);
            CHECK(resolved.text == "THE GILDED GULL");
            continue;
        }
        // Every other door: its own tmx-authored name, never a street's.
        CHECK(resolved.tier == docks::SignSourceTier::TmxRecovered);
        CHECK(resolved.text == std::string_view{sign.place});
    }
    CHECK(doorsChecked == 40);

    // The 8 doors the adversarial verify pass caught (or predicted) being
    // overwritten by a street/area name, named individually so a future
    // regression here fails with the door's own id in the output rather than
    // just a doorsChecked-loop CHECK further up.
    static const char* const kFormerlyWrongDoors[] = {
        "sign_k20_merles",         "sign_k21_watchpost",       "sign_k28_slopchest",
        "sign_k30_kestrel",        "sign_k24_eelpots",         "sign_k31_breggas_promise",
        "sign_k32_deep_keel",      "sign_k33_widows_grief",
    };
    for (const char* id : kFormerlyWrongDoors) {
        const docks::Sign* sign = findSign(id);
        REQUIRE(sign != nullptr);
        const docks::ResolvedSignLabel resolved = docks::resolveSignLabel(*sign);
        CHECK(resolved.tier == docks::SignSourceTier::TmxRecovered);
        CHECK(resolved.text == std::string_view{sign->place});
    }
}

TEST_CASE("a building with no docks::kPlaces entry reads its real tmx-recovered name") {
    const docks::Sign* weighhouse = findSign("sign_k01_weighhouse");
    REQUIRE(weighhouse != nullptr);

    const docks::ResolvedSignLabel resolved = docks::resolveSignLabel(*weighhouse);
    CHECK(resolved.tier == docks::SignSourceTier::TmxRecovered);
    CHECK(resolved.text == "The Weighhouse");
}

TEST_CASE("a sign in view draws its label, and the same sign behind you does not") {
    // The shipped spawn: docks.hpp's own header says the Gull's frontage and
    // its door lamp are in shot from here, which puts sign_k03_gilded_gull's
    // anchor in view too.
    Session session(docksAt(20));
    REQUIRE(session.body().spawnedLegally());

    Framebuffer facing(320, 180);
    session.renderer().renderFrame(facing, session.camera(), RenderSettings{}, {});
    drawSignage(facing, session.camera());
    CHECK(hasSignInk(facing));

    // Turn around: the same sign is now behind the eye and must not draw --
    // the near-cull (SignageSettings::minDistance) is a dot product against
    // forward, so a sign behind the camera projects to a negative distance
    // and is skipped before it ever reaches the screen or the depth test.
    session.body().setYaw(session.body().yaw() + sim::kTurnHalf);
    Framebuffer away(320, 180);
    session.renderer().renderFrame(away, session.camera(), RenderSettings{}, {});
    drawSignage(away, session.camera());
    CHECK_FALSE(hasSignInk(away));
}

TEST_CASE("a sign far past the legibility radius does not draw") {
    SessionConfig config = docksAt(20);
    // Out on the open water-facing quay, 40 tiles from anything named --
    // well past SignageSettings::maxDistance (16 tiles) from every sign in
    // the table.
    config.spawnX = 200;
    config.spawnY = 40;
    config.spawnBand = sim::docks::kBandQuayside;
    config.spawnYaw = sim::kFacingWest;
    config.spawnYawGiven = true;
    Session session(config);
    if (!session.body().spawnedLegally()) {
        return;  // not a legal stand here in every baked revision; not what this case is about
    }

    Framebuffer frame(320, 180);
    session.renderer().renderFrame(frame, session.camera(), RenderSettings{}, {});
    drawSignage(frame, session.camera());
    CHECK_FALSE(hasSignInk(frame));
}

// ---------------------------------------------------------------------------
// THE POLISH PASS: the interface is a box the signs place around
// ---------------------------------------------------------------------------
//
// Signage draws BEFORE the HUD so nothing paints over a legible sign, and the
// consequence was that the HUD painted over one: the crosshair prompt lands at
// the exact centre of the frame, which is where the nearest sign projects. A
// committed capture of the Tarwalk had "THE BILGE" cut off mid-word by the
// prompt's own "HEMP CAT" row -- the prompt won by being second.
//
// The fix is not a new mechanism. This file already skipped a label whose box
// collided with one already placed; it simply could not see the HUD. Seeding
// that rule with the prompt's own rectangle makes the interface one more box.

TEST_CASE("a label whose box lands on the interface is skipped, not painted over") {
    Session session(docksAt(20));
    REQUIRE(session.body().spawnedLegally());

    Framebuffer plain(320, 180);
    session.renderer().renderFrame(plain, session.camera(), RenderSettings{}, {});
    drawSignage(plain, session.camera());
    REQUIRE(hasSignInk(plain));
    const CentreRect where = signInkBox(plain);
    const int before = signInkCount(plain);
    REQUIRE(where.x1 > where.x0);

    // Claim the exact ground the signs landed on. Every one of their boxes now
    // collides with something already placed, so the whole pass stands down --
    // which is what the existing nearest-first rule has always done and is why
    // no second rule was written for this.
    SignageSettings settings;
    settings.exclusion = where;
    Framebuffer claimed(320, 180);
    session.renderer().renderFrame(claimed, session.camera(), RenderSettings{}, {});
    drawSignage(claimed, session.camera(), settings);
    CHECK(signInkCount(claimed) < before);
    CHECK_FALSE(hasSignInk(claimed));

    // AND AN EMPTY EXCLUSION CHANGES NOTHING AT ALL. A frame with nothing under
    // the reticle hands back an empty rect, and a caller that never sets one
    // gets the identical field of labels this pass drew before it existed --
    // asserted pixel for pixel rather than assumed.
    SignageSettings none;
    Framebuffer same(320, 180);
    session.renderer().renderFrame(same, session.camera(), RenderSettings{}, {});
    drawSignage(same, session.camera(), none);
    CHECK(same.pixels() == plain.pixels());
}

TEST_CASE("the prompt's own footprint is a fraction of the clamp fence it sits in") {
    // hudAimRect is a CLAMP: it spans the whole right half of the centre band,
    // because a proper noun off the sign table has no length this build
    // controls. Excluding all of THAT would cost signs the prompt was never
    // near -- the Tarwalk's own street post sits inside the fence and nowhere
    // near the two rows. So the exclusion is the rows, and this pins the
    // difference rather than trusting it.
    HudState aim;
    aim.aimKey = "E";
    aim.aimVerb = "TALK";
    aim.aimSubject = "HEMP CAT";
    for (const auto& size : {std::pair<int, int>{320, 180}, std::pair<int, int>{640, 360},
                            std::pair<int, int>{1280, 720}, std::pair<int, int>{1920, 1080}}) {
        const int w = size.first;
        const int h = size.second;
        CAPTURE(w);
        CAPTURE(h);
        const CentreRect fence = hudAimRect(w, h);
        const CentreRect prompt = hudAimPromptRect(aim, w, h);
        REQUIRE(prompt.x1 > prompt.x0);
        REQUIRE(prompt.y1 > prompt.y0);
        // Inside the fence, on every edge.
        CHECK(prompt.x0 >= fence.x0);
        CHECK(prompt.y0 >= fence.y0);
        CHECK(prompt.x1 <= fence.x1);
        CHECK(prompt.y1 <= fence.y1);
        // And genuinely smaller -- the whole reason this function exists.
        const long promptArea = static_cast<long>(prompt.x1 - prompt.x0) * (prompt.y1 - prompt.y0);
        const long fenceArea = static_cast<long>(fence.x1 - fence.x0) * (fence.y1 - fence.y0);
        CHECK(promptArea * 2 < fenceArea);
    }

    // NOTHING UNDER THE RETICLE IS AN EMPTY RECT, not a zero-width box at the
    // centre of the screen that a collision test would still trip on.
    HudState quiet;
    const CentreRect none = hudAimPromptRect(quiet, 640, 360);
    CHECK(none.x0 == 0);
    CHECK(none.y0 == 0);
    CHECK(none.x1 == 0);
    CHECK(none.y1 == 0);
}
