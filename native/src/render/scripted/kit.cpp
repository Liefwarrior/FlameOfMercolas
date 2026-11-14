// Scripted capture drivers: kit.
#include "scripted.hpp"

namespace granadad::render {

namespace {

/// Opens the tiled Menu on the Character tile and walks its cursor onto the
/// carried row for `itemId`. False when the row is not on the tile.
[[nodiscard]] bool openKitRow(Session& session, std::string_view itemId) {
    if (!session.characterOpen()) {
        session.toggleCharacter();
    } else {
        session.setMenuFocus(kMenuFocusCharacter);
    }
    const std::int32_t index = session.tavern().items().indexOf(itemId);
    const std::vector<Session::KitRow> rows = session.kitRows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].item != index) {
            continue;
        }
        const int target = static_cast<int>(session.characterKitOffset() + i);
        // Walk, never jump: the same wrap the arrows make.
        for (int guard = 0; guard < 64; ++guard) {
            const std::optional<Session::KitRow> at = session.highlightedKitRow();
            if (at.has_value() && at->item == index) {
                return true;
            }
            const int cursor = session.dialogueView().cursor;
            session.moveTopicCursor(cursor < target ? 1 : -1);
        }
        return false;
    }
    return false;
}

/// Walks to a tile and faces a neighbouring tile, so the crosshair reads
/// what lies there.
void standAtFacing(Session& session, std::int32_t standX, std::int32_t standY,
                   std::int32_t faceX, std::int32_t faceY) {
    walkToTile(session, standX, standY);
    session.closeConversation();
    facePlayerAndSync(session, sim::q8_tile_centre(faceX), sim::q8_tile_centre(faceY));
}

/// KIT POLISH. Tilts the eye onto the boards one tile ahead -- what a body
/// does to look at a thing it has just put down, or is about to pick up.
/// The look down IS the beat, the same mouse a player's own hand makes; the
/// roof line and the lock line already set their frames' pitch this way.
///
/// THE GEOMETRY, so nobody reaches for the item table again: the eye is
/// 1.70 tiles up (kEyeHeightTilesQ8), TAKE reaches two tiles (kReachQ8,
/// Manhattan from the tile's centre, and the walker lands up to an eighth
/// of a tile off centre -- so the stand is one tile back, not two). One
/// tile ahead a thing on the floor is 59.5 degrees below the horizon; the
/// 90-degree lens at 16:9 sees 29.4 degrees down. No scale, lift or pitch
/// of the MESH closes that gap -- a coil would have to stand 1.14 tiles
/// tall to break the frame's bottom edge, and a floor item is not inside a
/// level frame until it is three tiles off, past the reach that puts TAKE
/// on the crosshair. Fifty degrees down puts the coil just under the
/// reticle and its prompt, clear of the hands hanging at the foot of the
/// frame (measured on the baseline with --pitch: -40 hides it behind the
/// fist, -45 crowds it against the fist, -60 prints the prompt over it).
void lookDownAtBoards(Session& session) {
    session.body().setPitch(sim::angle_from_degrees(-50));
}

/// The eye on a thing the body is standing on: near straight down, what a
/// hand reaching to its own feet looks at. The reticle has to be on a thing
/// to name it and to take it (Session::groundItemUnderReticle), and a thing
/// underfoot is under a level frame altogether.
void lookAtFeet(Session& session) {
    session.body().setPitch(sim::angle_from_degrees(-85));
}

/// And the eye level again, for the next stop's frame.
void lookLevel(Session& session) {
    session.body().setPitch(0);
}

}  // namespace

namespace scripted {

[[nodiscard]] int runKitLine(Session& session, const std::string& ending) {
    int landed = 0;
    gKitNote.clear();
    const sim::Tavern& tavern = session.tavern();
    if (!tavern.items().loaded()) {
        gKitNote = " items=unloaded";
        return landed;
    }
    const auto item = [&](std::string_view id) { return tavern.items().indexOf(id); };
    const auto carried = [&](std::string_view id) {
        return tavern.kit().count(item(id)) > 0;
    };
    const auto noteAim = [&](const char* key, const Session::InteractTarget& aim) {
        gKitNote += std::string(" ") + key + "=" + aim.verb +
                    (aim.subject.empty() ? "" : ":" + aim.subject) +
                    (aim.note.empty() ? "" : "/" + aim.note);
    };

    // 1. TAKE. The rope at (151,63), from the tile east of it, facing west.
    // Two in the afternoon: daylight on the quay, and the thing you walked
    // to outranks the passers-by.
    standAtFacing(session, 152, 63, 151, 63);
    // The eye goes onto the coil BEFORE the prompt is read: the reticle has
    // to be on a thing to name it (Session::groundItemUnderReticle, the
    // 2026-10-06 director's frame), and a tile ahead it is sixty degrees
    // under a level eye. See lookDownAtBoards for the geometry. Level again
    // after the press: the next stop is a lantern on a barrel at eye height.
    lookDownAtBoards(session);
    Session::InteractTarget aim = session.interactTarget();
    noteAim("take", aim);
    if (ending == "take") {
        return aim.verb == "TAKE" && aim.subject == "ROPE" ? 1 : 0;
    }
    session.interact();
    if (aim.verb == "TAKE" && aim.subject == "ROPE" && carried("rope")) {
        ++landed;  // 1
    }
    lookLevel(session);

    // 2. THEIRS. Four the next morning, the house empty. The lantern on
    // Hobbin's table from Maell's chair, then the knife and the coat.
    session.skipToHour(4);
    session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
    standAtFacing(session, 149, 74, 150, 74);
    aim = session.interactTarget();
    noteAim("theirs", aim);
    const bool theirsCue = aim.verb == "TAKE" && aim.subject == "LANTERN" && aim.note == "THEIRS" &&
                           aim.kind == AimKind::Owned;
    if (ending == "theirs") {
        return landed + (theirsCue ? 1 : 0);
    }
    session.interact();
    standAtFacing(session, 155, 73, 156, 73);
    lookDownAtBoards(session);
    session.interact();  // the knife, on the boards a tile ahead
    walkToTile(session, 148, 72);
    session.closeConversation();
    lookAtFeet(session);
    session.interact();  // the coat, underfoot
    lookLevel(session);
    if (theirsCue && carried("lantern") && carried("knife") && carried("coat")) {
        ++landed;  // 2
    }
    gKitNote += " lifts=" + std::to_string(tavern.dialogue().crimes().tally(sim::Crime::Lift));

    // 3. THE SHEET, at two, on the carried rows.
    session.skipToHour(14);
    session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
    if (openKitRow(session, "knife")) {
        ++landed;  // 3
    }
    if (ending == "sheet") {
        return landed;
    }

    // 4. EQUIP: the coat, then the knife, by the tile's own press.
    if (openKitRow(session, "coat")) {
        session.chooseTopic(0);
    }
    if (openKitRow(session, "knife")) {
        session.chooseTopic(0);
    }
    if (tavern.kit().isWorn(item("coat")) && tavern.playerWeapon() == sim::Weapon::Edged) {
        ++landed;  // 4
    }
    if (ending == "equip") {
        return landed;
    }

    // 5. SLOT: the knife to slot 3 by three presses of RIGHT, then the key.
    if (openKitRow(session, "knife")) {
        session.adjustKitSlot(3);
    }
    session.closeConversation();
    session.selectQuickSlot(2);
    if (tavern.slotItemIndex(2) == item("knife")) {
        ++landed;  // 5
    }
    if (ending == "slot") {
        return landed;
    }

    // 6. DR: knife bared (the tile's press again), fists on Ox at the door,
    // his blow turned by the coat -- stepped ONE STEP AT A TIME so the run
    // stops on the exact step the row says COAT TURNS n, before the house's
    // own barks overwrite it.
    if (openKitRow(session, "knife")) {
        session.chooseTopic(0);
    }
    session.closeConversation();
    const sim::Actor* ox = actorNamed(session, "Ox Gullbane");
    const std::int32_t oxId = ox != nullptr ? ox->id() : -1;
    const std::int32_t turnedBefore = tavern.blowsTurned();
    if (oxId >= 0 && tavern.playerWeapon() == sim::Weapon::Fists) {
        (void)swingOnceAt(session, oxId, 8);
        for (int step = 0; step < 20 * sim::kStepsPerSecond && tavern.blowsTurned() == turnedBefore;
             ++step) {
            if (step % sim::kStepsPerSecond == 0) {
                (void)closeOnAndFace(session, oxId);
            }
            session.stepMany(sim::MoveInput{}, 1);
        }
    }
    gKitNote += " turned=" + std::to_string(tavern.blowsTurned() - turnedBefore) +
                " last=" + std::to_string(tavern.lastTurned());
    if (tavern.blowsTurned() > turnedBefore) {
        ++landed;  // 6
    }
    if (ending == "dr") {
        return landed;
    }

    // 7. SEARCH: the knife back in hand off its slot, Ox put down for good,
    // the crosshair over him, the list.
    session.selectQuickSlot(2);
    bool dead = false;
    if (oxId >= 0 && tavern.playerWeapon() == sim::Weapon::Edged) {
        for (int swings = 0; swings < 24 && !dead; ++swings) {
            if (!closeOnAndFace(session, oxId)) {
                break;
            }
            session.attackDown();
            session.stepMany(sim::MoveInput{}, sim::kHardSwingHoldSteps + 1);
            session.attackUp();
            session.stepMany(sim::MoveInput{}, sim::kHardSwingRecoverySteps + 2);
            const sim::Actor* struck = tavern.actorById(oxId);
            dead = struck != nullptr && struck->activity() == sim::Activity::Dead;
        }
    }
    if (dead) {
        const sim::Actor* corpse = tavern.actorById(oxId);
        walkToTile(session, corpse->tileX(), corpse->tileY());
        session.closeConversation();
        session.stepMany(sim::MoveInput{}, 1);
        aim = session.interactTarget();
        noteAim("search", aim);
        session.interact();
    }
    if (dead && session.searchOpen() && !session.grimoireRows().empty()) {
        ++landed;  // 7
    }
    if (ending == "search") {
        return landed;
    }

    // 8. LOAD: everything off him (TAKE ALL is the last row), the bottle and
    // the hood off the tables, the body near its budget, the tile open on
    // the LOAD line.
    if (session.searchOpen()) {
        session.chooseGrimoireRow(static_cast<int>(session.grimoireRows().size()) - 1);
    }
    session.closeConversation();
    standAtFacing(session, 150, 69, 150, 70);  // Bram's chair: the bottle on Wick's
    lookDownAtBoards(session);
    session.interact();
    walkToTile(session, 149, 69);  // the hood on Bram's own
    session.closeConversation();
    lookAtFeet(session);
    session.interact();
    lookLevel(session);
    gKitNote += " load=" + std::to_string(tavern.loadDrams()) + "/" +
                std::to_string(tavern.loadBudget()) + " legs=" + std::to_string(tavern.loadSpeedQ8()) +
                "/256";
    const bool loaded = openKitRow(session, "coat");
    if (loaded && tavern.loadSpeedQ8() < 256 && tavern.loadDrams() * 4 >= tavern.loadBudget() * 3) {
        ++landed;  // 8
    }
    if (ending == "load") {
        return landed;
    }

    // 9. DROP: out to the Tarwalk in daylight, the coil put down through the
    // tile's own X on a tile clear of the quay's clutter, then a tile back,
    // facing it, so the frame has it on the boards.
    //
    // KIT FIX PASS. (154,62)/(155,62) -- this beat's spot one commit back --
    // stood the camera one tile off a lone timber cell at world (155,61),
    // one of two PAIRS of them flanking a gap in the quay's own wall line
    // (world (149,61)/(151,61) and (155,61)/(157,61); see
    // content/maps/src/docks_surface.tmx's z:+11 group, local (117,29) etc.,
    // +32/+32/+8 per docks.hpp's own VOID-border rule). A lone timber cell
    // beside water becomes a thin mooring pile (isPost()+boardedPost(),
    // docks-pieces.json's "post", thickness 0.13); this one is not beside
    // water at this reach, so static_pieces.cpp's posts() falls through to
    // the OTHER branch -- a full architectural PILLAR (PieceRole::Pillar,
    // "SM_Bld_Base_Pillar_01.gltf"), deliberately scaled to fill its own
    // cell edge to edge ("fitted to the cell: its shaft the cell's width
    // plus a hair" -- (1 + 2*thickness) / width, confirmed live at 2.81x
    // off the catalogue's 0.37 m pillar against a GRANADAD_DEBUG_NEARBY
    // dump: role=38 pos=(155.50,*,61.50) scale=(2.81,0.99,2.81)) and a full
    // storey tall. Not a bug in the piece -- it is exactly as wide as its
    // own cell, on purpose -- but the gap it stands beside is only four
    // tiles wide, and standing IN that gap (tried at both (154,62) and the
    // gap's own midpoint (153,62)) never put more than about two tiles
    // between the camera and whichever of the pair sits ahead of it once
    // standAtFacing turns west: close enough that the frustum is still
    // inside the column's own personal space, and the second-attempt frame
    // read exactly like the first. The ground-item entry was always there
    // at every one of these spots -- this was never a case of the coil
    // being off-camera, only of the camera being too close to a piece that
    // is doing its job.
    //
    // So: OFF THE GAP ENTIRELY, onto the long clear reach of the Tarwalk
    // east of it. The wall line carries nothing at all from world x=158 to
    // beyond 200 (same tmx group, same row, checked the same way) -- ten
    // tiles of clearance in every direction is a different problem than the
    // four-tile gap ever could solve.
    //
    // KIT FIX PASS, SECOND CUT. (166,62)/(165,62) still put the quay's own
    // ambient set dressing dead in frame -- a `prop_crate` (docks-pieces.json,
    // SM_Gen_Prop_Crate_03.gltf) and its neighbouring barrel sit fixed at
    // roughly world (167,62), the wall line's own clearance never having
    // accounted for the loose clutter it carries, only the buildings framing
    // it. A camera one tile off that pair reads the same as one tile off a
    // pillar: full-frame and foreground. Walked the boards further east in
    // fixed-camera surveys (facing 270 from x=170 through x=200) until the
    // pair fell far enough behind to read as background scenery rather than
    // an obstruction -- clear by x=195, with nothing else fixed standing in
    // the stretch immediately ahead of it. (196,62) facing (195,62) keeps
    // the beat's own shape and stays north of the dockers' own line (63).
    session.closeConversation();
    walkToTile(session, 195, 62);
    session.closeConversation();
    if (openKitRow(session, "rope")) {
        session.dropHighlightedKitRow();
    }
    session.closeConversation();
    // One tile back, off the dockers' own line down the middle of the quay,
    // so the coil is in reach whatever side of the tile the walk landed on
    // and nobody is standing on it for the frame.
    standAtFacing(session, 196, 62, 195, 62);
    // KIT POLISH: and the eye down onto it. "Stepped back to look at it on
    // the boards" was only half true -- the crosshair named the coil while
    // the mesh sat sixty degrees under a level frame (TAKE was by reach
    // then; it is by the reticle now, Session::groundItemUnderReticle),
    // and the critic found it only at a --pitch of -40. Looking at it
    // means looking down: the mouse follows what the hand put down. See
    // lookDownAtBoards for the numbers and why no change to the mesh could
    // have done this instead.
    lookDownAtBoards(session);
    bool coilDown = false;
    for (const sim::GroundItem& entry : tavern.groundItems()) {
        if (entry.item == item("rope") && entry.x == 195 && entry.y == 62) {
            coilDown = true;
        }
    }
    if (!carried("rope") && coilDown) {
        ++landed;  // 9
    }
    if (ending == "drop") {
        return landed;
    }

    // 10. The tile again, on what is left.
    if (openKitRow(session, "coat")) {
        ++landed;  // 10
    }
    return landed;
}

}  // namespace scripted

}  // namespace granadad::render
