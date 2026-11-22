// Scripted capture drivers: gear (OBLIVION-PLAN phase 9).
#include "scripted.hpp"

#include <span>

namespace granadad::render {

namespace {

// ---------------------------------------------------------------------------
// what the line wears
// ---------------------------------------------------------------------------

/// THE LINE'S PIECES, by raws id (items_gear.json, and the cutlass, the mace
/// and the boots items.json already held). Ids and never names: the names are
/// the screen's, and the screen is what the line reads back.
///
/// The three LOADOUTS the captures put side by side. LIGHT is a docker who
/// dressed for the weather and a blade; GUARD is a house's man on the door,
/// covered head to foot with a shield on the arm; HEAVY trades the shield and
/// the one-hander for a maul in both hands.
constexpr std::string_view kLightSet[] = {"wool_cap", "gambeson", "cutlass"};
constexpr std::string_view kGuardSet[] = {"iron_pot", "brigandine", "gauntlets",
                                          "mace",     "round_shield", "boots"};
constexpr std::string_view kHeavySet[] = {"iron_pot", "brigandine", "gauntlets", "maul"};
/// The whole line's extras over the light set: the two-hander it compares
/// and swings, the shield it takes up beside the cutlass (the buckler, the
/// lighter of the two, because the line carries the maul at the same time
/// and the load is the load), and the iron it takes Ox's blow on.
constexpr std::string_view kTwoHander = "maul";
constexpr std::string_view kShield = "buckler";
constexpr std::string_view kHelmet = "iron_pot";
/// Every piece the whole line takes, for the hour the takes happen at.
constexpr std::string_view kLineSet[] = {"wool_cap", "gambeson", "cutlass",
                                         kTwoHander, kShield,    kHelmet};
/// What the line may put down to make room for the helmet: the pieces the
/// two-hander and the iron replace, oldest first.
constexpr std::string_view kLineSpare[] = {"cutlass", kShield, "wool_cap"};

/// THE STREET SPOT THE THREE LOADOUTS ARE PHOTOGRAPHED ON IS THE SPAWN: the
/// authored first-run stand on the Tarwalk (docks.hpp's kSpawnTile*, band and
/// yaw -- #79's measured opening shot, the street running away west, the
/// Gull's frontage on the left), at nine. It is where a plain held run stands
/// at --time=9, which is the BEFORE frame these loadouts are the after of
/// (--sheet=looks, --hands=sword), so a body dressed by the line looks down
/// the same street from the same spot. One tile, one yaw, one hour, the eye
/// level, the hands where a fresh body has them (down), the Menu down: the
/// frames differ by what is worn and nothing a capture chose.
constexpr std::int32_t kStreetSpotX = sim::docks::kSpawnTileX;
constexpr std::int32_t kStreetSpotY = sim::docks::kSpawnTileY;
constexpr std::int32_t kStreetSpotBand = sim::docks::kSpawnBand;
constexpr sim::Angle kStreetSpotYaw = sim::docks::kSpawnYaw;
constexpr int kLoadoutHour = 9;

/// THE GULL'S EVENING: inside the door, facing south down the room -- the
/// bar's seven tiles across the frame three tiles off, Gerta behind it, the
/// near tables on the right. Cull's stool at (152,68) is empty until nine
/// (test_street_voice pins the tile and its sightlines); the tile east of it
/// and the open floor south of it (test_roofrun spawns a body there) are the
/// fall-backs, in that order, so the stand is fixed per map.
constexpr std::int32_t kTaproomTiles[3][2] = {{152, 68}, {153, 68}, {152, 69}};
constexpr sim::Angle kTaproomYaw = sim::kFacingSouth;
constexpr int kTaproomHour = 20;

// ---------------------------------------------------------------------------
// reading the Kit
// ---------------------------------------------------------------------------

[[nodiscard]] std::string nameOf(const Session& session, std::string_view id) {
    const sim::ItemDef* def = session.tavern().items().find(id);
    return def != nullptr ? def->name : std::string{};
}

[[nodiscard]] bool carriedGear(const Session& session, std::string_view id) {
    const sim::Tavern& tavern = session.tavern();
    const std::int32_t index = tavern.items().indexOf(id);
    return index >= 0 && tavern.kit().count(index) > 0;
}

[[nodiscard]] bool wornGear(const Session& session, std::string_view id) {
    const sim::Tavern& tavern = session.tavern();
    const std::int32_t index = tavern.items().indexOf(id);
    return index >= 0 && tavern.kit().isWorn(index);
}

[[nodiscard]] bool hasLine(const std::vector<std::string>& lines, const std::string& prefix) {
    for (const std::string& line : lines) {
        if (line.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

void noteAim(const char* key, const Session::InteractTarget& aim) {
    gGearNote += std::string(" ") + key + "=" + aim.verb +
                 (aim.subject.empty() ? "" : ":" + aim.subject) +
                 (aim.note.empty() ? "" : "/" + aim.note);
}

// ---------------------------------------------------------------------------
// the walk
// ---------------------------------------------------------------------------

/// WALKS TO A TILE THE WAY walkToTile DOES -- the room's own pathfinder, each
/// waypoint steered to with ordinary movement steps through ordinary
/// collision -- over a box that holds both ends. walkToTile's box
/// (kCaptureRegion) stops at y=61, the quay's wall line, and the gear's
/// sea-chest stands on the apron north of it (items_gear.json); a router whose
/// box does not hold an end refuses outright and leaves the greedy walker to
/// grind along the wall, in or out. The box is kCaptureRegion grown to two
/// tiles round each end, so every walk the capture box already routed routes
/// the same.
void walkToGear(Session& session, std::int32_t tileX, std::int32_t tileY) {
    const std::int32_t fromX = session.body().tileX();
    const std::int32_t fromY = session.body().tileY();
    sim::TileBox box = kCaptureRegion;
    box.x0 = std::min({box.x0, tileX - 2, fromX - 2});
    box.y0 = std::min({box.y0, tileY - 2, fromY - 2});
    box.x1 = std::max({box.x1, tileX + 2, fromX + 2});
    box.y1 = std::max({box.y1, tileY + 2, fromY + 2});
    sim::RegionPath router(session.tiles(), box);
    std::vector<sim::PathStep> route;
    const sim::PathStep from{fromX, fromY, session.body().band()};
    const sim::PathStep to{tileX, tileY, session.body().band()};
    if (router.find(from, to, route)) {
        for (const sim::PathStep& waypoint : route) {
            const std::int32_t wx = sim::q8_tile_centre(waypoint.x);
            const std::int32_t wy = sim::q8_tile_centre(waypoint.y);
            bool arrived = false;
            for (int guard = 0; guard < 120 && !arrived; ++guard) {
                arrived = stepToward(session, wx, wy);
            }
            if (!arrived) {
                break;
            }
        }
    }
    walkStraightTo(session, tileX, tileY);
}

// ---------------------------------------------------------------------------
// the stands
// ---------------------------------------------------------------------------

/// WHERE A PIECE LIES, read off the ground list at run time by its id -- the
/// stands are the raws' to place and to move, and a line that typed their
/// tiles would photograph the boards the day they moved. The nearest entry on
/// the body's band (Chebyshev, ties to the list's own order), or -1 when the
/// raws stand none.
[[nodiscard]] std::int32_t standFor(const Session& session, std::string_view id) {
    const sim::Tavern& tavern = session.tavern();
    const std::int32_t index = tavern.items().indexOf(id);
    if (index < 0) {
        return -1;
    }
    const std::vector<sim::GroundItem>& ground = tavern.groundItems();
    std::int32_t best = -1;
    std::int32_t bestDistance = 0;
    for (std::size_t i = 0; i < ground.size(); ++i) {
        const sim::GroundItem& entry = ground[i];
        if (entry.item != index || entry.count <= 0 || entry.band != session.body().band()) {
            continue;
        }
        const std::int32_t distance = std::max(std::abs(entry.x - session.body().tileX()),
                                               std::abs(entry.y - session.body().tileY()));
        if (best < 0 || distance < bestDistance) {
            best = static_cast<std::int32_t>(i);
            bestDistance = distance;
        }
    }
    return best;
}

/// PUTS A STAND'S THING UNDER THE CROSSHAIR the way a body does it: from a
/// tile beside it, the eye down on the boards (the kit line's stance, fifty
/// degrees -- see lookDownAtBoards for the geometry), then sixty, where the
/// reticle lands a tile ahead dead on the thing's own tile (the stands are a
/// packed grid, and fifty lands near the far edge of it); failing every
/// side, from the stand's own tile with the eye at the feet (the kit line's
/// coat). The reticle has to be ON a thing to name it
/// (Session::groundItemUnderReticle), so each stance is asked of the prompt
/// itself. True once it reads TAKE <NAME>; `aim` holds what it read.
[[nodiscard]] bool aimAtStand(Session& session, const sim::GroundItem& stand,
                              const std::string& name, Session::InteractTarget& aim) {
    constexpr std::int32_t kSides[5][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0, 0}};
    for (const auto& side : kSides) {
        const std::int32_t sx = stand.x + side[0];
        const std::int32_t sy = stand.y + side[1];
        if (!session.tiles().standable(sx, sy, stand.band)) {
            continue;
        }
        const bool underfoot = side[0] == 0 && side[1] == 0;
        walkToGear(session, sx, sy);
        session.closeConversation();
        if (session.body().tileX() != sx || session.body().tileY() != sy) {
            continue;
        }
        if (!underfoot) {
            // standAtFacing's turn, after this file's own walk.
            facePlayerAndSync(session, sim::q8_tile_centre(stand.x), sim::q8_tile_centre(stand.y));
        }
        const int pitches[2] = {underfoot ? -85 : -50, underfoot ? -85 : -60};
        for (const int pitch : pitches) {
            session.body().setPitch(sim::angle_from_degrees(pitch));
            aim = session.interactTarget();
            if (aim.verb == "TAKE" && aim.subject == name) {
                return true;
            }
        }
        lookLevel(session);
    }
    return false;
}

/// The crosshair on the nearest stand of `id`, through aimAtStand. Notes a
/// piece the raws stand nowhere, so a short run says which.
[[nodiscard]] bool aimAtGear(Session& session, std::string_view id,
                             Session::InteractTarget& aim) {
    const std::int32_t at = standFor(session, id);
    if (at < 0) {
        gGearNote += " nostand=" + std::string(id);
        return false;
    }
    // A copy: the walk to it steps the room, and the room owns the list.
    const sim::GroundItem stand = session.tavern().groundItems()[static_cast<std::size_t>(at)];
    return aimAtStand(session, stand, nameOf(session, id), aim);
}

/// MAKES ROOM FOR A TAKE through the pack's own X: puts down pieces the line
/// took and no longer wears, in the order given, until `drams` more fits
/// under the budget -- what a body at a sea-chest does with the blade it
/// just swapped for a better one. Never anything worn, never a piece not in
/// `spare`. TAKE refuses past the load with its own line, so a take that
/// would not fit is a beat the line loses for a reason it could have seen.
void makeRoom(Session& session, std::int32_t drams, std::span<const std::string_view> spare) {
    const sim::Tavern& tavern = session.tavern();
    for (const std::string_view id : spare) {
        if (tavern.loadDrams() + drams <= tavern.loadBudget()) {
            break;
        }
        if (!carriedGear(session, id) || wornGear(session, id)) {
            continue;
        }
        if (openPackRow(session, nameOf(session, id) + " ")) {
            session.dropHighlightedKitRow();
            gGearNote += " put-down=" + std::string(id);
        }
    }
    session.closeConversation();
}

/// TAKES ONE PIECE OFF ITS STAND: room made, the crosshair on it, E. True once
/// the Kit carries it (at once when it already did).
[[nodiscard]] bool takeGear(Session& session, std::string_view id,
                            std::span<const std::string_view> spare) {
    if (carriedGear(session, id)) {
        return true;
    }
    const sim::ItemDef* def = session.tavern().items().find(id);
    if (def == nullptr) {
        gGearNote += " noitem=" + std::string(id);
        return false;
    }
    makeRoom(session, def->drams, spare);
    Session::InteractTarget aim;
    if (aimAtGear(session, id, aim)) {
        session.interact();
    }
    lookLevel(session);
    const bool took = carriedGear(session, id);
    if (!took) {
        gGearNote += " missed=" + std::string(id);
    }
    return took;
}

/// THE HOUSE'S GOODS ARE LIFTED IN THE SMALL HOURS. When any stand the run
/// takes from is somebody's, the takes happen at four in the morning -- the
/// kit and pack lines' own hour for THEIRS, the house empty and nobody to
/// see -- so a capture of the gear is never a capture of a witnessed theft
/// and the Watch it would bring. Free stands are taken at the run's hour.
void enterTakeHour(Session& session, std::span<const std::string_view> ids) {
    bool owned = false;
    for (const std::string_view id : ids) {
        const std::int32_t at = standFor(session, id);
        if (at >= 0 && session.tavern().groundItems()[static_cast<std::size_t>(at)].owned) {
            owned = true;
        }
    }
    const int hour = session.timeOfDay() / 3600;
    if (!owned || (hour >= 1 && hour <= 5)) {
        return;
    }
    session.closeConversation();
    session.skipToHour(4);
    session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
    gGearNote += " lifted-at=4";
}

/// WEARS a carried piece through the pack's own ENTER: the cursor walked onto
/// its carried row, the press. True once the Kit wears it.
[[nodiscard]] bool wearGear(Session& session, std::string_view id) {
    if (wornGear(session, id)) {
        return true;
    }
    if (!carriedGear(session, id)) {
        return false;
    }
    if (openPackRow(session, nameOf(session, id) + " ")) {
        session.chooseTopic(0);
    }
    return wornGear(session, id);
}

// ---------------------------------------------------------------------------
// the frames
// ---------------------------------------------------------------------------

/// STANDS THE BODY ON A FIXED SPOT for a frame that must compare with another
/// run's: walked there on its feet, then set on the tile's centre (the walker
/// stops within an eighth of a tile, and three loadouts photographed an
/// eighth apart do not compare -- placeBodyAt is every capture line's
/// staging), turned to a fixed yaw, the eye level, the Menu down, and the
/// clock put on the named hour. True when all of it held.
[[nodiscard]] bool standForFrame(Session& session, std::int32_t tileX, std::int32_t tileY,
                                 std::int32_t band, sim::Angle yaw, int hour) {
    session.closeConversation();
    lookLevel(session);
    walkToGear(session, tileX, tileY);
    session.closeConversation();
    if (session.body().tileX() != tileX || session.body().tileY() != tileY ||
        session.body().band() != band) {
        return false;
    }
    session.placeBodyAt(tileX, tileY, band);
    session.body().setYaw(yaw);
    lookLevel(session);
    session.skipToHour(hour);
    session.stepMany(sim::MoveInput{}, 1);
    return session.body().tileX() == tileX && session.body().tileY() == tileY &&
           session.body().band() == band && session.body().yaw() == yaw &&
           !session.characterOpen() && session.timeOfDay() / 3600 == hour;
}

/// The Gull's evening frame: the first of the taproom's three stands the map
/// lets a body onto.
[[nodiscard]] bool standInTaproom(Session& session) {
    for (const auto& tile : kTaproomTiles) {
        if (!session.tiles().standable(tile[0], tile[1], sim::gull::kGroundBand)) {
            continue;
        }
        if (standForFrame(session, tile[0], tile[1], sim::gull::kGroundBand, kTaproomYaw,
                          kTaproomHour)) {
            gGearNote += " taproom=" + std::to_string(tile[0]) + "," + std::to_string(tile[1]);
            return sim::gull::insideFootprint(tile[0], tile[1]);
        }
    }
    return false;
}

/// STANDS A DIAGONAL STEP OFF A BODY AND PUTS HIM ON THE CROSSHAIR: a tile and
/// two fifths down the look-ray (362 of 256), past the one-handed reach
/// (gripTiming(OneHanded), 320) and inside the two-handed (416). Each
/// diagonal neighbour of his tile is tried until one is open ground the
/// walker reaches; the body is then set on its centre, because an eighth of a
/// tile of the walker's slop is 45 down the ray and the window is 96 wide.
/// True when the measured distance is in the window and the swing's own ray
/// has him (Tavern::playerSightlineTarget, cast at the held grip's reach).
/// The distance is measured render-side (a square root, legal here and
/// never written back); the sim's ray decides the swing.
[[nodiscard]] bool standPastOneHand(Session& session, std::int32_t markId, std::int64_t& along) {
    const std::int64_t oneHand = sim::gripTiming(sim::WeaponGrip::OneHanded).reachQ8;
    constexpr std::int32_t kDiagonals[4][2] = {{1, -1}, {-1, 1}, {1, 1}, {-1, -1}};
    for (const auto& diagonal : kDiagonals) {
        const sim::Actor* mark = session.tavern().actorById(markId);
        if (mark == nullptr || !mark->present() || sim::isFloored(mark->activity())) {
            return false;
        }
        const std::int32_t tx = mark->tileX() + diagonal[0];
        const std::int32_t ty = mark->tileY() + diagonal[1];
        const std::int32_t band = mark->band();
        if (!session.tiles().standable(tx, ty, band)) {
            continue;
        }
        walkToGear(session, tx, ty);
        session.closeConversation();
        if (session.body().tileX() != tx || session.body().tileY() != ty) {
            continue;
        }
        session.placeBodyAt(tx, ty, band);
        mark = session.tavern().actorById(markId);
        if (mark == nullptr) {
            return false;
        }
        facePlayerAndSync(session, mark->x(), mark->y());
        lookLevel(session);
        const std::int64_t reach = session.tavern().playerReachQ8();
        // A man at his post stands within a hair of his tile's centre, not on
        // it, so the diagonal is 362 give or take. Should that leave him on
        // the window's edge, the body steps back (or in) along the ray a
        // movement step at a time -- the same legs, no input but the stick --
        // re-facing him each time, until he is a thirty-second of a
        // tile (8 of 256) inside it.
        constexpr std::int64_t kMargin = sim::kSubOne / 32;
        for (int nudge = 0; nudge < 24; ++nudge) {
            mark = session.tavern().actorById(markId);
            if (mark == nullptr) {
                return false;
            }
            const std::int64_t dx = static_cast<std::int64_t>(mark->x()) - session.body().x();
            const std::int64_t dy = static_cast<std::int64_t>(mark->y()) - session.body().y();
            const std::int64_t squared = dx * dx + dy * dy;
            along = static_cast<std::int64_t>(std::llround(std::sqrt(static_cast<double>(squared))));
            const bool tooNear = along <= oneHand + kMargin;
            const bool tooFar = along > reach - kMargin;
            if (!tooNear && !tooFar) {
                break;
            }
            sim::MoveInput step;
            step.forward = tooNear ? -1 : 1;
            step.autoTraverse = false;
            step.snapVelocity = true;
            session.step(step);
            facePlayerAndSync(session, mark->x(), mark->y());
        }
        if (along > oneHand && along <= reach && session.tavern().playerSightlineTarget()) {
            return true;
        }
    }
    return false;
}

/// A LOADOUT, worn through the same presses as the line and stood for its
/// frame: (1) every piece off its stand, (2) every piece on through the
/// pack's ENTER and still on once the last press is made (a later press must
/// not have taken an earlier piece off), (3) the frame -- the street spot at
/// nine, or the Gull's taproom at eight in the evening.
[[nodiscard]] int runLoadout(Session& session, std::span<const std::string_view> set,
                             bool taproom) {
    int landed = 0;
    enterTakeHour(session, set);
    int took = 0;
    for (const std::string_view id : set) {
        if (takeGear(session, id, {})) {
            ++took;
        }
    }
    gGearNote += " took=" + std::to_string(took) + "/" + std::to_string(set.size());
    if (took == static_cast<int>(set.size())) {
        ++landed;  // 1
    }
    for (const std::string_view id : set) {
        (void)wearGear(session, id);
    }
    bool allOn = true;
    for (const std::string_view id : set) {
        allOn = allOn && wornGear(session, id);
    }
    if (allOn) {
        ++landed;  // 2
    }
    const bool stood = taproom ? standInTaproom(session)
                               : standForFrame(session, kStreetSpotX, kStreetSpotY,
                                               kStreetSpotBand, kStreetSpotYaw, kLoadoutHour);
    if (stood) {
        ++landed;  // 3
    }
    return landed;
}

[[nodiscard]] bool isLoadout(const std::string& ending) {
    return ending == "light" || ending == "guard" || ending == "heavy" || ending == "gull";
}

}  // namespace

namespace scripted {

/// The three beats a loadout owes, and the line's seven by its ending.
[[nodiscard]] int gearBeatsFor(const std::string& ending) {
    if (isLoadout(ending)) {
        return 3;
    }
    if (ending == "take") {
        return 1;
    }
    if (ending == "wear") {
        return 2;
    }
    if (ending == "compare") {
        return 3;
    }
    if (ending == "shield") {
        return 4;
    }
    if (ending == "twohand") {
        return 5;
    }
    if (ending == "reach") {
        return 6;
    }
    return 7;
}

std::string gearWornIds(const Session& session) {
    const sim::Tavern& tavern = session.tavern();
    std::string out;
    for (const sim::ItemSlot slot : sim::kSlotScreenOrder) {
        const sim::ItemDef* worn = tavern.items().at(tavern.kit().worn(slot));
        if (worn == nullptr) {
            continue;
        }
        if (!out.empty()) {
            out += ',';
        }
        out += worn->id;
    }
    return out.empty() ? std::string("-") : out;
}

/// THE GEAR LINE. Seven beats, every ending a stop on the way:
///
///   1. take     the light set (wool cap, gambeson, cutlass) off its stands by
///               the crosshair; `take` stops on the first prompt, before E
///   2. wear     the three worn through the pack's own ENTER; the pack open on
///               the worn block, the cursor on the HAND row
///   3. compare  the maul taken; the pack on its carried row, the detail pane's
///               comparison block reading DR, DMG, REACH LONGER, SPEED SLOWER
///               and TAKES OFF HAND CUTLASS before anything is pressed
///   4. shield   the buckler taken up beside the cutlass; the cursor on the
///               OFF-HAND row, the pane on the shield
///   5. twohand  the maul's row again, now warning TAKES OFF OFF-HAND BUCKLER,
///               and ENTER: the maul in both hands, the buckler carried, the
///               cursor on the bare OFF-HAND row that says why it is bare
///   6. reach    the iron pot on, two in the afternoon, Ox Gullbane at the
///               door from a diagonal step -- past a one-hander's reach,
///               inside the maul's -- the reticle on him, one swing
///   7. dr       his blow back, turned by what is worn (the iron among it),
///               stepped one step at a time so the run stops on X TURNS n
///
/// Or a LOADOUT: light, guard, heavy -- taken, worn, and walked back to the
/// spawn's own tile, band and yaw at nine with the Menu down -- or gull, the
/// guard loadout stood in the Gull's taproom at eight in the evening.
[[nodiscard]] int runGearLine(Session& session, const std::string& ending) {
    int landed = 0;
    gGearNote.clear();
    const sim::Tavern& tavern = session.tavern();
    if (!tavern.items().loaded() || tavern.items().find(kTwoHander) == nullptr) {
        gGearNote = " items=unloaded";
        return landed;
    }
    if (ending == "light") {
        return runLoadout(session, kLightSet, false);
    }
    if (ending == "guard") {
        return runLoadout(session, kGuardSet, false);
    }
    if (ending == "heavy") {
        return runLoadout(session, kHeavySet, false);
    }
    if (ending == "gull") {
        return runLoadout(session, kGuardSet, true);
    }

    // 1. TAKE. The first piece by the crosshair, the prompt read before the
    // press; then the rest of the light set.
    enterTakeHour(session, kLineSet);
    Session::InteractTarget aim;
    const bool prompted = aimAtGear(session, kLightSet[0], aim);
    noteAim("take", aim);
    if (ending == "take") {
        return prompted ? 1 : 0;
    }
    if (prompted) {
        session.interact();
    }
    lookLevel(session);
    bool took = carriedGear(session, kLightSet[0]);
    for (std::size_t i = 1; i < std::size(kLightSet); ++i) {
        took = takeGear(session, kLightSet[i], {}) && took;
    }
    if (prompted && took) {
        ++landed;  // 1
    }

    // 2. WEAR. Each by the pack's ENTER, then the cursor home on the hand.
    bool on = true;
    for (const std::string_view id : kLightSet) {
        on = wearGear(session, id) && on;
    }
    // The hand's row is "HAND" padded to the slot column; the space keeps it
    // off the HANDS row.
    const bool onHand = openPackRow(session, "HAND ");
    if (on && onHand) {
        ++landed;  // 2
    }
    if (ending == "wear") {
        return landed;
    }

    // 3. COMPARE. The maul on its carried row, read before it is worn.
    session.closeConversation();
    const bool haveMaul = takeGear(session, kTwoHander, {});
    InspectPane pane;
    if (haveMaul && openPackRow(session, nameOf(session, kTwoHander) + " ")) {
        pane = session.inspectPaneView();
    }
    std::string read;
    for (const std::string& line : pane.compare) {
        read += (read.empty() ? "" : "|") + line;
    }
    gGearNote += " compare=" + (read.empty() ? std::string("-") : read);
    if (haveMaul && hasLine(pane.compare, "DR ") && hasLine(pane.compare, "DMG ") &&
        hasLine(pane.compare, "TAKES OFF HAND " + nameOf(session, "cutlass"))) {
        ++landed;  // 3
    }
    if (ending == "compare") {
        return landed;
    }

    // 4. SHIELD, beside the one-hander.
    session.closeConversation();
    const bool shieldOn = takeGear(session, kShield, {}) && wearGear(session, kShield);
    const bool onOffHand = openPackRow(session, "OFF-HAND");
    if (shieldOn && wornGear(session, "cutlass") && onOffHand &&
        session.inspectPaneView().title == nameOf(session, kShield)) {
        ++landed;  // 4
    }
    if (ending == "shield") {
        return landed;
    }

    // 5. TWOHAND. The warning read, then the press.
    InspectPane before;
    if (openPackRow(session, nameOf(session, kTwoHander) + " ")) {
        before = session.inspectPaneView();
        session.chooseTopic(0);
    }
    const bool warned = hasLine(before.compare, "TAKES OFF OFF-HAND " + nameOf(session, kShield));
    const bool swapped = wornGear(session, kTwoHander) &&
                         tavern.kit().worn(sim::ItemSlot::OffHand) < 0 &&
                         carriedGear(session, kShield);
    const bool bareRow = openPackRow(session, "OFF-HAND") &&
                         session.inspectPaneView().prose.rfind("BOTH HANDS", 0) == 0;
    gGearNote += " swap=" + session.lastMessage();
    if (warned && swapped && bareRow) {
        ++landed;  // 5
    }
    if (ending == "twohand") {
        return landed;
    }

    // 6. REACH. The iron on first (a helmet goes on before the swing, and
    // the take keeps the line's hour), then Ox's hours.
    session.closeConversation();
    const bool helmed = takeGear(session, kHelmet, kLineSpare) && wearGear(session, kHelmet);
    session.closeConversation();
    session.skipToHour(14);
    session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
    const sim::Actor* ox = actorNamed(session, "Ox Gullbane");
    const std::int32_t oxId = ox != nullptr ? ox->id() : -1;
    const std::int32_t turnedBefore = tavern.blowsTurned();
    std::int64_t along = -1;
    bool reached = false;
    if (oxId >= 0 && wornGear(session, kTwoHander)) {
        reached = standPastOneHand(session, oxId, along);
        if (reached) {
            session.punch();
            reached = inTheFight(session, oxId);
        }
    }
    gGearNote += " along=" + std::to_string(along) + "/" +
                 std::to_string(tavern.playerReachQ8()) + " helm=" + (helmed ? "on" : "off");
    if (reached) {
        ++landed;  // 6
    }
    if (ending == "reach") {
        return landed;
    }

    // 7. DR. His blow back, one step at a time.
    if (oxId >= 0 && reached) {
        for (int step = 0;
             step < 20 * sim::kStepsPerSecond && tavern.blowsTurned() == turnedBefore; ++step) {
            if (step % sim::kStepsPerSecond == 0) {
                (void)closeOnAndFace(session, oxId);
            }
            session.stepMany(sim::MoveInput{}, 1);
        }
    }
    gGearNote += " turned=" + std::to_string(tavern.blowsTurned() - turnedBefore) +
                 " last=" + std::to_string(tavern.lastTurned());
    if (helmed && tavern.blowsTurned() > turnedBefore) {
        ++landed;  // 7
    }
    return landed;
}

}  // namespace scripted

}  // namespace granadad::render
