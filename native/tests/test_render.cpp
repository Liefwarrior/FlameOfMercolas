// The HUD and the capture path, asserted on the pixels they actually produce.
//
// Everything here is the OVERLAY: the interface drawn into a Framebuffer on a
// transparent ground (Session::drawFrame), which the 3D backend composites over
// the world. It is drawn in software inside the test suite -- no window, no
// GPU, no display server -- so facts about it are checked on every build. "It
// looked right on my machine" is not a gate; "the HUD hugs the edges and the
// centre is clear" is. The WORLD under it is the raylib backend's, and its
// frames are asserted in test_render3d.cpp and test_light_model.cpp.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;

namespace {

SessionConfig docksAt(int hour) {
    SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.world = sim::docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.width = 320;
    config.height = 180;
    return config;
}

}  // namespace

TEST_CASE("the framebuffer packs to the byte order stb and SDL both want") {
    // R first in memory. Getting this backwards makes every screenshot look
    // like a bad TV and is invisible in a unit test that only round-trips.
    const std::uint32_t packed = packRgb(Rgb{1.0F, 0.0F, 0.0F});
    CHECK((packed & 0xFFU) == 255U);
    CHECK(((packed >> 8) & 0xFFU) == 0U);
    CHECK(((packed >> 16) & 0xFFU) == 0U);
    CHECK(((packed >> 24) & 0xFFU) == 255U);

    const Rgb back = unpackRgb(packRgb(Rgb{0.25F, 0.5F, 0.75F}));
    CHECK(back.r == doctest::Approx(0.25F).epsilon(0.01));
    CHECK(back.g == doctest::Approx(0.5F).epsilon(0.01));
    CHECK(back.b == doctest::Approx(0.75F).epsilon(0.01));
}

TEST_CASE("the day curve is committed dark and the lamps carry the night") {
    const SkyState noon = skyAt(12 * 3600);
    const SkyState midnight = skyAt(0);
    const SkyState dusk = skyAt(20 * 3600);
    const SkyState lateDusk = skyAt(21 * 3600);

    CHECK(noon.daylight == doctest::Approx(1.0F));
    CHECK(midnight.daylight == doctest::Approx(0.0F));
    // 20:00 is the default: the light is going but the district still reads.
    CHECK(dusk.daylight < 0.8F);
    CHECK(dusk.daylight > 0.3F);
    // An hour later it is all but gone and the lamps are carrying the frame.
    CHECK(lateDusk.daylight < 0.2F);
    CHECK(lateDusk.daylight < dusk.daylight);

    // Night ambient is near black -- torchlit pools against near-black is the
    // committed look, not a bright moonlit street.
    CHECK(midnight.ambient.r < 0.10F);
    CHECK(midnight.ambient.g < 0.10F);
    CHECK(noon.ambient.r > 0.4F);
    // And the fog closes in when the light goes.
    CHECK(midnight.fogDistance < noon.fogDistance);
}

TEST_CASE("a lamp lights a pool around itself and nothing far from it") {
    const Session session(docksAt(20));
    const LampGlow& glow = session.glow();
    REQUIRE(session.lampCount() == 28);
    CHECK(glow.litCellCount() > 1000);

    const std::vector<Lamp>& lamps = session.lamps();
    const Lamp& lamp = lamps.front();
    const Rgb centre = glow.at(lamp.x, lamp.y, lamp.z);
    CHECK(centre.r > 0.1F);
    // Warm: more red than blue, whichever kind of light it is.
    CHECK(centre.r > centre.b);
    // Falls off, and is gone well before ten tiles.
    const Rgb near = glow.at(lamp.x + 2, lamp.y, lamp.z);
    const Rgb far = glow.at(lamp.x + 10, lamp.y, lamp.z);
    CHECK(near.r < centre.r);
    CHECK(far.r == 0.0F);
    // And it does not shine through the floor two levels down.
    CHECK(glow.at(lamp.x, lamp.y, lamp.z - 3).r == 0.0F);
}

TEST_CASE("a storey is three tiles tall and the eye is a person's eye inside it") {
    // THE CASE ELI'S COMPLAINT DESERVED. "All buildings only appear to be one
    // tile high" was true and nothing in 469 tests said so, because every
    // height in the build was a level number cast to a float and the projection
    // agreed with itself all the way down.
    //
    // These are the numbers that make a district read as a city. They are
    // arithmetic, not pixels, so they cannot rot the way a screenshot does.
    CHECK(sim::kTilesPerBand == 3);
    CHECK(kBandHeight == doctest::Approx(3.0F));

    // A level's walking surface is three tiles above the one below it, so the
    // WALL that spans a level is three tiles of side face. One tile was the
    // crawlspace.
    CHECK(bandSurface(20) - bandSurface(19) == doctest::Approx(3.0F));

    // The body's z axis counts BANDS in Q8 and the renderer's counts TILES, and
    // the eye has to be at the same height on both. 435/256 = 1.70 tiles.
    CHECK(sim::kEyeHeight * sim::kTilesPerBand == sim::kEyeHeightTilesQ8);
    Session session(docksAt(20));
    const Camera view = session.camera();
    const float feet = bandSurface(session.body().band());
    CHECK(view.z - feet == doctest::Approx(1.70F).epsilon(0.01));

    // AND THE WHOLE POINT: the eye stands well under the top of the single
    // storey it is inside. It used to stand 0.80 of the way up it, which is why
    // a two-storey warehouse read as a garden wall.
    CHECK(view.z < bandSurface(session.body().band() + 1));
    CHECK(bandSurface(session.body().band() + 1) - view.z > 1.0F);

    // People are people-sized against the same scale. The 3D instancer sizes
    // every body off these rows, and the tallest of them must reach near the
    // player's own eye -- a district of knee-high figures would have made the
    // buildings look right and everything else look like a diorama.
    for (const sim::WardType kind :
         {sim::WardType::Serf, sim::WardType::Shopkeeper, sim::WardType::MilitiaWatch,
          sim::WardType::PriestOfTheFlame, sim::WardType::Thief}) {
        const float crown = figureScaleOf(kind).heightTiles;
        CHECK(crown > 1.5F);
        CHECK(crown < 2.3F);
    }
}

TEST_CASE("the HUD hugs the edges and leaves the centre completely clear") {
    // The direct answer to task #66. The Java build's first-person view put an
    // inspector sheet and a craftings bar in the play space; this is the check
    // that stops that happening again.
    Session session(docksAt(20));
    Framebuffer bare(320, 180);
    bare.clearTransparent();

    Framebuffer dressed(320, 180);
    dressed.clearTransparent();
    HudState hud;
    hud.health = 72;
    hud.yawBam = session.body().yaw();
    drawHud(dressed, hud);

    const CentreRect centre = hudCentreRect(320, 180);
    CHECK(centre.x0 > 0);
    CHECK(centre.y0 > 0);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(bare.pixels()[bare.index(x, y)] == dressed.pixels()[dressed.index(x, y)]);
        }
    }
    // And it drew SOMETHING, or the check above is vacuous.
    std::size_t changed = 0;
    for (std::size_t i = 0; i < bare.pixels().size(); ++i) {
        if (bare.pixels()[i] != dressed.pixels()[i]) {
            ++changed;
        }
    }
    CHECK(changed > 300);
}

TEST_CASE("every HUD row lit at once still leaves the centre clear") {
    // THE S10 GAP, CLOSED, AND IT WAS REAL. hud.hpp carried a VERIFICATION GAP
    // saying the guild row (y - 24*scale) and the objective row (y - 32*scale)
    // crossed the exclusion rectangle at 320x180 and 640x360 whenever they were
    // non-empty, and the top-right stack ran six rows deep at nine scale units
    // each into a rectangle that starts at 39 pixels down. Both were true. The
    // reason no case caught either is in the two cases above this one: the only
    // HudStates ever drawn in this suite set four or five fields, and the
    // defect needs eleven.
    //
    // So this one lights EVERY field the struct has, with the longest string
    // each is clipped to in session.cpp, at all three resolutions the game is
    // captured at. Rows are allocated out of a slot grid now and a row with no
    // legal slot is dropped rather than drawn, which is what makes it pass.
    HudState full;
    full.health = 61;
    full.healthMax = 100;
    // FATIGUE BUILD: the wind bar lit too, mid-pool, so the one element this
    // build adds is inside the every-field proof from the day it ships.
    full.fatigue = 80;
    full.fatigueMax = 160;
    full.yawBam = sim::kFacingWest;
    full.timeOfDaySeconds = 21 * 3600 + 47 * 60;
    full.coin = 99999;
    full.standingLabel = "THE WARD WANTS YOU GONE";
    full.heatLabel = "CONDEMNED  WANTED  HEAT 84  LOOT 12  BALE";
    full.stashLabel = "3 FLOWER  2 WIRE  4 DUST  240DR";
    // S13: the two rows the Cast/Block task added, at their own longest.
    full.spellLabel = "CAST  SET THE SHOULDERS (599S)";
    full.blockLabel = "GUARD UP";
    // STANCE & ROOM BUILD: the fighting-mode row at its longest, lit beside
    // the guard it can share the band with.
    full.handsLabel = "THE EVICTOR UP";
    // KIT BUILD: the turned-blow row, lit too -- the every-field proof had
    // not been told about it.
    full.turnLabel = "THE COAT TURNS 2";
    // HELD-EFFECTS BUILD: all four live-hold rows at once, at their own
    // longest -- the fullest top-right stack this game can produce.
    full.effectLabels = {"SET THE SHOULDERS 899S", "STEADY THE HAND 899S",
                         "CLEAR THE HEAD 899S", "WARM THE HANDS 599S"};
    // SPELLS BUILD: the quick bar strip at full strength -- every slot
    // loaded, the longest authored name selected, equipped and selected on
    // different cells so both marks draw.
    full.quickSlots = {"STING",  "SCALD", "SET THE SHOULDERS", "WARD THE COLD",
                       "A CRAFTING OF YOUR OWN", "STING", "SCALD", "STING",
                       "SCALD",  "STING"};
    full.quickSelected = 4;
    full.quickEquipped = 2;
    full.quickBarFade = 1.0F;
    full.stealthLabel = "SEEN CROUCH  LIT 88  LOUD";
    full.caseLabel = "CASE 4/6 > THE DROWNED HOLD  EMPTYING";
    full.guildLabel = "THE SKYRUNNERS - THE WARD'S OWN SHADOW";
    full.objectiveLabel = "TAKE THE BALE PAST THE WEIGHHOUSE";
    full.rivalLabel = "RIVAL BRAM MARROW - CRAFTLORD x7  HUNTING";
    full.lockLabel = "LOCK  PINS ***--  DEPTH ....+....  PICKS 2";
    full.roomLabel = "THE GULL  14 IN  LOUD";
    full.alert = "KLED TARBECK: THAT IS YOUR ONE. OUT OF THIS HOUSE, OR I PUT YOU OUT.";
    // DISTRICT PHASE D: the threshold plate, at the longest name docks.hpp's
    // kPlaces can produce and at full strength, sitting at the LOWEST point
    // its own drift can put it -- the one instant it comes closest to the
    // exclusion rectangle. It is a centred element in a HUD whose whole rule
    // is that the middle stays empty, so it belongs in the case that lights
    // everything at once rather than only in its own file.
    full.placePlate = "THE GILDED GULL - ROOMS";
    full.placePlateFade = 1.0F;
    full.placePlateDrift = -1.0F;
    // THE CROSSHAIR PASS: aimVerb/aimSubject/aimNote ARE THE ONE GROUP OF
    // FIELDS THIS CASE DELIBERATELY LEAVES DARK, and that is the point rather
    // than an omission. They are the documented exemption from the rule this
    // case exists to prove (hud.hpp's own header, and the owner's own
    // sentence), so lighting them here would turn a hard zero into a
    // negotiation. With the aim prompt down, drawHud() must still put ZERO
    // pixels in the play space with all sixteen other rows lit -- exactly the
    // guarantee this case has always made -- and the next case is what holds
    // the aim prompt itself to its own fence.

    for (const auto& [width, height] : {std::pair{320, 180}, std::pair{640, 360},
                                        std::pair{960, 540}}) {
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(dressed, full);

        const CentreRect centre = hudCentreRect(width, height);
        std::size_t trespass = 0;
        for (int y = centre.y0; y < centre.y1; ++y) {
            for (int x = centre.x0; x < centre.x1; ++x) {
                trespass +=
                    bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)] ? 1U
                                                                                            : 0U;
            }
        }
        INFO("at ", width, "x", height, " the HUD put ", trespass,
             " pixels inside the play space");
        CHECK(trespass == 0);

        // And it is not vacuous: with sixteen rows asked for, all four edges
        // drew something.
        const auto changedIn = [&](int x0, int y0, int x1, int y1) {
            std::size_t changed = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    changed += bare.pixels()[bare.index(x, y)] !=
                                       dressed.pixels()[dressed.index(x, y)]
                                   ? 1U
                                   : 0U;
                }
            }
            return changed;
        };
        CHECK(changedIn(0, 0, width, centre.y0) > 200);
        CHECK(changedIn(0, centre.y1, width, height) > 200);
    }
}

TEST_CASE("the turned-blow row draws beside a bouncer's warning and raised hands at every capture size") {
    // KIT POLISH. docs/frames/kit/kit-dr-960x540.png was shot to show COAT
    // TURNS 2 and never did. The bottom band at 960x540 is four minor slots
    // deep (base 501, floor 422, step 16); the alert at full scale holds two,
    // the KIT FIX PASS's spacer slot took the third, FISTS UP the fourth,
    // and the turn row -- last of the three in the priority order -- was
    // refused by BottomBand::take() one pixel over the exclusion floor.
    // From 1080p up the band is only THREE slots deep, so the turn has to
    // outrank the hands as well, or the same warning-plus-FISTS-UP pair eats
    // it on every monitor. Both are fixed in hud.cpp (the spacer is a pixel
    // pad now; the turn is taken ahead of the hands); this is the proof, at
    // the sizes the Kit is shot at and the one it is played at.
    HudState fight;
    fight.health = 61;
    fight.healthMax = 100;
    fight.yawBam = sim::kFacingWest;
    fight.alert = "OX GULLBANE: YOU HAVE HAD THE ONLY WORD YOU GET. THE DOOR.";
    fight.handsLabel = "FISTS UP";
    HudState turned = fight;
    turned.turnLabel = "COAT TURNS 2";

    // How many pixels two HUD states differ by, split at the exclusion
    // rectangle's floor: the band below it, and everywhere else.
    const auto inkBetween = [](const HudState& a, const HudState& b, int width, int height) {
        Framebuffer left(width, height);
        left.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(left, a);
        Framebuffer right(width, height);
        right.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(right, b);
        const CentreRect centre = hudCentreRect(width, height);
        std::pair<std::size_t, std::size_t> counts{0, 0};
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (left.pixels()[left.index(x, y)] == right.pixels()[right.index(x, y)]) {
                    continue;
                }
                if (y >= centre.y1) {
                    ++counts.first;
                } else {
                    ++counts.second;
                }
            }
        }
        return counts;
    };

    for (const auto& [width, height] : {std::pair{640, 360}, std::pair{960, 540},
                                        std::pair{1280, 720}, std::pair{1920, 1080}}) {
        const auto [band, elsewhere] = inkBetween(fight, turned, width, height);
        INFO("at ", width, "x", height, " the turn row put ", band,
             " pixels in the band and ", elsewhere, " outside it");
        CHECK(band > 50);
        CHECK(elsewhere == 0);
    }
    // And on the four-slot band the Kit is shot at, it cost the hands
    // nothing: FISTS UP is still on the frame beside it.
    HudState turnedHandsDown = turned;
    turnedHandsDown.handsLabel = {};
    const auto [handsInk, handsElsewhere] = inkBetween(turnedHandsDown, turned, 960, 540);
    CHECK(handsInk > 50);
    CHECK(handsElsewhere == 0);
}

TEST_CASE("a bottom-band or top-right label does not run off the frame at an off-16:9 window") {
    // THE SAME CLASS OF DEFECT the chargen sheet's clipLabel() had -- a hard
    // cut with no usable boundary, invisible to a suite that only ever
    // exercised the one shape that happened to survive it. session.cpp's
    // guildLine()/heatLine()/rivalLine() (and stashLine/stealthLine/
    // standingLabel/objectiveLine/caseLine alongside them) clip their own
    // text to a GUESSED CHARACTER COUNT -- 34, 34, 44 -- sized to fit the
    // three 16:9 captures this suite has always rendered at (320x180,
    // 640x360, 960x540). drawTopRight/drawBottomBand draw every one of those
    // strings straight off that guess with no clipToWidth of their own, the
    // way roomLabel/lockLabel/alert already have: "a line anchored right or
    // left is clamped by its anchor" (hud.hpp's own clipToWidth doc) is only
    // true if whoever pre-clipped it actually knew the frame's real pixel
    // width, and session.cpp's guess only agrees with the real one at 16:9.
    //
    // --width and --height are independent CLI flags (main.cpp: each is only
    // floored at 64, neither is derived from the other), so a window that is
    // not 16:9 is a supported configuration this suite never exercised.
    //
    // At 220x640 the budget these rows should have -- width - 2*margin -- is
    // well under what a 48-glyph label needs at this window's minor scale.
    // (THE CELLS PASS: the register keys on the width as well as the height
    // now, so a 220-wide window draws its reading rows at scale 1 where the
    // height alone said 2; the label was 34 glyphs and no longer overflowed,
    // which is the width rule doing its job, so the premise grew instead.)
    // Unclipped, a LEFT-anchored row (guildLabel) runs off the RIGHT edge
    // with no mark that anything was cut; a RIGHT-anchored one (rivalLabel,
    // and every row of the top-right stack) starts its draw at a negative x
    // and runs off the LEFT edge instead -- losing the FRONT of the line,
    // which is the higher-priority half of every one of these (heatLabel's
    // "CONDEMNED  WANTED" reads before "HEAT 84").
    const int width = 220;
    const int height = 640;
    const std::string longLabel(48, 'A');

    const int scale = hudScale(height);
    const int minor = hudMinorScale(width, height);
    const int margin = 6 * scale;
    // The premise: this label genuinely does not fit the row's real budget,
    // or every check below is vacuous.
    REQUIRE(textWidth(longLabel, minor) > width - 2 * margin);

    const auto rightmostInk = [&](const Framebuffer& bare, const Framebuffer& dressed) {
        int rightmost = -1;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]) {
                    rightmost = std::max(rightmost, x);
                }
            }
        }
        return rightmost;
    };
    const auto leftmostInk = [&](const Framebuffer& bare, const Framebuffer& dressed) {
        int leftmost = width;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]) {
                    leftmost = std::min(leftmost, x);
                }
            }
        }
        return leftmost;
    };

    SUBCASE("left-anchored: guildLabel") {
        HudState state;
        state.showHealth = false;
        state.showCompass = false;
        state.guildLabel = longLabel;
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(dressed, state);

        const int rightmost = rightmostInk(bare, dressed);
        REQUIRE(rightmost >= 0);  // it drew, so the check below is not vacuous
        INFO("rightmost ink at x=", rightmost, " of width ", width, " (margin ", margin, ")");
        CHECK(rightmost < width - margin);
    }

    SUBCASE("right-anchored, bottom band: rivalLabel") {
        HudState state;
        state.showHealth = false;
        state.showCompass = false;
        state.rivalLabel = longLabel;
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(dressed, state);

        const int leftmost = leftmostInk(bare, dressed);
        REQUIRE(leftmost < width);  // it drew, so the check below is not vacuous
        INFO("leftmost ink at x=", leftmost, " of width ", width, " (margin ", margin, ")");
        CHECK(leftmost >= margin);
    }

    SUBCASE("right-anchored, top-right stack: heatLabel") {
        HudState state;
        state.showHealth = false;
        state.showCompass = false;
        state.heatLabel = longLabel;
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(dressed, state);

        const int leftmost = leftmostInk(bare, dressed);
        REQUIRE(leftmost < width);
        INFO("leftmost ink at x=", leftmost, " of width ", width, " (margin ", margin, ")");
        CHECK(leftmost >= margin);
    }
}

TEST_CASE("the HUD costs a fraction of the frame, and the fraction is pinned") {
    // "I love the vibe of the UI but just be more careful with real estate."
    //
    // The vibe is not testable and is not being tested. What the owner was
    // looking at IS: eleven rows of 4x6 glyphs all drawn at the same size, a
    // build banner in the top-left of every screenshot, and a two-row block in
    // the top-centre. On the real scenes, measured with `--nohud` as the
    // baseline, the interface went from 5.80% of a street frame to 2.91% and
    // from 6.89% of a rooftop frame to 4.11%.
    //
    // WITHOUT A CEILING WRITTEN DOWN, the twelfth row costs nothing to add and
    // nobody notices until it is the fourteenth. This is that ceiling: ink over
    // a fully lit HUD -- every field this struct has, at once, which no real
    // frame ever shows -- counted the same way --nohud counts it. It measures
    // 5.78% and the bar is at 6.5%, which leaves room for a row and not for
    // four.
    //
    // It is INK and not claimed area on purpose: ink is a diff and cannot be
    // argued with. The claimed figure -- rows closed up into boxes, which is
    // the space you actually lose -- went 8.19% to 5.13% on the street frame
    // and is in docs/HUD-REAL-ESTATE.md with the commands that produce it.
    HudState full;
    full.health = 100;
    full.yawBam = sim::kFacingWest;
    full.timeOfDaySeconds = 20 * 3600;
    full.coin = 4071;
    full.standingLabel = "THE WARD WANTS YOU GONE";
    full.heatLabel = "WANTED  HEAT 84  LOOT 12";
    full.stashLabel = "3 FLOWER  240DR";
    full.stealthLabel = "HIDDEN CROUCH  DARK 4  QUIET";
    full.caseLabel = "CASE 4/6 > THE DROWNED HOLD";
    full.guildLabel = "THE SKYRUNNERS - SKYRUNNER";
    full.objectiveLabel = "TAKE THE BALE PAST THE WEIGHHOUSE";
    full.rivalLabel = "RIVAL BRAM MARROW - CRAFTLORD x7";
    full.roomLabel = "THE GULL  14 IN  LOUD";

    Framebuffer bare(960, 540);
    bare.clear(Rgb{0.20F, 0.18F, 0.16F});
    Framebuffer dressed(960, 540);
    dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawHud(dressed, full);
    std::size_t ink = 0;
    for (std::size_t i = 0; i < bare.pixels().size(); ++i) {
        ink += bare.pixels()[i] != dressed.pixels()[i] ? 1U : 0U;
    }
    const double fraction = static_cast<double>(ink) / static_cast<double>(bare.pixels().size());
    MESSAGE("a fully lit HUD at 960x540 is " << ink << " pixels, " << fraction * 100.0
                                             << "% of the frame");
    CHECK(ink > 2000);         // it drew, so the ceiling is not vacuous
    CHECK(fraction < 0.065);   // and every row it has fits in a sixteenth of the screen
}

TEST_CASE("the HUD's health bar tracks the number it is given") {
    // HARDENING PASS. THE BAR IS NO LONGER ONE FIXED RED, so "count the red
    // pixels" stopped being a legal way to measure how many segments are lit
    // -- a full bar is GREEN now, and a hue check tuned for the old constant
    // colour would just read a fully healthy player as an empty bar. Counted
    // by SATURATION instead (max channel minus min channel): every stop of
    // the health gradient (green, amber, red) is well clear of the muted
    // bronze frame border and the near-black backing plate, which is what a
    // segment count needs to stay a fact about how many segments are FILLED,
    // not about which colour they happen to be filled with.
    //
    // SCOPED TO THE BAR'S OWN RECTANGLE, NOT THE WHOLE FRAME -- a whole-frame
    // scan also caught the compass ribbon's fixed north mark (a small,
    // equally saturated yellow tick drawn regardless of health), which is
    // exactly why the ORIGINAL red-only filter looked safe: red hue happened
    // to exclude that mark by accident, not by scoping. Health is default
    // HudState's only non-empty field here, so nothing else in the bar's own
    // bottom-left corner can light up.
    const auto litPixels = [](int health) {
        Framebuffer frame(320, 180);
        frame.clear(Rgb{0.0F, 0.0F, 0.0F});
        HudState hud;
        hud.health = health;
        drawHud(frame, hud);
        const int scale = std::max(1, frame.height() / 180);
        const int barX0 = 6 * scale - scale;               // band.margin() - scale (frame ring)
        const int barY0 = frame.height() - 12 * scale - scale;
        const int barX1 = 6 * scale + 48 * scale + scale;   // margin + barWidth + scale
        const int barY1 = frame.height() - 6 * scale + scale;
        std::size_t lit = 0;
        for (int y = barY0; y < barY1; ++y) {
            for (int x = barX0; x < barX1; ++x) {
                const Rgb colour = unpackRgb(frame.pixels()[frame.index(x, y)]);
                const float lo = std::min({colour.r, colour.g, colour.b});
                const float hi = std::max({colour.r, colour.g, colour.b});
                if (hi - lo > 0.3F) {
                    ++lit;
                }
            }
        }
        return lit;
    };
    CHECK(litPixels(100) > litPixels(50));
    CHECK(litPixels(50) > litPixels(10));
    CHECK(litPixels(0) == 0);
}

TEST_CASE("the health bar's own colour answers danger at a glance, not just the segment count") {
    // TASK #4 OF THE HARDENING BRIEF: full health and near-death used to
    // render identically -- a fixed red bar regardless of the number behind
    // it. This is the glance test: sample the leftmost filled segment (the
    // one pixel every fill level from 1 to 100 lights) and check its own
    // colour, not merely whether it is lit.
    const auto firstSegmentColour = [](int health) {
        Framebuffer frame(320, 180);
        frame.clear(Rgb{0.0F, 0.0F, 0.0F});
        HudState hud;
        hud.health = health;
        drawHud(frame, hud);
        // The bar's own top-left corner interior, one pixel in from the
        // frame border drawHealth() paints -- see its own header on the
        // margin()/scale() geometry this mirrors. Always inside segment 0
        // for any health > 0, at every resolution hudScale() can produce.
        const int scale = std::max(1, frame.height() / 180);
        const int x = 6 * scale + 1;
        const int y = frame.height() - 6 * scale - 6 * scale + 1;
        return unpackRgb(frame.pixels()[frame.index(x, y)]);
    };
    const Rgb full = firstSegmentColour(100);
    const Rgb dying = firstSegmentColour(4);
    INFO("full: (", full.r, ", ", full.g, ", ", full.b, ")  dying: (", dying.r, ", ", dying.g,
         ", ", dying.b, ")");
    // Full health reads GREENER than red (g outweighs r); near death reads
    // the other way round -- the near-universal genre convention the brief
    // asks for, checkable without pinning the exact gradient stops.
    CHECK(full.g > full.r);
    CHECK(dying.r > dying.g);
    // And the two are not the same colour -- the gradient actually moves.
    CHECK((full.r != dying.r || full.g != dying.g || full.b != dying.b));
}

TEST_CASE("the compass names the direction the body is facing") {
    // All eight points, not two. S1 checked N and W and then asserted
    // `body.yaw() == kSpawnYaw` -- which session.cpp assigns from that same
    // constant -- under a title about the compass, and never drew one.
    CHECK(sim::compass_point(sim::kFacingNorth) == "N");
    CHECK(sim::compass_point(sim::kFacingEast) == "E");
    CHECK(sim::compass_point(sim::kFacingSouth) == "S");
    CHECK(sim::compass_point(sim::kFacingWest) == "W");
    CHECK(sim::compass_point(sim::kFacingNorth + sim::kTurnFull / 8) == "NE");
    CHECK(sim::compass_point(sim::kFacingEast + sim::kTurnFull / 8) == "SE");
    CHECK(sim::compass_point(sim::kFacingSouth + sim::kTurnFull / 8) == "SW");
    CHECK(sim::compass_point(sim::kFacingWest + sim::kTurnFull / 8) == "NW");
    // It rounds to the nearest point rather than truncating toward one.
    CHECK(sim::compass_point(sim::kFacingNorth + sim::kTurnFull / 32) == "N");
    CHECK(sim::compass_point(sim::kTurnFull - sim::kTurnFull / 32) == "N");

    // And the drawn needle tracks the BAM it is handed. Two headings, one
    // frame each, and the strip the compass lives in must not be identical --
    // otherwise the ribbon is a picture and the yaw is decoration.
    const auto compassStrip = [](std::int32_t yawBam) {
        Framebuffer frame(320, 180);
        frame.clear(Rgb{0.0F, 0.0F, 0.0F});
        HudState hud;
        hud.yawBam = yawBam;
        drawHud(frame, hud);
        std::vector<std::uint32_t> strip;
        for (int y = 0; y < 24; ++y) {
            for (int x = 80; x < 240; ++x) {
                strip.push_back(frame.pixels()[frame.index(x, y)]);
            }
        }
        return strip;
    };
    CHECK(compassStrip(sim::kFacingNorth) != compassStrip(sim::kFacingEast));
    CHECK(compassStrip(sim::kFacingNorth) == compassStrip(sim::kFacingNorth));
    // A degree of turn moves it; a full turn brings it back to the same pixels.
    CHECK(compassStrip(sim::kFacingNorth) ==
          compassStrip(sim::kFacingNorth + sim::kTurnFull));
}

TEST_CASE("the capture path produces a PNG with no window anywhere in sight") {
    SmokeRunConfig config;
    config.session = docksAt(20);
    config.steps = 45;
    config.captureScale = 1;
    config.screenshot =
        std::filesystem::temp_directory_path() / "granadad-test-capture.png";
    std::filesystem::remove(config.screenshot);

    const SmokeRunResult result = runSmoke(config);
    CHECK(result.ok);
    CHECK(result.lampCount == 28);
    REQUIRE(std::filesystem::exists(config.screenshot));
    // A real PNG, not an empty file: the 8-byte signature plus a chunk or two.
    CHECK(std::filesystem::file_size(config.screenshot) > 1000);

    // The body walked while the smoke ran, which is what makes a capture a
    // picture of the game rather than a picture of the spawn.
    CHECK((result.endTileX != sim::docks::kSpawnTileX ||
           result.endTileY != sim::docks::kSpawnTileY));
    CHECK(result.endBand == sim::docks::kBandQuayside);
    std::filesystem::remove(config.screenshot);
}

TEST_CASE("the capture upscales with nearest neighbour and nothing else") {
    Framebuffer small(4, 4);
    small.clear(Rgb{0.0F, 0.0F, 0.0F});
    small.set(1, 1, Rgb{1.0F, 1.0F, 1.0F}, 1.0F);
    const Framebuffer big = upscaleNearest(small, 3);
    CHECK(big.width() == 12);
    CHECK(big.height() == 12);
    // The white texel becomes a hard 3x3 block with no blur anywhere near it.
    for (int y = 3; y < 6; ++y) {
        for (int x = 3; x < 6; ++x) {
            REQUIRE(big.pixels()[big.index(x, y)] == small.pixels()[small.index(1, 1)]);
        }
    }
    CHECK(big.pixels()[big.index(2, 3)] == small.pixels()[small.index(0, 1)]);
    CHECK(big.pixels()[big.index(6, 3)] == small.pixels()[small.index(2, 1)]);
}

TEST_CASE("walking north off the Tarwalk stops the body at the quay lip") {
    // The walk half of what used to be "walking to the water's edge keeps the
    // harbour in front of the eye". What was a deck two feet under the eye is
    // open harbour two levels down and thirty tiles wide; that the lower half of
    // the 3D frame darkens and recedes there is asserted in test_light_model.cpp,
    // against the real frame. What is left here is the behavioural check on the
    // body: it walks, and it stops at the lip rather than in the water.
    //
    // IT STARTS FROM ITS OWN TILE AND NOT FROM THE SPAWN (#79):
    // docks::kQuayApproach is the open apron this walk needs, named where the
    // spawn is named.
    SessionConfig config = docksAt(20);
    config.spawnX = sim::docks::kQuayApproachX;
    config.spawnY = sim::docks::kQuayApproachY;
    config.spawnBand = sim::docks::kBandQuayside;
    config.hud = false;
    Session session(config);
    session.body().setYaw(sim::kFacingNorth);

    sim::MoveInput forward;
    forward.forward = 1;
    session.stepMany(forward, 8 * sim::kStepsPerSecond);

    CHECK(session.body().tileY() < sim::docks::kQuayApproachY);
    CHECK(session.body().band() == sim::docks::kBandQuayside);
}

// ===========================================================================
// THE TOPIC LIST -- the S3 review's second and third findings, closed
// ===========================================================================

namespace {

/// Draws a surface over a flat frame and answers whether the exclusion
/// rectangle came out untouched. The same proof the S3 case uses, in a helper,
/// because S4 has three more surfaces to hold to it.
[[nodiscard]] bool centreUntouched(const DialogueViewState& view, int width, int height) {
    Framebuffer bare(width, height);
    bare.clear(Rgb{0.20F, 0.18F, 0.16F});
    Framebuffer dressed(width, height);
    dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawDialogue(dressed, view);
    const CentreRect centre = hudCentreRect(width, height);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            if (bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]) {
                return false;
            }
        }
    }
    return true;
}

/// How many pixels a surface actually changed. Keeps every check above from
/// passing because nothing drew at all.
[[nodiscard]] std::size_t inkOf(const DialogueViewState& view, int width, int height) {
    Framebuffer bare(width, height);
    bare.clear(Rgb{0.20F, 0.18F, 0.16F});
    Framebuffer dressed(width, height);
    dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawDialogue(dressed, view);
    std::size_t changed = 0;
    for (std::size_t i = 0; i < bare.pixels().size(); ++i) {
        changed += bare.pixels()[i] != dressed.pixels()[i] ? 1U : 0U;
    }
    return changed;
}

}  // namespace

TEST_CASE("every topic is reachable by a number printed beside it") {
    // S3 numbered twelve slots 1-9 and then printed a full stop for the rest,
    // reachable only by arrow keys with nothing on screen saying so; and the
    // thirteenth topic would have vanished with no ellipsis at all. Master Venn
    // already filled all twelve. Both findings are closed by paging.
    CHECK(topicPageCount(0) == 1);
    CHECK(topicPageCount(1) == 1);
    CHECK(topicPageCount(9) == 1);
    CHECK(topicPageCount(10) == 2);
    CHECK(topicPageCount(18) == 2);
    CHECK(topicPageCount(19) == 3);
    CHECK_FALSE(topicsPaginate(9));
    CHECK(topicsPaginate(10));

    // NO TOPIC IS EVER DROPPED: every index of a list of any length lands on
    // exactly one page, in a slot that has a key printed on it.
    for (int count = 1; count <= 40; ++count) {
        for (int i = 0; i < count; ++i) {
            const int page = topicPageOf(i);
            const int slot = i - page * kTopicPageSize;
            CHECK(page >= 0);
            CHECK(page < topicPageCount(static_cast<std::size_t>(count)));
            CHECK(slot >= 0);
            CHECK(slot < kTopicPageSize);
        }
    }

    // A page and its MORE row fit the grid, so a page is never short of room.
    CHECK(kTopicPageSize + 1 <= kTopicSlots);
}

TEST_CASE("page two of a long list DRAWS nine numbered rows, not none") {
    // THE CASE S4 SHOULD HAVE HAD, and the S4 review is why it exists.
    //
    // S4 closed "topics past the ninth were unreachable" with the arithmetic
    // case above, which never touches the drawing code. The review reinstated
    // the original bug in dialogue_view.cpp -- `std::min(total,
    // kTopicPageSize)` where the real line reads `std::min(total, first +
    // kTopicPageSize)`, which makes page two and page three draw ZERO topic
    // rows -- and the whole 311-case gate stayed green, because the neighbour
    // below only asserts that SOME ink is on screen and an empty page still has
    // a speaker header on it.
    //
    // topicRowsFor IS the drawing path: drawDialogue calls it and prints
    // exactly what it returns. That mutation empties the vector this asserts.
    std::vector<std::string> topics;
    for (int i = 0; i < 20; ++i) {
        topics.push_back("ASK ABOUT SOMETHING NUMBER " + std::to_string(i + 1));
    }
    const int capacity = kTopicSlots;

    // Page one: nine numbered rows plus the row that names the key which turns
    // the page.
    const std::vector<TopicRow> first = topicRowsFor(topics, 0, 0, capacity);
    REQUIRE(first.size() == static_cast<std::size_t>(kTopicPageSize) + 1);
    for (int i = 0; i < kTopicPageSize; ++i) {
        const std::string& label = first[static_cast<std::size_t>(i)].label;
        INFO("page 1 row ", i, " = ", label);
        REQUIRE(label.rfind(std::to_string(i + 1) + " ", 0) == 0);
        REQUIRE(label.find(topics[static_cast<std::size_t>(i)]) != std::string::npos);
    }
    CHECK(first.back().label == "0 MORE (1/3)");
    CHECK(first.front().picked);

    // PAGE TWO. Nine rows again, numbered 1..9 again -- the number is the KEY,
    // and the key is a slot on the visible page -- each carrying the tenth to
    // eighteenth topic of the list.
    const std::vector<TopicRow> second = topicRowsFor(topics, 1, kTopicPageSize, capacity);
    REQUIRE(second.size() == static_cast<std::size_t>(kTopicPageSize) + 1);
    for (int i = 0; i < kTopicPageSize; ++i) {
        const std::string& label = second[static_cast<std::size_t>(i)].label;
        INFO("page 2 row ", i, " = ", label);
        REQUIRE(label.rfind(std::to_string(i + 1) + " ", 0) == 0);
        REQUIRE(label.find(topics[static_cast<std::size_t>(kTopicPageSize + i)]) !=
                std::string::npos);
    }
    CHECK(second.back().label == "0 MORE (2/3)");
    CHECK(second.front().picked);

    // Page three is the short one: two topics and the MORE row.
    const std::vector<TopicRow> third = topicRowsFor(topics, 2, 2 * kTopicPageSize, capacity);
    REQUIRE(third.size() == 3);
    CHECK(third[0].label.rfind("1 ", 0) == 0);
    CHECK(third[1].label.rfind("2 ", 0) == 0);
    CHECK(third.back().label == "0 MORE (3/3)");

    // Every topic of the list appears on exactly one page, in a row with a key
    // printed on it. This is the claim the whole paging scheme rests on.
    //
    // Matched on the WHOLE row and not on a substring of it: "...NUMBER 1" is a
    // prefix of "...NUMBER 12", and the first version of this counted every
    // topic three times and told me the paging was broken when it was not.
    for (const std::string& topic : topics) {
        int seen = 0;
        for (int page = 0; page < topicPageCount(topics.size()); ++page) {
            for (const TopicRow& row : topicRowsFor(topics, page, -1, capacity)) {
                const std::size_t space = row.label.find(' ');
                if (space != std::string::npos && row.label.substr(space + 1) == topic) {
                    ++seen;
                }
            }
        }
        INFO("topic ", topic);
        REQUIRE(seen == 1);
    }

    // A single page of nine has no MORE row at all.
    const std::vector<std::string> few(topics.begin(), topics.begin() + kTopicPageSize);
    const std::vector<TopicRow> only = topicRowsFor(few, 0, 0, capacity);
    CHECK(only.size() == static_cast<std::size_t>(kTopicPageSize));
}

TEST_CASE("a topic label stops short of the next column's key") {
    // S4's own headline frame shows topic 5 reading "ASK TO BE MADE SHE" with
    // the next column's "9" jammed against the E. A row is drawn at x + half a
    // glyph and the next column's cursor arrow at x - half a glyph, so a label
    // sized to the whole column overruns it by a glyph and collides with the
    // arrow after it. Two glyphs back, and the arithmetic is pinned here.
    for (const int height : {180, 360}) {
        const int width = height * 16 / 9;
        const int scale = std::max(1, height / 180);
        const int margin = 5 * scale;
        const int glyphAdvance = 5 * scale;
        const int columnWidth = (width - 2 * margin) / kTopicColumns;
        const int room = std::max(1, columnWidth / glyphAdvance - 2);
        const int labelEnds = glyphAdvance / 2 + room * glyphAdvance;
        const int nextArrowStarts = columnWidth - glyphAdvance / 2;
        INFO("height ", height, " label ends ", labelEnds, " arrow at ", nextArrowStarts);
        REQUIRE(labelEnds <= nextArrowStarts);
    }
}

TEST_CASE("a long topic list draws its page, says there is more, and keeps the centre clear") {
    DialogueViewState view;
    view.open = true;
    view.speaker = "MASTER VENN";
    view.epithet = "LANDLORD OF THE GILDED GULL";
    view.attitude = "WARM";
    view.line = "A BED IS TWELVE AND IT COMES WITH THE DOOR BOLTED.";
    // Twenty topics: three pages, and more than the grid can hold at once.
    for (int i = 0; i < 20; ++i) {
        view.topics.push_back("ASK ABOUT SOMETHING NUMBER " + std::to_string(i + 1));
    }

    for (const int height : {180, 360}) {
        const int width = height * 16 / 9;
        view.page = 0;
        view.cursor = 0;
        CHECK(centreUntouched(view, width, height));
        const std::size_t firstInk = inkOf(view, width, height);
        CHECK(firstInk > 0);

        view.page = 2;
        view.cursor = 2 * kTopicPageSize;
        CHECK(centreUntouched(view, width, height));
        CHECK(inkOf(view, width, height) > 0);
    }

    // Two different pages of the same list are two different pictures, so the
    // MORE key is doing something a player can see.
    Framebuffer first(640, 360);
    first.clear(Rgb{0.20F, 0.18F, 0.16F});
    view.page = 0;
    view.cursor = 0;
    drawDialogue(first, view);
    Framebuffer second(640, 360);
    second.clear(Rgb{0.20F, 0.18F, 0.16F});
    view.page = 1;
    view.cursor = kTopicPageSize;
    drawDialogue(second, view);
    bool differs = false;
    for (std::size_t i = 0; i < first.pixels().size(); ++i) {
        if (first.pixels()[i] != second.pixels()[i]) {
            differs = true;
            break;
        }
    }
    CHECK(differs);
}

TEST_CASE("the topic band is as deep as the list it holds and no deeper") {
    // polish-1. The bottom band claimed the deepest it could legally go
    // whichever speaker was in front of you, so a doorman with two things to
    // say took the same fifth of the frame as Master Venn with twelve. The top
    // band has been sized from what it draws since S3; this one never was.
    const auto bandTop = [](int topics) {
        DialogueViewState view;
        view.open = true;
        view.speaker = "GERTA";
        view.line = "WHAT.";
        for (int i = 0; i < topics; ++i) {
            view.topics.push_back("A THING " + std::to_string(i + 1));
        }
        Framebuffer bare(960, 540);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(960, 540);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawDialogue(dressed, view);
        const CentreRect centre = hudCentreRect(960, 540);
        for (int y = centre.y1; y < 540; ++y) {
            for (int x = 0; x < 960; ++x) {
                if (bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]) {
                    return y;
                }
            }
        }
        return 540;
    };
    const int shortList = bandTop(2);
    const int fullList = bandTop(20);
    INFO("two topics start the band at ", shortList, ", twenty at ", fullList);
    CHECK(shortList > fullList);
    // And the deep one still stops at the rectangle rather than inside it.
    CHECK(fullList >= hudCentreRect(960, 540).y1);
}

TEST_CASE("the workbench draws where a conversation is allowed to be") {
    DialogueViewState view;
    view.open = true;
    view.speaker = "FATHER MAELL";
    view.epithet = "OF THE MISSION";
    view.attitude = "FRIEND";
    view.line = "THE BOOKS STOP WHERE THE MONEY STOPPED. AFTER THAT YOU ARE COMPOSING.";
    view.forging = true;
    view.forgeCursor = 2;
    view.forgeDifficulty = 9;
    view.forgeCeiling = 21;
    view.forgeFields = {"MOVES: VITALITY", "SHAPE: INSTANT", "HOW MUCH: -2", "HOW LONG: 0T",
                        "ACROSS: TOUCH"};

    for (const int height : {180, 360}) {
        const int width = height * 16 / 9;
        CHECK(centreUntouched(view, width, height));
        CHECK(inkOf(view, width, height) > 0);
    }

    // A refused composition says so on the bench, before the priest has to --
    // and saying so is visible. Compared PIXEL BY PIXEL rather than by counting
    // changed pixels: the panel fill already covers every cell the text lands
    // in, so a count is the same number whatever the words are.
    Framebuffer hinted(640, 360);
    hinted.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawDialogue(hinted, view);
    view.forgeProblem = "A WOUND IS DELIVERED, NOT HELD.";
    Framebuffer complained(640, 360);
    complained.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawDialogue(complained, view);
    CHECK(centreUntouched(view, 640, 360));
    bool saysWhy = false;
    for (std::size_t i = 0; i < hinted.pixels().size(); ++i) {
        if (hinted.pixels()[i] != complained.pixels()[i]) {
            saysWhy = true;
            break;
        }
    }
    CHECK(saysWhy);
}

// ===========================================================================
// THE CROSSHAIR PASS -- the one element allowed in the middle, fenced
// ===========================================================================

namespace {

/// Every pixel drawHud() changed over a flat field, as a list of coordinates.
[[nodiscard]] std::vector<std::pair<int, int>> hudInk(const HudState& state, int width,
                                                      int height) {
    Framebuffer bare(width, height);
    bare.clear(Rgb{0.20F, 0.18F, 0.16F});
    Framebuffer dressed(width, height);
    dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawHud(dressed, state);
    std::vector<std::pair<int, int>> out;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]) {
                out.emplace_back(x, y);
            }
        }
    }
    return out;
}

/// The aim prompt at its loudest, with the longest strings session.cpp can
/// actually hand it: the casebook's longest authored `what` and a ward trade.
[[nodiscard]] HudState loudAim() {
    HudState aim;
    aim.showHealth = false;
    aim.showCompass = false;
    aim.timeOfDaySeconds = -1;
    aim.coin = -1;
    aim.aimKey = "E";
    aim.aimVerb = "PICKPOCKET";
    aim.aimSubject = "THE BODY, AND WHOEVER FOUND IT";
    aim.aimNote = "ALREADY READ";
    aim.aimKind = static_cast<int>(AimKind::Clue);
    return aim;
}

}  // namespace

TEST_CASE("the aim prompt is the only thing in the play space, and it stays in its fence") {
    // THE OWNER'S OWN INSTRUCTION, HELD TO A RECTANGLE. "when shown hover a
    // bit to the top-right of the center crosshair" is a direct order to break
    // the centre-clear rule, so it is broken in exactly one place and that
    // place is a clamp rather than a comment -- see hudAimRect().
    for (const auto& [width, height] : {std::pair{320, 180}, std::pair{640, 360},
                                        std::pair{960, 540}, std::pair{1920, 1080}}) {
        const HudState aim = loudAim();
        const CentreRect fence = hudAimRect(width, height);
        const std::vector<std::pair<int, int>> lit = hudInk(aim, width, height);

        INFO("at ", width, "x", height);
        // NOT VACUOUS: it drew something.
        CHECK(lit.size() > 200);
        std::size_t escaped = 0;
        for (const auto& [x, y] : lit) {
            if (x < fence.x0 || x >= fence.x1 || y < fence.y0 || y >= fence.y1) {
                ++escaped;
            }
        }
        CHECK(escaped == 0);

        // UP AND TO THE RIGHT, LITERALLY. Everything the PROMPT draws is above
        // the horizontal centre line and right of the vertical one; the only
        // ink below or left of centre belongs to the reticle's own two ticks,
        // which is what the prompt is hanging off.
        const int cx = width / 2;
        const int cy = height / 2;
        std::size_t textLeftOfCentre = 0;
        for (const auto& [x, y] : lit) {
            if (y < cy - hudMinorScale(width, height) * 4 && x < cx) {
                ++textLeftOfCentre;
            }
        }
        CHECK(textLeftOfCentre == 0);

        // AND THE EXACT AIM PIXEL IS NEVER PAINTED. A reticle that covered the
        // thing it points at would be a worse crosshair than none.
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(dressed, aim);
        CHECK(bare.pixels()[bare.index(cx, cy)] == dressed.pixels()[dressed.index(cx, cy)]);
    }
}

TEST_CASE("the verb row holds its place when a subject arrives and when it goes") {
    // THE REFERENCE'S "PANES HOLD THEIR HEIGHT", ON THE SMALLEST SURFACE IN
    // THE GAME. Sweeping the crosshair across a doorway changes what the
    // subject row says and whether there IS one; if that moved the verb, the
    // one row a player reads every second of play would jitter under their
    // eye. So the subject grows UPWARD off a verb row that never moves.
    constexpr int kWidth = 960;
    constexpr int kHeight = 540;

    HudState bare = loudAim();
    bare.aimSubject = {};
    bare.aimNote = {};
    bare.aimKind = static_cast<int>(AimKind::Nothing);

    const HudState named = loudAim();

    const auto lowestRow = [](const std::vector<std::pair<int, int>>& lit, int cy) {
        // The lowest text row: the ticks live within a few units of centre, so
        // anything above them is prompt.
        int top = 1 << 30;
        for (const auto& [x, y] : lit) {
            (void)x;
            if (y < cy - 8 && y < top) {
                top = y;
            }
        }
        return top;
    };

    const std::vector<std::pair<int, int>> withoutSubject = hudInk(bare, kWidth, kHeight);
    const std::vector<std::pair<int, int>> withSubject = hudInk(named, kWidth, kHeight);
    CHECK(withSubject.size() > withoutSubject.size());

    // THE VERB ROW'S OWN BAND IS PIXEL-IDENTICAL BETWEEN THE TWO. Compared as
    // a band rather than as a y, because the subject row genuinely does draw
    // above it and would otherwise be counted as movement.
    const CentreRect band = hudAimVerbRow(kWidth, kHeight);
    Framebuffer a(kWidth, kHeight);
    a.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawHud(a, bare);
    Framebuffer b(kWidth, kHeight);
    b.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawHud(b, named);
    // THE BAND'S OWN COLUMNS, NOT THE WHOLE ROW. hudAimVerbRow starts at the
    // prompt's left edge on purpose: the reticle's upper tick shares these
    // pixel rows and it is SUPPOSED to change -- it takes the subject's accent
    // when there is one, which is the reference's "highlight the current
    // interaction target". What must hold still is the verb's own text.
    std::size_t moved = 0;
    for (int y = band.y0; y < band.y1; ++y) {
        for (int x = band.x0; x < band.x1; ++x) {
            moved += a.pixels()[a.index(x, y)] != b.pixels()[b.index(x, y)] ? 1U : 0U;
        }
    }
    CHECK(moved == 0);

    // And the subject really did land ABOVE it rather than pushing it down.
    CHECK(lowestRow(withSubject, kHeight / 2) < lowestRow(withoutSubject, kHeight / 2));
}

TEST_CASE("an empty verb draws no reticle and no prompt at all") {
    // THIS IS WHAT KEEPS THE OLD GUARANTEE TRUE. Session empties the verb for
    // every page in the game (conversingNow), so under a menu, a map or a
    // conversation the middle of the screen is exactly as clear as it was
    // before this pass existed.
    HudState quiet = loudAim();
    quiet.aimVerb = {};
    for (const auto& [width, height] : {std::pair{320, 180}, std::pair{960, 540}}) {
        INFO("at ", width, "x", height);
        CHECK(hudInk(quiet, width, height).empty());
    }
}
