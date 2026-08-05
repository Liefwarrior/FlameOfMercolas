#include "granadad/sim/player.hpp"

#include "granadad/sim/stealth.hpp"

#include "granadad/sim/rng.hpp"

namespace granadad::sim {

namespace {

/// Clamps a signed displacement to at most kMaxStepQ8, keeping its sign.
[[nodiscard]] std::int32_t cappedStep(std::int32_t remaining) noexcept {
    if (remaining > kMaxStepQ8) {
        return kMaxStepQ8;
    }
    if (remaining < -kMaxStepQ8) {
        return -kMaxStepQ8;
    }
    return remaining;
}

}  // namespace

std::string_view roofMoveName(RoofMove move) noexcept {
    switch (move) {
        case RoofMove::Done:
            return "done";
        case RoofMove::NoLedge:
            return "no ledge";
        case RoofMove::NoHeadroom:
            return "no headroom";
        case RoofMove::NoGap:
            return "no gap";
        case RoofMove::NoLanding:
            return "no landing";
        case RoofMove::Airborne:
            return "airborne";
        case RoofMove::Blocked:
            return "blocked";
    }
    return "?";
}

std::string_view roofRefusal(RoofMove move) noexcept {
    switch (move) {
        case RoofMove::Done:
            return "";
        case RoofMove::NoLedge:
            return "NOTHING HERE TO GET A HAND ON.";
        case RoofMove::NoHeadroom:
            return "THERE IS A FLOOR OVER YOUR HEAD.";
        case RoofMove::NoGap:
            return "THAT IS A STEP, NOT A LEAP.";
        case RoofMove::NoLanding:
            return "NOTHING TO COME DOWN ON.";
        case RoofMove::Airborne:
            return "YOUR FEET ARE ALREADY OFF THE LEAD.";
        case RoofMove::Blocked:
            return "NO ROOM TO PUT A BODY DOWN THERE.";
    }
    return "";
}

std::int32_t safeDropBands(std::int32_t skyrunningLevel, bool taughtByTheRoofs) noexcept {
    // One band free to anybody, a second at journeyman skyrunning, and a third
    // only to somebody the roofs have shown where to land.
    //
    // CAPPED AT kMaxSafeDropBands, NOT kMaxDropBands. The old cap was the
    // deepest fall the geometry allows, which was harmless when a band was one
    // tile and is not now: three bands is 8.2 m, and a rule that hands the best
    // roof-runner an 8.2 m drop for nothing has stopped being a skill. Two
    // bands is the ceiling, so the deepest drop in the district costs
    // everybody something. See player.hpp on both constants.
    std::int32_t bands = kSafeDropBands;
    if (skyrunningLevel >= 10) {
        bands += 1;
    }
    if (taughtByTheRoofs) {
        bands += 1;
    }
    return bands > kMaxSafeDropBands ? kMaxSafeDropBands : bands;
}

std::int32_t leapReachTiles(std::int32_t skyrunningLevel, bool taughtByTheRoofs) noexcept {
    std::int32_t tiles = kLeapReachTiles;
    if (taughtByTheRoofs) {
        tiles += 1;
    }
    if (skyrunningLevel >= 20) {
        tiles += 1;
    }
    return tiles;
}

PlayerBody::PlayerBody(const TileQuery& tiles, std::int32_t tileX, std::int32_t tileY,
                       std::int32_t band, Angle yaw) noexcept
    : tiles_(&tiles),
      x_(q8_tile_centre(tileX)),
      y_(q8_tile_centre(tileY)),
      band_(band),
      feetZ_(q8_of_tile(band)),
      yaw_(yaw & (kTurnFull - 1)) {
    spawnedLegally_ = tiles.standable(tileX, tileY, band) && bodyFits(x_, y_, band);
}

void PlayerBody::setPitch(Angle pitch) noexcept {
    pitch_ = pitch > kMaxPitch ? kMaxPitch : (pitch < -kMaxPitch ? -kMaxPitch : pitch);
}

bool PlayerBody::bodyFits(std::int32_t cx, std::int32_t cy, std::int32_t band) const noexcept {
    const std::int32_t ctx = q8_tile(cx);
    const std::int32_t cty = q8_tile(cy);
    if (!tiles_->standable(ctx, cty, band)) {
        return false;
    }
    // The square's footprint. kBodyRadius < 128, so this spans at most two
    // tiles on each axis, but the loop does not depend on that.
    const std::int32_t x0 = q8_tile(cx - kBodyRadius);
    const std::int32_t x1 = q8_tile(cx + kBodyRadius);
    const std::int32_t y0 = q8_tile(cy - kBodyRadius);
    const std::int32_t y1 = q8_tile(cy + kBodyRadius);
    for (std::int32_t ty = y0; ty <= y1; ++ty) {
        for (std::int32_t tx = x0; tx <= x1; ++tx) {
            if (!tiles_->solid(tx, ty, band)) {
                continue;
            }
            // A filled cell at the body's own level is a wall — UNLESS its top
            // face is a surface exactly one level up, in which case it is the
            // kerb the body just stepped off and the shoulder is allowed to
            // overhang it. Without this, stepping down off any raised walkway
            // is impossible: the tile you came from is solid rock at your new
            // level, and it is still inside your shoulder.
            if (!tiles_->walkable(tx, ty, band + 1)) {
                return false;
            }
        }
    }
    return true;
}

void PlayerBody::moveAxis(std::int32_t deltaX, std::int32_t deltaY) noexcept {
    std::int32_t remainingX = deltaX;
    std::int32_t remainingY = deltaY;
    while (remainingX != 0 || remainingY != 0) {
        const std::int32_t stepX = cappedStep(remainingX);
        const std::int32_t stepY = cappedStep(remainingY);
        const std::int32_t nx = wrap_add(x_, stepX);
        const std::int32_t ny = wrap_add(y_, stepY);

        std::int32_t nextBand = band_;
        const std::int32_t ntx = q8_tile(nx);
        const std::int32_t nty = q8_tile(ny);
        if (ntx != tileX() || nty != tileY()) {
            nextBand = tiles_->stepBand(tileX(), tileY(), band_, ntx, nty);
            if (nextBand == TileQuery::kNoBand) {
                return;
            }
        }
        if (!bodyFits(nx, ny, nextBand)) {
            return;
        }
        x_ = nx;
        y_ = ny;
        band_ = nextBand;
        remainingX -= stepX;
        remainingY -= stepY;
    }
}

void PlayerBody::settleFeet() noexcept {
    const std::int32_t targetZ = q8_of_tile(band_);
    if (feetZ_ < targetZ) {
        feetZ_ = feetZ_ + kEyeEaseRate > targetZ ? targetZ : feetZ_ + kEyeEaseRate;
    } else if (feetZ_ > targetZ) {
        feetZ_ = feetZ_ - kEyeEaseRate < targetZ ? targetZ : feetZ_ - kEyeEaseRate;
    }
}

void PlayerBody::step(const MoveInput& input) noexcept {
    ++steps_;

    // --- look ---------------------------------------------------------------
    //
    // RAW, AND IT MUST STAY RAW. The mouse delta goes on the yaw with nothing
    // between them -- no smoothing filter, no acceleration curve, no per-step
    // clamp. Everything a player can tune about aiming (sensitivity, invert)
    // happens once, in the client, before the number arrives here, so the
    // simulation sees an angle and never a preference.
    yaw_ = wrap_add(yaw_, wrap_mul(input.turn, kTurnRate));
    yaw_ = wrap_add(yaw_, input.yawDelta);
    yaw_ &= (kTurnFull - 1);
    setPitch(wrap_add(pitch_, input.pitchDelta));

    // --- fly ----------------------------------------------------------------
    //
    // A body in a LEAP is not steering. The look above still runs, because
    // turning your head mid-jump is free and looking down at the street you are
    // crossing is the whole point of a leap; the legs are not. A leap is a
    // committed move between two validated endpoints and steering it would let
    // a player steer into a wall.
    if (leapStepsLeft_ > 0) {
        flyLeapStep();
        return;
    }

    // --- haul ---------------------------------------------------------------
    //
    // A 2.7 m wall takes a second and both hands. The band changed the instant
    // the climb started -- the simulation is never half inside a wall -- and the
    // legs are locked until the eye catches up. See kHaulSteps.
    if (haulStepsLeft_ > 0) {
        --haulStepsLeft_;
        settleFeet();
        return;
    }

    // --- walk ---------------------------------------------------------------
    //
    // THE DEFAULT IS A JOG. Neither modifier is 5 m/s; sprint is the accelerator
    // and walk is the brake. S9: crouching halves the WALK and beats sprinting.
    // What being unseen costs in a first-person game is TIME, and this is where
    // the bill is paid.
    std::int32_t speed = kJogSpeed;
    if (input.crouch) {
        speed = kCrouchSpeed;
    } else if (input.walk) {
        speed = kWalkSpeed;
    } else if (input.sprint) {
        speed = kSprintSpeed;
    }
    // A body in the air keeps the speed it had; there is no sprinting off a
    // ledge into a faster jump.
    if (input.jump && jumpStepsLeft_ == 0) {
        jumpStepsLeft_ = kJumpSteps;
        jumpStepsTotal_ = kJumpSteps;
    }
    // --- what the legs were ASKED for, before what they are DOING ----------
    std::int32_t wantX = 0;
    std::int32_t wantY = 0;
    if (input.forward != 0 || input.strafe != 0) {
        // Direction in Q16, intent in {-1,0,1}, speed in Q8-per-step. The
        // product is Q16*Q8 and comes back to Q8 with one shift, in 64 bits so
        // nothing can overflow on the way.
        const std::int64_t dirX =
            static_cast<std::int64_t>(forward_x_q16(yaw_)) * input.forward +
            static_cast<std::int64_t>(right_x_q16(yaw_)) * input.strafe;
        const std::int64_t dirY =
            static_cast<std::int64_t>(forward_y_q16(yaw_)) * input.forward +
            static_cast<std::int64_t>(right_y_q16(yaw_)) * input.strafe;

        // Diagonal input would otherwise be sqrt(2) faster than straight. The
        // Q16 vector is already unit length per axis, so normalising means
        // scaling by 1/sqrt(2) when both axes are engaged -- 46341/65536, the
        // same constant the sine table already carries at its midpoint.
        std::int64_t scale = kTrigOne;
        if (input.forward != 0 && input.strafe != 0) {
            scale = 46341;
        }

        wantX = static_cast<std::int32_t>((dirX * speed * scale) >> 32);
        wantY = static_cast<std::int32_t>((dirY * speed * scale) >> 32);
    }

    // --- and what they are doing -------------------------------------------
    //
    // A BODY HAS MASS. The legs approach the gait they were asked for over
    // kAccelSteps and drop it over kBrakeSteps, so starting is a shove and
    // stopping is a heel -- see human_scale.hpp. In the air it is a third of
    // both, which is enough to steer a jump and not enough to fly one.
    if (input.snapVelocity) {
        // No legs. See MoveInput::snapVelocity -- this is the capture script,
        // and it is the pre-#77 behaviour preserved exactly.
        velX_ = wantX;
        velY_ = wantY;
    } else {
        const bool airborne = jumpStepsLeft_ > 0;
        const std::int32_t accel =
            airborne ? (kGroundAccelQ8 * kAirControlPercent) / 100 : kGroundAccelQ8;
        const std::int32_t brake =
            airborne ? (kGroundBrakeQ8 * kAirControlPercent) / 100 : kGroundBrakeQ8;
        velX_ = approach(velX_, wantX, accel, brake);
        velY_ = approach(velY_, wantY, accel, brake);
    }

    if (velX_ != 0 || velY_ != 0) {
        // One axis at a time: a body sliding along a warehouse front keeps its
        // tangential speed instead of stopping dead on the corner.
        const std::int32_t wasX = x_;
        const std::int32_t wasY = y_;
        moveAxis(velX_, 0);
        if (x_ == wasX && velX_ != 0) {
            // INTO A WALL IS STOPPED, and the velocity has to know. Leaving it
            // banked means a body that has been pressed against a warehouse for
            // three seconds shoots sideways at full speed the instant it turns
            // away, which is a very old bug in a very recognisable costume.
            velX_ = 0;
        }
        const std::int32_t midY = y_;
        moveAxis(0, velY_);
        if (y_ == midY && velY_ != 0) {
            velY_ = 0;
        }

        // --- CONTEXTUAL TRAVERSAL -------------------------------------------
        //
        // THIS IS THE ARCHAIC THING, FIXED. Until now the only way onto anything
        // was to stop, line the wall up by eye and press a verb key that no
        // other game has bound; a player who simply walked at a ledge got a body
        // stuck against masonry and no indication that climbing existed at all.
        //
        // The rule is the one every game made this decade uses, and it is short:
        // YOU WALKED FORWARD AND YOU DID NOT MOVE, so try to get over whatever
        // stopped you. Nothing else. It cannot fire while strafing (climbing
        // sideways is not a thing), while crouched (you are hiding, not
        // vaulting), while in the air, or while already hauling.
        //
        // The wall itself decides whether there is a climb here: mantleToward
        // wants a solid face at the body's own band, a standable top exactly one
        // band up and headroom to rise into. A warehouse wall two storeys high
        // fails the second clause and the body just stops, which is what a wall
        // is for. The explicit verb key is still bound and still works -- it is
        // the fallback now rather than the only path.
        if (input.autoTraverse && input.forward > 0 && !input.crouch && x_ == wasX &&
            y_ == wasY) {
            const RoofResult climbed = mantleToward(facing_step(yaw_));
            if (climbed.ok()) {
                autoMove_ = climbed;
            }
        }
    }

    // --- the jump -----------------------------------------------------------
    //
    // AFTER the walk, deliberately: air control is what makes a jump feel like a
    // jump, so the legs run first and the arc is laid over wherever they got to.
    if (jumpStepsLeft_ > 0) {
        flyJumpStep();
        return;
    }

    // --- settle onto the band's surface -------------------------------------
    settleFeet();
}

bool PlayerBody::jump() noexcept {
    if (leapStepsLeft_ > 0 || jumpStepsLeft_ > 0 || haulStepsLeft_ > 0) {
        return false;
    }
    jumpStepsLeft_ = kJumpSteps;
    jumpStepsTotal_ = kJumpSteps;
    return true;
}

void PlayerBody::flyJumpStep() noexcept {
    --jumpStepsLeft_;
    if (jumpStepsLeft_ <= 0) {
        jumpStepsTotal_ = 0;
        settleFeet();
        return;
    }
    // The same integer parabola a leap flies: 4*a*t*(1-t), with the multiply
    // before the divide so nothing rounds twice. Measured against the band the
    // jump LEFT rather than the one under the feet now, so walking off a kerb
    // mid-hop does not teleport the arc.
    const std::int32_t done = jumpStepsTotal_ - jumpStepsLeft_;
    // Against the band the feet are OVER, not the one the jump left: air control
    // can walk a body off a kerb mid-hop and the arc should follow the ground it
    // is going to come down on rather than the one it left.
    const std::int32_t floorZ = q8_of_tile(band_);
    const std::int32_t rise = 4 * kJumpRiseQ8 * done * (jumpStepsTotal_ - done) /
                              (jumpStepsTotal_ * jumpStepsTotal_);
    feetZ_ = floorZ + rise;
}

std::int32_t PlayerBody::approach(std::int32_t have, std::int32_t want, std::int32_t accel,
                                  std::int32_t brake) noexcept {
    if (have == want) {
        return want;
    }
    // SPEEDING UP means the same direction and more of it. Everything else --
    // slowing, stopping, and above all REVERSING -- is braking, which is why a
    // body that turns round does it on a planted foot and not on a slide.
    const bool sameWay = (want > 0 && have >= 0) || (want < 0 && have <= 0);
    const std::int32_t magnitudeHave = have < 0 ? -have : have;
    const std::int32_t magnitudeWant = want < 0 ? -want : want;
    const std::int32_t rate =
        (want != 0 && sameWay && magnitudeWant > magnitudeHave) ? accel : brake;
    if (want > have) {
        const std::int32_t next = have + rate;
        return next > want ? want : next;
    }
    const std::int32_t next = have - rate;
    return next < want ? want : next;
}

RoofResult PlayerBody::takeAutoMove() noexcept {
    const RoofResult out = autoMove_;
    autoMove_ = RoofResult{};
    return out;
}

void PlayerBody::push(std::int32_t dxQ8, std::int32_t dyQ8) noexcept {
    // Same order as a walking step: one axis, then the other, so a body shoved
    // along a wall slides down it instead of stopping dead on the first
    // corner. moveAxis already caps and substeps, so an impulse of any size is
    // safe against tunnelling.
    if (leapStepsLeft_ > 0) {
        // A bouncer cannot shove somebody who is over the alley. The ejection
        // path calls this every second and it must not silently teleport a body
        // out of an arc it is already committed to.
        return;
    }
    moveAxis(dxQ8, 0);
    moveAxis(0, dyQ8);
    settleFeet();
}

// ---------------------------------------------------------------------------
// S5: the roof moves
// ---------------------------------------------------------------------------

RoofResult PlayerBody::land(std::int32_t tileX, std::int32_t tileY, std::int32_t fromBand,
                            std::int32_t toBand, std::int32_t tiles) noexcept {
    const std::int32_t cx = q8_tile_centre(tileX);
    const std::int32_t cy = q8_tile_centre(tileY);
    if (!bodyFits(cx, cy, toBand)) {
        return RoofResult{RoofMove::Blocked, 0, 0};
    }
    x_ = cx;
    y_ = cy;
    band_ = toBand;
    const std::int32_t fell = fromBand > toBand ? fromBand - toBand : 0;
    fallBands_ += fell;
    RoofResult out;
    out.move = RoofMove::Done;
    out.bands = toBand > fromBand ? toBand - fromBand : fell;
    out.tiles = tiles;
    return out;
}

std::int32_t PlayerBody::takeFallBands() noexcept {
    const std::int32_t bands = fallBands_;
    fallBands_ = 0;
    return bands;
}

void PlayerBody::placeAt(std::int32_t tileX, std::int32_t tileY, std::int32_t band) noexcept {
    x_ = q8_tile_centre(tileX);
    y_ = q8_tile_centre(tileY);
    band_ = band;
    // Any arc in progress is over -- a body that was in the air is now in a
    // cell -- and there is no fall waiting to be charged for the journey.
    leapStepsLeft_ = 0;
    leapStepsTotal_ = 0;
    fallBands_ = 0;
    // #77: and neither is the hop or the haul. A body the Watch has carried to
    // the impound gate is not still half way up a wall three streets away.
    jumpStepsLeft_ = 0;
    jumpStepsTotal_ = 0;
    haulStepsLeft_ = 0;
    velX_ = 0;
    velY_ = 0;
    autoMove_ = RoofResult{};
    feetZ_ = q8_of_tile(band_);
}

RoofResult PlayerBody::mantle() noexcept { return mantleToward(facing_step(yaw_)); }

RoofResult PlayerBody::mantleToward(const TileStep& facing) noexcept {
    if (leapStepsLeft_ > 0 || jumpStepsLeft_ > 0) {
        return RoofResult{RoofMove::Airborne, 0, 0};
    }
    if (haulStepsLeft_ > 0) {
        // Already climbing. Silently, because the automatic path calls this
        // every step a held forward key is pressed against a wall and a refusal
        // line on the HUD sixty times a second is not a message, it is a strobe.
        return RoofResult{RoofMove::Airborne, 0, 0};
    }

    // THE FLIGHT YOU ARE STANDING ON, FIRST -- and this is a bug fix wearing a
    // feature's clothes.
    //
    // The Gilded Gull's stair is authored as ONE cell, (159,77), a STAIR at
    // z19 and a STAIR again at z20, with open floor all round it on both
    // levels. Walk onto it and stepBand's preference order -- same level, then
    // down, then up -- keeps you on z19 forever, because every neighbour is
    // standable at z19 and the same-level answer always wins. A flood fill over
    // the walking rule finds ZERO z19 -> z20 transitions anywhere in the
    // building.
    //
    // So the guest floor of the ward's grandest house, with its four rentable
    // rooms, its innkeeper and its four strongboxes, HAS NEVER BEEN REACHABLE
    // ON FOOT. S2 shipped rentRoom() and sleep() and S4 wrote a case about a
    // robbery on that floor; every one of them put the body up there by
    // constructing it there. Nobody noticed, because nobody ever walked.
    //
    // Rather than reorder the walking rule -- which would turn every ramp on
    // Saltgate Rise into a one-way trip and move a pinned reachability count
    // that has nothing to do with this -- the sprint's own up-key takes the
    // stairs. Stand on the flight, press it, and you are on the next floor.
    // dropOff() comes back down the same way.
    if (tiles_->climbable(tileX(), tileY(), band_) &&
        tiles_->standable(tileX(), tileY(), band_ + 1)) {
        return startHaul(land(tileX(), tileY(), band_, band_ + 1, 0));
    }

    const std::int32_t ahead = tileX() + facing.dx;
    const std::int32_t asideY = tileY() + facing.dy;
    if (tiles_->solid(tileX(), tileY(), band_ + 1)) {
        return RoofResult{RoofMove::NoHeadroom, 0, 0};
    }
    const std::int32_t top = tiles_->mantleBand(tileX(), tileY(), band_, ahead, asideY);
    if (top == TileQuery::kNoBand) {
        return RoofResult{RoofMove::NoLedge, 0, 0};
    }
    return startHaul(land(ahead, asideY, band_, top, 1));
}

/// A CLIMB THAT WENT COSTS TIME. Only a real one -- a refusal leaves the legs
/// alone, so walking at a wall that has no top does not freeze the player for
/// half a second every step they hold forward.
RoofResult PlayerBody::startHaul(const RoofResult& climbed) noexcept {
    if (climbed.ok()) {
        haulStepsLeft_ = kHaulSteps;
    }
    return climbed;
}

RoofResult PlayerBody::dropOff() noexcept {
    if (leapStepsLeft_ > 0) {
        return RoofResult{RoofMove::Airborne, 0, 0};
    }
    // Back down the flight you are standing on -- see mantle() on why a stair
    // needs a verb at all in this build.
    if (tiles_->climbable(tileX(), tileY(), band_) &&
        tiles_->standable(tileX(), tileY(), band_ - 1)) {
        return land(tileX(), tileY(), band_, band_ - 1, 0);
    }
    const TileStep facing = facing_step(yaw_);
    const std::int32_t ahead = tileX() + facing.dx;
    const std::int32_t asideY = tileY() + facing.dy;
    if (tiles_->standable(ahead, asideY, band_)) {
        // Walkable ground. Walk onto it.
        return RoofResult{RoofMove::NoGap, 0, 0};
    }
    if (tiles_->solid(ahead, asideY, band_)) {
        return RoofResult{RoofMove::NoLanding, 0, 0};
    }
    const std::int32_t deepest = deepestLanding();
    if (deepest > band_ - 1) {
        return RoofResult{RoofMove::NoLanding, 0, 0};
    }
    const std::int32_t floorBand =
        tiles_->landingBand(ahead, asideY, band_ - 1, band_ - 1 - deepest);
    if (floorBand == TileQuery::kNoBand) {
        // Nothing under it. This is what stops a body stepping off the Long Quay
        // into the harbour on purpose.
        return RoofResult{RoofMove::NoLanding, 0, 0};
    }
    return land(ahead, asideY, band_, floorBand, 1);
}

RoofResult PlayerBody::leap(std::int32_t reachTiles) noexcept {
    if (leapStepsLeft_ > 0) {
        return RoofResult{RoofMove::Airborne, 0, 0};
    }
    const TileStep facing = facing_step(yaw_);
    const std::int32_t reach = reachTiles < 1 ? 1 : reachTiles;
    const std::int32_t fromX = tileX();
    const std::int32_t fromY = tileY();

    // THE FAR ROOF FIRST. A jumper who lines up the gap is aiming at the other
    // side of it, not at the alley floor two storeys down -- and the alley is
    // always there, so a nearest-landing-wins search would make every leap a
    // fall and the roofs would still not join up. That was the first version of
    // this and the case that crosses the Gull's own alley caught it.
    //
    // Pass one walks out to the end of the reach looking only at the launch
    // band, and stops at the first wall: nothing is ever leapt THROUGH.
    std::int32_t clear = 0;
    for (std::int32_t t = 1; t <= reach; ++t) {
        const std::int32_t tx = fromX + facing.dx * t;
        const std::int32_t ty = fromY + facing.dy * t;
        if (tiles_->solid(tx, ty, band_)) {
            break;
        }
        if (tiles_->standable(tx, ty, band_)) {
            if (t == 1) {
                // Ground at arm's length. That is a step, and a leap that
                // pretended otherwise would be a free dash across open floor.
                return RoofResult{RoofMove::NoGap, 0, 0};
            }
            return armLeap(tx, ty, band_, t);
        }
        clear = t;
    }
    // Pass two: no far side at this height, so come down. Up to two bands, and
    // the NEAREST one, because a body dropping out of a jump does not get to
    // choose which roof it hits.
    const std::int32_t deepest = deepestLanding();
    for (std::int32_t t = 1; t <= clear; ++t) {
        const std::int32_t tx = fromX + facing.dx * t;
        const std::int32_t ty = fromY + facing.dy * t;
        for (std::int32_t drop = 1; drop <= 2; ++drop) {
            if (band_ - drop >= deepest && tiles_->standable(tx, ty, band_ - drop)) {
                return armLeap(tx, ty, band_ - drop, t);
            }
        }
    }
    return RoofResult{RoofMove::NoLanding, 0, 0};
}

std::int32_t PlayerBody::deepestLanding() const noexcept {
    const std::int32_t byHeight = band_ - kMaxDropBands;
    return landingFloor_ > byHeight ? landingFloor_ : byHeight;
}

RoofResult PlayerBody::armLeap(std::int32_t tileX, std::int32_t tileY, std::int32_t toBand,
                               std::int32_t tiles) noexcept {
    const std::int32_t total = tiles * kLeapStepsPerTile;
    leapFromX_ = x_;
    leapFromY_ = y_;
    leapToX_ = q8_tile_centre(tileX);
    leapToY_ = q8_tile_centre(tileY);
    leapFromBand_ = band_;
    leapToBand_ = toBand;
    leapStepsTotal_ = total;
    leapStepsLeft_ = total;
    RoofResult out;
    out.move = RoofMove::Done;
    out.bands = band_ > toBand ? band_ - toBand : 0;
    out.tiles = tiles;
    return out;
}

void PlayerBody::flyLeapStep() noexcept {
    --leapStepsLeft_;
    const std::int32_t done = leapStepsTotal_ - leapStepsLeft_;
    // Position by exact integer interpolation from the ENDPOINTS rather than by
    // accumulating a per-step delta: a remainder that accumulated would put two
    // machines a Q8 unit apart by the far side of the alley.
    x_ = leapFromX_ + (leapToX_ - leapFromX_) * done / leapStepsTotal_;
    y_ = leapFromY_ + (leapToY_ - leapFromY_) * done / leapStepsTotal_;

    if (leapStepsLeft_ > 0) {
        // VERIFICATION GAP (S5): the arc is not collided against. The two
        // ENDPOINTS are validated when the jump is armed -- every tile of the
        // flight path is checked for solidity at the launch band, and the
        // landing is checked with bodyFits -- but the parabola between them
        // passes through whatever is there. Nothing in the shipped district can
        // be in the way (the flight path is open cells at the launch band, and
        // the arc only ever rises above it), so this is a latent hole rather
        // than a live one; a low overhang authored across an alley would find
        // it.
        //
        // The arc. A parabola in integers: 4*a*t*(1-t) at its simplest, with t
        // as done/total, which stays exact because the multiply happens before
        // the divide.
        const std::int32_t startZ = q8_of_tile(leapFromBand_);
        const std::int32_t endZ = q8_of_tile(leapToBand_);
        const std::int32_t glide = startZ + (endZ - startZ) * done / leapStepsTotal_;
        const std::int32_t rise = 4 * kLeapArcQ8 * done * (leapStepsTotal_ - done) /
                                  (leapStepsTotal_ * leapStepsTotal_);
        feetZ_ = glide + rise;
        return;
    }

    // Down. Through the same collision a walking step uses -- and if the far
    // roof has been made unstandable underneath us since the jump was armed,
    // the body stays where it was rather than ending up inside masonry.
    const RoofResult landed =
        land(q8_tile(leapToX_), q8_tile(leapToY_), leapFromBand_, leapToBand_, 0);
    if (!landed.ok()) {
        x_ = leapFromX_;
        y_ = leapFromY_;
        band_ = leapFromBand_;
    }
    leapStepsTotal_ = 0;
    settleFeet();
}

// VERIFICATION GAP (S2): the body's digest is NOT part of the world hash.
// PlayerBody is not a SimulationSystem, so nothing folds it into a WorldHasher
// section; the twin-run gate compares this number only where a workload asks
// for it by hand. Two runs that diverged in the player's position alone, with
// every registered system agreeing, would pass the gate. Closing it means
// either registering the body as a system or hashing it from whoever owns it.
std::uint64_t PlayerBody::digest() const noexcept {
    std::uint64_t h =
        mix64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(x_)) * 0x9E3779B97F4A7C15ull);
    h = mix64(h ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(y_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(band_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(feetZ_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(yaw_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(pitch_)));
    // S5. A body in the air is a body two runs have to agree about: the arc is
    // integer, the landing is decided when the jump is armed, and a fingerprint
    // taken mid-flight has to see all of it.
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(leapStepsLeft_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(leapToX_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(leapToY_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(leapToBand_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(fallBands_)));
    // #77. A hop and a haul are both several steps long, so a fingerprint taken
    // during either is a fingerprint of a body two runs have to agree about --
    // the same argument the leap's fields are folded in for.
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(jumpStepsLeft_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(haulStepsLeft_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(velX_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(velY_)));
    return mix64(h + static_cast<std::uint64_t>(steps_));
}

}  // namespace granadad::sim
