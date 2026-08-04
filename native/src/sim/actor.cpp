#include "granadad/sim/actor.hpp"

#include <algorithm>
#include <utility>

#include "granadad/sim/fixed.hpp"

namespace granadad::sim {

std::string_view actorRoleName(ActorRole role) noexcept {
    switch (role) {
        case ActorRole::Patron:
            return "patron";
        case ActorRole::Bartender:
            return "bartender";
        case ActorRole::Innkeeper:
            return "innkeeper";
        case ActorRole::Bouncer:
            return "bouncer";
        case ActorRole::PriestOfTheFlame:
            return "priest";
        case ActorRole::SkyrunnerContact:
            return "skyrunner";
        case ActorRole::Vermin:
            return "vermin";
    }
    return "?";
}

std::string_view activityName(Activity activity) noexcept {
    switch (activity) {
        case Activity::Away:
            return "away";
        case Activity::Walking:
            return "walking";
        case Activity::Working:
            return "working";
        case Activity::Drinking:
            return "drinking";
        case Activity::Watching:
            return "watching";
        case Activity::Warning:
            return "warning";
        case Activity::Ejecting:
            return "ejecting";
        case Activity::Brawling:
            return "brawling";
        case Activity::Downed:
            return "downed";
    }
    return "?";
}

bool ScheduleBlock::covers(std::int32_t secondOfDay) const noexcept {
    if (fromSecond <= toSecond) {
        return secondOfDay >= fromSecond && secondOfDay < toSecond;
    }
    // Wraps midnight: 22:00 to 03:00 is a night shift, not an empty set.
    return secondOfDay >= fromSecond || secondOfDay < toSecond;
}

const ScheduleBlock* Schedule::at(std::int32_t secondOfDay) const noexcept {
    for (const ScheduleBlock& block : blocks_) {
        if (block.covers(secondOfDay)) {
            return &block;
        }
    }
    return nullptr;
}

Actor::Actor(std::int32_t id, std::string name, std::string epithet, ActorRole role,
             std::int32_t tileX, std::int32_t tileY, std::int32_t band)
    : id_(id),
      name_(std::move(name)),
      epithet_(std::move(epithet)),
      role_(role),
      x_(q8_tile_centre(tileX)),
      y_(q8_tile_centre(tileY)),
      band_(band),
      destX_(tileX),
      destY_(tileY),
      destBand_(band),
      nextX_(tileX),
      nextY_(tileY),
      nextBand_(band) {
    if (role_ == ActorRole::Bouncer) {
        hp_ = kBouncerHealth;
        hpMax_ = kBouncerHealth;
        // A cudgel, and it does NOT make the fight lethal -- see brawl.hpp.
        // Bouncers are hired precisely because blunt force ends a fight without
        // ending a man.
        weapon_ = Weapon::Blunt;
    }
}

void Actor::setHealth(std::int32_t hp, std::int32_t hpMax) noexcept {
    hpMax_ = std::max(1, hpMax);
    hp_ = std::clamp(hp, 0, hpMax_);
}

Fighter Actor::asFighter() const noexcept {
    Fighter fighter;
    fighter.actorId = id_;
    fighter.weapon = weapon_;
    fighter.intent = intent_;
    fighter.hp = hp_;
    fighter.hpMax = hpMax_;
    return fighter;
}

bool Actor::takeCoin(std::int32_t amount) noexcept {
    if (amount <= 0 || coin_ < amount) {
        return false;
    }
    coin_ -= amount;
    return true;
}

void Actor::giveCoin(std::int32_t amount) noexcept {
    if (amount > 0) {
        coin_ = wrap_add(coin_, amount);
    }
}

std::int32_t Actor::distanceTo(std::int32_t xQ8, std::int32_t yQ8) const noexcept {
    return std::max(wrap_abs(wrap_sub(x_, xQ8)), wrap_abs(wrap_sub(y_, yQ8)));
}

void Actor::setDestination(std::int32_t tileX, std::int32_t tileY, std::int32_t band) noexcept {
    destX_ = tileX;
    destY_ = tileY;
    destBand_ = band;
}

bool Actor::atDestination() const noexcept {
    return tileX() == destX_ && tileY() == destY_ && band_ == destBand_ &&
           distanceTo(q8_tile_centre(destX_), q8_tile_centre(destY_)) <= kAtPostRadius;
}

void Actor::placeAt(std::int32_t tileX, std::int32_t tileY, std::int32_t band) noexcept {
    x_ = q8_tile_centre(tileX);
    y_ = q8_tile_centre(tileY);
    band_ = band;
    nextX_ = tileX;
    nextY_ = tileY;
    nextBand_ = band;
    destX_ = tileX;
    destY_ = tileY;
    destBand_ = band;
}

void Actor::faceToward(std::int32_t xQ8, std::int32_t yQ8) noexcept {
    const std::int32_t dx = wrap_sub(xQ8, x_);
    const std::int32_t dy = wrap_sub(yQ8, y_);
    if (dx == 0 && dy == 0) {
        return;
    }
    // Eight-point facing, resolved by comparing magnitudes rather than by an
    // arctangent: this is simulation state and there is no integer atan2 here.
    // Eight points is what a billboarded sprite can show anyway.
    const std::int32_t ax = wrap_abs(dx);
    const std::int32_t ay = wrap_abs(dy);
    Angle facing = 0;
    if (ax > ay * 2) {
        facing = dx > 0 ? kFacingEast : kFacingWest;
    } else if (ay > ax * 2) {
        facing = dy > 0 ? kFacingSouth : kFacingNorth;
    } else if (dx > 0) {
        facing = dy > 0 ? (kFacingEast + kTurnFull / 8) : (kFacingNorth + kTurnFull / 8);
    } else {
        facing = dy > 0 ? (kFacingSouth + kTurnFull / 8) : (kFacingWest + kTurnFull / 8);
    }
    setFacing(facing);
}

void Actor::step(RegionPath& path, std::int32_t speed) noexcept {
    const std::int32_t targetX = q8_tile_centre(nextX_);
    const std::int32_t targetY = q8_tile_centre(nextY_);

    // Already standing on the tile being walked to: ask for the next one.
    if (x_ == targetX && y_ == targetY) {
        band_ = nextBand_;
        if (tileX() == destX_ && tileY() == destY_ && band_ == destBand_) {
            return;
        }
        const PathStep next =
            path.firstStepToward(PathStep{tileX(), tileY(), band_},
                                 PathStep{destX_, destY_, destBand_});
        if (next.x == tileX() && next.y == tileY() && next.band == band_) {
            // Nowhere to go: the destination is unreachable inside the box.
            // Standing still is the honest answer; the caller decides whether
            // that means the schedule is wrong.
            return;
        }
        nextX_ = next.x;
        nextY_ = next.y;
        nextBand_ = next.band;
        faceToward(q8_tile_centre(nextX_), q8_tile_centre(nextY_));
        return;
    }

    // Close the gap on each axis at up to `speed`, snapping when inside it.
    const auto approach = [speed](std::int32_t from, std::int32_t to) {
        const std::int32_t gap = wrap_sub(to, from);
        if (wrap_abs(gap) <= speed) {
            return to;
        }
        return wrap_add(from, gap > 0 ? speed : -speed);
    };
    x_ = approach(x_, targetX);
    y_ = approach(y_, targetY);
}

// VERIFICATION GAP (S2): THERE IS NO ACTOR-ACTOR COLLISION. Neither step() nor
// push() knows another actor exists. Two actors routed to adjacent posts will
// walk through each other, and the player can stand inside a bouncer. Only the
// tile grid pushes back. Nothing tests this because there is nothing to test:
// the rule is absent, not wrong.
void Actor::push(const TileQuery& tiles, std::int32_t dxQ8, std::int32_t dyQ8) noexcept {
    // One axis at a time, same as the player's body, so being shoved along a
    // wall slides instead of sticking. No substepping: a shove is bounded by
    // kShoveImpulse, which is well under a tile.
    const auto tryAxis = [this, &tiles](std::int32_t dx, std::int32_t dy) {
        if (dx == 0 && dy == 0) {
            return;
        }
        const std::int32_t nx = wrap_add(x_, dx);
        const std::int32_t ny = wrap_add(y_, dy);
        const std::int32_t ntx = q8_tile(nx);
        const std::int32_t nty = q8_tile(ny);
        std::int32_t nextBand = band_;
        if (ntx != tileX() || nty != tileY()) {
            nextBand = tiles.stepBand(tileX(), tileY(), band_, ntx, nty);
            if (nextBand == TileQuery::kNoBand) {
                return;
            }
        }
        x_ = nx;
        y_ = ny;
        band_ = nextBand;
    };
    tryAxis(dxQ8, 0);
    tryAxis(0, dyQ8);
    // A shoved actor is no longer walking to the tile it had picked; re-anchor
    // on where it now stands, or the next step teleports it back.
    nextX_ = tileX();
    nextY_ = tileY();
    nextBand_ = band_;
}

void Actor::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(id_));
    sink.put_byte(static_cast<std::uint32_t>(role_));
    sink.put_int(static_cast<std::uint32_t>(x_));
    sink.put_int(static_cast<std::uint32_t>(y_));
    sink.put_int(static_cast<std::uint32_t>(band_));
    sink.put_int(static_cast<std::uint32_t>(facing_));
    sink.put_int(static_cast<std::uint32_t>(destX_));
    sink.put_int(static_cast<std::uint32_t>(destY_));
    sink.put_int(static_cast<std::uint32_t>(destBand_));
    sink.put_int(static_cast<std::uint32_t>(nextX_));
    sink.put_int(static_cast<std::uint32_t>(nextY_));
    sink.put_int(static_cast<std::uint32_t>(nextBand_));
    sink.put_byte(static_cast<std::uint32_t>(activity_));
    sink.put_int(static_cast<std::uint32_t>(hp_));
    sink.put_int(static_cast<std::uint32_t>(hpMax_));
    sink.put_byte(static_cast<std::uint32_t>(weapon_));
    sink.put_byte(static_cast<std::uint32_t>(intent_));
    sink.put_int(static_cast<std::uint32_t>(coin_));
}

}  // namespace granadad::sim
