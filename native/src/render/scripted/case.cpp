// Scripted capture drivers: case.
#include "scripted.hpp"

namespace granadad::render {

namespace scripted {

/// COURIER CASE. PLAYS THE QUIET TENANT END TO END, through the same Session
/// calls a keypress makes -- courierDeliverNow (the courier's own beat, its
/// countdown skipped for the shutter), toggleLetters, walkAcrossDistrict, the
/// Wait page's own skipToHour, toggleCrouch, examine, punch and interact.
/// Nothing here reaches into the simulation sideways, which is the only thing
/// that makes a captured frame evidence of the errand rather than a diagram of
/// it. `ending` stops the run for a shutter of one beat; empty delivers the
/// whole errand.
[[nodiscard]] int runCaseLine(Session& session, const std::string& ending) {
    gCaseBeatMask = 0;
    int landed = 0;
    std::int32_t beat = 0;
    const auto mark = [&](bool ok) {
        // CASE WATCH. The verdict lands on the tape as well as in the mask, so
        // the watch director's shutter can go on the beat's own payoff frame.
        // Recorder-only; with no tape attached this whole line is a no-op.
        session.watchBeatLanded(beat, ok);
        if (ok) {
            gCaseBeatMask |= 1 << beat;
            ++landed;
        }
        ++beat;
    };
    // CASE WATCH. Announces "beat N plays now" onto the tape ahead of each
    // block below -- the caption hook, and nothing else; see watchChapter.
    const auto chapter = [&]() { session.watchChapter(beat); };
    const sim::CasebookRaws& raws = session.sheetRaws();
    if (!raws.loaded()) {
        return 0;
    }

    chapter();
    // 1. THE SHEET INTO THE HAND. The courier's own beat, countdown skipped so
    // a headless run does not walk in place for six seconds. The book goes
    // live and the handed letter turns up on the Letters tile at once.
    session.courierDeliverNow();
    mark(session.sheetCaseLive());

    chapter();
    // 2. THE SHEET READ. The first thing the errand teaches is that paper is
    // read here -- the handed document unfolds on the Letters tile the moment
    // its lead is heard, no walk owed. A `sheet` shutter stops here with it
    // open; every other ending closes the menu and walks on.
    session.toggleLetters();
    session.chooseVisibleTopic(0);
    mark(!session.unlockedLetters().empty());

    if (ending == "sheet") {
        return landed;
    }
    session.toggleLetters();  // put the paper down and get the world back

    chapter();
    // 3. THE GULL. Walk to the door lead and look -- the map-and-casebook beat,
    // and it opens both the box and the tenant.
    const sim::Lead& door = raws.leads()[static_cast<std::size_t>(raws.indexOf("gull-door"))];
    (void)walkAcrossDistrict(session, door.site.x, door.site.y);
    session.examine();
    mark(session.sheetBook().state(raws.indexOf("gull-door")) != sim::LeadState::Open);

    if (ending == "gull") {
        session.toggleCasebook();
        return landed;
    }

    chapter();
    // 4. THE WAIT. To two in the morning: the doors just barred, the candles
    // out, the hearth dying, and the one hour Finch keeps the snug that the
    // room is also dark enough to work. skipToHour is exactly what a WAIT pick
    // spends -- the clock the tutorial teaches, driven the way the page drives
    // it.
    session.skipToHour(2);
    mark(session.timeOfDay() / 3600 == 2);

    chapter();
    // 5. THE BOX, ON THE GUEST FLOOR. Up the stair, crouched, and a look at the
    // box lead -- the break-in taught as a place stood over in the dark. The
    // box lead sits on the upper band, so the climb is part of the beat.
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    climbAndLand(session);
    if (session.stance() != sim::Stance::Crouched) {
        session.toggleCrouch();
    }
    const sim::Lead& box = raws.leads()[static_cast<std::size_t>(raws.indexOf("stair-box"))];
    walkToTile(session, box.site.x, box.site.y);
    session.examine();
    const bool boxRead = session.sheetBook().state(raws.indexOf("stair-box")) !=
                         sim::LeadState::Open;
    mark(boxRead && session.stance() == sim::Stance::Crouched);

    if (ending == "night") {
        return landed;
    }

    chapter();
    // 6. THE TENANT, PUT DOWN WITH FISTS. Down off the guest floor, upright
    // again (a man is taken up standing over him, not from a crouch), and to
    // Finch's own snug post. Punch -- NOTHING EDGED, the player carries fists,
    // so the fight resolves in the world by the brawl line's own law -- until
    // he is on the boards. Looped without stepping the room between blows, so
    // the bouncer does not cross the floor mid-beat and nobody regenerates:
    // the same room a player who kept their nerve would face.
    // GATE FIX: dropDown() alone refuses over the box -- no ledge ahead --
    // and left the body on the guest floor (beats 7-8 dead, mask 0x3F). The
    // way down is the way up: back to the stair-head and off it, the exact
    // helper every other Gull line descends by.
    comeDownstairs(session);
    if (session.stance() == sim::Stance::Crouched) {
        session.toggleCrouch();
    }
    const sim::Actor* tenant = nullptr;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.role() == sim::ActorRole::SkyrunnerContact) {
            tenant = &actor;
            break;
        }
    }
    if (tenant != nullptr) {
        const std::int32_t tenantId = tenant->id();
        walkToTile(session, tenant->tileX(), tenant->tileY());
        // ACTION-COMBAT BUILD: punch() is now a sightline TAP with a recovery
        // lockout, so the old zero-step rapid punch is gone. This tenant FIGHTS
        // BACK (unlike the rat, whom nobody defends and who does not swing), so
        // chasing him between blows never lets a swing settle and drags the fight
        // across the floor. Stand where the approach left us and face him HEAD-ON
        // each swing (the crosshair dead on him, the door crossing in behind us
        // rather than between), tapping with FISTS -- Subdue, so he goes DOWN and
        // not dead -- until he is on the boards, re-closing only if he drifts out
        // of reach. Six clean taps put a full man down well inside the door's
        // cross-warn-grace, so the beat lands before the bouncer can.
        for (int guard = 0; guard < 40 && !session.tenantDown(); ++guard) {
            const sim::Actor* mark = session.tavern().actorById(tenantId);
            if (mark == nullptr || mark->activity() == sim::Activity::Downed) {
                break;
            }
            if (mark->distanceTo(session.body().x(), session.body().y()) > sim::kMeleeReach) {
                walkToTile(session, mark->tileX(), mark->tileY());
            }
            facePlayerAndSync(session, mark->x(), mark->y());
            session.punch();
            session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps);
        }
    }
    const bool down = session.tenantDown();
    mark(down);

    if (ending == "down") {
        return landed;
    }

    chapter();
    // 7. TAKE HIM UP. The one press the errand adds -- interact() resolves it
    // ahead of TALK when a downed tenant is in reach.
    session.interact();
    mark(session.sheetCarry());

    if (ending == "taken") {
        // TRAVEL lane: stop with the man genuinely in hand -- the one state
        // the errand never otherwise parks in -- so the travel probe (and the
        // case that drives it) can press TRAVEL against the carry refusal for
        // real rather than against a flag a test set sideways.
        return landed;
    }

    chapter();
    // 8. TO THE MISSION. The back room the sheet named. Walking in with the man
    // IS the delivery -- stepSheetCase() closes the book on arrival, no press
    // owed -- so the run steps the pump a beat at the anchor to let that fire.
    const sim::Lead& close =
        raws.leads()[static_cast<std::size_t>(raws.indexOf("bring-him-in"))];
    (void)walkAcrossDistrict(session, close.site.x, close.site.y);
    for (int guard = 0; guard < 8 && !session.sheetBook().closed(); ++guard) {
        session.stepMany(sim::MoveInput{}, 1);
    }
    mark(session.sheetBook().closed());
    return landed;
}

}  // namespace scripted

}  // namespace granadad::render
