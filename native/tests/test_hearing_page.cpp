// JUSTICE BUILD (HEARING PAGE LANE) -- the Flame's bench as a page, the rope
// as the game's one end, and the two rows after it.
//
// THE RULING (Eli, 2026-09-02): "If the player is tagged as a criminal it
// should be like Daggerfall where you can go to court and you can face jail or
// execution (game over)." The sim half is test_court.cpp's; this file is the
// PRESENTATION -- JUSTICE-SPEC sections 5 and 6 -- proved the way
// test_casebook_page.cpp proves the book:
//
//   TAKEN      an arrest with paper has its moment ON SCREEN before the page:
//              the officer's line on the row with his hand on you, then the
//              plate -- TAKEN TO THE MISSION with the hour, pinned on the
//              rendered frame -- then the body at the Mission's door and the
//              page; the body cannot walk out of custody; the world verbs and
//              the pages are refused; PAUSE still opens over it with WAIT
//              refused.
//   THE PAGE   what it SAYS (the paper laid, the charge off the sheet, what the
//              paper asks, the rows, the consequence of the hovered row, the
//              sheet as phrases) and that it draws BYTE-IDENTICAL twice; both
//              pleas' screens; the geometry holding at every window the game
//              runs at with nothing clipped.
//   THE CHECK  the four-line block: the arithmetic, the plea's own term, the
//              lines, the verdict badge, the sentence in numbers, the priest's
//              word -- for a thief, for a murder, for a disbelieved denial.
//   THE GRAMMAR digits pick, arrows and the D-pad move, ENTER/A arm and
//              confirm, ESC/B disarm and close the paper and do nothing else;
//              every other key is swallowed; the sentence row waits its hold.
//   THE ROPE   the drop, the held plate that never fades, the rows, LEAVE and
//              A NEW MAN -- and a new man's creation flow and clean ledger.
//   THE HUD    WANTED / WANTED FOR BLOOD / CONDEMNED drawn on the frame.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/controls.hpp"
#include "granadad/render/creation.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hearing_page.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/map_view.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/barks.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/justice.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/social.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/watch.hpp"

using namespace granadad::sim;
namespace render = granadad::render;
namespace content = granadad::content;
using render::Key;
using render::Session;

namespace {

/// The five windows the game runs at (test_casebook_page's own list).
struct Window {
    int width;
    int height;
};
constexpr Window kWindows[] = {{320, 180}, {640, 360}, {960, 540}, {1280, 720}, {1920, 1080}};

[[nodiscard]] render::SessionConfig docksAt(int hour, int width = 320, int height = 180) {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.world = docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.spawnYaw = kFacingNorth;
    config.spawnYawGiven = true;
    config.width = width;
    config.height = height;
    return config;
}

/// Inside the Gilded Gull, at the bar, at an hour the Watch drinks.
[[nodiscard]] render::SessionConfig gullAt(int hour) {
    render::SessionConfig config = docksAt(hour);
    config.spawnX = gull::kBartenderX;
    config.spawnY = gull::kBarY + 2;
    config.spawnBand = gull::kGroundBand;
    return config;
}

[[nodiscard]] const Actor* actorNamed(const Session& session, std::string_view name) {
    for (const Actor& actor : session.tavern().actors()) {
        if (actor.name() == name && actor.present()) {
            return &actor;
        }
    }
    return nullptr;
}

/// Tells the room where the body is and which way it faces -- the step's
/// own sync (Session::syncTavernToBody is the step's and private), through
/// the tavern's public setters the way test_court's Room pushes its body.
void syncBody(Session& session) {
    session.tavern().setPlayer(session.body().x(), session.body().y(), session.body().band());
    session.tavern().setPlayerYaw(session.body().yaw());
}

/// Stands the body on `actor`'s own tile -- distance zero beats every
/// tie-break there is -- and tells the room so.
void standOn(Session& session, const Actor& actor) {
    session.placeBodyAt(actor.tileX(), actor.tileY(), actor.band());
    syncBody(session);
}

/// Stands where `watcher` can SEE the body by the three-clause notice rule
/// -- his own tile first, then a standable neighbour -- asked of the room
/// itself rather than assumed. False when no such tile is offered.
[[nodiscard]] bool standInSightOf(Session& session, const Actor& watcher) {
    const std::int32_t offsets[5][2] = {{0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    for (const auto& o : offsets) {
        const std::int32_t px = watcher.tileX() + o[0];
        const std::int32_t py = watcher.tileY() + o[1];
        if (!session.tiles().standable(px, py, watcher.band())) {
            continue;
        }
        session.placeBodyAt(px, py, watcher.band());
        syncBody(session);
        if (session.tavern().noticeBy(watcher).seen) {
            return true;
        }
    }
    return false;
}

/// The first present, upright body on the look-ray, the projection VETO 1
/// specifies -- test_court's own helper, read off a Session's body.
[[nodiscard]] std::int32_t sightlineId(const Session& session) {
    const PlayerBody& body = session.body();
    const std::int64_t fx = forward_x_q16(body.yaw());
    const std::int64_t fy = forward_y_q16(body.yaw());
    std::int32_t best = -1;
    std::int64_t bestAlong = static_cast<std::int64_t>(kMeleeReach) + 1;
    for (const Actor& actor : session.tavern().actors()) {
        if (!actor.present() || isFloored(actor.activity())) {
            continue;
        }
        const std::int64_t dx = static_cast<std::int64_t>(actor.x()) - body.x();
        const std::int64_t dy = static_cast<std::int64_t>(actor.y()) - body.y();
        const std::int64_t along = (fx * dx + fy * dy) >> 16;
        if (along <= 0 || along > kMeleeReach) {
            continue;
        }
        const std::int64_t perp = (-fy * dx + fx * dy) >> 16;
        if (perp > kBodyHalfWidth || perp < -kBodyHalfWidth) {
            continue;
        }
        if (along < bestAlong) {
            bestAlong = along;
            best = actor.id();
        }
    }
    return best;
}

/// One tile off `mark` on a standable cardinal side, facing it.
[[nodiscard]] bool standFacing(Session& session, const Actor& mark) {
    struct Side {
        std::int32_t dx;
        std::int32_t dy;
        Angle yaw;
    };
    const Side sides[] = {
        {0, -1, kFacingSouth}, {0, 1, kFacingNorth}, {-1, 0, kFacingEast}, {1, 0, kFacingWest},
    };
    for (const Side& s : sides) {
        const std::int32_t px = mark.tileX() + s.dx;
        const std::int32_t py = mark.tileY() + s.dy;
        if (session.tiles().standable(px, py, mark.band())) {
            session.placeBodyAt(px, py, mark.band());
            session.body().setYaw(s.yaw);
            syncBody(session);
            return true;
        }
    }
    return false;
}

/// Puts `markId` down for good under lethal rules through the Session's own
/// Attack verbs -- the keypress's exact path -- in as many hard swings as
/// it takes. test_court's killInSight, one layer up.
[[nodiscard]] bool killInSight(Session& session, std::int32_t markId) {
    Tavern& tavern = session.tavern();
    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    for (int swings = 0; swings < 40; ++swings) {
        const Actor* mark = tavern.actorById(markId);
        if (mark == nullptr) {
            return false;
        }
        if (mark->activity() == Activity::Dead) {
            return true;
        }
        for (int i = 0; i < kRecoilSteps + kBlockStaggerSteps + kHardSwingRecoverySteps + 8 &&
                        (tavern.playerRecoilSteps() > 0 || tavern.playerBlockStaggerSteps() > 0 ||
                         !tavern.playerCombatIdle());
             ++i) {
            session.stepMany(MoveInput{}, 1);
        }
        if (!standFacing(session, *mark)) {
            return false;
        }
        for (int i = 0; i < 30 && sightlineId(session) != markId; ++i) {
            session.stepMany(MoveInput{}, 1);
            if (!standFacing(session, *mark)) {
                return false;
            }
        }
        session.attackDown();
        session.stepMany(MoveInput{}, kHardSwingHoldSteps + 1);
        session.attackUp();
        session.stepMany(MoveInput{}, 1);
    }
    return false;
}

/// A hearing seeded on the ledger in the arrest's own shape -- the sheet
/// written draw-free, the hearing opened with the officer -- and one step so
/// the page derives itself from the record.
void seedHearing(Session& session, std::uint64_t draw, std::int32_t streetwise = 12,
                 std::int32_t temple = 30, std::int32_t reputation = 0, bool skyrunner = false) {
    CrimeLedger& crimes = session.tavern().dialogue().crimes();
    const ChargeSheet sheet = crimes.charge(skyrunner, streetwise, temple, reputation);
    REQUIRE(sheet.written);
    crimes.openHearing(sheet, 0, draw, "Watchman Cull");
    REQUIRE(session.tavern().hearingPending());
    session.stepMany(MoveInput{}, 1);
    REQUIRE(session.courtOpen());
}

/// The spec's own first-time thief: two lifts and a cracked box, heat 68.
void thiefLedger(Session& session) {
    CrimeLedger& crimes = session.tavern().dialogue().crimes();
    crimes.commit(Crime::Lift, true);
    crimes.commit(Crime::Lift, true);
    crimes.commit(Crime::Burgle, true);
    while (crimes.heat() < 68) {
        crimes.addHeat(1);
    }
    REQUIRE(crimes.warrant());
    REQUIRE(crimes.heat() == 68);
}

/// The draw whose band above the nights reads exactly `jitter` (-10..+10),
/// the low residue `nights` (0..48) -- test_court's drawWithBand with the
/// nights kept.
[[nodiscard]] std::uint64_t drawOf(std::int32_t jitter, std::uint64_t nights = 0) {
    return (static_cast<std::uint64_t>(jitter + kPriestBand) << kPriestBandShift) | nights;
}

/// Presses the page's keys the way the client's router does, and answers
/// whether the page took each of them.
[[nodiscard]] bool press(Session& session, Key key) { return session.routeCourtKey(key); }

[[nodiscard]] render::Framebuffer shot(Session& session, int width, int height) {
    render::Framebuffer frame(width, height);
    session.drawFrame(frame);
    return frame;
}

[[nodiscard]] bool same(const render::Framebuffer& a, const render::Framebuffer& b) {
    return a.width() == b.width() && a.height() == b.height() && a.pixels() == b.pixels();
}

/// Every character drawable, and no identifier where a sentence belongs --
/// test_copy.cpp's two rules, applied to what the bench says.
void mustRead(std::string_view what, std::string_view text) {
    for (const char c : text) {
        INFO("in ", what, ": ", text);
        INFO("undrawable character: '", c, "' (", static_cast<int>(c), ")");
        CHECK(render::isDrawableGlyph(c));
    }
    CHECK(text.find('_') == std::string_view::npos);
}

}  // namespace

// ===========================================================================
// TAKEN
// ===========================================================================

TEST_CASE("TAKEN: an arrest with paper puts the body at the Mission's door, says the hour, opens the page, and the body cannot walk out of custody") {
    Session session(gullAt(23));
    Tavern& gull = session.tavern();
    CrimeLedger& crimes = gull.dialogue().crimes();
    session.stepMany(MoveInput{}, 2);
    REQUIRE(gull.playerInside());
    const Actor* cull = actorNamed(session, "Watchman Cull");
    REQUIRE_MESSAGE(cull != nullptr, "the Watch drinks in the Gull after ten");
    const std::int32_t cullId = cull->id();

    // Paper out, and steel up in his sight: the feel build's own Closing on
    // VIOLENCE (no glance gate, no die), then taken at reach -- test_court's
    // proven shape, driven through the Session's own guard press.
    crimes.commit(Crime::Lift, true);
    crimes.addHeat(kWarrantAt + 2);
    REQUIRE(crimes.warrant());
    REQUIRE(standInSightOf(session, *cull));
    session.stepMany(MoveInput{}, kStepsPerSecond);
    gull.setPlayerCombat(Weapon::Edged, Intent::Subdue);
    session.setBlocking(true);
    session.stepMany(MoveInput{}, 1);
    session.setBlocking(false);
    session.stepMany(MoveInput{}, 1);
    REQUIRE(gull.playerHandsUp());
    bool taken = false;
    for (int second = 0; second < 120 && !taken; ++second) {
        if (const Actor* him = gull.actorById(cullId); him != nullptr && him->present()) {
            standOn(session, *him);
        }
        session.stepMany(MoveInput{}, kStepsPerSecond);
        taken = gull.lastArrest().happened;
    }
    REQUIRE_MESSAGE(taken, "Cull never took the player at reach");
    REQUIRE(gull.lastArrest().sentence == Sentence::Held);

    // THE ARREST HAS ITS MOMENT ON SCREEN, before the bench does anything.
    // BEAT ONE, off the release step() read: the officer's own line -- the
    // room's watch.held row, his name on it -- said on the alert row, his
    // hand on you, the room still around you: the page is NOT up, the body
    // is still in the Gull, and the beat holds.
    REQUIRE(session.takenBeatUp());
    CHECK(session.inCustody());
    CHECK_FALSE(session.courtOpen());
    CHECK_FALSE(session.takenPlateUp());
    CHECK(gull.playerInside());
    CHECK(gull.hearingPending());
    CHECK(session.lastMessage() == gull.lastArrest().line);
    CHECK(session.lastMessage().rfind("Watchman Cull: ", 0) == 0);
    mustRead("the officer's line", session.lastMessage());
    // Nothing reaches the body or the world from under his hand: a held
    // forward key moves nothing, the pages are refused, ENTER is swallowed.
    {
        MoveInput walk;
        walk.forward = 1;
        const std::int32_t wx = session.body().tileX();
        const std::int32_t wy = session.body().tileY();
        session.stepMany(walk, 10);
        CHECK(session.body().tileX() == wx);
        CHECK(session.body().tileY() == wy);
        CHECK(press(session, Key::Enter));
        CHECK(press(session, Key::Num1));
        CHECK_FALSE(press(session, Key::Escape));  // Pause falls through, as from the page
        session.toggleCasebook();
        CHECK_FALSE(session.casebookOpen());
        // PAUSE opens over the beat (a player can always quit the game), and
        // its WAIT row is the refusal itself, in the officer's own words --
        // read on the row and said when pressed, the beat still running.
        session.togglePause();
        REQUIRE(session.pauseOpen());
        const std::vector<std::string> rows = session.pauseRows();
        REQUIRE(rows.size() == 5);
        CHECK(rows[1] == "THE WATCH HAS YOU.");
        session.movePauseCursor(1);
        session.choosePause();
        CHECK(session.pauseOpen());
        CHECK_FALSE(session.waitOpen());
        CHECK(session.lastMessage() == "THE WATCH HAS YOU.");
        session.closeConversation();
        CHECK_FALSE(session.pauseOpen());
        CHECK(session.takenBeatUp());
    }
    // BEAT TWO, THE PLATE: the beat spent to the cut, and the cut is an
    // instant black with the one line the spec promised on it, said (the
    // message row carries it) and DRAWN -- the rendered frame is the plate
    // byte for byte against the plate composed alone over black, and it is
    // not black alone. It holds for its whole hold, and the page is still
    // not up.
    for (int i = 0; i < render::kTakenOfficerSteps + 2 && !session.takenPlateUp(); ++i) {
        session.stepMany(MoveInput{}, 1);
    }
    REQUIRE(session.takenPlateUp());
    CHECK_FALSE(session.takenBeatUp());
    CHECK_FALSE(session.courtOpen());
    CHECK(session.inCustody());
    const std::string plate = session.takenPlate();
    INFO("plate: ", plate);
    CHECK(plate == session.lastMessage());
    CHECK(plate.rfind("TAKEN TO THE MISSION. ", 0) == 0);
    CHECK(plate.size() == std::string_view("TAKEN TO THE MISSION. 23:40.").size());
    CHECK(plate.back() == '.');
    CHECK(plate[plate.size() - 4] == ':');
    mustRead("plate", plate);
    {
        const render::Framebuffer drawn = shot(session, 320, 180);
        render::Framebuffer black(320, 180);
        black.fillRect(0, 0, 320, 180, render::Rgb{0.0F, 0.0F, 0.0F}, 1.0F);
        render::Framebuffer expected(320, 180);
        expected.fillRect(0, 0, 320, 180, render::Rgb{0.0F, 0.0F, 0.0F}, 1.0F);
        session.composeTakenPlate(expected);
        CHECK(same(drawn, expected));
        CHECK_FALSE(same(drawn, black));
        // The line is on the frame's middle band and nowhere else: ink above
        // and below the centre is black.
        bool inkMid = false;
        bool inkTop = false;
        for (int y = 0; y < 180; ++y) {
            for (int x = 0; x < 320; ++x) {
                const bool ink = drawn.pixels()[drawn.index(x, y)] != black.pixels()[black.index(x, y)];
                if (ink && y >= 60 && y < 120) {
                    inkMid = true;
                } else if (ink) {
                    inkTop = true;
                }
            }
        }
        CHECK(inkMid);
        CHECK_FALSE(inkTop);
        // And it holds: the same frame halfway through the hold.
        session.stepMany(MoveInput{}, render::kTakenPlateSteps / 2);
        REQUIRE(session.takenPlateUp());
        CHECK(same(drawn, shot(session, 320, 180)));
        CHECK_FALSE(session.courtOpen());
    }
    // THE PAGE, when the plate's hold runs out: the body at the Mission's
    // own arrival tile (the sign's aim point, snapped to ground -- outside
    // the Gull, inside the Mission's footprint or a tile off its door), the
    // seam dipped, the line still on the row with the live minute on it.
    for (int i = 0; i < render::kTakenPlateSteps + 2 && !session.courtOpen(); ++i) {
        session.stepMany(MoveInput{}, 1);
    }
    CHECK(session.courtOpen());
    CHECK_FALSE(session.takenPlateUp());
    CHECK_FALSE(session.takenBeatUp());
    CHECK(session.takenPlate().empty());
    CHECK(session.inCustody());
    CHECK(gull.hearingPending());
    CHECK(gull.hearing().awaitingPlea());
    CHECK_FALSE(gull.playerInside());
    const int at = render::mapPlaceIndex("Mission of the Flame");
    REQUIRE(at >= 0);
    const render::MapPlace& mission = render::mapPlaces()[static_cast<std::size_t>(at)];
    const std::int32_t bx = session.body().tileX();
    const std::int32_t by = session.body().tileY();
    INFO("body at ", bx, ",", by, " mission ", mission.x0, "..", mission.x1, " x ", mission.y0,
         "..", mission.y1);
    CHECK(bx >= mission.x0 - 8);
    CHECK(bx <= mission.x1 + 8);
    CHECK(by >= mission.y0 - 8);
    CHECK(by <= mission.y1 + 8);
    CHECK(session.body().band() == mission.band);
    const std::string& line = session.lastMessage();
    INFO("line: ", line);
    CHECK(line.rfind("TAKEN TO THE MISSION. ", 0) == 0);
    CHECK(line.size() == std::string_view("TAKEN TO THE MISSION. 23:40.").size());
    CHECK(line.back() == '.');
    CHECK(line[line.size() - 4] == ':');
    CHECK(session.cutVeil() > 0.0F);  // the dip
    CHECK_FALSE(gull.playerHandsUp());       // taken: the hands come down

    // THE READING, off the record: the officer who laid it, the one line the
    // sheet names, what the paper asks.
    const render::HearingPageState page = session.hearingPageState();
    CHECK(page.open);
    CHECK(page.title == "THE MISSION -- A HEARING");
    CHECK(page.laid == "WATCHMAN CULL LAYS THE PAPER ON THE TABLE.");
    CHECK(page.charge == "THE WARD HAS YOU FOR A LIFT.");
    CHECK(page.asks == "THE PAPER ASKS FOR A CELL.");
    CHECK(page.view == render::HearingView::Plea);
    REQUIRE(page.rows.size() == 3);
    CHECK(page.rows[0].key == "1");
    CHECK(page.rows[0].label == "I DID IT.");
    CHECK(page.rows[1].label == "I DID NOT.");
    CHECK(page.rows[2].label == "HEAR THE PAPER");
    CHECK(page.readout.rfind("DAY 1  ", 0) == 0);
    CHECK_FALSE(page.priest.empty());  // the priest opens

    // THE PLAYER CANNOT WALK OUT OF CUSTODY. Sixty steps of a held forward
    // key move the body nowhere; every world verb and every page toggle is
    // refused; the hearing is not in dismissOverlays.
    MoveInput walk;
    walk.forward = 1;
    walk.sprint = true;
    session.stepMany(walk, kStepsPerSecond);
    CHECK(session.body().tileX() == bx);
    CHECK(session.body().tileY() == by);
    CHECK(session.courtOpen());
    session.toggleCasebook();
    CHECK_FALSE(session.casebookOpen());
    session.toggleDistrictMap();
    CHECK_FALSE(session.districtMapOpen());
    session.toggleGrimoire();
    CHECK_FALSE(session.grimoireOpen());
    session.openWait(false);
    CHECK_FALSE(session.waitOpen());
    session.interact();
    CHECK_FALSE(session.talking());
    session.attackDown();
    session.stepMany(MoveInput{}, 1);
    session.attackUp();
    CHECK_FALSE(gull.playerHandsUp());
    session.climb();
    session.toggleCrouch();
    CHECK(gull.stance() != Stance::Crouched);
    CHECK(session.courtOpen());
    CHECK(gull.hearingPending());

    // PAUSE STILL OPENS OVER IT -- a player can always quit the game -- with
    // the WAIT row worded as the refusal and refused out loud.
    session.togglePause();
    REQUIRE(session.pauseOpen());
    CHECK(session.courtOpen());
    const std::vector<std::string> rows = session.pauseRows();
    REQUIRE(rows.size() == 5);
    CHECK(rows[1] == "THE PRIEST IS WAITING.");
    session.movePauseCursor(1);
    session.choosePause();
    CHECK(session.pauseOpen());
    CHECK_FALSE(session.waitOpen());
    CHECK(session.lastMessage() == "THE PRIEST IS WAITING.");
    session.closeConversation();
    CHECK_FALSE(session.pauseOpen());
    CHECK(session.courtOpen());
    // ...and the page draws under it and over the world: the frame with the
    // court differs from the frame with pause over it.
    const render::Framebuffer court = shot(session, 320, 180);
    session.togglePause();
    session.stepMany(MoveInput{}, 16);
    const render::Framebuffer paused = shot(session, 320, 180);
    CHECK_FALSE(same(court, paused));
    session.closeConversation();
}

TEST_CASE("a hearing pending on the ledger with no page up opens one on the next step -- the page is derived from the record") {
    Session session(docksAt(20));
    thiefLedger(session);
    CHECK_FALSE(session.courtOpen());
    seedHearing(session, drawOf(0, 5));
    CHECK(session.courtOpen());
    CHECK(session.lastMessage().rfind("TAKEN TO THE MISSION. ", 0) == 0);
    // A step later it is still up: nothing closes it but the sentence.
    session.stepMany(MoveInput{}, kStepsPerSecond);
    CHECK(session.courtOpen());
    CHECK(session.inCustody());
}

// ===========================================================================
// THE PAGE
// ===========================================================================

TEST_CASE("the hearing page draws byte-identical twice, and the two pleas' screens differ from the plea's and from each other") {
    const auto opened = []() {
        auto session = std::make_unique<Session>(docksAt(20));
        thiefLedger(*session);
        seedHearing(*session, drawOf(0, 5));
        // The page's own ease, fully open, so the pixels are the page's.
        session->stepMany(MoveInput{}, 16);
        return session;
    };
    std::unique_ptr<Session> one = opened();
    const render::Framebuffer a = shot(*one, 320, 180);
    const render::Framebuffer b = shot(*one, 320, 180);
    CHECK(same(a, b));
    // Two sessions, one script: the same page, pixel for pixel.
    std::unique_ptr<Session> two = opened();
    const render::Framebuffer c = shot(*two, 320, 180);
    CHECK(same(a, c));

    // I DID IT on one, I DID NOT on the other: both are judged, and the
    // check block they draw differs -- from the plea screen and from each
    // other (CONFESSED is not THE PRIEST IS A MAN).
    REQUIRE(press(*one, Key::Num1));
    REQUIRE(one->courtPleaArmed());
    REQUIRE(press(*one, Key::Num1));
    REQUIRE(one->tavern().hearing().judged());
    CHECK(one->tavern().hearing().plea == Plea::Guilty);
    one->stepMany(MoveInput{}, 4);
    const render::Framebuffer confessed = shot(*one, 320, 180);
    REQUIRE(press(*two, Key::Num2));
    REQUIRE(press(*two, Key::Num2));
    REQUIRE(two->tavern().hearing().judged());
    CHECK(two->tavern().hearing().plea == Plea::NotGuilty);
    two->stepMany(MoveInput{}, 4);
    const render::Framebuffer denied = shot(*two, 320, 180);
    CHECK_FALSE(same(a, confessed));
    CHECK_FALSE(same(a, denied));
    CHECK_FALSE(same(confessed, denied));
    // And each judged screen is itself steady frame to frame.
    CHECK(same(confessed, shot(*one, 320, 180)));
    // The page is what changed, not the world under it: the HUD stood down,
    // the centre of the frame is the page's.
    const render::CentreRect centre = render::hudCentreRect(320, 180);
    bool differs = false;
    for (int y = centre.y0; y < centre.y1 && !differs; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            if (a.pixels()[a.index(x, y)] != confessed.pixels()[confessed.index(x, y)]) {
                differs = true;
                break;
            }
        }
    }
    CHECK(differs);
}

TEST_CASE("the page says the charge off the sheet, the consequence of the hovered row, and the sheet as phrases -- never numbers before the plea") {
    Session session(docksAt(20));
    thiefLedger(session);
    seedHearing(session, drawOf(0, 5), 12, 30, -15);
    render::HearingPageState page = session.hearingPageState();
    CHECK(page.charge == "THE WARD HAS YOU FOR TWO LIFTS AND A CRACKED BOX.");
    CHECK(page.asks == "THE PAPER ASKS FOR A CELL.");
    CHECK(page.laid == "WATCHMAN CULL LAYS THE PAPER ON THE TABLE.");
    // The hovered row's consequence, in words: the informed choice.
    CHECK(page.cursor == 0);
    CHECK(page.consequence.rfind("A CONFESSION IS WEIGHED AS IT IS GIVEN.", 0) == 0);
    session.moveCourtCursor(1);
    page = session.hearingPageState();
    CHECK(page.cursor == 1);
    CHECK(page.consequence.rfind("A DENIAL IS WEIGHED WITH THE PRIEST'S OWN DOUBT IN IT.", 0) == 0);
    CHECK(page.consequence.find("THE SENTENCE DOUBLES") != std::string::npos);
    // The priest's opening, BY THE CASE: this thief has given at the door
    // (temple 30 -- the sheet's own THE DOOR term is above zero), so it is
    // court.paper.door, rotated on the hearings; the row that names the
    // door is true of him.
    const BarkTables& barks = session.tavern().dialogue().barks();
    REQUIRE(barks.has("court.paper"));
    REQUIRE(barks.has("court.paper.door"));
    CHECK(page.priest == std::string(barks.line("court.paper.door", 0)));
    CHECK(page.priest == "I have read what you gave at this door. It is the only reason we are talking.");
    // THE OFFICER WHO WALKED YOU IN stands by the wall under the rows: his
    // name off the record, his line out of court.taken, rotated with the
    // opening (BARKS lane).
    REQUIRE(barks.has("court.taken"));
    CHECK(page.officerName == "WATCHMAN CULL");
    CHECK(page.officerSays == std::string(barks.line("court.taken", 0)));
    CHECK(page.officerSays ==
          "Through the door and sit where he points. The priest asks the questions in here. I "
          "only carry the paper.");
    // ARMING A PLEA puts the question in the priest's mouth (court.plead);
    // disarming gives him his opening back. The paper row arms nothing and
    // asks nothing.
    REQUIRE(barks.has("court.plead"));
    session.moveCourtCursor(-1);
    session.chooseCourtRow();  // arms I DID IT
    REQUIRE(session.courtPleaArmed());
    page = session.hearingPageState();
    CHECK(page.priest == std::string(barks.line("court.plead", 0)));
    CHECK(page.priest == "Well? The lamp is lit and I am an old man. Did you, or did you not?");
    CHECK(page.officerSays == std::string(barks.line("court.taken", 0)));
    session.courtBack();
    REQUIRE_FALSE(session.courtPleaArmed());
    page = session.hearingPageState();
    CHECK(page.priest == std::string(barks.line("court.paper.door", 0)));
    session.moveCourtCursor(1);
    page = session.hearingPageState();
    CHECK(page.cursor == 1);
    // HEAR THE PAPER: the sheet as phrases. No digit anywhere on it.
    session.moveCourtCursor(1);
    session.chooseCourtRow();
    REQUIRE(session.courtPaperOpen());
    page = session.hearingPageState();
    CHECK(page.view == render::HearingView::Paper);
    REQUIRE(page.rows.size() == 4);
    CHECK(page.rows[3].key == "0");
    CHECK(page.rows[3].label == "BACK");
    REQUIRE_FALSE(page.paper.empty());
    for (const std::string& fact : page.paper) {
        INFO(fact);
        CHECK(fact.find_first_of("0123456789") == std::string::npos);
        mustRead("the paper", fact);
    }
    CHECK(std::find(page.paper.begin(), page.paper.end(), "NEVER TAKEN BEFORE.") != page.paper.end());
    CHECK(std::find(page.paper.begin(), page.paper.end(), "THE MISSION KNOWS YOUR NAME.") !=
          page.paper.end());
    CHECK(std::find(page.paper.begin(), page.paper.end(), "THE WARD THINKS ILL OF YOU.") !=
          page.paper.end());
    // 0 - BACK returns to the rows, cursor on the row that opened it.
    session.chooseCourtVisibleRow(-1);
    CHECK_FALSE(session.courtPaperOpen());
    CHECK(session.courtCursor() == 2);
    page = session.hearingPageState();
    CHECK(page.rows.size() == 3);
    // Every word the page can show reads.
    mustRead("title", page.title);
    mustRead("laid", page.laid);
    mustRead("charge", page.charge);
    mustRead("asks", page.asks);
    mustRead("consequence", page.consequence);
    mustRead("priest", page.priest);
    mustRead("officerName", page.officerName);
    mustRead("officerSays", page.officerSays);
    for (const render::HearingRow& row : page.rows) {
        mustRead("row", row.label);
    }
    for (const std::string& row : session.courtRows()) {
        mustRead("courtRows", row);
    }
    // EVERY AUTHORED COURT ROW READS, all sixteen tables: the redline list
    // is drawable to the glyph, and no row is blank in anybody's mouth. The
    // openings are keyed BY THE CASE (court.paper / court.paper.door,
    // court.roofs.hand / court.roofs.rope) so every row of a table is true
    // of every hearing that table can open.
    for (const char* key : {"court.taken", "court.paper", "court.paper.door", "court.blood",
                            "court.roofs.hand", "court.roofs.rope", "court.nothing",
                            "court.plead", "court.spared", "court.fined", "court.held",
                            "court.bound", "court.hand", "court.commuted", "court.rope",
                            "court.lie"}) {
        const std::vector<std::string>* rows = barks.rows(key);
        REQUIRE(rows != nullptr);
        CHECK(rows->size() >= 3);
        CHECK(rows->size() <= 5);
        for (const std::string& row : *rows) {
            mustRead(key, row);
        }
    }
}

TEST_CASE("the page holds its geometry at every window the game runs at, and clips nothing") {
    Session session(docksAt(20));
    thiefLedger(session);
    seedHearing(session, drawOf(0, 5));
    const render::HearingPageState plea = session.hearingPageState();
    REQUIRE(press(session, Key::Num1));
    REQUIRE(press(session, Key::Num1));
    session.stepMany(MoveInput{}, kJudgmentHoldSteps + 1);
    const render::HearingPageState judged = session.hearingPageState();
    REQUIRE(judged.view == render::HearingView::Judged);
    for (const Window& window : kWindows) {
        INFO("window ", window.width, "x", window.height);
        const render::HearingPageMetrics before =
            render::hearingPageMetrics(plea, window.width, window.height);
        const render::HearingPageMetrics after =
            render::hearingPageMetrics(judged, window.width, window.height);
        REQUIRE(before.usable);
        REQUIRE(after.usable);
        CHECK(before.split);
        CHECK(after.split);
        // The panes hold their height: pleading moves no border.
        CHECK(before.bounds.x == after.bounds.x);
        CHECK(before.bounds.y == after.bounds.y);
        CHECK(before.bounds.w == after.bounds.w);
        CHECK(before.bounds.h == after.bounds.h);
        CHECK(before.master.w == after.master.w);
        CHECK(before.detail.w == after.detail.w);
        CHECK(before.bodyRows == render::kHearingBodyRows);
        // Nothing clipped: the reading fits its band, the check block fits
        // the body.
        CHECK(before.readingRowsWanted <= before.readingRows);
        CHECK(after.readingRowsWanted <= after.readingRows);
        CHECK(after.detailRowsWanted <= after.bodyRows);
        // The rows are whole: the widest row -- the armed denial with the
        // one-cell confirm on its tail -- fits the master pane.
        CHECK(before.masterCells >= static_cast<int>(std::string_view("2 - I DID NOT. -- SURE? A").size()));
        CHECK(before.detailCells >= 26);
        // And the frame draws at this size without complaint.
        render::Framebuffer frame(window.width, window.height);
        render::drawHearingPage(frame, judged);
    }
}

// ===========================================================================
// THE CHECK BLOCK
// ===========================================================================

TEST_CASE("the check block shows the weighing: the arithmetic, + 6 CONFESSED, the lines, the verdict, the sentence in numbers, the priest's word") {
    Session session(docksAt(20));
    thiefLedger(session);
    // The spec's own worked thief: streetwise 12, Almsbearer 30, ward cold
    // -15, heat 68 -> 24 + 6 + 10 - 3 - 2 = 35; I DID IT -> 41, FINED.
    seedHearing(session, drawOf(0, 5), 12, 30, -15);
    REQUIRE(press(session, Key::Num1));
    REQUIRE(press(session, Key::Num1));
    const HearingState& hearing = session.tavern().hearing();
    REQUIRE(hearing.judged());
    CHECK(hearing.weight == 35);
    CHECK(hearing.scored == 41);
    CHECK(hearing.judgment == Judgment::Fined);
    render::HearingPageState page = session.hearingPageState();
    CHECK(page.view == render::HearingView::Judged);
    CHECK(page.weighsBadge == "THE PRIEST WEIGHS");
    CHECK(page.arithmetic == "24 THE FLAME + 6 TONGUE + 10 THE DOOR - 3 THE WARD - 2 HEAT MAKES 35");
    CHECK(page.arithmeticTerms == std::vector<std::string>{"24 THE FLAME", "+ 6 TONGUE",
                                                            "+ 10 THE DOOR", "- 3 THE WARD",
                                                            "- 2 HEAT", "MAKES 35"});
    CHECK(page.pleaTerm == "+ 6 CONFESSED MAKES 41");
    CHECK(page.pleaTerms == std::vector<std::string>{"+ 6 CONFESSED", "MAKES 41"});
    // A CONFESSION CAN NEVER BE SPARED (paperBand), so its block prints only
    // the lines it could have crossed.
    CHECK(page.lines == "THE LINES: 38 FINED  14 HELD");
    // The officer walked you in and laid the paper; the bench has answered
    // and his line is not beside the answer.
    CHECK(page.officerSays.empty());
    CHECK(page.officerName.empty());
    CHECK(page.verdict == "FINED");
    const SentenceTerms terms = sentenceTerms(hearing, session.tavern().playerCoin());
    CHECK(page.sentence == std::to_string(terms.finePaid) + " ROYALS.");
    const BarkTables& barks = session.tavern().dialogue().barks();
    CHECK(page.priest == std::string(barks.line("court.fined", 0)));
    // The commit beat landed on the badge (contract b), and decays.
    CHECK(page.commitPulse > 0.0F);
    // THE ROW WAITS ITS HOLD: nothing to press until kJudgmentHoldSteps have
    // run, then the one row -- PAY IT -- and no BACK.
    CHECK(page.rows.empty());
    CHECK_FALSE(session.courtSentenceOffered());
    session.stepMany(MoveInput{}, kJudgmentHoldSteps - 1);
    CHECK_FALSE(session.courtSentenceOffered());
    session.stepMany(MoveInput{}, 1);
    CHECK(session.courtSentenceOffered());
    page = session.hearingPageState();
    REQUIRE(page.rows.size() == 1);
    CHECK(page.rows[0].key == "1");
    CHECK(page.rows[0].label == "PAY IT.");
    CHECK(page.backVerb.empty());
    mustRead("arithmetic", page.arithmetic);
    mustRead("pleaTerm", page.pleaTerm);
    mustRead("lines", page.lines);
    mustRead("sentence", page.sentence);
    mustRead("priest", page.priest);
    // PAY IT: the fine out of the purse, released where you stand -- the
    // Mission's door -- the page down, the line said with the day on it.
    const std::int32_t purse = session.tavern().playerCoin();
    const std::int32_t bx = session.body().tileX();
    const std::int32_t by = session.body().tileY();
    REQUIRE(press(session, Key::Num1));
    session.stepMany(MoveInput{}, 1);
    CHECK_FALSE(session.courtOpen());
    CHECK_FALSE(session.inCustody());
    CHECK_FALSE(session.tavern().hearingPending());
    CHECK(session.tavern().playerCoin() == purse - terms.finePaid);
    CHECK(session.body().tileX() == bx);
    CHECK(session.body().tileY() == by);
    CHECK(session.lastMessage().rfind("TURNED LOOSE AT THE MISSION'S DOOR. DAY 1. ", 0) == 0);
    CHECK(session.tavern().dialogue().crimes().heat() == kHeatAfterSentence);
    // Free again: the verbs answer.
    session.toggleCasebook();
    CHECK(session.casebookOpen());
    session.toggleCasebook();
}

TEST_CASE("a disbelieved denial doubles on the page: THE PRIEST IS A MAN in the block, the nights doubled, the lie in the priest's mouth, the Tarwalk at the end") {
    Session session(docksAt(20));
    thiefLedger(session);
    // The band at its floor: 35 - 10 = 25, HELD, doubled. Nights: 24 + 30.
    seedHearing(session, drawOf(-10, 30), 12, 30, -15);
    REQUIRE(press(session, Key::Num2));
    REQUIRE(press(session, Key::Num2));
    const HearingState& hearing = session.tavern().hearing();
    REQUIRE(hearing.judged());
    CHECK(hearing.judgment == Judgment::Held);
    CHECK(hearing.doubled);
    render::HearingPageState page = session.hearingPageState();
    CHECK(page.pleaTerm == "- 10 THE PRIEST IS A MAN MAKES 25");
    CHECK(page.verdict == "HELD");
    const SentenceTerms terms = sentenceTerms(hearing, session.tavern().playerCoin());
    // WHOLE NIGHTS: 24 + 30 hours off the draw is two nights (cellNights),
    // doubled to four, and the clock will skip exactly those.
    CHECK(cellNights(drawOf(-10, 30)) == 2);
    CHECK(terms.cellHours == 2 * 2 * kHoursPerNight);
    CHECK(page.sentence == "FOUR NIGHTS. " + std::to_string(terms.finePaid) + " ROYALS.");
    // The denial's lines carry SPARED: it was on the table.
    CHECK(page.lines == "THE LINES: 55 SPARED  38 FINED  14 HELD");
    const BarkTables& barks = session.tavern().dialogue().barks();
    CHECK(page.priest == std::string(barks.line("court.lie", 0)));
    CHECK(page.priest == "You lied to the Flame's face. The Mission will remember the lie longer than the sentence.");
    session.stepMany(MoveInput{}, kJudgmentHoldSteps);
    page = session.hearingPageState();
    REQUIRE(page.rows.size() == 1);
    CHECK(page.rows[0].label == "SERVE IT.");
    // SERVE IT: the nights on the world clock, then the Tarwalk with the
    // day and the hour on the row.
    const std::int32_t dayBefore = session.tavern().dayNumber();
    const int clockBefore = session.timeOfDay();
    REQUIRE(press(session, Key::Enter));
    session.stepMany(MoveInput{}, 1);
    CHECK_FALSE(session.courtOpen());
    // FOUR NIGHTS PROMISED, FOUR DAYS ON THE CALENDAR, and the clock face
    // back where the sentence started: the promise and the calendar agree.
    CHECK(session.tavern().dayNumber() == dayBefore + 4);
    CHECK(session.tavern().lastServed().timeReleased / 60 == clockBefore / 60);
    CHECK(session.body().tileX() == gull::kStreetX);
    CHECK(session.body().tileY() == gull::kStreetY);
    const std::string& line = session.lastMessage();
    INFO(line);
    CHECK(line.rfind("TURNED LOOSE ON THE TARWALK. DAY ", 0) == 0);
    CHECK(line.find("DAY " + std::to_string(session.tavern().dayNumber() + 1) + ". ") !=
          std::string::npos);
    CHECK(session.tavern().dialogue().crimes().arrests() == 1);
}

TEST_CASE("a murder hearing reads the corpse's name and who saw it, weighs BLOOD and SAW IT against the mercy line, and commutes the devout") {
    // AT EIGHT, BEFORE THE WATCH DRINKS: a real killing in a full taproom,
    // witnessed by the room and not by Cull -- so the corpse is on the
    // roster with a name and the hearing can be opened in the arrest's
    // shape without a watchman taking the player mid-swing (the Watch's own
    // arrest at reach is proved in TAKEN above and in test_court).
    Session session(gullAt(20));
    Tavern& gull = session.tavern();
    session.stepMany(MoveInput{}, 2);
    REQUIRE(actorNamed(session, "Watchman Cull") == nullptr);
    // The mark: the upright patron with the most other people close enough
    // to see it done.
    std::int32_t markId = -1;
    std::string markName;
    int bestNear = -1;
    for (const Actor& actor : gull.actors()) {
        if (!actor.present() || isFloored(actor.activity()) || actor.role() != ActorRole::Patron ||
            gull.isProfessional(actor)) {
            continue;
        }
        int near = 0;
        for (const Actor& other : gull.actors()) {
            if (other.id() != actor.id() && other.present() && !isFloored(other.activity()) &&
                other.role() != ActorRole::Vermin && other.band() == actor.band() &&
                std::max(std::abs(other.tileX() - actor.tileX()),
                         std::abs(other.tileY() - actor.tileY())) <= 4) {
                ++near;
            }
        }
        if (near > bestNear && standFacing(session, actor)) {
            bestNear = near;
            markId = actor.id();
            markName = actor.name();
        }
    }
    REQUIRE(markId >= 0);
    REQUIRE(killInSight(session, markId));
    CrimeLedger& crimes = gull.dialogue().crimes();
    REQUIRE_MESSAGE(crimes.murderer(), "nobody saw the killing");
    const std::int32_t saw = crimes.slewWitnesses();
    REQUIRE(saw >= 1);
    CHECK(gull.slainName() == markName);
    // The Shepherd's own record: streetwise 30, temple 84, the ward warm --
    // COMMUTED, certain, on a confession (the spec's own worked example).
    // The hearing opened in the arrest's shape, and the page derives itself.
    session.stepMany(MoveInput{}, 1);
    const ChargeSheet sheet = crimes.charge(false, 30, 84, 20);
    REQUIRE(sheet.blood);
    REQUIRE(sheet.tier == Sentence::Condemned);
    CHECK(sheet.witnesses == saw);
    crimes.openHearing(sheet, 0, drawOf(0, 5), "Watchman Cull");
    session.stepMany(MoveInput{}, 1);
    REQUIRE(session.courtOpen());
    render::HearingPageState page = session.hearingPageState();
    std::string upper = markName;
    for (char& c : upper) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    INFO(page.charge);
    CHECK(page.charge.rfind("THE WARD SAYS YOU PUT " + upper + " DOWN IN THE GILDED GULL.", 0) == 0);
    CHECK(page.charge.find(" SAW IT.") != std::string::npos);
    CHECK(page.asks == "THE PAPER ASKS FOR THE ROPE.");
    const BarkTables& barks = session.tavern().dialogue().barks();
    CHECK(page.priest == std::string(barks.line("court.blood", 0)));
    CHECK(page.consequence.find("MERCY OR THE ROPE, AND NOTHING ELSE.") != std::string::npos);
    CHECK(page.consequence.find("ROPE FOR THE TONGUE") == std::string::npos);
    session.moveCourtCursor(1);
    page = session.hearingPageState();
    CHECK(page.consequence.find("MERCY OR THE ROPE, AND NEVER SPARED.") != std::string::npos);
    CHECK(page.consequence.find("CORPSE") == std::string::npos);
    session.moveCourtCursor(-1);
    REQUIRE(press(session, Key::Num1));
    REQUIRE(press(session, Key::Num1));
    const HearingState& hearing = gull.hearing();
    REQUIRE(hearing.judged());
    CHECK(hearing.judgment == Judgment::Commuted);
    page = session.hearingPageState();
    INFO(page.arithmetic);
    CHECK(page.arithmetic.find("- 30 BLOOD") != std::string::npos);
    CHECK(page.arithmetic.find(" SAW IT") != std::string::npos);
    CHECK(page.lines == "THE LINE: 24 MERCY");
    CHECK(page.verdict == "COMMUTED");
    CHECK(page.sentence == "THE HAND. BONDSWORN 12 DAYS.");
    CHECK(page.priest == std::string(barks.line("court.commuted", 0)));
    session.stepMany(MoveInput{}, kJudgmentHoldSteps);
    REQUIRE(press(session, Key::Num1));  // SERVE IT.
    session.stepMany(MoveInput{}, 1);
    CHECK_FALSE(session.courtOpen());
    CHECK(crimes.condemned());
    CHECK_FALSE(crimes.murderer());
    CHECK(session.heatLine().rfind("CONDEMNED  HEAT ", 0) == 0);
    // AND THE ROPE'S PLATE WOULD NAME HIM: the one rule the reading shares
    // with the end (Tavern::slainName), proved on the record the kill left.
    CHECK(gull.slainName() == markName);
}

// ===========================================================================
// THE GRAMMAR
// ===========================================================================

TEST_CASE("the page routes input per the register grammar on the keyboard: digits pick, arrows move, ENTER arms then confirms, ESC disarms and closes the paper and does nothing else") {
    Session session(docksAt(20));
    thiefLedger(session);
    seedHearing(session, drawOf(3, 5));
    session.noteInputDevice(render::InputDevice::KeyboardMouse);
    CHECK(session.courtCursor() == 0);
    // Arrows and the bound movement keys move the list; W is Forward.
    CHECK(press(session, Key::Down));
    CHECK(session.courtCursor() == 1);
    CHECK(press(session, Key::S));
    CHECK(session.courtCursor() == 2);
    CHECK(press(session, Key::Down));
    CHECK(session.courtCursor() == 0);  // wraps
    CHECK(press(session, Key::W));
    CHECK(session.courtCursor() == 2);
    CHECK(press(session, Key::Up));
    CHECK(session.courtCursor() == 1);
    // 3 opens the paper; 0 (BACK) and ESC both close it, cursor on row 3.
    CHECK(press(session, Key::Num3));
    CHECK(session.courtPaperOpen());
    CHECK(session.courtRows().size() == 4);
    CHECK(session.courtRows()[3] == "0 - BACK");
    CHECK(press(session, Key::Escape));
    CHECK_FALSE(session.courtPaperOpen());
    CHECK(session.courtCursor() == 2);
    CHECK(press(session, Key::Num3));
    CHECK(press(session, Key::Num0));
    CHECK_FALSE(session.courtPaperOpen());
    // ENTER on a plea ARMS it; the row names the confirm; ESC disarms and
    // leaves the page exactly where it was.
    CHECK(press(session, Key::Num1));
    CHECK(session.courtPleaArmed());
    CHECK_FALSE(session.tavern().hearing().judged());
    CHECK(session.courtRows()[0] == "1 - I DID IT. -- SURE? " +
                                        std::string(render::promptConfirmKey(
                                            render::InputDevice::KeyboardMouse)));
    CHECK(session.hearingPageState().backVerb == "DISARM");
    CHECK(press(session, Key::Escape));
    CHECK_FALSE(session.courtPleaArmed());
    CHECK(session.courtOpen());
    CHECK_FALSE(session.tavern().hearing().judged());
    // Moving the cursor disarms too; a different digit re-arms that row.
    CHECK(press(session, Key::Num1));
    CHECK(press(session, Key::Down));
    CHECK_FALSE(session.courtPleaArmed());
    CHECK(press(session, Key::Num2));
    CHECK(session.courtPleaArmed());
    CHECK(press(session, Key::Num1));
    CHECK(session.courtPleaArmed());
    CHECK(session.courtCursor() == 0);
    CHECK_FALSE(session.tavern().hearing().judged());
    // ESC with nothing to back out of is NOT the page's: it falls through
    // to the client (the pause menu, the one way to quit) and the page is
    // untouched. Every other world key is swallowed and does nothing.
    CHECK(press(session, Key::Escape));  // the arm
    CHECK_FALSE(press(session, Key::Escape));
    CHECK(session.courtOpen());
    CHECK(press(session, Key::T));
    CHECK(press(session, Key::Space));
    CHECK(press(session, Key::MouseLeft));
    CHECK(press(session, Key::J));
    CHECK(press(session, Key::M));
    CHECK(session.courtOpen());
    CHECK_FALSE(session.casebookOpen());
    CHECK_FALSE(session.districtMapOpen());
    CHECK_FALSE(session.tavern().hearing().judged());
    // ENTER twice pleads (the cursor is on I DID IT).
    CHECK(press(session, Key::Enter));
    CHECK(session.courtPleaArmed());
    CHECK(press(session, Key::Enter));
    CHECK(session.tavern().hearing().judged());
    CHECK(session.tavern().hearing().plea == Plea::Guilty);
    // The plea trained the tongue (a haggle with your neck on the table).
    CHECK(session.tavern().dialogue().skills().level(kHaggleSkill) >= 0);
    // A press during the hold is swallowed and serves nothing.
    CHECK(press(session, Key::Enter));
    CHECK(press(session, Key::Num1));
    CHECK(session.courtOpen());
    CHECK(session.tavern().hearingPending());
    session.stepMany(MoveInput{}, kJudgmentHoldSteps);
    CHECK(session.courtSentenceOffered());
    CHECK(press(session, Key::Enter));
    session.stepMany(MoveInput{}, 1);
    CHECK_FALSE(session.courtOpen());
    CHECK_FALSE(session.tavern().hearingPending());
    // Free: the router declines everything now.
    CHECK_FALSE(press(session, Key::Enter));
}

TEST_CASE("the page routes input per the register grammar on the pad: the D-pad moves, A arms and confirms, B disarms, and the prompts speak pad") {
    Session session(docksAt(20));
    thiefLedger(session);
    seedHearing(session, drawOf(3, 5));
    session.noteInputDevice(render::InputDevice::Pad);
    REQUIRE(session.promptDevice() == render::InputDevice::Pad);
    render::HearingPageState page = session.hearingPageState();
    CHECK(page.confirmKey == "A");
    CHECK(page.backKey == "B");
    CHECK(press(session, Key::PadDown));
    CHECK(session.courtCursor() == 1);
    CHECK(press(session, Key::PadUp));
    CHECK(session.courtCursor() == 0);
    CHECK(press(session, Key::PadDown));
    CHECK(press(session, Key::PadDown));
    CHECK(session.courtCursor() == 2);
    // A on HEAR THE PAPER opens it; B closes it -- both as the raw East
    // button and as the Escape pageBackRemap turns it into at the edge.
    CHECK(press(session, Key::PadSouth));
    CHECK(session.courtPaperOpen());
    CHECK(render::pageBackRemap(Key::PadEast, true) == Key::Escape);
    CHECK(press(session, render::pageBackRemap(Key::PadEast, true)));
    CHECK_FALSE(session.courtPaperOpen());
    CHECK(press(session, Key::PadSouth));
    CHECK(session.courtPaperOpen());
    CHECK(press(session, Key::PadEast));
    CHECK_FALSE(session.courtPaperOpen());
    // A arms I DID NOT; the row names A; B disarms; A twice pleads.
    CHECK(press(session, Key::PadUp));
    CHECK(session.courtCursor() == 1);
    CHECK(press(session, Key::PadSouth));
    CHECK(session.courtPleaArmed());
    CHECK(session.courtRows()[1] == "2 - I DID NOT. -- SURE? A");
    CHECK(press(session, Key::PadEast));
    CHECK_FALSE(session.courtPleaArmed());
    CHECK(press(session, Key::PadSouth));
    CHECK(press(session, Key::PadSouth));
    CHECK(session.tavern().hearing().judged());
    CHECK(session.tavern().hearing().plea == Plea::NotGuilty);
    // The other face buttons and the bumpers reach nothing.
    CHECK(press(session, Key::PadWest));
    CHECK(press(session, Key::PadNorth));
    CHECK(press(session, Key::PadLeftBumper));
    CHECK(session.courtOpen());
    // Start is Pause: declined here so the client opens the pause menu.
    CHECK_FALSE(press(session, Key::PadStart));
    session.stepMany(MoveInput{}, kJudgmentHoldSteps);
    CHECK(press(session, Key::PadSouth));
    session.stepMany(MoveInput{}, 1);
    CHECK_FALSE(session.courtOpen());
}

TEST_CASE("mercy is given once on the page: a commuted man before a rope bench gets the one row, and it hangs him") {
    Session session(docksAt(20));
    CrimeLedger& crimes = session.tavern().dialogue().crimes();
    crimes.markMurderer(2);
    crimes.sentence(Judgment::Commuted, kCommutedDays);
    REQUIRE(crimes.commuted());
    crimes.markMurderer(1);
    seedHearing(session, drawOf(10, 5), 30, 84, 20);
    render::HearingPageState page = session.hearingPageState();
    REQUIRE(page.rows.size() == 1);
    CHECK(page.rows[0].label == "I HAVE NOTHING TO SAY.");
    const BarkTables& barks = session.tavern().dialogue().barks();
    CHECK(page.priest == std::string(barks.line("court.nothing", crimes.hearings())));
    CHECK(std::find(page.paper.begin(), page.paper.end(), "MERCY WAS GIVEN ONCE.") !=
          page.paper.end());
    CHECK(press(session, Key::Num1));
    CHECK(press(session, Key::Num1));
    REQUIRE(session.tavern().hearing().judged());
    CHECK(session.tavern().hearing().judgment == Judgment::TheRope);
    page = session.hearingPageState();
    CHECK(page.arithmetic.empty());  // nothing is weighed
    CHECK(page.arithmeticTerms.empty());
    CHECK(page.pleaTerm.empty());
    CHECK(page.lines.empty());
    CHECK(page.verdict == "THE ROPE");
    CHECK(page.sentence.rfind("THE SALTGATE POST. ", 0) == 0);
    CHECK(page.sentence.find("ROPE") == std::string::npos);
    CHECK(page.priest == std::string(barks.line("court.rope", 0)));  // the first hearing pleaded
    // NO BADGE OVER NOTHING: the page with its weighs badge and the page
    // with the badge blanked draw the same pixels -- [THE PRIEST WEIGHS] is
    // not on a frame where nothing was weighed -- and the verdict leads.
    session.stepMany(MoveInput{}, 16);
    render::HearingPageState blank = session.hearingPageState();
    blank.commitPulse = 0.0F;
    render::HearingPageState badged = blank;
    blank.weighsBadge.clear();
    render::Framebuffer withBadge(320, 180);
    render::Framebuffer without(320, 180);
    render::drawHearingPage(withBadge, badged);
    render::drawHearingPage(without, blank);
    CHECK(same(withBadge, without));
    const render::HearingPageMetrics metrics = render::hearingPageMetrics(badged, 320, 180);
    REQUIRE(metrics.usable);
    // The verdict, the sentence and the priest: no badge row, no block, no
    // air before the verdict.
    CHECK(metrics.detailRowsWanted <= 1 + 1 + 4);
}

// ===========================================================================
// THE ROPE
// ===========================================================================

TEST_CASE("THE ROPE staged: the drop, the held black dip, the plate that never fades, then A NEW MAN / LEAVE -- and LEAVE quits") {
    Session session(docksAt(20));
    Tavern& gull = session.tavern();
    CrimeLedger& crimes = gull.dialogue().crimes();
    // The roofs' second: a Skyrunner with a prior and paper -- the ladder's
    // own rope with no corpse to name, so the plate says THE SECOND RUNG.
    crimes.commit(Crime::RoofRun, true);
    crimes.sentence(Judgment::Held, 1);
    crimes.commit(Crime::Lift, true);
    crimes.addHeat(kWarrantAt + 2);
    seedHearing(session, drawOf(0, 5), 10, 0, 0, true);
    render::HearingPageState page = session.hearingPageState();
    REQUIRE(gull.hearing().sheet.secondRung);
    CHECK(page.asks == "THE PAPER ASKS FOR THE ROPE.");
    CHECK(page.charge == "THE WARD HAS YOU FOR A LIFT.");
    const BarkTables& barks = gull.dialogue().barks();
    CHECK(page.priest == std::string(barks.line("court.roofs.rope", crimes.hearings())));
    CHECK(page.priest.find("second rung") != std::string::npos);
    REQUIRE(press(session, Key::Num1));
    REQUIRE(press(session, Key::Num1));
    REQUIRE(gull.hearing().judged());
    REQUIRE(gull.hearing().judgment == Judgment::TheRope);
    page = session.hearingPageState();
    CHECK(page.verdict == "THE ROPE");
    // THE ROPE'S ROW is the post and the hour, never an echo of the badge.
    CHECK(page.sentence.rfind("THE SALTGATE POST. ", 0) == 0);
    CHECK(page.sentence.size() == std::string_view("THE SALTGATE POST. 22:00.").size());
    CHECK(page.sentence.back() == '.');
    CHECK(page.sentence[page.sentence.size() - 4] == ':');
    CHECK(page.sentence.find("ROPE") == std::string::npos);
    CHECK(page.arithmetic.find("- 24 THE SECOND RUNG") != std::string::npos);
    session.stepMany(MoveInput{}, kJudgmentHoldSteps);
    page = session.hearingPageState();
    REQUIRE(page.rows.size() == 1);
    CHECK(page.rows[0].label == "THE DROP.");
    // THE DROP, taken by the player's own hand: the page comes down, the
    // ceremony goes up, the bit is set, no clock and no release.
    const std::int32_t day = gull.dayNumber();
    const int second = session.timeOfDay();
    REQUIRE(press(session, Key::Num1));
    CHECK_FALSE(session.courtOpen());
    CHECK(session.ropeCeremonyUp());
    CHECK(session.inCustody());
    CHECK(gull.executed());
    CHECK(gull.runEnded());
    CHECK_FALSE(session.ropeRowsUp());
    CHECK_FALSE(session.runEnded());
    CHECK(gull.dayNumber() == day);
    CHECK(session.timeOfDay() == second);
    CHECK(session.ropePlateTop() == "HANGED AT THE SALTGATE POST.");
    CHECK(session.ropePlateMid() == "BY THE WARD. FOR THE SECOND RUNG.");
    CHECK(session.ropePlateFoot().rfind("THE FIRST DAY. ", 0) == 0);
    mustRead("plate", session.ropePlateTop());
    mustRead("plate", session.ropePlateMid());
    mustRead("plate", session.ropePlateFoot());
    // THE DIP: over kPageEaseSteps the frame goes to black, and then it HOLDS
    // -- the plate never fades. No input, no pause.
    const render::Framebuffer first = shot(session, 320, 180);
    session.stepMany(MoveInput{}, render::kPageEaseSteps);
    const render::Framebuffer dark = shot(session, 320, 180);
    CHECK_FALSE(same(first, dark));
    CHECK(press(session, Key::Enter));
    CHECK(press(session, Key::Num1));
    CHECK(press(session, Key::Escape));
    CHECK_FALSE(session.ropeRowsUp());
    CHECK_FALSE(session.runEnded());
    session.togglePause();
    CHECK_FALSE(session.pauseOpen());
    // A corner far from the centred plate is black through the hold.
    const render::Framebuffer mid = shot(session, 320, 180);
    CHECK(same(dark, mid));  // the veil holds: nothing under it moves the picture
    session.stepMany(MoveInput{}, kDeathHoldSteps - 1);
    CHECK_FALSE(session.ropeRowsUp());
    CHECK(same(dark, shot(session, 320, 180)));
    session.stepMany(MoveInput{}, 1);
    CHECK(session.ropeRowsUp());
    // THE ROWS rise under the plate: two, no save row, and they draw.
    CHECK(session.ropeRows() == std::vector<std::string>{"1 - A NEW MAN", "2 - LEAVE"});
    const render::Framebuffer withRows = shot(session, 320, 180);
    CHECK_FALSE(same(dark, withRows));
    CHECK(session.ropeCursor() == 0);
    CHECK(press(session, Key::Down));
    CHECK(session.ropeCursor() == 1);
    const render::Framebuffer onLeave = shot(session, 320, 180);
    CHECK_FALSE(same(withRows, onLeave));  // the inverted fill moved
    CHECK(press(session, Key::Up));
    CHECK(session.ropeCursor() == 0);
    // Still no pause, still no world: the rows are the only live input.
    session.togglePause();
    CHECK_FALSE(session.pauseOpen());
    session.interact();
    CHECK_FALSE(session.talking());
    // THE ROWS ARM ON THE FIRST PRESS (the QUIT pattern): the armed row
    // carries SURE and the device's confirm on its tail, the run has not
    // ended, and the frame changed -- the one state of the plate that is
    // not the plate. Moving the cursor or ESC disarms it.
    CHECK(press(session, Key::Num2));
    CHECK(session.ropeRowArmed() == 1);
    CHECK_FALSE(session.runEnded());
    CHECK(session.ropeRows()[1] == "2 - LEAVE -- SURE? " +
                                       std::string(render::promptConfirmKey(render::InputDevice::KeyboardMouse)));
    CHECK(session.ropeRows()[0] == "1 - A NEW MAN");
    const render::Framebuffer armedLeave = shot(session, 320, 180);
    CHECK_FALSE(same(onLeave, armedLeave));
    CHECK(press(session, Key::Escape));
    CHECK(session.ropeRowArmed() == -1);
    CHECK_FALSE(session.runEnded());
    CHECK(press(session, Key::Num2));
    CHECK(session.ropeRowArmed() == 1);
    CHECK(press(session, Key::Up));
    CHECK(session.ropeRowArmed() == -1);
    CHECK(session.ropeCursor() == 0);
    // A digit on another row arms THAT row, not the one it left.
    CHECK(press(session, Key::Num1));
    CHECK(session.ropeRowArmed() == 0);
    CHECK(press(session, Key::Num2));
    CHECK(session.ropeRowArmed() == 1);
    CHECK_FALSE(session.runEnded());
    // LEAVE, on the second press: the shipped quit.
    CHECK(press(session, Key::Num2));
    CHECK(session.runEnded());
    CHECK(session.runEndReason() == Session::RunEndChoice::Leave);
    CHECK(session.quitRequested());
}

TEST_CASE("A NEW MAN reaches the creation screen: the end reason routes the loop, the flow opens on its origin, and a fresh run has a clean ledger") {
    Session session(docksAt(20));
    Tavern& gull = session.tavern();
    CrimeLedger& crimes = gull.dialogue().crimes();
    // A nobody's witnessed killing: 24 + 5 - 30 - 8 = -9, THE ROPE.
    crimes.markMurderer(2);
    seedHearing(session, drawOf(0, 5), 10, 0, 0);
    REQUIRE(press(session, Key::Num1));
    REQUIRE(press(session, Key::Num1));
    REQUIRE(gull.hearing().judgment == Judgment::TheRope);
    session.stepMany(MoveInput{}, kJudgmentHoldSteps);
    REQUIRE(press(session, Key::Num1));
    REQUIRE(gull.executed());
    // No corpse on this roster (the killing was seeded), so the plate names
    // the ladder's own rope rather than inventing a name.
    CHECK(session.ropePlateMid() == "BY THE WARD. FOR THE SECOND RUNG.");
    session.stepMany(MoveInput{}, render::kPageEaseSteps + kDeathHoldSteps);
    REQUIRE(session.ropeRowsUp());
    // A NEW MAN, on the pad: the D-pad is already on it; A arms it -- the
    // tail names the pad's own confirm -- and A takes it.
    session.noteInputDevice(render::InputDevice::Pad);
    CHECK(press(session, Key::PadSouth));
    CHECK(session.ropeRowArmed() == 0);
    CHECK_FALSE(session.runEnded());
    CHECK(session.ropeRows()[0] == "1 - A NEW MAN -- SURE? " +
                                       std::string(render::promptConfirmKey(render::InputDevice::Pad)));
    CHECK(press(session, Key::PadSouth));
    CHECK(session.runEnded());
    CHECK(session.runEndReason() == Session::RunEndChoice::NewMan);
    CHECK_FALSE(session.quitRequested());
    // The row that ended the run stays lit with its tail: the last frame
    // before main() opens the creation window is not the bare plate.
    CHECK(session.ropeRowArmed() == 0);
    // The row is one-shot: a second press changes nothing.
    CHECK(press(session, Key::PadDown));
    CHECK(press(session, Key::PadSouth));
    CHECK(session.runEndReason() == Session::RunEndChoice::NewMan);
    CHECK_FALSE(session.quitRequested());
    // The plate is still up under the choice -- the veil holds to the last
    // frame -- and the world is still refused.
    CHECK(session.ropeCeremonyUp());
    CHECK(session.inCustody());
    // WHAT MAIN DOES WITH IT: run_client answers kRunClientNewMan and the
    // loop opens the creation window again. Headless, that window is the
    // creation FLOW, and it opens on the origin screen with nothing chosen.
    render::CreationFlow flow(content::contentDir());
    CHECK(flow.step() == render::CreationStep::Origin);
    CHECK_FALSE(flow.done());
    const render::CreationPage origin = flow.page();
    CHECK_FALSE(origin.rows.empty());
    // And a fresh run through the boot path is a fresh ledger: no rope, no
    // blood, no hearing, nothing the hanged man did.
    Session fresh(docksAt(20));
    const CrimeLedger& clean = fresh.tavern().dialogue().crimes();
    CHECK_FALSE(clean.executed());
    CHECK_FALSE(clean.murderer());
    CHECK_FALSE(clean.condemned());
    CHECK(clean.arrests() == 0);
    CHECK(clean.heat() == 0);
    CHECK_FALSE(fresh.tavern().hearingPending());
    CHECK_FALSE(fresh.inCustody());
    CHECK_FALSE(fresh.runEnded());
    CHECK(fresh.heatLine().empty());
}

// ===========================================================================
// THE HUD
// ===========================================================================

TEST_CASE("the tag is drawn: WANTED, WANTED FOR BLOOD and CONDEMNED each change the frame's corner, and the heat row is the tag lane's own line") {
    Session session(docksAt(20));
    CrimeLedger& crimes = session.tavern().dialogue().crimes();
    session.stepMany(MoveInput{}, 2);
    const render::Framebuffer clean = shot(session, 320, 180);
    REQUIRE(session.heatLine().empty());

    crimes.addHeat(kWarrantAt + 10);
    // The row's own ease, fully up.
    session.stepMany(MoveInput{}, 16);
    REQUIRE(session.heatLine() == "WANTED  HEAT 70");
    const render::Framebuffer wanted = shot(session, 320, 180);
    CHECK_FALSE(same(clean, wanted));

    crimes.markMurderer(2);
    session.stepMany(MoveInput{}, 16);
    REQUIRE(session.heatLine() == "WANTED FOR BLOOD  HEAT 100");
    const render::Framebuffer blood = shot(session, 320, 180);
    CHECK_FALSE(same(wanted, blood));

    crimes.sentence(Judgment::Commuted, kCommutedDays);
    session.stepMany(MoveInput{}, 16);
    REQUIRE(session.heatLine() == "CONDEMNED  HEAT " + std::to_string(kHeatAfterSentence));
    const render::Framebuffer condemned = shot(session, 320, 180);
    CHECK_FALSE(same(blood, condemned));
    CHECK_FALSE(same(clean, condemned));
    // The three differ in the top-right stack the heat row lives in, not
    // only somewhere on the frame.
    const int x0 = 320 * 2 / 3;
    const int y1 = 180 / 3;
    bool cornerDiffers = false;
    for (int y = 0; y < y1 && !cornerDiffers; ++y) {
        for (int x = x0; x < 320; ++x) {
            if (wanted.pixels()[wanted.index(x, y)] != blood.pixels()[blood.index(x, y)]) {
                cornerDiffers = true;
                break;
            }
        }
    }
    CHECK(cornerDiffers);
}

// ===========================================================================
// THE CHECK BLOCK'S TERMS, THE TAG ROW'S WIDTH, AND WHAT EACH TIER SAYS
// ===========================================================================

TEST_CASE("the arithmetic is packed by term: a sign never wraps away from its term at any window the game runs at") {
    // The packer itself: rows break only between terms.
    const std::vector<std::string> terms = {"24 THE FLAME", "+ 24 THE DOOR", "- 20 THE WARD",
                                            "- 1 HEAT", "MAKES 27"};
    CHECK(render::packTerms(terms, 30) ==
          std::vector<std::string>{"24 THE FLAME + 24 THE DOOR", "- 20 THE WARD - 1 HEAT",
                                   "MAKES 27"});
    CHECK(render::packTerms(terms, 100) ==
          std::vector<std::string>{"24 THE FLAME + 24 THE DOOR - 20 THE WARD - 1 HEAT MAKES 27"});
    // A term wider than the row stands alone; it is never cut.
    CHECK(render::packTerms(terms, 8) == terms);
    CHECK(render::packTerms({}, 30).empty());

    // On the page: the widest block the bench prints -- a denial with the
    // tongue, the door, the ward, the priors and the heat all weighed -- at
    // every window. Every packed row fits the detail pane, every row is
    // whole terms, and no row ends on a sign.
    Session session(docksAt(20));
    thiefLedger(session);
    CrimeLedger& crimes = session.tavern().dialogue().crimes();
    crimes.sentence(Judgment::Held, 1);  // a prior: TAKEN BEFORE weighs
    crimes.commit(Crime::Lift, true);
    crimes.addHeat(kWarrantAt + 8);
    seedHearing(session, drawOf(-10, 5), 40, 84, -15);
    REQUIRE(press(session, Key::Num2));
    REQUIRE(press(session, Key::Num2));
    session.stepMany(MoveInput{}, kJudgmentHoldSteps + 1);
    const render::HearingPageState judged = session.hearingPageState();
    REQUIRE(judged.view == render::HearingView::Judged);
    INFO(judged.arithmetic);
    REQUIRE(judged.arithmeticTerms.size() >= 6);
    CHECK(judged.pleaTerms ==
          std::vector<std::string>{"- 10 THE PRIEST IS A MAN",
                                   "MAKES " + std::to_string(session.tavern().hearing().scored)});
    for (const Window& window : kWindows) {
        INFO("window ", window.width, "x", window.height);
        const render::HearingPageMetrics metrics =
            render::hearingPageMetrics(judged, window.width, window.height);
        REQUIRE(metrics.usable);
        REQUIRE(metrics.split);
        for (const std::vector<std::string>* tokens :
             {&judged.arithmeticTerms, &judged.pleaTerms}) {
            const std::vector<std::string> rows = render::packTerms(*tokens, metrics.detailCells);
            std::string joined;
            for (const std::string& row : rows) {
                INFO(row);
                CHECK(static_cast<int>(row.size()) <= metrics.detailCells);
                CHECK(row.back() != '+');
                CHECK(row.back() != '-');
                if (!joined.empty()) {
                    joined += ' ';
                }
                joined += row;
            }
            std::string whole;
            for (const std::string& token : *tokens) {
                if (!whole.empty()) {
                    whole += ' ';
                }
                whole += token;
            }
            CHECK(joined == whole);
        }
        // And the block still fits the body at this width.
        CHECK(metrics.detailRowsWanted <= metrics.bodyRows);
    }
}

TEST_CASE("the tag row fits its thirty-four columns by construction: the number is never lost, a rider that does not fit is dropped whole") {
    Session session(docksAt(20));
    CrimeLedger& crimes = session.tavern().dialogue().crimes();
    // A commuted man with fresh blood on him: WANTED FOR BLOOD, and the
    // CONDEMNED word gives way to it -- the two together are thirty-six.
    // The hand the commutation took still shows: MAIMED beside the blood
    // fits the row.
    crimes.sentence(Judgment::Commuted, kCommutedDays);
    REQUIRE(crimes.condemned());
    REQUIRE(crimes.maimed());
    crimes.markMurderer(2);
    REQUIRE(crimes.warrant());
    std::string line = session.heatLine();
    INFO(line);
    CHECK(line == "MAIMED  WANTED FOR BLOOD  HEAT " + std::to_string(crimes.heat()));
    CHECK(line.size() <= 34);
    CHECK(line.find("CONDEMNED") == std::string::npos);
    CHECK(line.find("HEAT " + std::to_string(crimes.heat())) != std::string::npos);
    // A rider that would push the row past its width is dropped whole,
    // never cut mid-word, and the number stays.
    crimes.takeLoot(12);
    line = session.heatLine();
    INFO(line);
    CHECK(line == "MAIMED  WANTED FOR BLOOD  HEAT " + std::to_string(crimes.heat()));
    CHECK(line.size() <= 34);
    // The riders on a plain murderer's row: the loot fits and rides; the
    // bale would not and is dropped whole.
    Session plain(docksAt(20));
    CrimeLedger& blood = plain.tavern().dialogue().crimes();
    blood.markMurderer(2);
    REQUIRE(blood.warrant());
    blood.takeLoot(12);
    line = plain.heatLine();
    INFO(line);
    CHECK(line == "WANTED FOR BLOOD  HEAT " + std::to_string(blood.heat()) + "  LOOT 12");
    CHECK(line.size() <= 34);
    blood.takeBale(Contraband::Flower, kBaleUnits);
    REQUIRE(blood.carryingBale());
    line = plain.heatLine();
    INFO(line);
    CHECK(line.size() <= 34);
    CHECK(line.find("HEAT " + std::to_string(blood.heat())) != std::string::npos);
    CHECK(line.find("LOOT 12") != std::string::npos);
    CHECK(line.find("BALE") == std::string::npos);
    // MAIMED beside the blood is the row's full width and keeps both.
    Session other(docksAt(20));
    CrimeLedger& maimed = other.tavern().dialogue().crimes();
    maimed.sentence(Judgment::TheHand, 1);
    maimed.markMurderer(3);
    while (maimed.heat() < 100) {
        maimed.addHeat(1);
    }
    line = other.heatLine();
    INFO(line);
    CHECK(line == "MAIMED  WANTED FOR BLOOD  HEAT 100");
    CHECK(line.size() == 34);
}

TEST_CASE("each tier's consequence says only what is on the table: no rope on a cell or a hand charge, the two answers on the rope's") {
    Session session(docksAt(20));
    thiefLedger(session);
    seedHearing(session, drawOf(0, 5), 12, 0, -15);
    render::HearingPageState page = session.hearingPageState();
    // A thief the Mission does not know: the cold opening, none of the
    // door's rows.
    const BarkTables& barks = session.tavern().dialogue().barks();
    CHECK(page.priest == std::string(barks.line("court.paper", 0)));
    CHECK(page.priest.find("gave at this door") == std::string::npos);
    CHECK(std::find(page.paper.begin(), page.paper.end(), "THE MISSION DOES NOT KNOW YOU.") !=
          page.paper.end());
    // The cell tier: a confession is never doubled and never spared, and
    // there is no rope to mention.
    CHECK(page.cursor == 0);
    CHECK(page.consequence.find("NEVER DOUBLED, AND NEVER SPARED.") != std::string::npos);
    CHECK(page.consequence.find("ROPE") == std::string::npos);
    CHECK(page.consequence.find("MERCY") == std::string::npos);
    session.moveCourtCursor(1);
    page = session.hearingPageState();
    CHECK(page.consequence.find("THE SENTENCE DOUBLES") != std::string::npos);
    CHECK(page.consequence.find("ROPE") == std::string::npos);

    // The hand tier: a Skyrunner's first, the same two literals -- the
    // hand is on the table, the rope is not -- and the roofs' own opening.
    Session roofs(docksAt(20));
    thiefLedger(roofs);
    seedHearing(roofs, drawOf(0, 5), 12, 0, -15, true);
    page = roofs.hearingPageState();
    REQUIRE(page.asks == "THE PAPER ASKS FOR THE HAND.");
    CHECK(page.priest == std::string(barks.line("court.roofs.hand", 0)));
    CHECK(page.consequence.find("ROPE") == std::string::npos);
    CHECK(page.consequence.find("NEVER DOUBLED, AND NEVER SPARED.") != std::string::npos);
    mustRead("consequence", page.consequence);
    // THE HAND TIER'S OWN LINES in the block: 38 buys HELD with the hand
    // spared and 14 THE HAND with nights; there is no FINED on this tier and
    // the row does not print one. A confession's row has no SPARED either.
    REQUIRE(press(roofs, Key::Num1));
    REQUIRE(press(roofs, Key::Num1));
    REQUIRE(roofs.tavern().hearing().judged());
    page = roofs.hearingPageState();
    CHECK(page.lines == "THE LINES: 38 HELD  14 THE HAND");
    CHECK(page.lines.find("FINED") == std::string::npos);
    mustRead("lines", page.lines);
    Session denied(docksAt(20));
    thiefLedger(denied);
    seedHearing(denied, drawOf(0, 5), 12, 0, -15, true);
    REQUIRE(press(denied, Key::Num2));
    REQUIRE(press(denied, Key::Num2));
    REQUIRE(denied.tavern().hearing().judged());
    page = denied.hearingPageState();
    CHECK(page.lines == "THE LINES: 55 SPARED  38 HELD  14 THE HAND");
    // And the paper tier's row still names the fine and the cell.
    Session paper(docksAt(20));
    thiefLedger(paper);
    seedHearing(paper, drawOf(0, 5), 12, 0, -15);
    REQUIRE(press(paper, Key::Num2));
    REQUIRE(press(paper, Key::Num2));
    page = paper.hearingPageState();
    CHECK(page.lines == "THE LINES: 55 SPARED  38 FINED  14 HELD");
}

TEST_CASE("the priest's openings are chosen by the case, and every row of the chosen table is true of the sheet") {
    const auto opening = [](std::int32_t temple, bool skyrunner, bool second, bool blood) {
        Session session(docksAt(20));
        CrimeLedger& crimes = session.tavern().dialogue().crimes();
        if (second) {
            crimes.commit(Crime::RoofRun, true);
            crimes.sentence(Judgment::Held, 1);
        }
        crimes.commit(Crime::Lift, true);
        crimes.addHeat(kWarrantAt + 2);
        if (blood) {
            crimes.markMurderer(2);
        }
        seedHearing(session, drawOf(0, 5), 10, temple, 0, skyrunner);
        const render::HearingPageState page = session.hearingPageState();
        const BarkTables& barks = session.tavern().dialogue().barks();
        std::string table;
        for (const char* key : {"court.paper", "court.paper.door", "court.blood",
                                "court.roofs.hand", "court.roofs.rope", "court.nothing"}) {
            const std::vector<std::string>* rows = barks.rows(key);
            REQUIRE(rows != nullptr);
            if (std::find(rows->begin(), rows->end(), page.priest) != rows->end()) {
                table = key;
            }
        }
        return table;
    };
    CHECK(opening(0, false, false, false) == "court.paper");
    CHECK(opening(1, false, false, false) == "court.paper.door");
    CHECK(opening(84, false, false, false) == "court.paper.door");
    CHECK(opening(0, true, false, false) == "court.roofs.hand");
    CHECK(opening(0, true, true, false) == "court.roofs.rope");
    CHECK(opening(0, false, false, true) == "court.blood");
    CHECK(opening(0, true, true, true) == "court.blood");  // the corpse outranks the roofs
    // The rows that depend on the sheet live only in the table keyed by it:
    // no cold row names a gift at the door, no hand row names the rope, no
    // rope row names the hand.
    Session session(docksAt(20));
    const BarkTables& barks = session.tavern().dialogue().barks();
    for (const std::string& row : *barks.rows("court.paper")) {
        INFO(row);
        CHECK(row.find("gave") == std::string::npos);
    }
    for (const std::string& row : *barks.rows("court.roofs.hand")) {
        INFO(row);
        CHECK(row.find("rope") == std::string::npos);
        CHECK(row.find("second") == std::string::npos);
    }
    for (const std::string& row : *barks.rows("court.roofs.rope")) {
        INFO(row);
        CHECK(row.find("hand") == std::string::npos);
    }
    // No judgment row names a shape the judgment may not have: HELD never
    // names a corpse, the lie never names the nights or the fine, SPARED
    // never thanks a gift.
    for (const std::string& row : *barks.rows("court.held")) {
        INFO(row);
        CHECK(row.find(" him") == std::string::npos);
    }
    for (const std::string& row : *barks.rows("court.lie")) {
        INFO(row);
        CHECK(row.find("night") == std::string::npos);
        CHECK(row.find("fine") == std::string::npos);
    }
    for (const std::string& row : *barks.rows("court.spared")) {
        INFO(row);
        CHECK(row.find("gave") == std::string::npos);
    }
    // And the learned word is never said to a layman, not even nearly; no
    // spoken row carries an em-dash or the design document's own words.
    for (const BarkTables::Table& table : barks.tables()) {
        if (table.key.rfind("court.", 0) != 0) {
            continue;
        }
        for (const std::string& row : table.rows) {
            INFO(table.key, ": ", row);
            CHECK(row.find("Bloodletter") == std::string::npos);
            CHECK(row.find("do not name") == std::string::npos);
            CHECK(row.find("Canon") == std::string::npos);
            CHECK(row.find(" -- ") == std::string::npos);
        }
    }
}
