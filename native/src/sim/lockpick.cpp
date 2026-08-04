#include "granadad/sim/lockpick.hpp"

#include <algorithm>

#include "granadad/sim/rng.hpp"

namespace granadad::sim {

namespace {

/// The salt every lock in the ward draws its pins through. A NAME, per the
/// rule in rng.hpp: renaming it re-rolls every lock in the game, so it never
/// gets renamed.
const std::uint64_t kLockSalt = system_salt("granadad.sim.lockpick");

constexpr std::int32_t clampTo(std::int32_t value, std::int32_t low,
                               std::int32_t high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

}  // namespace

std::string_view feelName(Feel feel) noexcept {
    switch (feel) {
        case Feel::Idle:
            return "idle";
        case Feel::Set:
            return "set";
        case Feel::TooShallow:
            return "too shallow";
        case Feel::TooDeep:
            return "too deep";
        case Feel::NoFeel:
            return "no feel";
        case Feel::Broke:
            return "broke";
        case Feel::Jammed:
            return "jammed";
        case Feel::Open:
            return "open";
        case Feel::Forced:
            return "forced";
        case Feel::Nothing:
            return "nothing";
    }
    return "idle";
}

std::int32_t pinDepth(std::uint64_t worldSeed, const Lock& lock, std::int32_t pin) noexcept {
    // PURE. The same four-step chain rng.hpp fixes, with the lock's id as the
    // spatial key and the pin index as the draw index. No tick, because a lock
    // does not change between one second and the next -- which is the whole
    // reason a player may walk away from a half-picked box and come back to the
    // same box.
    std::uint64_t h = mix64(worldSeed);
    h = mix64(h ^ kLockSalt);
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(lock.id)));
    const std::uint64_t draw = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(pin)));
    // Wards narrow the range a pin can hide in from the BOTTOM: a hard lock
    // never has a shallow pin, so an apprentice's habit of trying zero first
    // stops working on the captain's box.
    const std::int32_t floorDepth = clampTo(lock.wards, 0, kPinDepths - 2);
    const std::int32_t span = kPinDepths - floorDepth;
    return floorDepth + static_cast<std::int32_t>(draw % static_cast<std::uint64_t>(span));
}

std::int32_t pickTolerance(std::int32_t craftLevel) noexcept {
    return clampTo(std::max(0, craftLevel) / kTolerancePerCraftLevels, 0, kToleranceCap);
}

std::int32_t pickStrain(std::int32_t craftLevel, std::int32_t wards) noexcept {
    const std::int32_t fromCraft = std::max(0, craftLevel) / kStrainPerCraftLevels;
    // WARDS HALVED. A full point of strain per ward put the captain's box at a
    // limit of one -- a single wrong probe snapping the wire -- which is not a
    // hard lock, it is a lock nobody untrained can ever touch. Halved, the good
    // rooms cost an apprentice one wrong probe of slack and no more.
    const std::int32_t raw = kStrainPerPick + fromCraft - clampTo(wards, 0, 3) / 2;
    return clampTo(raw, 1, kStrainCeiling);
}

bool hasFeel(std::int32_t craftLevel) noexcept {
    return craftLevel >= kFeelLevel;
}

void Lockpicking::begin(const Lock& lock, std::uint64_t worldSeed,
                        std::int32_t craftLevel) noexcept {
    lock_ = lock;
    lock_.pins = clampTo(lock.pins, 1, kMaxPins);
    depths_.fill(0);
    for (std::int32_t i = 0; i < lock_.pins; ++i) {
        depths_[static_cast<std::size_t>(i)] = pinDepth(worldSeed, lock_, i);
    }
    open_ = true;
    opened_ = false;
    jammed_ = false;
    pin_ = 0;
    depth_ = 0;
    strain_ = 0;
    strainLimit_ = pickStrain(craftLevel, lock_.wards);
    tolerance_ = pickTolerance(craftLevel);
    probes_ = 0;
    feel_ = hasFeel(craftLevel);
    last_ = Feel::Idle;
}

void Lockpicking::moveDepth(std::int32_t delta) noexcept {
    if (!open_) {
        return;
    }
    depth_ = clampTo(depth_ + delta, 0, kPinDepths - 1);
}

Feel Lockpicking::probe(std::int32_t& picksLeft) noexcept {
    if (!open_) {
        last_ = Feel::Nothing;
        return last_;
    }
    ++probes_;
    const std::int32_t want = depths_[static_cast<std::size_t>(pin_)];
    const std::int32_t off = depth_ - want;
    const std::int32_t magnitude = off < 0 ? -off : off;
    if (magnitude <= tolerance_) {
        ++pin_;
        // S10: THE WIRE GETS ITS SLACK BACK WHEN A PIN DROPS. Strain used to
        // run for the whole attempt, so the last pin of a lock was always
        // worked on a wire the earlier pins had already half-spent -- which is
        // the arithmetic that made every shipped burglary end in the boot. A
        // pin is now its own budget. The snap still costs you every pin you
        // set, so this buys patience, not safety.
        strain_ = 0;
        if (pin_ >= lock_.pins) {
            open_ = false;
            opened_ = true;
            last_ = Feel::Open;
            return last_;
        }
        // The next pin starts where the wire already is. A cracksman does not
        // drop the pick back to the bottom between pins, and a player should
        // not have to either.
        last_ = Feel::Set;
        return last_;
    }

    ++strain_;
    if (strain_ >= strainLimit_) {
        // THE WIRE GOES. Everything set drops back: a snapped pick is not a
        // checkpoint, and this is the whole reason a lock is a risk rather than
        // a delay.
        strain_ = 0;
        pin_ = 0;
        if (picksLeft > 0) {
            --picksLeft;
        }
        if (picksLeft <= 0) {
            open_ = false;
            jammed_ = true;
            last_ = Feel::Jammed;
            return last_;
        }
        last_ = Feel::Broke;
        return last_;
    }
    if (!feel_) {
        last_ = Feel::NoFeel;
        return last_;
    }
    last_ = off < 0 ? Feel::TooShallow : Feel::TooDeep;
    return last_;
}

void Lockpicking::abandon() noexcept {
    open_ = false;
    pin_ = 0;
    strain_ = 0;
    last_ = Feel::Idle;
}

void Lockpicking::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(lock_.id));
    sink.put_int(static_cast<std::uint32_t>(lock_.pins));
    sink.put_int(static_cast<std::uint32_t>(lock_.wards));
    sink.put_byte(open_ ? 1U : 0U);
    sink.put_byte(opened_ ? 1U : 0U);
    sink.put_byte(jammed_ ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(pin_));
    sink.put_int(static_cast<std::uint32_t>(depth_));
    sink.put_int(static_cast<std::uint32_t>(strain_));
    sink.put_int(static_cast<std::uint32_t>(probes_));
    // S10, closing the S9 review's third minor finding verbatim: "hashInto
    // omits strainLimit_, tolerance_, feel_ ... it is a surface that will bite
    // when a save file resumes an attempt". They are derived from a hashed
    // skill today, so nothing diverges today; a resumed attempt is exactly the
    // case where they stop being derived, and the gate should already cover it.
    sink.put_int(static_cast<std::uint32_t>(strainLimit_));
    sink.put_int(static_cast<std::uint32_t>(tolerance_));
    sink.put_byte(feel_ ? 1U : 0U);
    sink.put_byte(static_cast<std::uint32_t>(last_));
}

}  // namespace granadad::sim
