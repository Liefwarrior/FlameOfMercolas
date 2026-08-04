#include "granadad/sim/crime.hpp"

#include <algorithm>

#include "granadad/sim/faction.hpp"
#include "granadad/sim/social.hpp"

namespace granadad::sim {

namespace {

constexpr std::uint8_t kCrimeMagic0 = 'G';
constexpr std::uint8_t kCrimeMagic1 = 'C';
/// 2 (S6): the heat clock is written as sixty-four bits.
///
/// S5 encoded `cooledAtTick_` -- an std::int64_t -- through putI32, so a ledger
/// round-tripped through a save came back with the low half of the tick it was
/// cooled at. The S5 review found it, and found that the round-trip case could
/// not see it either: the only tick it ever used was four cooling periods, well
/// inside thirty-two bits. Unreachable in practice is not the same as correct,
/// and a codec that silently narrows is the sort of thing that is discovered by
/// a save file rather than by a test.
constexpr std::uint8_t kCrimeVersion = 2;

void putI32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    out.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 24) & 0xFFU));
}

[[nodiscard]] bool takeI32(const std::vector<std::uint8_t>& bytes, std::size_t& cursor,
                           std::int32_t& out) {
    if (cursor + 4 > bytes.size()) {
        return false;
    }
    const std::uint32_t bits = static_cast<std::uint32_t>(bytes[cursor]) |
                               (static_cast<std::uint32_t>(bytes[cursor + 1]) << 8) |
                               (static_cast<std::uint32_t>(bytes[cursor + 2]) << 16) |
                               (static_cast<std::uint32_t>(bytes[cursor + 3]) << 24);
    cursor += 4;
    out = static_cast<std::int32_t>(bits);
    return true;
}

void putI64(std::vector<std::uint8_t>& out, std::int64_t value) {
    const std::uint64_t bits = static_cast<std::uint64_t>(value);
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xFFU));
    }
}

[[nodiscard]] bool takeI64(const std::vector<std::uint8_t>& bytes, std::size_t& cursor,
                           std::int64_t& out) {
    if (cursor + 8 > bytes.size()) {
        return false;
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) {
        bits |= static_cast<std::uint64_t>(bytes[cursor + static_cast<std::size_t>(i)])
                << (i * 8);
    }
    cursor += 8;
    out = static_cast<std::int64_t>(bits);
    return true;
}

[[nodiscard]] constexpr bool inRange(Crime crime) noexcept {
    return static_cast<std::size_t>(crime) < kCrimeCount;
}

}  // namespace

// ---------------------------------------------------------------------------
// the acts
// ---------------------------------------------------------------------------

std::string_view crimeName(Crime crime) noexcept {
    switch (crime) {
        case Crime::Lift:
            return "lift";
        case Crime::Burgle:
            return "burgle";
        case Crime::Smuggle:
            return "smuggle";
        case Crime::Fence:
            return "fence";
        case Crime::Extort:
            return "extort";
        case Crime::RoofRun:
            return "roof run";
    }
    return "?";
}

std::string_view crimeTally(Crime crime) noexcept {
    switch (crime) {
        case Crime::Lift:
            return "lifts";
        case Crime::Burgle:
            return "cracks";
        case Crime::Smuggle:
            return "runs";
        case Crime::Fence:
            return "fences";
        case Crime::Extort:
            return "leans";
        case Crime::RoofRun:
            return "roofs";
    }
    return "";
}

std::int32_t crimeHeat(Crime crime) noexcept {
    switch (crime) {
        // A purse is a complaint. A cracked box is a report, and a bale carried
        // past a watchman is the one thing on the list the state is actually
        // organised to care about.
        case Crime::Lift:
            return 8;
        case Crime::Burgle:
            return 18;
        case Crime::Smuggle:
            return 26;
        // Nobody is standing there to be robbed, so there is nobody to
        // complain. The Watch hears about a fence from informers, eventually.
        case Crime::Fence:
            return 4;
        case Crime::Extort:
            return 14;
        // Being on a roof is unseemly, not criminal. It is what the unseemly
        // are on their way to that the Watch minds.
        case Crime::RoofRun:
            return 1;
    }
    return 0;
}

std::int32_t crimeStanding(Crime crime) noexcept {
    switch (crime) {
        case Crime::Lift:
            return 3;
        case Crime::Burgle:
            return 6;
        case Crime::Smuggle:
            return 8;
        case Crime::Fence:
            return 2;
        case Crime::Extort:
            return 5;
        // The one act on the list that IS the guild's identity, and the only
        // one worth standing before you have taken anything from anybody.
        case Crime::RoofRun:
            return 2;
    }
    return 0;
}

std::string_view crimeSkill(Crime crime) noexcept {
    switch (crime) {
        case Crime::Lift:
        case Crime::Burgle:
            return kThieverySkill;
        case Crime::Smuggle:
        case Crime::Extort:
            return kHaggleSkill;
        // Fencing is a conversation, and the haggle already charges for one; a
        // roof-run is the body's own craft, and the body already charges for
        // that. Neither gets paid twice.
        case Crime::Fence:
        case Crime::RoofRun:
            return {};
    }
    return {};
}

// ---------------------------------------------------------------------------
// the ledger
// ---------------------------------------------------------------------------

std::int32_t CrimeLedger::tally(Crime crime) const noexcept {
    return inRange(crime) ? tallies_[static_cast<std::size_t>(crime)] : 0;
}

void CrimeLedger::commit(Crime crime, bool witnessed) {
    if (!inRange(crime)) {
        return;
    }
    ++tallies_[static_cast<std::size_t>(crime)];
    ++committed_;
    if (witnessed) {
        addHeat(crimeHeat(crime));
    }
}

void CrimeLedger::takeLoot(std::int32_t pieces) {
    if (pieces <= 0) {
        return;
    }
    loot_ += pieces;
}

std::int32_t CrimeLedger::sellLoot(std::int32_t pieces, std::int32_t ratePercent) {
    const std::int32_t sold = std::clamp(pieces, 0, loot_);
    if (sold == 0) {
        return 0;
    }
    const std::int32_t rate = std::clamp(ratePercent, kFenceRateFloor, kFenceRateCeiling);
    loot_ -= sold;
    return sold * kLootValue * rate / 100;
}

std::int32_t CrimeLedger::deliverBale() {
    if (!bale_) {
        return 0;
    }
    bale_ = false;
    ++balesRun_;
    return kBalePay;
}

void CrimeLedger::addHeat(std::int32_t delta) {
    heat_ = std::clamp(heat_ + delta, 0, kHeatMax);
    if (heat_ >= kWarrantAt) {
        warrant_ = true;
    } else if (heat_ < kWarrantLapsesAt) {
        // Paper lapses on its own well below where it was issued, so the state
        // of being wanted has HYSTERESIS: one cooled point does not flicker a
        // warrant on and off, which is what a single threshold would do.
        warrant_ = false;
    }
}

void CrimeLedger::cool(std::int64_t tick) {
    if (tick <= cooledAtTick_) {
        // Time went backwards or stood still. Nothing to forget.
        cooledAtTick_ = tick;
        return;
    }
    const std::int64_t elapsed = tick - cooledAtTick_;
    const std::int64_t points = elapsed / kHeatCoolSeconds;
    if (points <= 0) {
        return;
    }
    // Charge only for the whole periods used, so the remainder is still owed
    // and a clock read once a second cools at exactly the same rate as one that
    // jumped a night in a rented bed.
    cooledAtTick_ += points * kHeatCoolSeconds;
    addHeat(static_cast<std::int32_t>(-std::min<std::int64_t>(points, kHeatMax)));
}

void CrimeLedger::quashWarrant() noexcept {
    warrant_ = false;
}

void CrimeLedger::lieLow() noexcept {
    heat_ = 0;
    warrant_ = false;
    ++laidLow_;
}

// ---------------------------------------------------------------------------
// persistence
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> CrimeLedger::encode() const {
    std::vector<std::uint8_t> out;
    out.push_back(kCrimeMagic0);
    out.push_back(kCrimeMagic1);
    out.push_back(kCrimeVersion);
    out.push_back(static_cast<std::uint8_t>(kCrimeCount));
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        putI32(out, tallies_[i]);
    }
    putI32(out, committed_);
    putI32(out, loot_);
    putI32(out, heat_);
    putI32(out, laidLow_);
    putI32(out, balesRun_);
    putI64(out, cooledAtTick_);
    out.push_back(bale_ ? 1U : 0U);
    out.push_back(warrant_ ? 1U : 0U);
    return out;
}

bool CrimeLedger::decode(const std::vector<std::uint8_t>& bytes, CrimeLedger& out) {
    if (bytes.size() < 4 || bytes[0] != kCrimeMagic0 || bytes[1] != kCrimeMagic1 ||
        bytes[2] != kCrimeVersion || bytes[3] != static_cast<std::uint8_t>(kCrimeCount)) {
        return false;
    }
    CrimeLedger parsed;
    std::size_t cursor = 4;
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        if (!takeI32(bytes, cursor, parsed.tallies_[i])) {
            return false;
        }
    }
    if (!takeI32(bytes, cursor, parsed.committed_) || !takeI32(bytes, cursor, parsed.loot_) ||
        !takeI32(bytes, cursor, parsed.heat_) || !takeI32(bytes, cursor, parsed.laidLow_) ||
        !takeI32(bytes, cursor, parsed.balesRun_) ||
        !takeI64(bytes, cursor, parsed.cooledAtTick_)) {
        return false;
    }
    if (cursor + 2 > bytes.size()) {
        return false;
    }
    parsed.bale_ = bytes[cursor] != 0;
    parsed.warrant_ = bytes[cursor + 1] != 0;
    out = parsed;
    return true;
}

void CrimeLedger::hashInto(HashSink& sink) const {
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        sink.put_int(static_cast<std::uint32_t>(tallies_[i]));
    }
    sink.put_int(static_cast<std::uint32_t>(committed_));
    sink.put_int(static_cast<std::uint32_t>(loot_));
    sink.put_int(static_cast<std::uint32_t>(heat_));
    sink.put_int(static_cast<std::uint32_t>(laidLow_));
    sink.put_int(static_cast<std::uint32_t>(balesRun_));
    // Sixty-four bits, like the codec: a digest that folded the low half would
    // agree about two ledgers cooled 2^32 seconds apart.
    sink.put_long(static_cast<std::uint64_t>(cooledAtTick_));
    sink.put_byte(bale_ ? 1U : 0U);
    sink.put_byte(warrant_ ? 1U : 0U);
}

// ---------------------------------------------------------------------------
// what a fence pays
// ---------------------------------------------------------------------------

std::int32_t fenceRatePercent(std::int32_t rank, std::int32_t standing) noexcept {
    const std::int32_t byRank = std::max(0, rank) * 9;
    const std::int32_t byStanding = std::clamp(standing, kFactionStandingMin,
                                               kFactionStandingMax) /
                                    8;
    return std::clamp(kFenceBaseRate + byRank + byStanding, kFenceRateFloor, kFenceRateCeiling);
}

}  // namespace granadad::sim
