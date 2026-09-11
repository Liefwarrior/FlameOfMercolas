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
/// 5 (justice): the court's record -- mercy given, the rope, the last plea and
/// judgment, hearings and days served, who saw the killing, the tallies the
/// last sentence covered, and the hearing itself between the arrest and the
/// sentence, so a run saved at the bench reopens at the bench. All appended.
constexpr std::uint8_t kCrimeVersion = 5;

/// The officer's name on a hearing is written length-prefixed in one byte.
/// A roster name is a dozen characters; this is a guard, not a budget.
constexpr std::size_t kOfficerNameMax = 255;

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

void putByte(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

[[nodiscard]] bool takeByte(const std::vector<std::uint8_t>& bytes, std::size_t& cursor,
                            std::uint8_t& out) {
    if (cursor >= bytes.size()) {
        return false;
    }
    out = bytes[cursor];
    ++cursor;
    return true;
}

/// A byte that must be at most `ceiling` -- an enum ordinal, a flag.
[[nodiscard]] bool takeByteAtMost(const std::vector<std::uint8_t>& bytes, std::size_t& cursor,
                                  std::uint8_t ceiling, std::uint8_t& out) {
    return takeByte(bytes, cursor, out) && out <= ceiling;
}

/// The ladder's own answer, served the short way: what combat/build's
/// one-call arrest wrote on the record, said as the court's type. The
/// Condemned rung has NO judgment of its own here: combat's inert status
/// (condemned_ and the hand, nothing else) is neither COMMUTED (which also
/// spends mercy and serves the blood) nor THE ROPE, so arrest() writes those
/// two bits itself beside HELD's record rather than borrowing a judgment
/// that would write more.
[[nodiscard]] Judgment ladderJudgment(Sentence tier) noexcept {
    switch (tier) {
        case Sentence::Held:
        case Sentence::Condemned:
            return Judgment::Held;
        case Sentence::Maimed:
            return Judgment::TheHand;
        case Sentence::Fined:
        case Sentence::None:
            break;
    }
    return Judgment::None;
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

void CrimeLedger::markMurderer(std::int32_t witnesses) noexcept {
    // ACTION-COMBAT BUILD. A witnessed killing: the record stands, and the heat
    // jumps to exactly the warrant line in one act (kMurderHeat == kWarrantAt),
    // so addHeat also raises the paper. Idempotent on the flag -- a second
    // murder does not un-mark the first -- but the heat is charged each time,
    // the same as any witnessed crime.
    //
    // JUSTICE BUILD: and who saw it is kept, because the rope tier weighs it
    // (N SAW IT). A witnessed killing had at least one witness by definition;
    // the most recent one is what the sheet names.
    murderer_ = true;
    slewWitnesses_ = std::max(1, witnesses);
    addHeat(kMurderHeat);
}

// ---------------------------------------------------------------------------
// the arrest, split: the charge, the impound, the hearing, the sentence
// ---------------------------------------------------------------------------

ChargeSheet CrimeLedger::charge(bool skyrunner, std::int32_t streetwise,
                                std::int32_t templeStanding,
                                std::int32_t reputation) const {
    ChargeSheet sheet;
    sheet.written = true;
    // WHAT THE PAPER ASKS FOR is the shipped ladder, unchanged: Eli's
    // 2026-07-14 sentence survives verbatim as the Watch's petition. The
    // murder override with it -- the rope is for the blade, not the purse,
    // and a murderer is tried for the rope whatever he was stopped for,
    // because the corpse is on the roster whether or not the paper is.
    sheet.tier = sentenceFor(skyrunner, warrant_, arrests_);
    if (murderer_) {
        sheet.tier = Sentence::Condemned;
    }
    sheet.blood = murderer_;
    sheet.skyrunner = skyrunner;
    // The ladder's own rope needs paper and a prior. It weighs beside the
    // blood, not instead of it.
    sheet.secondRung = skyrunner && warrant_ && arrests_ > 0;
    // The one line the sheet names: the highest-heat act with a count since
    // the bench last heard you. No ties -- the six heats are distinct.
    std::int32_t worstHeat = 0;
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        const std::int32_t since = std::max(0, tallies_[i] - servedTallies_[i]);
        sheet.since[i] = since;
        const Crime act = static_cast<Crime>(i);
        if (since > 0 && crimeHeat(act) > worstHeat) {
            worstHeat = crimeHeat(act);
            sheet.hasWorst = true;
            sheet.worst = act;
        }
    }
    sheet.heatAtArrest = heat_;
    sheet.witnesses = slewWitnesses_;
    sheet.priors = arrests_;
    sheet.condemnedBefore = condemned_;
    sheet.commutedBefore = commuted_;
    sheet.streetwise = streetwise;
    sheet.templeStanding = templeStanding;
    sheet.reputation = reputation;
    return sheet;
}

std::int32_t CrimeLedger::seizeAtArrest() {
    // The impound: Watchman Cull's whole job is seized cargo, and it is the
    // one part of an arrest that happens whether or not there was paper.
    const std::int32_t units = stash_.seizeIllicit();
    // A bale on your shoulder goes with the rest of it.
    bale_ = false;
    baleUnits_ = 0;
    return units;
}

void CrimeLedger::openHearing(const ChargeSheet& sheet, std::int32_t unitsSeized,
                              std::uint64_t draw, std::string_view officer) {
    if (!sheet.written || sheet.tier == Sentence::None || sheet.tier == Sentence::Fined) {
        // No paper, so no bench. The door's fine is the whole of a search.
        return;
    }
    hearing_ = HearingState{};
    hearing_.stage = HearingStage::Arraigned;
    hearing_.sheet = sheet;
    hearing_.sheet.unitsSeized = std::max(0, unitsSeized);
    hearing_.sheet.draw = draw;
    hearing_.officer = std::string(officer.substr(0, kOfficerNameMax));
    // Sentence stays the Watch's ASK and lastSentence_ keeps recording it,
    // exactly as the short-way arrest does: what the paper asked for is on
    // the record from the moment it is laid, whatever the bench answers.
    lastSentence_ = sheet.tier;
}

Arraignment CrimeLedger::plead(Plea plea) {
    if (!hearing_.awaitingPlea()) {
        return Arraignment{};
    }
    const Arraignment answer = weighArraignment(hearing_.sheet, plea);
    if (!answer.heard) {
        return answer;
    }
    hearing_.stage = HearingStage::Judged;
    hearing_.plea = answer.plea;
    hearing_.judgment = answer.judgment;
    hearing_.band = answer.band;
    hearing_.weight = answer.weight;
    hearing_.scored = answer.scored;
    hearing_.doubled = answer.doubled;
    lastPlea_ = answer.plea;
    lastJudgment_ = answer.judgment;
    ++hearings_;
    return answer;
}

void CrimeLedger::sentence(Judgment judgment, std::int32_t daysServed) {
    if (judgment == Judgment::None) {
        return;
    }
    lastJudgment_ = judgment;
    // A conviction is a prior. SPARED is the one answer that is not one:
    // "hearings_++; no prior."
    if (judgment != Judgment::Spared) {
        ++arrests_;
    }
    switch (judgment) {
        case Judgment::TheHand:
            maimed_ = true;
            break;
        case Judgment::Commuted:
            // The rope does not un-take the hand. The ward is told the face
            // for the rest of the run, mercy is spent, and the blood is
            // SERVED: the next arrest on any paper is an ordinary hearing
            // with THE ROPE ONCE weighed against him, not a rope hearing.
            maimed_ = true;
            condemned_ = true;
            commuted_ = true;
            murderer_ = false;
            break;
        case Judgment::TheRope:
            executed_ = true;
            break;
        case Judgment::None:
        case Judgment::Spared:
        case Judgment::Fined:
        case Judgment::Held:
        case Judgment::Bound:
            break;
    }
    // Served. The paper goes and the ward keeps a little of its memory --
    // see kHeatAfterSentence on why this is not zero. Written AFTER the
    // room's clock jump by the room's own ordering, so the skip's cooling
    // does not take it back to nothing.
    heat_ = kHeatAfterSentence;
    warrant_ = false;
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        servedTallies_[i] = tallies_[i];
    }
    daysServed_ += std::max(0, daysServed);
    hearing_ = HearingState{};
}

std::int32_t CrimeLedger::servedTally(Crime crime) const noexcept {
    return inRange(crime) ? servedTallies_[static_cast<std::size_t>(crime)] : 0;
}

CrimeLedger::ArrestOutcome CrimeLedger::arrest(bool skyrunner, std::int32_t purse,
                                               std::uint64_t draw) {
    // THE SHORT WAY: charge, seize, and serve the ladder's own answer at once.
    // Combat/build's one call, kept bit-for-bit in what it writes on the
    // record's shipped fields (a prior, the heat after a sentence, the paper
    // torn up, the hand, the condemned status) so the ladder's tests and the
    // murder hook read as they did: the Condemned rung sets condemned_ and
    // maimed_ and NOTHING ELSE -- not commuted_ (mercy is the bench's to
    // spend) and not murderer_ (only the bench serves the blood). What the
    // court added to the record (lastJudgment_, servedTallies_) is written as
    // HELD's; combat had neither field. The room's arrest with paper does not
    // come through here any more: it opens a hearing and the sentence waits
    // on the plea.
    ArrestOutcome out;
    const ChargeSheet sheet = charge(skyrunner, 0, 0, 0);
    out.sentence = sheet.tier;
    out.unitsSeized = seizeAtArrest();
    out.fine = std::min(std::max(0, purse), fineFor(heat_, out.unitsSeized));

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
            out.heldHours = heldHours(draw);
            sentence(ladderJudgment(out.sentence), out.heldHours / 24);
            if (out.sentence == Sentence::Condemned) {
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
    // v5, appended: the court's record, then the hearing itself. Appended and
    // never inserted, which is the same rule the draw schedule follows and for
    // the same reason.
    putByte(out, commuted_ ? 1U : 0U);
    putByte(out, executed_ ? 1U : 0U);
    putByte(out, static_cast<std::uint8_t>(lastPlea_));
    putByte(out, static_cast<std::uint8_t>(lastJudgment_));
    putI32(out, hearings_);
    putI32(out, daysServed_);
    putI32(out, slewWitnesses_);
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        putI32(out, servedTallies_[i]);
    }
    const HearingState& h = hearing_;
    putByte(out, static_cast<std::uint8_t>(h.stage));
    putByte(out, h.sheet.written ? 1U : 0U);
    putByte(out, static_cast<std::uint8_t>(h.sheet.tier));
    putByte(out, h.sheet.blood ? 1U : 0U);
    putByte(out, h.sheet.hasWorst ? 1U : 0U);
    putByte(out, static_cast<std::uint8_t>(h.sheet.worst));
    for (std::size_t i = 0; i < kSheetCrimes; ++i) {
        putI32(out, h.sheet.since[i]);
    }
    putI32(out, h.sheet.heatAtArrest);
    putI32(out, h.sheet.unitsSeized);
    putI32(out, h.sheet.witnesses);
    putI32(out, h.sheet.priors);
    putByte(out, h.sheet.skyrunner ? 1U : 0U);
    putByte(out, h.sheet.secondRung ? 1U : 0U);
    putByte(out, h.sheet.condemnedBefore ? 1U : 0U);
    putByte(out, h.sheet.commutedBefore ? 1U : 0U);
    putI32(out, h.sheet.streetwise);
    putI32(out, h.sheet.templeStanding);
    putI32(out, h.sheet.reputation);
    putI64(out, static_cast<std::int64_t>(h.sheet.draw));
    putByte(out, static_cast<std::uint8_t>(h.plea));
    putByte(out, static_cast<std::uint8_t>(h.judgment));
    putByte(out, static_cast<std::uint8_t>(h.band));
    putI32(out, h.weight);
    putI32(out, h.scored);
    putByte(out, h.doubled ? 1U : 0U);
    const std::size_t nameLength = std::min(h.officer.size(), kOfficerNameMax);
    putByte(out, static_cast<std::uint8_t>(nameLength));
    for (std::size_t i = 0; i < nameLength; ++i) {
        putByte(out, static_cast<std::uint8_t>(static_cast<unsigned char>(h.officer[i])));
    }
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
    cursor += 4;

    // v5: the court's record. Every ordinal is guarded against its own
    // ceiling, so a blob from a build with an eighth judgment is refused
    // rather than read as something this one has a name for.
    std::uint8_t flag = 0;
    if (!takeByte(bytes, cursor, flag)) {
        return false;
    }
    parsed.commuted_ = flag != 0;
    if (!takeByte(bytes, cursor, flag)) {
        return false;
    }
    parsed.executed_ = flag != 0;
    if (!takeByteAtMost(bytes, cursor, static_cast<std::uint8_t>(Plea::NoPlea), flag)) {
        return false;
    }
    parsed.lastPlea_ = static_cast<Plea>(flag);
    if (!takeByteAtMost(bytes, cursor, static_cast<std::uint8_t>(Judgment::TheRope), flag)) {
        return false;
    }
    parsed.lastJudgment_ = static_cast<Judgment>(flag);
    if (!takeI32(bytes, cursor, parsed.hearings_) || !takeI32(bytes, cursor, parsed.daysServed_) ||
        !takeI32(bytes, cursor, parsed.slewWitnesses_)) {
        return false;
    }
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        if (!takeI32(bytes, cursor, parsed.servedTallies_[i])) {
            return false;
        }
    }
    HearingState& h = parsed.hearing_;
    if (!takeByteAtMost(bytes, cursor, static_cast<std::uint8_t>(HearingStage::Judged), flag)) {
        return false;
    }
    h.stage = static_cast<HearingStage>(flag);
    if (!takeByte(bytes, cursor, flag)) {
        return false;
    }
    h.sheet.written = flag != 0;
    if (!takeByteAtMost(bytes, cursor, static_cast<std::uint8_t>(Sentence::Condemned), flag)) {
        return false;
    }
    h.sheet.tier = static_cast<Sentence>(flag);
    if (!takeByte(bytes, cursor, flag)) {
        return false;
    }
    h.sheet.blood = flag != 0;
    if (!takeByte(bytes, cursor, flag)) {
        return false;
    }
    h.sheet.hasWorst = flag != 0;
    if (!takeByteAtMost(bytes, cursor, static_cast<std::uint8_t>(kCrimeCount - 1), flag)) {
        return false;
    }
    h.sheet.worst = static_cast<Crime>(flag);
    for (std::size_t i = 0; i < kSheetCrimes; ++i) {
        if (!takeI32(bytes, cursor, h.sheet.since[i])) {
            return false;
        }
    }
    if (!takeI32(bytes, cursor, h.sheet.heatAtArrest) ||
        !takeI32(bytes, cursor, h.sheet.unitsSeized) ||
        !takeI32(bytes, cursor, h.sheet.witnesses) || !takeI32(bytes, cursor, h.sheet.priors)) {
        return false;
    }
    if (!takeByte(bytes, cursor, flag)) {
        return false;
    }
    h.sheet.skyrunner = flag != 0;
    if (!takeByte(bytes, cursor, flag)) {
        return false;
    }
    h.sheet.secondRung = flag != 0;
    if (!takeByte(bytes, cursor, flag)) {
        return false;
    }
    h.sheet.condemnedBefore = flag != 0;
    if (!takeByte(bytes, cursor, flag)) {
        return false;
    }
    h.sheet.commutedBefore = flag != 0;
    std::int64_t draw = 0;
    if (!takeI32(bytes, cursor, h.sheet.streetwise) ||
        !takeI32(bytes, cursor, h.sheet.templeStanding) ||
        !takeI32(bytes, cursor, h.sheet.reputation) || !takeI64(bytes, cursor, draw)) {
        return false;
    }
    h.sheet.draw = static_cast<std::uint64_t>(draw);
    if (!takeByteAtMost(bytes, cursor, static_cast<std::uint8_t>(Plea::NoPlea), flag)) {
        return false;
    }
    h.plea = static_cast<Plea>(flag);
    if (!takeByteAtMost(bytes, cursor, static_cast<std::uint8_t>(Judgment::TheRope), flag)) {
        return false;
    }
    h.judgment = static_cast<Judgment>(flag);
    if (!takeByteAtMost(bytes, cursor, static_cast<std::uint8_t>(Judgment::TheRope), flag)) {
        return false;
    }
    h.band = static_cast<Judgment>(flag);
    if (!takeI32(bytes, cursor, h.weight) || !takeI32(bytes, cursor, h.scored)) {
        return false;
    }
    if (!takeByte(bytes, cursor, flag)) {
        return false;
    }
    h.doubled = flag != 0;
    std::uint8_t nameLength = 0;
    if (!takeByte(bytes, cursor, nameLength) || cursor + nameLength > bytes.size()) {
        return false;
    }
    h.officer.assign(reinterpret_cast<const char*>(bytes.data() + cursor), nameLength);
    cursor += nameLength;
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
    // JUSTICE BUILD, appended in the same order the codec writes them: the
    // court's record and the open hearing. Every byte of it decides what the
    // next arrest resolves to, so all of it is state the twin-run gate
    // compares. THE ONE DECLARED tavern/gate-workload baseline move of the
    // justice build (the ledger is hashed under the tavern through the
    // dialogue director); the population baseline never reaches this code.
    sink.put_byte(commuted_ ? 1U : 0U);
    sink.put_byte(executed_ ? 1U : 0U);
    sink.put_byte(static_cast<std::uint32_t>(lastPlea_));
    sink.put_byte(static_cast<std::uint32_t>(lastJudgment_));
    sink.put_int(static_cast<std::uint32_t>(hearings_));
    sink.put_int(static_cast<std::uint32_t>(daysServed_));
    sink.put_int(static_cast<std::uint32_t>(slewWitnesses_));
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        sink.put_int(static_cast<std::uint32_t>(servedTallies_[i]));
    }
    const HearingState& h = hearing_;
    sink.put_byte(static_cast<std::uint32_t>(h.stage));
    sink.put_byte(h.sheet.written ? 1U : 0U);
    sink.put_byte(static_cast<std::uint32_t>(h.sheet.tier));
    sink.put_byte(h.sheet.blood ? 1U : 0U);
    sink.put_byte(h.sheet.hasWorst ? 1U : 0U);
    sink.put_byte(static_cast<std::uint32_t>(h.sheet.worst));
    for (std::size_t i = 0; i < kSheetCrimes; ++i) {
        sink.put_int(static_cast<std::uint32_t>(h.sheet.since[i]));
    }
    sink.put_int(static_cast<std::uint32_t>(h.sheet.heatAtArrest));
    sink.put_int(static_cast<std::uint32_t>(h.sheet.unitsSeized));
    sink.put_int(static_cast<std::uint32_t>(h.sheet.witnesses));
    sink.put_int(static_cast<std::uint32_t>(h.sheet.priors));
    sink.put_byte(h.sheet.skyrunner ? 1U : 0U);
    sink.put_byte(h.sheet.secondRung ? 1U : 0U);
    sink.put_byte(h.sheet.condemnedBefore ? 1U : 0U);
    sink.put_byte(h.sheet.commutedBefore ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(h.sheet.streetwise));
    sink.put_int(static_cast<std::uint32_t>(h.sheet.templeStanding));
    sink.put_int(static_cast<std::uint32_t>(h.sheet.reputation));
    sink.put_long(h.sheet.draw);
    sink.put_byte(static_cast<std::uint32_t>(h.plea));
    sink.put_byte(static_cast<std::uint32_t>(h.judgment));
    sink.put_byte(static_cast<std::uint32_t>(h.band));
    sink.put_int(static_cast<std::uint32_t>(h.weight));
    sink.put_int(static_cast<std::uint32_t>(h.scored));
    sink.put_byte(h.doubled ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(h.officer.size()));
    for (const char character : h.officer) {
        sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(character)));
    }
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
