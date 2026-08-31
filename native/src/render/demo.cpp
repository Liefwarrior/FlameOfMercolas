#include "granadad/render/demo.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "granadad/render/capture.hpp"
#include "granadad/render/panel.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/region_path.hpp"
#include "granadad/sim/stealth.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render {
namespace {

// ---------------------------------------------------------------------------
// THE ROUTE
// ---------------------------------------------------------------------------
//
// Every coordinate below is either a constant this build already owns
// (sim::docks::kSpawnTileX/Y) or a number read straight off content the ward is
// generated from -- the casebook's own authored lead sites, the sign table's
// door coordinates, and the Saltgate roadway's cells as the generator lays them
// down. NOTHING HERE INVENTS A PLACE. The demo is routing and presentation; it
// authors no content, no map data and no names.
//
// THE ORDER, and why it is the brief's order with one change. The route builds:
// a character, a ward, a landmark, a case, a map, a night. The one change is
// that the three-leads plate lands TWICE -- once at the Mission and once at the
// Weighhouse -- because that is simply what the authored trail does
// (mission-backroom opens three, weighhouse-ledger opens three), and cutting
// the first would have meant walking to the Weighhouse with nothing having
// opened the lead that sends you there. The second one is the one the caption
// names, because Harl's Yard comes out of it and Harl's Yard is where the map
// cursor lands.
//
// CUTS BETWEEN SECTIONS, WALKS INSIDE THEM. The quay, the head of Saltgate
// Rise, the Mission and the Weighhouse are between forty and ninety tiles
// apart; a route that walked every link would be twenty minutes of pavement and
// would be the pathfinder's demo rather than the ward's. So each section cuts
// to its mark and then walks, on foot, through the real geometry, with the real
// router -- which is what the shots are actually of.

constexpr std::int32_t kQuayside = sim::docks::kBandQuayside;
constexpr std::int32_t kMidSlope = sim::docks::kBandMidSlope;

// The Mission's lantern-turret: a 4x4 granite shaft the generator raises at
// local (86-89, 66-69), z12-z15. Local + 32, band = z + 8 -- docks.hpp's one
// documented conversion rule -- puts its centre at global (119.5, 99.5) with
// its crown at band 23, one band over the ward's tallest existing masonry.
constexpr std::int32_t kTurretX = 119;
constexpr std::int32_t kTurretY = 99;

// THE VANTAGE THE TURRET IS ACTUALLY VISIBLE FROM, and it is measured rather
// than reasoned about. The turret shares the Mission's x, so the sightline that
// works is straight up its own column from out on the piers: eleven positions
// were photographed (the spawn, the Tarwalk at three points, the Ropewynd, and
// the pier column at y42/45/47/50/52/54/56) and the turret clears the roofline
// from y47 through y56 and from nowhere inside the Tarwalk corridor at all.
// y42 and y45 are inside a warehouse face. The route walks 47 -> 54.
constexpr std::int32_t kPierX = kTurretX;

// The Saltgate gate-house. Two 2x2 granite towers at local x70-71 and x80-81,
// y115-116; the roadway between them (local x72-79) stays open. Global: towers
// at x102-103 and x112-113, roadway x104-111, the frame at y147-148. The Rise
// runs due north-south, so the view UP it from downhill is azimuth 180 -- and
// the backdrop palace's main mass spans azimuth 175.5-184.5, dead centre in the
// opening. The generator's own arithmetic says the lintel soffit sits about 1.4
// tiles over the eye and the palace stands 8.8 degrees up, so the two subtend
// the same angle at about nine tiles out: closer and the whole mass shows in
// the opening, further and the lintel crops its top. The route walks THROUGH
// that range rather than picking one number out of it.
constexpr std::int32_t kGateX = 107;
constexpr std::int32_t kGateY = 147;

const DemoBeat kRoute[] = {
    // -----------------------------------------------------------------------
    // 1. THE WARD, FROM THE QUAY
    //
    // Two vantages, because the ward has two things to say here and one camera
    // position cannot say both. The Tarwalk at the authored spawn is what the
    // game looks like to play -- a working street corridor, signed doors,
    // people at their day. The pier out on the water is where the SKYLINE is:
    // measured, not guessed, by photographing the district from eleven
    // positions, and the Mission's turret only ever clears the roofline from
    // out on the quayside proper. From inside the Tarwalk it is behind a
    // warehouse, which is exactly what a warehouse is for.
    // -----------------------------------------------------------------------
    {"quay", DemoAct::Card, 200, 0, 0, 0, 0, "GRANADAD", "THE DARKSTREETS", "", nullptr, -1},
    {"quay", DemoAct::Cut, 1, sim::docks::kSpawnTileX, sim::docks::kSpawnTileY, kQuayside, 265,
     nullptr, nullptr, nullptr, nullptr, -1},
    {"quay", DemoAct::Card, 170, 0, 0, 0, 0, "THE DOCKS", "EIGHT IN THE MORNING", nullptr,
     nullptr, -1},
    {"quay", DemoAct::Hold, 130, 0, 0, 0, 0, nullptr, nullptr,
     "THE DOCKS OF GRANADAD. YOU CAME IN ON THE TIDE.", "quay-spawn", 105},
    {"quay", DemoAct::Walk, 300, 148, 64, 0, 0, nullptr, nullptr,
     "EVERY DOOR AND EVERY STREET IN THIS WARD IS NAMED AND SIGNED.", nullptr, -1},
    {"quay", DemoAct::Hold, 150, 0, 0, 0, 0, nullptr, nullptr, nullptr, "quay-tarwalk", -1},
    {"quay", DemoAct::Cut, 1, kPierX, 47, kQuayside, 180, nullptr, nullptr, "", nullptr, -1},
    {"quay", DemoAct::Hold, 120, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, -1},
    {"quay", DemoAct::Walk, 300, kPierX, 54, 0, 0, nullptr, nullptr, nullptr, nullptr, -1},
    {"quay", DemoAct::Pan, 30, 0, 0, 0, 180, nullptr, nullptr, nullptr, nullptr, -1},
    {"quay", DemoAct::Hold, 200, 0, 0, 0, 0, nullptr, nullptr,
     "THE MISSION'S LANTERN-TURRET STANDS OVER EVERY ROOF IN THE WARD.", "quay-turret", -1},

    // -----------------------------------------------------------------------
    // 2. THE SALTGATE GATE-HOUSE, FROM DOWNHILL
    //
    // NINETEEN TILES OUT, THEN FIFTEEN, THEN TEN -- walked, on the road's own
    // centreline, so the frame closes on the palace as the eye comes up the
    // Rise. Photographed at 20/17/14/11/9 first: at nineteen the whole gate is
    // in shot with the palace a low grey mass dead centre in the opening; by
    // ten the lintel is leaving the top of the frame and by six the towers own
    // the picture and the palace is gone. So the route stops at ten.
    // -----------------------------------------------------------------------
    {"saltgate", DemoAct::Card, 170, 0, 0, 0, 0, "SALTGATE RISE", "THE CHOKEPOINT ROAD", "",
     nullptr, -1},
    {"saltgate", DemoAct::Cut, 1, kGateX, kGateY - 19, kMidSlope, 180, nullptr, nullptr, nullptr,
     nullptr, -1},
    {"saltgate", DemoAct::Hold, 170, 0, 0, 0, 0, nullptr, nullptr,
     "NINETEEN TILES DOWNHILL: THE GATE-HOUSE, AND THE PALACE BEHIND IT.", "gate-19", -1},
    {"saltgate", DemoAct::Walk, 220, kGateX, kGateY - 15, 0, 0, nullptr, nullptr, "", nullptr,
     -1},
    {"saltgate", DemoAct::Pan, 40, 0, 0, 0, 180, nullptr, nullptr, nullptr, nullptr, -1},
    {"saltgate", DemoAct::Hold, 190, 0, 0, 0, 0, nullptr, nullptr,
     "THE PALACE SITS DEAD CENTRE IN THE GATE'S OPENING.", "gate-15", -1},
    {"saltgate", DemoAct::Walk, 240, kGateX, kGateY - 10, 0, 0, nullptr, nullptr, "", nullptr,
     -1},
    {"saltgate", DemoAct::Pan, 40, 0, 0, 0, 180, nullptr, nullptr, nullptr, nullptr, -1},
    {"saltgate", DemoAct::Hold, 180, 0, 0, 0, 0, nullptr, nullptr,
     "THE ROAD NARROWS, SAYS ITS NAME, AND OPENS AGAIN.", "gate-10", -1},

    // -----------------------------------------------------------------------
    // 3. THE INVESTIGATION -- the beat the whole route is built to reach
    //
    // Both clue sites are WALKED TO from the street outside their building,
    // through the real door, by the district's own router. Nothing is
    // teleported onto a clue: a demo that cut straight to the flagstone would
    // be showing the plate and hiding the game.
    // -----------------------------------------------------------------------
    {"case", DemoAct::Card, 200, 0, 0, 0, 0, "THE BLOODLETTER", "ONE OPEN LEAD", "", nullptr, -1},
    {"case", DemoAct::Cut, 1, 120, 95, kQuayside, 180, nullptr, nullptr, nullptr, nullptr, -1},
    {"case", DemoAct::Hold, 150, 0, 0, 0, 0, nullptr, nullptr,
     "THE MISSION OF THE FLAME. THE CASE STARTS HERE.", "case-mission", -1},
    {"case", DemoAct::Walk, 560, 125, 110, 0, 0, nullptr, nullptr, "", nullptr, -1},
    {"case", DemoAct::Examine, 1, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, -1},
    // THE SHUTTER GOES EARLY HERE. The lead-opened plate is up for three
    // seconds; this hold is nearly four, because the words on the plate want
    // reading. See DemoBeat::shotAt.
    {"case", DemoAct::Hold, 340, 0, 0, 0, 0, nullptr, nullptr,
     "READ THE ROOM. THREE MORE LEADS OPEN.", "case-mission-read", 15},
    {"case", DemoAct::Cut, 1, 95, 62, kQuayside, 180, nullptr, nullptr, "", nullptr, -1},
    {"case", DemoAct::Hold, 140, 0, 0, 0, 0, nullptr, nullptr,
     "THE WEIGHHOUSE. HARBORMASTER AND CUSTOMS HOUSE.", "case-weighhouse", -1},
    {"case", DemoAct::Walk, 420, 99, 69, 0, 0, nullptr, nullptr, "", nullptr, -1},
    {"case", DemoAct::Examine, 1, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, -1},
    {"case", DemoAct::Hold, 350, 0, 0, 0, 0, nullptr, nullptr,
     "CRELL'S LEDGER. THREE LEADS OPEN AT ONCE.", "case-weighhouse-read", 15},
    // The book, the lead, and the lead's own commit verb -- which is what puts
    // the ward map on Harl's Yard. Not a Map beat: the point is that the
    // casebook SENDS you there.
    {"case", DemoAct::Casebook, 1, 0, 0, 0, 0, nullptr, nullptr, "", nullptr, -1},
    {"case", DemoAct::Hold, 210, 0, 0, 0, 0, nullptr, nullptr, nullptr, "case-book", -1},
    {"case", DemoAct::CasebookLead, 1, 0, 0, 0, 0, "harls-yard", nullptr, nullptr, nullptr, -1},
    {"case", DemoAct::Hold, 210, 0, 0, 0, 0, nullptr, nullptr, nullptr, "case-book-harls", -1},
    {"case", DemoAct::Commit, 1, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, -1},
    {"case", DemoAct::Hold, 240, 0, 0, 0, 0, nullptr, nullptr, nullptr, "case-map-harls", -1},

    // -----------------------------------------------------------------------
    // 4. THE MAP -- names sitting inside their own building shapes
    // -----------------------------------------------------------------------
    {"map", DemoAct::MapZoom, 1, 1, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, -1},
    {"map", DemoAct::Hold, 200, 0, 0, 0, 0, nullptr, nullptr, nullptr, "map-zoom2", -1},
    {"map", DemoAct::MapZoom, 1, 1, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, -1},
    {"map", DemoAct::Hold, 220, 0, 0, 0, 0, nullptr, nullptr, nullptr, "map-zoom3", -1},
    {"map", DemoAct::Close, 1, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, -1},

    // -----------------------------------------------------------------------
    // 5. NIGHT
    //
    // The pier again, at eleven at night: the ward goes black and the turret's
    // lamp is a point at the skyline. Then back into the Tarwalk, which is lit,
    // because the contrast IS the beat -- the district's light law is
    // up-slope bright, waterline dim, and the Gullet black.
    // -----------------------------------------------------------------------
    {"night", DemoAct::Card, 190, 0, 0, 0, 0, "AFTER DARK", "THE BEACON BURNS ALL NIGHT", "",
     nullptr, -1},
    {"night", DemoAct::Clock, 1, 23, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, -1},
    {"night", DemoAct::Cut, 1, kPierX, 50, kQuayside, 180, nullptr, nullptr, nullptr, nullptr,
     -1},
    {"night", DemoAct::Hold, 260, 0, 0, 0, 0, nullptr, nullptr,
     "THE TURRET'S LAMP AT THE SKYLINE, OVER A BLACK WARD.", "night-beacon", -1},
    {"night", DemoAct::Cut, 1, 150, 63, kQuayside, 265, nullptr, nullptr, "", nullptr, -1},
    {"night", DemoAct::Hold, 220, 0, 0, 0, 0, nullptr, nullptr,
     "LAMPLIT WHERE THE WARD PAYS FOR LAMPS, BLACK WHERE IT DOES NOT.", "night-tarwalk", -1},

    // -----------------------------------------------------------------------
    // THE END CARD -- and the route ends the run, so the last thing on screen
    // is this and not a street corner with nobody driving.
    // -----------------------------------------------------------------------
    {"end", DemoAct::Card, 330, 0, 0, 0, 0, "GRANADAD", "THE DARKSTREETS", "", "end-card", 260},
};

std::vector<DemoBeat>& routeStore() {
    static std::vector<DemoBeat> route(std::begin(kRoute), std::end(kRoute));
    return route;
}

/// The shortest signed turn from `from` to `to`, in the sim's own units.
[[nodiscard]] std::int32_t turnDelta(std::int32_t from, std::int32_t to) noexcept {
    const std::int32_t raw = (to - from) & (sim::kTurnFull - 1);
    return raw > sim::kTurnHalf ? raw - sim::kTurnFull : raw;
}

/// The nearest standable cell to (x, y) on `band`, searched in a fixed
/// widening square so the answer is a pure function of the request. Returns
/// false when nothing within eight tiles will hold a body -- which is a real
/// answer and is reported rather than papered over.
[[nodiscard]] bool nearestStandable(const Session& session, std::int32_t x, std::int32_t y,
                                    std::int32_t band, std::int32_t* outX, std::int32_t* outY) {
    if (session.tiles().standable(x, y, band)) {
        *outX = x;
        *outY = y;
        return true;
    }
    for (std::int32_t ring = 1; ring <= 8; ++ring) {
        for (std::int32_t dy = -ring; dy <= ring; ++dy) {
            for (std::int32_t dx = -ring; dx <= ring; ++dx) {
                if (std::abs(dx) != ring && std::abs(dy) != ring) {
                    continue;
                }
                if (session.tiles().standable(x + dx, y + dy, band)) {
                    *outX = x + dx;
                    *outY = y + dy;
                    return true;
                }
            }
        }
    }
    return false;
}

/// True while something the player would call a page owns the screen. The
/// caption stands down under all of them: a page IS the content, and a line of
/// commentary over one is the exact fighting-for-the-same-pixels this build's
/// UI conventions exist to stop.
[[nodiscard]] bool pageOwnsScreen(const Session& session) {
    return session.casebookOpen() || session.districtMapOpen() || session.keysOpen() ||
           session.pauseOpen() || session.optionsOpen() || session.grimoireOpen() ||
           session.waitOpen() || session.talking();
}

}  // namespace

const std::vector<DemoBeat>& demoRoute() { return routeStore(); }

std::vector<std::string> demoSections() {
    std::vector<std::string> out;
    for (const DemoBeat& beat : routeStore()) {
        if (out.empty() || out.back() != beat.section) {
            out.emplace_back(beat.section);
        }
    }
    return out;
}

int demoFrameCount(const std::string& section) {
    const std::vector<DemoBeat>& route = routeStore();
    int total = 0;
    bool counting = section.empty();
    for (const DemoBeat& beat : route) {
        if (!counting && section == beat.section) {
            counting = true;
        }
        if (counting) {
            total += std::max(1, beat.frames);
        }
    }
    return total;
}

DemoDirector::DemoDirector(std::string section, std::filesystem::path shotDir)
    : route_(&routeStore()), shotDir_(std::move(shotDir)) {
    if (!section.empty()) {
        for (std::size_t i = 0; i < route_->size(); ++i) {
            if (section == (*route_)[i].section) {
                at_ = static_cast<int>(i);
                break;
            }
        }
    }
}

void DemoDirector::enter(Session& session) {
    const DemoBeat& beat = (*route_)[static_cast<std::size_t>(at_)];
    entered_ = true;
    held_ = 0;
    stuck_ = 0;
    leg_ = 0;
    pathX_.clear();
    pathY_.clear();
    yawFrom_ = session.body().yaw();

    if (beat.caption != nullptr) {
        caption_ = beat.caption;
    }
    if (beat.act == DemoAct::Card) {
        cardLine_ = beat.line == nullptr ? "" : beat.line;
        cardSub_ = beat.sub == nullptr ? "" : beat.sub;
    }

    switch (beat.act) {
        case DemoAct::Cut: {
            // A CUT CLOSES WHATEVER WAS UP. A demo that teleports across the
            // ward with the casebook still open would be photographing the
            // book, and the next beat's caption would be describing a street
            // nobody can see.
            if (session.talking()) {
                session.closeConversation();
            }
            if (session.casebookOpen()) {
                session.toggleCasebook();
            }
            if (session.districtMapOpen()) {
                session.toggleDistrictMap();
            }
            std::int32_t x = beat.a;
            std::int32_t y = beat.b;
            if (nearestStandable(session, beat.a, beat.b, beat.c, &x, &y)) {
                session.body().placeAt(x, y, beat.c);
            }
            session.body().setYaw(sim::angle_from_degrees(beat.degrees));
            yawFrom_ = session.body().yaw();
            break;
        }
        case DemoAct::Walk: {
            sim::TileBox box;
            box.x0 = 0;
            box.y0 = 0;
            box.x1 = session.tiles().sizeX() - 1;
            box.y1 = session.tiles().sizeY() - 1;
            box.z0 = session.body().band();
            box.z1 = session.body().band();
            std::int32_t gx = beat.a;
            std::int32_t gy = beat.b;
            (void)nearestStandable(session, beat.a, beat.b, session.body().band(), &gx, &gy);
            sim::RegionPath router(session.tiles(), box);
            std::vector<sim::PathStep> route;
            const sim::PathStep from{session.body().tileX(), session.body().tileY(),
                                     session.body().band()};
            const sim::PathStep to{gx, gy, session.body().band()};
            if (router.find(from, to, route)) {
                for (const sim::PathStep& step : route) {
                    pathX_.push_back(step.x);
                    pathY_.push_back(step.y);
                }
            }
            break;
        }
        case DemoAct::Examine:
            session.examine();
            break;
        case DemoAct::Casebook:
            session.toggleCasebook();
            break;
        case DemoAct::CasebookLead:
            if (beat.line != nullptr) {
                (void)session.selectCasebookLead(beat.line);
            }
            break;
        case DemoAct::Commit:
            session.commitCasebookLead();
            break;
        case DemoAct::Map:
            session.toggleDistrictMap();
            break;
        case DemoAct::MapZoom:
            session.adjustDistrictMapZoom(beat.a);
            break;
        case DemoAct::Close:
            if (session.districtMapOpen()) {
                session.toggleDistrictMap();
            }
            if (session.casebookOpen()) {
                session.toggleCasebook();
            }
            if (session.talking()) {
                session.closeConversation();
            }
            break;
        case DemoAct::Clock:
            session.skipToHour(beat.a);
            break;
        case DemoAct::Card:
        case DemoAct::Hold:
        case DemoAct::Face:
        case DemoAct::Pan:
            break;
    }
}

DemoDirector::Tick DemoDirector::advance(Session& session) {
    Tick tick;
    if (finished_ || route_->empty()) {
        tick.running = false;
        return tick;
    }
    if (!entered_) {
        enter(session);
    }
    const DemoBeat& beat = (*route_)[static_cast<std::size_t>(at_)];
    const int span = std::max(1, beat.frames);

    // The card and the caption are STATE, eased, and each owns its own toggle
    // -- this build's UI convention for every overlay it has.
    card_.setTarget(beat.act == DemoAct::Card);
    // AND IT STANDS DOWN FOR THE LEAD-OPENED PLATE TOO, which the second run
    // of this route is what found: the plate lives in the bottom band, the
    // caption was sitting on top of it, and the two beats whose whole subject
    // is "three leads just opened" were photographed with the announcement
    // hidden underneath a line of commentary about it. Session::casePlateWanted
    // already existed for the HUD; this is the same fact, used the same way.
    captionFade_.setTarget(!caption_.empty() && beat.act != DemoAct::Card &&
                           !pageOwnsScreen(session) && !session.casePlateWanted());
    card_.advance();
    captionFade_.advance();

    switch (beat.act) {
        case DemoAct::Face:
        case DemoAct::Pan: {
            const std::int32_t target =
                beat.act == DemoAct::Pan
                    ? sim::angle_from_degrees(beat.degrees)
                    : sim::bearingTo(session.body().tileX(), session.body().tileY(), beat.a,
                                     beat.b);
            // EASED FROM THE BEAT'S OWN START YAW, by fraction of the beat --
            // so the turn is a pure function of how far into the beat the
            // playhead is and never of how the last frame happened to land.
            const std::int32_t delta = turnDelta(yawFrom_, target);
            const std::int32_t travelled =
                static_cast<std::int32_t>((static_cast<std::int64_t>(delta) * (held_ + 1)) / span);
            session.body().setYaw(yawFrom_ + travelled);
            break;
        }
        case DemoAct::Walk: {
            if (leg_ < pathX_.size()) {
                const std::int32_t goalX = sim::q8_tile_centre(pathX_[leg_]);
                const std::int32_t goalY = sim::q8_tile_centre(pathY_[leg_]);
                const std::int32_t dx = goalX - session.body().x();
                const std::int32_t dy = goalY - session.body().y();
                // AN EIGHTH OF A TILE, the same slop runSmoke's walker uses and
                // for the same reason: a half leaves the eye pressed against
                // the next cell's face.
                const std::int32_t tolerance = sim::kSubOne / 8;
                const bool closeX = std::abs(dx) < tolerance;
                const bool closeY = std::abs(dy) < tolerance;
                if (closeX && closeY) {
                    ++leg_;
                    stuck_ = 0;
                } else {
                    // THE HEAD AND THE FEET ARE SEPARATE, and BOTH HALVES OF
                    // THAT WERE WRONG IN THE FIRST TWO VERSIONS -- the guard in
                    // test_demo.cpp caught it twice, which is the whole reason
                    // the guard drives the route rather than reading the table.
                    //
                    // WHAT WENT WRONG. Version one eased the head and then
                    // walked FORWARD along it, so a head still mid-turn put the
                    // body into the jamb of the Mission's one-tile door.
                    // Version two resolved an EIGHT-way forward/strafe toward
                    // the waypoint, which walks a clean diagonal at a doorway
                    // and jams on the corner instead. Both left readCount at
                    // zero on a route whose whole point is reading clues.
                    //
                    // WHAT IS RIGHT. RegionPath's legs are four-way, so the
                    // push is FOUR-way too: the dominant axis, exactly, in
                    // world space -- runSmoke's walker's own rule. The head
                    // eases toward the bearing independently, so the picture
                    // still turns like a person and the feet still track the
                    // router's tiles.
                    //
                    // AND IT HAS THE SAME FALLBACK LADDER, spread over time
                    // instead of over one step: runSmoke's walker tries each
                    // compass direction inside a single step and reads "did the
                    // body move" as "was that way open". This one cannot -- it
                    // gets one MoveInput per frame -- so a leg that has not
                    // moved swaps to the other axis, then to the reverse, then
                    // gives the leg up. Same ladder, same purpose: get round
                    // one table.
                    const sim::Angle eastWest = dx > 0 ? sim::kFacingEast : sim::kFacingWest;
                    const sim::Angle northSouth = dy > 0 ? sim::kFacingSouth : sim::kFacingNorth;
                    const bool xFirst = std::abs(dx) >= std::abs(dy);
                    sim::Angle push = xFirst ? eastWest : northSouth;
                    if (closeX) {
                        push = northSouth;
                    } else if (closeY) {
                        push = eastWest;
                    } else if (stuck_ > 30) {
                        push = xFirst ? northSouth : eastWest;
                    } else if (stuck_ > 15) {
                        push = xFirst ? eastWest : northSouth;
                    }
                    // The head, eased, toward the leg -- never snapped.
                    const std::int32_t want =
                        sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                       pathX_[leg_], pathY_[leg_]);
                    constexpr std::int32_t kTurnRate = sim::kTurnFull / 90;  // ~4 deg/step
                    session.body().setYaw(
                        session.body().yaw() +
                        std::clamp(turnDelta(session.body().yaw(), want), -kTurnRate, kTurnRate));
                    // The world direction, resolved into the body's own frame,
                    // so the feet go where the router said whatever the head is
                    // doing. Quadrants, not octants: a cardinal push stays
                    // cardinal.
                    const std::int32_t rel = turnDelta(session.body().yaw(), push);
                    const std::int32_t magnitude = rel < 0 ? -rel : rel;
                    if (magnitude <= sim::kTurnFull / 8) {
                        tick.move.forward = 1;
                    } else if (magnitude >= 3 * sim::kTurnFull / 8) {
                        tick.move.forward = -1;
                    } else {
                        tick.move.strafe = rel > 0 ? 1 : -1;
                    }
                    // NO CLIMBING AND NO MOMENTUM, the same two reasons
                    // runSmoke's walker states: a demo that hauled itself up a
                    // warehouse would photograph the wrong district, and legs
                    // that carry the last direction into the next one overshoot
                    // a waypoint and orbit it.
                    tick.move.autoTraverse = false;
                    tick.move.snapVelocity = true;
                    if (++stuck_ > 45) {
                        // A leg not reached in three quarters of a second is a
                        // leg the geometry is not going to give up. Skip it
                        // rather than grinding: the route survives, the beat's
                        // own frame budget still ends it, and the demo cannot
                        // stall.
                        ++leg_;
                        stuck_ = 0;
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    ++held_;
    ++frame_;
    // ARM THE SHUTTER HERE, not in shutter(), because the block below may end
    // the beat and move the playhead on -- see DemoDirector::pending_.
    pending_ = nullptr;
    if (beat.shot != nullptr) {
        const int want = beat.shotAt >= 0 ? std::min(beat.shotAt, span) : span;
        if (held_ == want) {
            pending_ = beat.shot;
        }
    }
    const bool last = held_ >= span || (beat.act == DemoAct::Walk && leg_ >= pathX_.size() &&
                                        !pathX_.empty() && held_ >= span / 4);
    if (last) {
        // A BEAT THAT ENDS EARLY STILL GETS ITS PICTURE. Only Walk can, and
        // only by arriving; without this a shot named on an arriving walk would
        // silently never be written, which is the failure mode a capture tool
        // must never have.
        if (pending_ == nullptr && beat.shot != nullptr) {
            pending_ = beat.shot;
        }
        entered_ = false;
        ++at_;
        if (at_ >= static_cast<int>(route_->size())) {
            finished_ = true;
        }
    }
    return tick;
}

void DemoDirector::shutter(const Framebuffer& target) {
    if (pending_ == nullptr) {
        return;
    }
    const std::string name = pending_;
    pending_ = nullptr;
    if (shotDir_.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(shotDir_, ec);
    (void)writePng(target, (shotDir_ / (name + ".png")).string());
}

bool DemoDirector::cardOwnsFrame() const noexcept {
    return card_.value() > 0.01F && !cardLine_.empty();
}

void DemoDirector::drawOverlay(Framebuffer& target, const Session& session) const {
    const PanelMetric metric = panelMetric(target.height());
    const PanelInk& ink = panelInk();

    // --- the caption -------------------------------------------------------
    //
    // SIZED TO ITS CONTENT, because it is static text that does not swap under
    // a cursor -- the spec's own "size to content where content is static".
    //
    // AND IT STANDS OFF THE HUD RATHER THAN OVER IT, which the first run of
    // this route got wrong: pinned to the bottom edge it half-covered the trail
    // row (`CASE 1/4 > MISSION OF THE FLAME`) and the fatigue bar, leaving two
    // sentences interleaved a row apart. Those rows are part of what the demo
    // is SHOWING, so the caption reserves the bottom band for them and sits
    // above it. It also stands down entirely under any page, above -- a page IS
    // the content and a line of commentary over one is the same collision one
    // step worse.
    const float captionAlpha = captionFade_.value();
    if (captionAlpha > 0.01F && !caption_.empty()) {
        const int cells = std::min(static_cast<int>(caption_.size()),
                                   std::max(8, metric.cellsIn(target.width()) - 4));
        PanelRect bounds;
        bounds.w = std::min(metric.widthOf(cells) + 2 * metric.cellW(),
                            target.width() - 2 * metric.cellW());
        bounds.h = metric.heightOf(3);
        bounds.x = metric.cellW();
        // The HUD's own bottom band, in rows of the HUD's metric rather than a
        // pixel constant, so it holds at every window size.
        bounds.y = target.height() - target.height() / 9 - bounds.h;
        FrameStyle style;
        style.junction = Motif::Plus;
        style.alpha = captionAlpha;
        style.groundAlpha = 0.93F;
        PanelFrame pane(target, bounds, metric, style);
        pane.draw();
        (void)drawCellText(target, pane.interior(), metric, 0, 0, caption_, ink.prose,
                           captionAlpha);
    }

    // --- the title card ----------------------------------------------------
    const float cardAlpha = card_.value();
    if (cardAlpha <= 0.01F || cardLine_.empty()) {
        return;
    }
    // AT TWICE THE BODY METRIC, and this is the one place in the build that
    // gets to do that. The spec's ONE METRIC PER SCREEN rule is about a screen
    // whose columns have to line up with each other; a card is a screen of its
    // own with three centred rows on it and nothing to line up WITH, and drawn
    // at hudMinorScale over a 640x360 frame the first version's title was
    // eight pixels tall and unreadable at a glance -- which for a title card is
    // the whole job failed.
    const PanelMetric big{metric.scale * 2};
    const int titleCells = static_cast<int>(cardLine_.size());
    const int subCells = static_cast<int>(cardSub_.size());
    const std::string where = session.placeLabel();
    const int footCells = static_cast<int>(where.size());
    const int widest = std::max(std::max(titleCells + 2, subCells), footCells);
    const int cells = std::min(std::max(widest + 4, 18), std::max(8, big.cellsIn(target.width()) - 4));
    PanelRect bounds;
    bounds.w =
        std::min(big.widthOf(cells) + 2 * big.cellW(), target.width() - 2 * big.cellW());
    bounds.h = big.heightOf(where.empty() ? 3 : 5) + 2 * big.cellH();
    bounds.x = (target.width() - bounds.w) / 2;
    bounds.y = (target.height() - bounds.h) / 2;
    FrameStyle style;
    style.junction = Motif::Diamond;
    style.alpha = cardAlpha;
    // NEARLY OPAQUE, like every other composed page in this build
    // (kPageGroundAlpha): a card the ward shows through is a card nobody reads.
    style.groundAlpha = kPageGroundAlpha;
    style.stipple = true;
    PanelFrame pane(target, bounds, big, style);
    if (!where.empty()) {
        pane.addRule(3);
    }
    pane.draw();
    const PanelRect body = pane.interior();
    // The title, knocked out of an inverted fill in the accent -- this build's
    // one idiom for emphasis.
    const int titleCell = std::max(1, (cells - titleCells) / 2);
    drawInvertedFill(target, body, big, titleCell - 1, 0, titleCells + 2, ink.accent, cardAlpha);
    (void)drawCellTextKnockout(target, body, big, titleCell, 0, cardLine_, ink.knockout,
                               cardAlpha);
    if (!cardSub_.empty()) {
        (void)drawCellText(target, body, big, std::max(0, (cells - subCells) / 2), 2, cardSub_,
                           ink.prose, cardAlpha);
    }
    // The foot: where the demo is standing, in the ward's own words -- a
    // dateline, not a caption, so it takes the dim role under its own rule.
    if (!where.empty()) {
        (void)drawCellText(target, body, big, std::max(0, (cells - footCells) / 2), 4, where,
                           ink.dim, cardAlpha);
    }
}

}  // namespace granadad::render
