#include "granadad/sim/legend.hpp"

#include <algorithm>

#include "granadad/sim/casebook.hpp"
#include "granadad/sim/contract.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/social.hpp"

namespace granadad::sim {

namespace {

/// The titles. Four rungs a track, plus the rung-zero name at index 0.
///
/// EVERY ONE OF THEM IS THE WARD'S OWN VOCABULARY, not a fantasy-game ladder:
/// Tenant, Cutpurse, Robber and Skyrunner are content/raws/factions/ranks.json's
/// own four Skyrunner rungs; Disciple is the Temple's; Bondsworn, Laborer and
/// Foreman are the dockhands'; Stallkeep, Trader and Craftlord are the
/// merchants'. Where a fifth was needed the gazetteer supplied it rather than
/// this file inventing one.
constexpr const char* kTitles[kLegendTracks][kLegendRungs + 1] = {
    // Wire -- the hands.
    {"NOBODY", "LIGHT FINGERS", "CUTPURSE", "ROBBER", "THE QUIET TENANT"},
    // Roofs -- the road nobody looks up at.
    {"NOBODY", "TENANT", "ROOF-WALKER", "SKYRUNNER", "THE WARD'S OWN SHADOW"},
    // Flame -- the white garb the locals distrust.
    {"NOBODY", "DISCIPLE", "THE FLAME'S MAN", "WIELDER OF THE FLAME",
     "THE ONE WHO WENT DOWN THERE"},
    // Trade -- coin, and the paper behind it.
    {"NOBODY", "STALLKEEP", "TRADER", "CRAFTLORD", "THE WARD'S CREDITOR"},
    // Law -- the state's only ambient voice, and whether it likes you.
    {"NOBODY", "KNOWN TO THE WATCH", "SWORN IN", "THE SERGEANT'S MAN",
     "THE MAN VESS SENDS FOR"},
};

[[nodiscard]] std::int32_t rungFor(std::int32_t score) noexcept {
    std::int32_t rung = 0;
    for (std::int32_t i = 0; i < kLegendRungs; ++i) {
        if (score >= kLegendThresholds[i]) {
            rung = i + 1;
        }
    }
    return rung;
}

[[nodiscard]] LegendRow rowFor(LegendTrack track, std::int32_t score) noexcept {
    LegendRow row;
    row.track = track;
    row.score = std::max<std::int32_t>(0, score);
    row.rung = rungFor(row.score);
    row.title = legendTitle(track, row.rung);
    row.nextAt = row.rung >= kLegendRungs ? 0 : kLegendThresholds[row.rung];
    return row;
}

}  // namespace

std::string_view legendTrackName(LegendTrack track) noexcept {
    switch (track) {
        case LegendTrack::Wire:
            return "THE WIRE";
        case LegendTrack::Roofs:
            return "THE ROOFS";
        case LegendTrack::Flame:
            return "THE FLAME";
        case LegendTrack::Trade:
            return "THE TRADE";
        case LegendTrack::Law:
            return "THE LAW";
    }
    return "THE WIRE";
}

std::string_view legendTitle(LegendTrack track, std::int32_t rung) noexcept {
    const std::int32_t clamped = std::clamp<std::int32_t>(rung, 0, kLegendRungs);
    return kTitles[static_cast<std::size_t>(track)][static_cast<std::size_t>(clamped)];
}

Legend legendOf(const CrimeLedger& crimes, const SkillTrack& skills,
                const FactionLedger& standings, const ContractBoard& work,
                const Casebook& notes) {
    Legend out;

    // THE WIRE. What the hands have actually done, weighted by how much of a
    // crime each act is: a cracked strongbox is worth four lifted purses, and
    // fencing what you took is the half of the job most players forget.
    const std::int32_t wire = crimes.tally(Crime::Lift) * 2 +
                              crimes.tally(Crime::Burgle) * 8 +
                              crimes.tally(Crime::Fence) * 4 +
                              crimes.tally(Crime::Extort) * 3 +
                              skills.level(kThieverySkill);

    // THE ROOFS. Climbs and runs, and the skill they teach.
    const std::int32_t roofs =
        crimes.tally(Crime::RoofRun) * 3 + crimes.tally(Crime::Smuggle) * 4 +
        skills.level(kRoofSkill) * 2;

    // THE FLAME. THE TRAIL IS THE TRACK. Following a lead is worth six, a dead
    // end four -- because walking to the King's Bond to find the struck line
    // never came there is work the Flame's own man does, and a system that paid
    // nothing for it would be a system telling the player not to look. Closing
    // the case is worth a rung on its own.
    const std::int32_t temple = standings.registry() == nullptr
                                    ? -1
                                    : standings.registry()->indexOf("temple");
    const std::int32_t flame =
        (notes.readCount() - notes.coldCount()) * 6 + notes.coldCount() * 4 +
        (notes.closed() ? 30 : 0) +
        (temple < 0 ? 0 : std::max<std::int32_t>(0, standings.standing(temple)) / 2);

    // THE TRADE. Contracts finished and the coin they paid. Divided down
    // because coin is quoted in ones and rungs are quoted in tens.
    const std::int32_t trade =
        work.paidCount() * 6 + work.coinEarned() / 4 - work.failedCount() * 4 +
        skills.level(kHaggleSkill);

    // THE LAW. Standing with the Watch, minus what they have heard about you --
    // and the heat term is why a player cannot be top of this and top of THE
    // WIRE in the same week. That tension IS the sandbox.
    const std::int32_t watch =
        standings.registry() == nullptr ? -1 : standings.registry()->indexOf("watch");
    const std::int32_t law = (watch < 0 ? 0 : standings.standing(watch)) - crimes.heat() / 2 -
                             crimes.arrests() * 8 + work.paidCount() * 2;

    out.rows_[0] = rowFor(LegendTrack::Wire, wire);
    out.rows_[1] = rowFor(LegendTrack::Roofs, roofs);
    out.rows_[2] = rowFor(LegendTrack::Flame, flame);
    out.rows_[3] = rowFor(LegendTrack::Trade, trade);
    out.rows_[4] = rowFor(LegendTrack::Law, law);
    return out;
}

LegendTrack Legend::best() const noexcept {
    std::size_t bestIndex = 0;
    for (std::size_t i = 1; i < kLegendTracks; ++i) {
        // Strictly greater, so a tie keeps the EARLIER track and two runs
        // cannot disagree about what the ward calls you.
        if (rows_[i].rung > rows_[bestIndex].rung ||
            (rows_[i].rung == rows_[bestIndex].rung &&
             rows_[i].score > rows_[bestIndex].score)) {
            bestIndex = i;
        }
    }
    return rows_[bestIndex].track;
}

std::string_view Legend::title() const noexcept { return row(best()).title; }

std::int32_t Legend::totalRungs() const noexcept {
    std::int32_t total = 0;
    for (const LegendRow& one : rows_) {
        total += one.rung;
    }
    return total;
}

std::int32_t Legend::picksPerSetBonus() const noexcept { return row(LegendTrack::Wire).rung; }

std::int32_t Legend::lookRangeBonus() const noexcept {
    // Half a rung apiece, so the Flame's eye reaches two tiles further at the
    // top and never far enough to read the Mission's flagstones off the street.
    return row(LegendTrack::Flame).rung / 2;
}

std::int32_t Legend::pricePercent() const noexcept {
    // Five percent a rung, in the same sign convention guildPricePercent uses:
    // a NEGATIVE number is a discount.
    return -5 * row(LegendTrack::Trade).rung;
}

}  // namespace granadad::sim
