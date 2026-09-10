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
/// 3 (S6): the sack has contents and the ward has a record of what it did back.
/// 4 (action-combat): a witnessed killing stands on the record -- one appended
/// byte, so a v3 blob is refused by version rather than silently misread.
constexpr std::uint8_t kCrimeVersion = 4;

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

void CrimeLedger::takeBale(Contraband good, std::int32_t units) noexcept {
    bale_ = true;
    baleGood_ = good;
    baleUnits_ = std::max(0, units);
}

std::int32_t CrimeLedger::deliverBale(bool ownBuyer) {
    if (!bale_) {
        return 0;
    }
    bale_ = false;
    ++balesRun_;
    if (ownBuyer) {
        // The boat's own buyer takes it off you at the door. What was in it is
        // theirs; the fee is yours.
        return kBalePay;
    }
    // Somebody else hired you. The sack comes off your shoulder into your own,
    // and whatever fitted is what you are now carrying -- which is also what a
    // watchman will find and what a contract will take.
    (void)stash_.add(baleGood_, baleUnits_);
    return 0;
}

void CrimeLedger::markMurderer() noexcept {
    // ACTION-COMBAT BUILD. A witnessed killing: the record stands, and the heat
    // jumps to exactly the warrant line in one act (kMurderHeat == kWarrantAt),
    // so addHeat also raises the paper. Idempotent on the flag -- a second
    // murder does not un-mark the first -- but the heat is charged each time,
    // the same as any witnessed crime.
    murderer_ = true;
    addHeat(kMurderHeat);
}

CrimeLedger::ArrestOutcome CrimeLedger::arrest(bool skyrunner, std::int32_t purse,
                                               std::uint64_t draw) {
    ArrestOutcome out;
    out.sentence = sentenceFor(skyrunner, warrant_, arrests_);
    // ACTION-COMBAT BUILD: a murderer is CONDEMNED whatever the theft ladder
    // said -- the rope is for the blade, not the purse. A witnessed murder
    // always left a warrant (kMurderHeat == kWarrantAt), so there is a cause to
    // close on; this only decides what the sentence IS once he is taken. What
    // condemnation then means beyond the status bit -- the court, jail, the
    // rope as a true game over -- is the justice build, not this one.
    if (murderer_) {
        out.sentence = Sentence::Condemned;
    }
    // The impound first: Watchman Cull's whole job is seized cargo, and it is
    // the one part of an arrest that happens whether or not there was paper.
    out.unitsSeized = stash_.seizeIllicit();
    out.fine = std::min(std::max(0, purse), fineFor(heat_, out.unitsSeized));
    // A bale on your shoulder goes with the rest of it.
    bale_ = false;
    baleUnits_ = 0;

    switch (out.sentence) {
        case Sentence::Fined:
            // No paper, so no cell and no record of an ARREST -- he stopped
            // you, he took the jars, he charged you for his evening. The heat
            // is untouched: being searched is not being punished for anything
            // the ward had already heard about.
            break;
        case Sentence::Held:
        case Sentence::Maimed:
        case Sentence::Condemned:
            ++arrests_;
            out.heldHours = heldHours(draw);
            // Served. The paper goes and the ward keeps a little of its memory
            // -- see kHeatAfterSentence on why this is not zero.
            heat_ = kHeatAfterSentence;
            warrant_ = false;
            if (out.sentence == Sentence::Maimed) {
                maimed_ = true;
            } else if (out.sentence == Sentence::Condemned) {
                condemned_ = true;
                // The rope does not un-take the hand.
                maimed_ = true;
            }
            break;
        case Sentence::None:
            break;
    }
    lastSentence_ = out.sentence;
    return out;
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
    // S6, appended: what is in the sack, what is on the shoulder, and what the
    // ward has done about it. Appended and never inserted, which is the same
    // rule the draw schedule follows and for the same reason.
    const std::vector<std::uint8_t> sack = stash_.encode();
    out.insert(out.end(), sack.begin(), sack.end());
    out.push_back(static_cast<std::uint8_t>(baleGood_));
    putI32(out, baleUnits_);
    putI32(out, arrests_);
    out.push_back(static_cast<std::uint8_t>(lastSentence_));
    out.push_back(maimed_ ? 1U : 0U);
    out.push_back(condemned_ ? 1U : 0U);
    // v4, appended: the murder record.
    out.push_back(murderer_ ? 1U : 0U);
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
    cursor += 2;

    // The sack. Its own codec owns its own header, so a stash that grows a
    // sixth good refuses this blob by its own count rather than by ours.
    const std::vector<std::uint8_t> sack(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                         bytes.end());
    if (!Stash::decode(sack, parsed.stash_)) {
        return false;
    }
    cursor += 4 + 4 * kContrabandCount;

    if (cursor >= bytes.size() || bytes[cursor] >= static_cast<std::uint8_t>(kContrabandCount)) {
        return false;
    }
    parsed.baleGood_ = static_cast<Contraband>(bytes[cursor]);
    ++cursor;
    if (!takeI32(bytes, cursor, parsed.baleUnits_) ||
        !takeI32(bytes, cursor, parsed.arrests_)) {
        return false;
    }
    if (cursor + 4 > bytes.size() || bytes[cursor] > static_cast<std::uint8_t>(Sentence::Condemned)) {
        return false;
    }
    parsed.lastSentence_ = static_cast<Sentence>(bytes[cursor]);
    parsed.maimed_ = bytes[cursor + 1] != 0;
    parsed.condemned_ = bytes[cursor + 2] != 0;
    parsed.murderer_ = bytes[cursor + 3] != 0;
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
    // S6, appended in the same order the codec writes them.
    stash_.hashInto(sink);
    sink.put_byte(static_cast<std::uint32_t>(baleGood_));
    sink.put_int(static_cast<std::uint32_t>(baleUnits_));
    sink.put_int(static_cast<std::uint32_t>(arrests_));
    sink.put_byte(static_cast<std::uint32_t>(lastSentence_));
    sink.put_byte(maimed_ ? 1U : 0U);
    sink.put_byte(condemned_ ? 1U : 0U);
    sink.put_byte(murderer_ ? 1U : 0U);
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
