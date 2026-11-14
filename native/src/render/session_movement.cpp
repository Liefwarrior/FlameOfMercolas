// Session movement: climb, drop, jump, crouch, landing, the leap.
#include "session_internal.hpp"

namespace granadad::render {

// ---------------------------------------------------------------------------
// S5: the roof verbs
// ---------------------------------------------------------------------------

void Session::settleLanding(const sim::RoofResult& move) {
    // THE CHARGE IS THE ROOM'S. Everything a landing moves -- the craft, the
    // hit points, the roof-run, the counted verb -- is simulation state, and it
    // moved out of this file in S6 so the simulation suite can drive it and a
    // mutation to any clause of it can go red. See Tavern::settleLanding.
    const sim::Tavern::LandingResult charged = tavern_->settleLanding(
        move, body_->takeFallBands(), body_->band(), body_->tileX(), body_->tileY());
    if (charged.hurt > 0) {
        // IN METRES, because the fall curve is in metres now and a player who is
        // told "50 HURT" learns nothing they can act on. "5M - 50 HURT" tells
        // them the roof they are standing on is the last one they can come off.
        roofMove_ += " - " + std::to_string(charged.fellMm / 1000) + "M";
        if (charged.softLanding) {
            roofMove_ += " INTO WATER";
        }
        roofMove_ += " - " + std::to_string(charged.hurt) + " HURT";
    }
}

sim::RoofResult Session::tryClimb() {
    // FATIGUE BUILD: an empty pool gates the climb verbs before the body is
    // ever asked -- the body knows geometry, not stamina, which is why
    // RoofMove::Winded is the one refusal PlayerBody itself never returns
    // (see the enum's own note). A refused climb costs nothing, exactly as a
    // NoLedge costs nothing.
    if (tavern_->playerWinded()) {
        return sim::RoofResult{sim::RoofMove::Winded, 0, 0};
    }
    sim::RoofResult move = body_->mantle();
    bool leapt = false;
    if (!move.ok()) {
        const sim::DialogueDirector& talk = tavern_->dialogue();
        const std::int32_t roofs = talk.factions().indexOf("skyrunners");
        move = body_->leap(sim::leapReachTiles(talk.skills().level(sim::kRoofSkill),
                                               talk.standings().unlocked(roofs, "roof")));
        leapt = move.ok();
    }
    if (!move.ok()) {
        // NOTHING SAID. The caller decides what a failed climb means: climb()
        // itself reports the refusal, vertical() moves on to tryDropDown().
        return move;
    }
    if (leapt) {
        // FATIGUE BUILD: the leap's wind is spent when the jump is armed --
        // the moment the body commits -- AGI- and skyrunning-scaled through
        // the one charge path (Tavern::chargePlayerLeap).
        tavern_->chargePlayerLeap();
        // A LEAP IS WATCHED, NOT TELEPORTED, and this is the S5 review's second
        // finding closed. S5 shipped a `while (body_->airborne()) step()` right
        // here, inside the keypress: the arc ran to its end before the frame
        // that showed the jump was ever drawn, so every leap in real play was
        // instant -- and it burned twenty-four movement steps of tavern clock
        // inside one frame while it did it. player.hpp:227 says a leap is
        // "something the player watches happen rather than a teleport with a
        // sound effect" and test_roofrun.cpp asserts it of PlayerBody; the
        // client then threw the arc away.
        //
        // So the press ARMS the leap and nothing more. The ordinary step pump
        // -- the client's, a capture script's, a test's -- flies it, and the
        // landing is settled in step() at the moment the feet touch, which is
        // also the only moment takeFallBands() has anything to report.
        // A leap down onto a roof one tile away is a real move -- PlayerBody's
        // second pass allows it -- and it used to announce itself as "OVER 1
        // TILES".
        roofMove_ =
            "OVER " + std::to_string(move.tiles) + (move.tiles == 1 ? " TILE." : " TILES.");
        say(roofMove_);
        // A MOMENT, NOT NEWS: this line's clock runs under the rung plate.
        messageHeld_ = false;
        pendingLanding_ = move;
        awaitingLanding_ = true;
        syncTavernToBody();
        return move;
    }
    roofMove_ = "UP ONTO THE LEDGE.";
    // FATIGUE BUILD: the haul's wind, spent on a climb that happened -- the
    // same bill the automatic path pays in step().
    tavern_->chargePlayerMantle();
    // Same order as dropDown, and for the same reason: a mantle onto a LOWER
    // ledge is a fall too, and the line has to be said after it is charged.
    settleLanding(move);
    say(roofMove_);
    messageHeld_ = false;
    syncTavernToBody();
    return move;
}

void Session::climb() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Climb);
    const WatchDepthGuard watchGuard(watchDepth_);
    dismissOverlays();
    if (talking()) {
        return;
    }
    const sim::RoofResult move = tryClimb();
    if (!move.ok()) {
        roofMove_ = std::string(sim::roofRefusal(move.move));
        say(roofMove_);
    }
}

sim::RoofResult Session::tryDropDown() {
    const sim::RoofResult move = body_->dropOff();
    if (!move.ok()) {
        return move;
    }
    roofMove_ =
        "DOWN " + std::to_string(move.bands) + (move.bands == 1 ? " LEVEL." : " LEVELS.");
    // SETTLE FIRST, THEN SAY IT. #77 found this by driving the real window and
    // stepping off the Gull's lead: the alert read "DOWN 2 LEVELS." and the
    // health bar quietly halved with nothing on screen connecting the two.
    // settleLanding appends the height and the injury to roofMove_, so saying
    // the line before charging the fall throws away the only part of it a player
    // can act on. What they see now is "DOWN 2 LEVELS. - 5M - 50 HURT".
    // NEWS, not a moment: the hurt is the part a player acts on, so this
    // line is held under a rung plate like the take off a box.
    settleLanding(move);
    say(roofMove_);
    syncTavernToBody();
    return move;
}

void Session::dropDown() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Drop);
    const WatchDepthGuard watchGuard(watchDepth_);
    dismissOverlays();
    if (talking()) {
        return;
    }
    const sim::RoofResult move = tryDropDown();
    if (!move.ok()) {
        roofMove_ = std::string(sim::roofRefusal(move.move));
        say(roofMove_);
    }
}

void Session::vertical() {
    if (refusedInCustody()) {
        return;
    }
    dismissOverlays();
    if (talking() || picking()) {
        return;
    }
    // #85. ONE BUTTON, RESOLVED BY WHAT IS DIRECTLY AHEAD OR BELOW: climb
    // (mantle, or the leap it falls back to) first, a drop if there is a
    // ledge to step off, an ordinary standing jump when neither is there.
    // Each of the first two already refuses cleanly on ground that is
    // neither -- RoofMove::NoLedge/NoGap for a climb, "the tile ahead is
    // walkable" for a drop -- so trying them first costs nothing on flat
    // ground, which is where this actually resolves to a jump.
    if (tryClimb().ok()) {
        return;
    }
    if (tryDropDown().ok()) {
        return;
    }
    if (!body_->jump()) {
        return;
    }
    // FATIGUE BUILD: the reference prices every jump (5 points, AGI-scaled)
    // and so does this one -- charged only on a hop that actually left the
    // ground, never on the buffered request jump() banks instead.
    tavern_->chargePlayerJump();
    // NOTHING IS SAID -- see jump()'s own note: a jump that announced itself
    // on the alert row every time would be the noisiest thing in the game.
    tavern_->setPlayerMotion(true, true);
}

// ---------------------------------------------------------------------------
// S9: crouching, lifting, and the wire
// ---------------------------------------------------------------------------

void Session::toggleCrouch() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Crouch);
    const WatchDepthGuard watchGuard(watchDepth_);
    dismissOverlays();
    if (talking()) {
        return;
    }
    tavern_->toggleStance();
    say(tavern_->stance() == sim::Stance::Crouched ? "CROUCHED" : "UPRIGHT");
}

sim::Stance Session::stance() const noexcept { return tavern_->stance(); }

bool Session::hidden() const noexcept { return tavern_->hidden(); }

void Session::setCrouched(bool crouched) {
    if (refusedInCustody()) {
        return;
    }
    // A CALL THAT CHANGES NOTHING TOUCHES NOTHING. The client calls this on
    // every release of the sneak key with the HoldToggle's current answer --
    // including the release of a B whose press was Escape on a page -- and
    // dismissOverlays() ran first, unconditionally, so a release that left
    // the stance exactly as it was still put every page down: CONTROLS, B
    // landed on the street instead of the pause menu it had just returned
    // to. The stance check comes first now; only a press that actually
    // changes the stance is a world verb that puts the pages away.
    if ((tavern_->stance() == sim::Stance::Crouched) == crouched) {
        return;
    }
    dismissOverlays();
    if (talking()) {
        return;
    }
    tavern_->toggleStance();
    say(crouched ? "CROUCHED" : "UPRIGHT");
}

void Session::jump() {
    if (refusedInCustody()) {
        return;
    }
    dismissOverlays();
    if (talking() || picking()) {
        return;
    }
    if (!body_->jump()) {
        return;
    }
    // NOTHING IS SAID. A jump that announced itself on the alert row every time
    // would be the noisiest thing in the game, and the player can see it.
    tavern_->setPlayerMotion(true, true);
}

int Session::flyOutLeap() {
    int steps = 0;
    // Bounded by construction -- kLeapStepsPerTile * the longest reach any
    // teaching buys, or the haul -- but bounded HERE as well, because a loop
    // whose exit depends on simulation state is a loop that hangs a build the
    // day that state is wrong.
    //
    // #77 ADDS THE HAUL, and leaving it out cost four scripted lines their
    // beats before the gate caught it. A mantle is no longer instantaneous: the
    // band changes at once but the legs are locked for kHaulSteps while the eye
    // rises, because a 2.7 m wall is a climb and not a hop. A capture script
    // that drained only the LEAP walked away from the ledge on the very next
    // call, found the body would not move, counted four stuck steps and gave up
    // -- so `--skyrun` and the burglary both stopped on the roof.
    constexpr int kCeiling = 4 * sim::kLeapStepsPerTile * sim::kLeapReachTiles + sim::kHaulSteps;
    while ((body_->airborne() || body_->hauling()) && steps < kCeiling) {
        step(sim::MoveInput{});
        ++steps;
    }
    return steps;
}

}  // namespace granadad::render
