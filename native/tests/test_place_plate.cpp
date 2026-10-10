// DISTRICT PHASE D: THE THRESHOLD MOMENT.
//
// Crossing into a named place is an EVENT, and until this pass the only thing
// in the game that noticed was four small dim words under the compass ribbon
// quietly changing. The plate says it once, at the instant it becomes true,
// and then gets out of the way.
//
// WHAT THIS FILE IS FOR. The plate is up for two seconds after a crossing and
// for no other reason -- there is no key that opens it and no page it lives
// on -- so almost every way it can be wrong is a way that leaves NO trace in a
// screenshot: it fires on a spawn, it re-fires at a seam between two rects of
// the same road, it re-arms while the player stands still, it survives a menu
// opening on top of it, or it never fires at all. Every one of those is a case
// here, asserted against the real baked Docks and the real authored kPlaces
// table rather than against a fixture.
//
// AND THE GEOMETRY IS ASSERTED IN PIXELS, not in arithmetic. The plate is
// centred and the top-right stack is right-anchored and they share one band --
// which is exactly the shape of the two collisions this HUD has already
// shipped (the lock row through the guild row, a clue through the case row),
// both of which were found in a PNG by a person and neither of which a test
// knew to look for. So the last cases in this file draw the thing and count
// pixels.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/player.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;

namespace {

SessionConfig wardAt(std::int32_t x, std::int32_t y, std::int32_t band, int hour = 20) {
    SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.world = sim::docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.spawnX = x;
    config.spawnY = y;
    config.spawnBand = band;
    // NAMED, ALWAYS. Two sessions compared pixel for pixel below have to agree
    // about where the camera is pointing, and the authored spawn yaw is only
    // used when nobody asks for one.
    config.spawnYaw = sim::kFacingNorth;
    config.spawnYawGiven = true;
    config.width = 320;
    config.height = 180;
    return config;
}

/// The authored spawn, untouched.
SessionConfig atSpawn(int hour = 20) {
    SessionConfig config = wardAt(-1, -1, -1, hour);
    return config;
}

/// Puts the body on a tile and runs one step, so the session's own per-step
/// bookkeeping has seen it. A PLACEMENT is a legitimate way to reach a place
/// -- runThresholdLine below walks instead, and says why -- and it is the
/// cheapest way to drive the edge logic these first cases are about.
void standOn(Session& session, std::int32_t x, std::int32_t y, std::int32_t band) {
    session.body().placeAt(x, y, band);
    session.stepMany(sim::MoveInput{}, 1);
}

}  // namespace

TEST_CASE("nothing announces on frame one, wherever the session boots") {
    // lastPlayerHp_'s own reasoning, applied to a place: a session that opens
    // with the player already standing on the Tarwalk has not CROSSED onto it.
    // Seeded in the constructor BEFORE the first syncPanelAnim() call, which is
    // the only ordering that makes this true.
    Session spawned(atSpawn());
    CHECK(spawned.placeLabel() == "TARWALK");
    CHECK(spawned.lastPlaceName() == "TARWALK");
    CHECK_FALSE(spawned.placePlateWanted());
    CHECK(spawned.placePlateLabel().empty());

    // And the same for a session booted somewhere else entirely -- --spawn is
    // not a threshold either.
    Session onTheRise(wardAt(107, 137, sim::docks::kBandMidSlope));
    CHECK(onTheRise.placeLabel() == "SALTGATE RISE");
    CHECK(onTheRise.lastPlaceName() == "SALTGATE RISE");
    CHECK_FALSE(onTheRise.placePlateWanted());

    // Including one booted on ground nobody has named, where there is no name
    // to remember and still nothing to announce.
    Session inTheCompound(wardAt(101, 137, sim::docks::kBandMidSlope));
    CHECK(inTheCompound.placeLabel() == "THE DOCKS - MID SLOPE");
    CHECK(inTheCompound.lastPlaceName().empty());
    CHECK_FALSE(inTheCompound.placePlateWanted());
}

TEST_CASE("crossing into a named place announces it, once") {
    Session session(wardAt(101, 137, sim::docks::kBandMidSlope));
    REQUIRE_FALSE(session.placePlateWanted());

    // Out of the Quayward compound's courtyard onto Saltgate Rise.
    standOn(session, 105, 137, sim::docks::kBandMidSlope);
    CHECK(session.placePlateWanted());
    CHECK(session.placePlateLabel() == "SALTGATE RISE");
    CHECK(session.lastPlaceName() == "SALTGATE RISE");

    // ONCE. Standing on the road for the rest of the plate's life must not
    // re-arm it -- the trigger is the CHANGE, not the state, or a body at rest
    // would re-arm it sixty times a second and the plate would never go down.
    int held = 0;
    while (session.placePlateWanted() && held < 400) {
        session.stepMany(sim::MoveInput{}, 1);
        ++held;
    }
    // TWO SECONDS AT THE 60 Hz STEP CADENCE, MINUS THE STEP THE CROSSING
    // ITSELF WAS ON. The countdown is armed inside syncPanelAnim() and spent
    // at the bottom of the same step() call, so the step that noticed the
    // crossing has already paid the first of the hundred and twenty -- which
    // leaves a hundred and nineteen for this loop. Spent in STEPS and never in
    // frames drawn (anim.hpp's own header on why), so this is an exact number
    // and not a range that happens to hold on this machine.
    CHECK(held == 119);
    CHECK_FALSE(session.placePlateWanted());
}

TEST_CASE("an unnamed gap between two reaches of the same road is not a crossing") {
    // TARWALK IS AUTHORED AS THREE ABUTTING RECTANGLES and Saltgate Rise as
    // three legs on three bands, so "the place changed" cannot mean "the rect
    // changed". And two thirds of the district has no name at all: stepping
    // into a back lane and out again is one road, once.
    Session session(atSpawn());
    REQUIRE(session.lastPlaceName() == "TARWALK");

    // North off the spine onto the unnamed quay apron. Nothing to announce:
    // an unnamed tile is a gap between places, not a place.
    standOn(session, 156, 59, sim::docks::kBandQuayside);
    CHECK(session.placeLabel() == "THE DOCKS - QUAYSIDE");
    CHECK_FALSE(session.placePlateWanted());
    // And the memory did NOT move, which is the whole mechanism.
    CHECK(session.lastPlaceName() == "TARWALK");

    // Back onto the spine. Same road, no second announcement.
    standOn(session, 156, 60, sim::docks::kBandQuayside);
    CHECK(session.placeLabel() == "TARWALK");
    CHECK_FALSE(session.placePlateWanted());

    // Across the seam between two authored TARWALK rects (x111 -> x112). Same
    // name, so still nothing.
    standOn(session, 111, 63, sim::docks::kBandQuayside);
    standOn(session, 112, 63, sim::docks::kBandQuayside);
    CHECK(session.placeLabel() == "TARWALK");
    CHECK_FALSE(session.placePlateWanted());

    // And on out to the piers, which IS a different place and does announce.
    standOn(session, 156, 57, sim::docks::kBandQuayside);
    CHECK(session.placePlateWanted());
    CHECK(session.placePlateLabel() == "THE LONG PIERS");
}

TEST_CASE("climbing the Rise from one band to the next is one road, not two") {
    // SALTGATE RISE has a leg on each of the three bands (docks.hpp kPlaces)
    // and the Gilded Gull's rooms are a DIFFERENT name from its taproom on
    // purpose. Both are band changes; only one of them is a crossing, and the
    // name is what tells them apart.
    Session rise(wardAt(107, 137, sim::docks::kBandMidSlope));
    REQUIRE(rise.lastPlaceName() == "SALTGATE RISE");
    standOn(rise, 107, 153, sim::docks::kBandUpper);
    CHECK(rise.placeLabel() == "SALTGATE RISE");
    CHECK_FALSE(rise.placePlateWanted());

    Session gull(wardAt(153, 70, sim::docks::kBandQuayside));
    REQUIRE(gull.lastPlaceName() == "THE GILDED GULL");
    standOn(gull, 153, 70, sim::docks::kBandMidSlope);
    CHECK(gull.placeLabel() == "THE GILDED GULL - ROOMS");
    CHECK(gull.placePlateWanted());
    CHECK(gull.placePlateLabel() == "THE GILDED GULL - ROOMS");
}

TEST_CASE("the newest crossing wins, and does not restart the rise") {
    // say()'s own rule. A second boundary crossed while the first plate is
    // still up swaps the words and re-arms the hold; it does NOT re-open a
    // toggle that is already open, so the plate reads as one notice being
    // corrected rather than two notices fighting.
    Session session(atSpawn());
    standOn(session, 156, 57, sim::docks::kBandQuayside);
    REQUIRE(session.placePlateLabel() == "THE LONG PIERS");
    session.stepMany(sim::MoveInput{}, 30);
    REQUIRE(session.placePlateWanted());

    standOn(session, 156, 63, sim::docks::kBandQuayside);
    CHECK(session.placePlateLabel() == "TARWALK");
    // Re-armed to its full two seconds from here -- the same 119 the case
    // above measures, and NOT the thirty-odd the first announcement had left
    // when this one replaced it.
    int held = 0;
    while (session.placePlateWanted() && held < 400) {
        session.stepMany(sim::MoveInput{}, 1);
        ++held;
    }
    CHECK(held == 119);
}

TEST_CASE("a panel owning the screen stands the plate down, and it does not come back") {
    // THE STAND-DOWN RULE EVERY OTHER OVERLAY IN THIS BUILD FOLLOWS. The plate
    // lives in the compass ribbon's own band; while somebody's page is up
    // there is no ribbon and nothing but that page, so the plate stops --
    // and it STOPS rather than pausing, because a notice that pops the instant
    // a menu closes is a notice about the menu.
    Session session(atSpawn());
    standOn(session, 156, 57, sim::docks::kBandQuayside);
    REQUIRE(session.placePlateWanted());

    session.toggleDistrictMap();
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.districtMapOpen());
    CHECK_FALSE(session.placePlateWanted());

    session.toggleDistrictMap();
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE_FALSE(session.districtMapOpen());
    CHECK_FALSE(session.placePlateWanted());

    // A crossing WALKED while the page is up is not announced either -- stood
    // down, not queued. The memory still moves, so stepping back out and in
    // again is not a fresh crossing.
    session.toggleDistrictMap();
    session.stepMany(sim::MoveInput{}, 1);
    standOn(session, 156, 63, sim::docks::kBandQuayside);
    CHECK_FALSE(session.placePlateWanted());
    CHECK(session.lastPlaceName() == "TARWALK");
    session.toggleDistrictMap();
    session.stepMany(sim::MoveInput{}, 1);
    CHECK_FALSE(session.placePlateWanted());
}

TEST_CASE("the plate never draws over a panel, whatever its own fade says") {
    // The belt to the suppression's braces, and it is a DIFFERENT claim: the
    // case above is about state, this one is about pixels. The countdown going
    // to zero flips the toggle's TARGET, and a target takes eight steps to
    // reach -- so there is a short window in which a live plate and an open
    // page are on screen together, and drawFrame's `conversing ? 0 : ...` is
    // what closes it. Nothing is stepped between the toggle and the draw here,
    // so the plate is caught at exactly full strength: the worst frame there
    // is.
    //
    // TWO SESSIONS THAT DIFFER IN ONE THING. Both end on the same tile, facing
    // the same way, after the same single step -- the only difference is
    // whether that last placement crossed a boundary. Which makes the first
    // half of this case load-bearing: with no page up the two frames MUST
    // differ (that is the plate, and nothing else in either session can
    // account for it), and only then does them being identical with a page up
    // mean anything at all.
    Session crossed(wardAt(156, 60, sim::docks::kBandQuayside));   // spawned on TARWALK
    standOn(crossed, 156, 57, sim::docks::kBandQuayside);          // ... walked to the piers
    Session stayed(wardAt(156, 57, sim::docks::kBandQuayside));    // spawned on the piers
    standOn(stayed, 156, 57, sim::docks::kBandQuayside);           // ... and stayed there
    REQUIRE(crossed.placePlateWanted());
    REQUIRE_FALSE(stayed.placePlateWanted());

    const auto differingPixels = [](Framebuffer& a, Framebuffer& b) {
        std::size_t differing = 0;
        for (std::size_t i = 0; i < a.pixels().size(); ++i) {
            differing += a.pixels()[i] != b.pixels()[i] ? 1U : 0U;
        }
        return differing;
    };

    Framebuffer plateUp(320, 180);
    crossed.drawFrame(plateUp);
    Framebuffer noPlate(320, 180);
    stayed.drawFrame(noPlate);
    const std::size_t inTheOpen = differingPixels(plateUp, noPlate);
    INFO("with no page up the plate accounted for ", inTheOpen, " pixels");
    // BOUNDED ON BOTH SIDES, and the upper bound is the one that makes this
    // case honest. It says the two sessions really are the same session apart
    // from the plate: a plate at 320x180 is a black field a few hundred pixels
    // across, so a difference in the thousands would mean the two worlds had
    // drifted apart and the zero asserted below would be proving nothing.
    CHECK(inTheOpen > 100);
    CHECK(inTheOpen < 3000);

    // And now the same pair with the ward map over them. Not one pixel.
    crossed.toggleDistrictMap();
    stayed.toggleDistrictMap();
    REQUIRE(crossed.districtMapOpen());
    Framebuffer mapOverPlate(320, 180);
    crossed.drawFrame(mapOverPlate);
    Framebuffer mapAlone(320, 180);
    stayed.drawFrame(mapAlone);
    const std::size_t underThePage = differingPixels(mapOverPlate, mapAlone);
    INFO("the map page differed by ", underThePage, " pixels with a live plate behind it");
    CHECK(underThePage == 0);
}

TEST_CASE("the plate rises through its settled row rather than sliding back down") {
    // DECISIONS.md UI rule 4: one continuous value drives the alpha AND the
    // offset, so the plate can never be caught bright and mid-slide or settled
    // and half-faded. The sign comes off the toggle's own target, which is what
    // makes the motion a RISE and not a slide in and back out the way it came.
    HudState state;
    state.placePlate = "SALTGATE RISE";

    // Below its row while rising, settled at full strength, above it while
    // fading -- the three points that define the whole motion.
    const struct {
        float fade;
        float drift;
        const char* what;
    } beats[] = {
        {0.125F, -0.875F, "the first frame after the crossing"},
        {1.0F, 0.0F, "settled"},
        {0.125F, 0.875F, "the last frame of the fade"},
    };
    // DIFFED AGAINST THE SAME HUD WITH THE PLATE OFF, not against a blank
    // frame: drawHud puts the compass ribbon at the very top of every one of
    // these, and asking a bare frame for its topmost changed pixel would have
    // measured the ribbon three times and called it a rise.
    HudState off = state;
    off.placePlateFade = 0.0F;
    Framebuffer without(320, 180);
    without.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawHud(without, off);

    std::vector<int> tops;
    for (const auto& beat : beats) {
        state.placePlateFade = beat.fade;
        state.placePlateDrift = beat.drift;
        Framebuffer frame(320, 180);
        frame.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(frame, state);
        int top = -1;
        for (int y = 0; y < 180 && top < 0; ++y) {
            for (int x = 0; x < 320; ++x) {
                if (frame.pixels()[frame.index(x, y)] != without.pixels()[without.index(x, y)]) {
                    top = y;
                    break;
                }
            }
        }
        INFO(beat.what, " drew its topmost pixel at y=", top);
        REQUIRE(top >= 0);
        tops.push_back(top);
    }
    // Strictly monotonic upward across the three beats: it starts low, settles,
    // and keeps going up as it goes out.
    CHECK(tops[0] > tops[1]);
    CHECK(tops[1] > tops[2]);
}

namespace {

/// Draws `state` twice -- once with the plate off, once with it on -- and
/// answers how many pixels the plate itself accounted for, how many of those
/// landed in the play space, and how many landed on something the rest of the
/// HUD had already drawn in the top band. Diffing the SAME state against
/// itself is the only way to ask a question about this one element.
struct PlateFootprint {
    std::size_t drew = 0;
    std::size_t trespass = 0;
    std::size_t overprinted = 0;
};

PlateFootprint measurePlate(HudState state, int width, int height, float drift) {
    const Rgb ground{0.20F, 0.18F, 0.16F};
    HudState quiet = state;
    quiet.placePlateFade = 0.0F;
    HudState loud = state;
    loud.placePlateFade = 1.0F;
    loud.placePlateDrift = drift;

    Framebuffer empty(width, height);
    empty.clear(ground);
    Framebuffer without(width, height);
    without.clear(ground);
    drawHud(without, quiet);
    Framebuffer with(width, height);
    with.clear(ground);
    drawHud(with, loud);

    PlateFootprint out;
    const CentreRect centre = hudCentreRect(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = with.index(x, y);
            if (with.pixels()[i] == without.pixels()[i]) {
                continue;
            }
            ++out.drew;
            if (x >= centre.x0 && x < centre.x1 && y >= centre.y0 && y < centre.y1) {
                ++out.trespass;
            }
            // Anything the rest of the HUD had already put on this pixel and
            // the plate then changed. Not "the plate drew near it" -- the
            // plate drew ON it.
            if (without.pixels()[i] != empty.pixels()[i]) {
                ++out.overprinted;
            }
        }
    }
    return out;
}

}  // namespace

TEST_CASE("the plate stays out of the play space and off every row already drawn") {
    // THE CENTRE-CLEAR RULE (hud.hpp's own header) AND THE ONE COLLISION THIS
    // ELEMENT CAN ACTUALLY CAUSE. The plate is centred, the top-right stack is
    // right-anchored, and they share the top band -- so the stack draws first,
    // hands back the width it claimed, and the plate's whole budget is what is
    // left of the band between two of them.
    //
    // ASSERTED WITH THE HUD AT ITS LOUDEST, which is a state nothing in this
    // repo has ever captured by accident: every top-right row lit at its own
    // longest, the longest place name kPlaces can produce on the plate, and
    // the plate at the LOWEST point its own drift can put it (the instant of
    // the crossing) -- the one frame it comes closest to the play space.
    HudState full;
    full.health = 42;
    full.healthMax = 100;
    full.fatigue = 30;
    full.fatigueMax = 100;
    full.yawBam = sim::angle_from_degrees(200);
    full.timeOfDaySeconds = 20 * 3600 + 42 * 60;
    full.coin = 99999;
    full.standingLabel = "THE WARD WANTS YOU GONE";
    full.heatLabel = "CONDEMNED  WANTED  HEAT 84  LOOT 12  BALE";
    full.stashLabel = "3 FLOWER  2 WIRE  4 DUST  240DR";
    full.spellLabel = "CAST  SET THE SHOULDERS (599S)";
    full.effectLabels = {"SET THE SHOULDERS 899S", "STEADY THE HAND 899S",
                         "CLEAR THE HEAD 899S", "WARM THE HANDS 599S"};
    full.stealthLabel = "SEEN CROUCH  LIT 88  LOUD";
    full.alert = "KLED TARBECK: THAT IS YOUR ONE. OUT OF THIS HOUSE.";
    full.placePlate = "THE GILDED GULL - ROOMS";

    for (const auto& [width, height] : {std::pair{320, 180}, std::pair{640, 360},
                                        std::pair{960, 540}, std::pair{1280, 720}}) {
        for (const float drift : {-1.0F, 0.0F, 1.0F}) {
            const PlateFootprint shot = measurePlate(full, width, height, drift);
            INFO("at ", width, "x", height, " drift ", drift, ": the plate drew ", shot.drew,
                 " px, ", shot.trespass, " of them in the play space and ", shot.overprinted,
                 " of them on somebody else's row");
            CHECK(shot.trespass == 0);
            CHECK(shot.overprinted == 0);
        }
    }

    // AND WHEN THE CORNER LEAVES IT NO ROOM AT ALL IT IS DROPPED, NOT SQUEEZED.
    // BottomBand::take()'s own rule at the other edge: a row with no legal slot
    // is not drawn in the play space, it is not drawn. At 320x180 the heat line
    // alone is 199 of the frame's 320 pixels wide, which leaves the centre
    // channel negative -- so the honest answer is nothing, and the two rules
    // above hold vacuously rather than by luck.
    const PlateFootprint tightest = measurePlate(full, 320, 180, 0.0F);
    INFO("at 320x180 with the corner at its widest the plate drew ", tightest.drew, " px");
    CHECK(tightest.drew == 0);
}

TEST_CASE("on an ordinary street HUD the plate is drawn, and drawn big") {
    // The case above proves the plate never lands on anything. This one proves
    // it is not merely never drawing -- which is the way a geometry test
    // passes for the wrong reason, and the reason this pair is two cases and
    // not one. Clock, purse and a room line is what the HUD actually looks
    // like while somebody is walking around the ward, which is the only state
    // a crossing can happen in.
    HudState street;
    street.health = 100;
    street.healthMax = 100;
    street.yawBam = sim::angle_from_degrees(180);
    street.timeOfDaySeconds = 20 * 3600;
    street.coin = 120;
    street.roomLabel = "THE GULL  14 IN  LOUD";
    street.placePlate = "SALTGATE RISE";

    for (const auto& [width, height] : {std::pair{320, 180}, std::pair{640, 360},
                                        std::pair{960, 540}, std::pair{1280, 720}}) {
        const PlateFootprint shot = measurePlate(street, width, height, 0.0F);
        INFO("at ", width, "x", height, " the plate drew ", shot.drew, " px, ", shot.trespass,
             " in the play space, ", shot.overprinted, " on another row");
        // A plate at the register size is a black field the width of its own
        // words: hundreds of pixels at the smallest frame this game admits to
        // being legible at, and thousands at the ones it is captured at.
        CHECK(shot.drew > 400);
        CHECK(shot.trespass == 0);
        CHECK(shot.overprinted == 0);
    }
}

TEST_CASE("a HudState that never heard of the plate is pixel-identical") {
    // PURE RENDER, AND FREE WHEN OFF. Every hand-built HudState in this suite
    // and every capture taken before this pass leaves placePlate empty and
    // placePlateFade at its 0 default; both of those have to draw exactly what
    // they drew before the fields existed, or this pass moved frames it had no
    // business moving.
    HudState before;
    before.health = 80;
    before.healthMax = 100;
    before.yawBam = sim::angle_from_degrees(90);
    before.timeOfDaySeconds = 8 * 3600;
    before.coin = 120;
    before.roomLabel = "THE GULL  14 IN  LOUD";

    HudState named = before;
    named.placePlate = "SALTGATE RISE";  // set, but never faded up

    for (const auto& [width, height] : {std::pair{320, 180}, std::pair{960, 540}}) {
        Framebuffer plain(width, height);
        plain.clear(Rgb{0.10F, 0.12F, 0.14F});
        drawHud(plain, before);
        Framebuffer withName(width, height);
        withName.clear(Rgb{0.10F, 0.12F, 0.14F});
        drawHud(withName, named);
        std::size_t differing = 0;
        for (std::size_t i = 0; i < plain.pixels().size(); ++i) {
            differing += plain.pixels()[i] != withName.pixels()[i] ? 1U : 0U;
        }
        INFO("at ", width, "x", height, " a zero-fade plate moved ", differing, " pixels");
        CHECK(differing == 0);
    }
}

TEST_CASE("every authored crossing is walked, and every one of them announces") {
    // THE CAPTURE FLAG'S OWN TABLE, driven through the same runThresholdLine
    // the CLI calls. This is what stops a coordinate in kThresholds rotting
    // into a pair of tiles that are both on the same side of a boundary --
    // which is a failure a screenshot cannot report about itself, because a
    // picture of a street with no plate on it looks exactly like a street.
    const struct {
        const char* word;
        const char* from;
        const char* to;
    } crossings[] = {
        {"saltgate", "", "SALTGATE RISE"},
        {"gull", "TARWALK", "THE GILDED GULL"},
        {"piers", "TARWALK", "THE LONG PIERS"},
        {"gallows", "GALLOWS ROW", "SALTGATE RISE"},
    };
    for (const auto& crossing : crossings) {
        Session session(atSpawn());
        const ThresholdLineResult out = runThresholdLine(session, crossing.word, "in");
        INFO("--threshold=", crossing.word, " from=\"", out.from, "\" to=\"", out.to,
             "\" crossed=", out.crossed, " plate=\"", out.plate, "\"");
        CHECK(out.found);
        CHECK(out.from == crossing.from);
        CHECK(out.to == crossing.to);
        CHECK(out.crossed);
        // THE DELIVERABLE. The walk ended with the plate live and saying the
        // name of the place walked into.
        CHECK(out.announced);
        CHECK(out.plate == crossing.to);
    }

    // AND AN UNKNOWN KEYWORD IS A LOUD NOTHING, not a silent no-op that a
    // capture would report as a successful run of a crossing that does not
    // exist -- runSmoke counts `found` as one of its three beats.
    Session session(atSpawn());
    const ThresholdLineResult none = runThresholdLine(session, "not-a-place", "in");
    CHECK_FALSE(none.found);
    CHECK_FALSE(none.crossed);
    CHECK_FALSE(none.announced);
}

TEST_CASE("the payoff crossing walks out of a compound gate and looks back through it") {
    // The Quayward's east ring gate: (103,136-139) on the mid slope, wearing
    // District Phase B's own one-band reman frame directly overhead. Walking
    // out of it is the first tile of Saltgate Rise, and `--threshold-end=back`
    // turns to put that frame in the middle of the picture with the plate up.
    Session session(atSpawn());
    const ThresholdLineResult out = runThresholdLine(session, "saltgate", "back");
    REQUIRE(out.found);
    CHECK(out.crossed);
    CHECK(out.announced);
    CHECK(out.plate == "SALTGATE RISE");
    // Stood ON the road, looking back WEST at the gate it came through.
    CHECK(session.placeLabel() == "SALTGATE RISE");
    CHECK(session.body().band() == sim::docks::kBandMidSlope);
    CHECK(session.body().yaw() == sim::kFacingWest);
    // And the gate really is west of where the body is standing: the walk went
    // east, so the mouth is behind it, not somewhere the yaw happens to point.
    CHECK(session.body().tileX() > 103);
}
