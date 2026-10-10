#include "granadad/sim/justice.hpp"

#include <algorithm>

// The header cannot include crime.hpp (crime.hpp includes it); the rules can.
// HEAT weighs what stands above the ledger's own warrant line, kWarrantAt.
#include "granadad/sim/crime.hpp"

namespace granadad::sim {

std::string_view pleaName(Plea plea) noexcept {
    switch (plea) {
        case Plea::None:
            return "none";
        case Plea::Guilty:
            return "guilty";
        case Plea::NotGuilty:
            return "not guilty";
        case Plea::NoPlea:
            return "no plea";
    }
    return "?";
}

std::string_view judgmentName(Judgment judgment) noexcept {
    switch (judgment) {
        case Judgment::None:
            return "none";
        case Judgment::Spared:
            return "spared";
        case Judgment::Fined:
            return "fined";
        case Judgment::Held:
            return "held";
        case Judgment::Bound:
            return "bound";
        case Judgment::TheHand:
            return "the hand";
        case Judgment::Commuted:
            return "commuted";
        case Judgment::TheRope:
            return "the rope";
    }
    return "?";
}

Judgment paperBand(std::int32_t scored, Plea plea) noexcept {
    // SPARED is the denial's road and only the denial's: a confession's best
    // case is the fine. Daggerfall exactly -- a not-guilty plea is the only
    // way to walk out clean, and the only way to the doubled sentence.
    if (plea == Plea::NotGuilty && scored >= kSparedLine) {
        return Judgment::Spared;
    }
    if (scored >= kFinedLine) {
        return Judgment::Fined;
    }
    if (scored >= kHeldLine) {
        return Judgment::Held;
    }
    return Judgment::Bound;
}

Arraignment weighArraignment(const ChargeSheet& sheet, Plea plea) {
    Arraignment out;
    if (!sheet.written || plea == Plea::None) {
        return out;
    }
    const bool rope = sheet.tier == Sentence::Condemned;
    const bool hand = sheet.tier == Sentence::Maimed;
    const bool paper = sheet.tier == Sentence::Held;
    if (!rope && !hand && !paper) {
        // No paper on you, so no bench for you. Cull's fine at the door is the
        // pre-court fast path and it is never weighed.
        return out;
    }
    out.heard = true;

    // MERCY IS GIVEN ONCE. A rope hearing for a man the bench already spared
    // the rope has no plea: the page opens, the priest speaks, the one row is
    // I HAVE NOTHING TO SAY, and the answer is the rope. Nothing is weighed
    // and nothing is printed, because nothing could change it.
    if (rope && sheet.commutedBefore) {
        out.plea = Plea::NoPlea;
        out.judgment = Judgment::TheRope;
        return out;
    }
    // And a man who has a plea does not get to decline it: the rows offered
    // are the pleas accepted, and NoPlea outside the mercy-once case is
    // REFUSED rather than read as anything. The hearing waits.
    if (plea == Plea::NoPlea) {
        out.heard = false;
        return out;
    }
    out.plea = plea;

    // THE PRIEST WEIGHS, in the Flame's own register of concern for the soul
    // rather than the sergeant's interest. Positive favours the accused. THE
    // FLAME'S DEFAULT IS MERCY, and the arithmetic has to start there or the
    // institution is a formality with seven names.
    std::int32_t weight = 0;
    const auto term = [&](std::string_view name, std::int32_t value, std::int32_t count = 0) {
        if (value == 0 && name != kTermFlame) {
            // A zero line says nothing; the check block prints what weighed.
            return;
        }
        out.terms.push_back(ArraignmentTerm{name, value, count});
        weight += value;
    };
    term(kTermFlame, kFlameBase);
    // What the accused may plead: the tongue (Daggerfall's Streetwise), what
    // he gave at this door (temple standing -- the only coin the Flame
    // reads), and what the ward thinks of him.
    term(kTermTongue, std::min(20, std::max(0, sheet.streetwise) / 2));
    term(kTermDoor, std::min(24, std::max(0, sheet.templeStanding) / 3));
    term(kTermWard, std::clamp(sheet.reputation / 5, -20, 20));
    // What the Watch must show: convictions, not hearings; the paper above
    // the line; the blood; who saw it.
    term(kTermTakenBefore, -std::min(30, std::max(0, sheet.priors) * 10));
    term(kTermHeat, -std::min(10, std::max(0, sheet.heatAtArrest - kWarrantAt) / 4));
    if (sheet.blood) {
        term(kTermBlood, -30);
        term(kTermSawIt, -std::min(16, std::max(0, sheet.witnesses) * 4), sheet.witnesses);
    }
    // The roofs: a Skyrunner's first is the hand's tier, and his second is
    // the ladder's own rope, blood or no blood beside it.
    if (hand) {
        term(kTermRoofs, -12);
    }
    if (sheet.secondRung) {
        term(kTermSecondRung, -24);
    }
    // A commuted man before the bench again.
    if (sheet.condemnedBefore) {
        term(kTermRopeOnce, -16);
    }
    out.weight = weight;

    // THE PLEA. A confession is weighed as it is given: six for the honesty,
    // draw-free, the same answer every time. A denial is weighed with the
    // priest's own doubt in it: the arrest draw's band above the nights,
    // compound.cpp's own "the priest is a man", ten points either way.
    if (out.plea == Plea::Guilty) {
        out.pleaTerm = kConfessedTerm;
        out.terms.push_back(ArraignmentTerm{kTermConfessed, kConfessedTerm, 0});
    } else {
        const std::uint64_t band = (sheet.draw >> kPriestBandShift) %
                                   static_cast<std::uint64_t>(2 * kPriestBand + 1);
        out.pleaTerm = static_cast<std::int32_t>(band) - kPriestBand;
        out.terms.push_back(ArraignmentTerm{kTermPriestIsAMan, out.pleaTerm, 0});
    }
    out.scored = out.weight + out.pleaTerm;

    if (rope) {
        // TWO ANSWERS AND NOTHING ELSE. Never SPARED: the corpse is on the
        // roster and the witnesses are named. Never doubled: there is nothing
        // under the rope to double.
        out.judgment = out.scored >= kMercyLine ? Judgment::Commuted : Judgment::TheRope;
        return out;
    }

    out.band = paperBand(out.scored, out.plea);
    if (paper) {
        out.judgment = out.band;
    } else {
        // THE HAND TIER: the PAPER bands with one substitution. At the fine's
        // line and above, the priest overrules the sergeant and it is HELD
        // with the hand spared; below it, the hand -- with HELD's nights in
        // the middle band and BOUND's five days under the last line, which the
        // band records.
        switch (out.band) {
            case Judgment::Spared:
                out.judgment = Judgment::Spared;
                break;
            case Judgment::Fined:
                out.judgment = Judgment::Held;
                break;
            default:
                out.judgment = Judgment::TheHand;
                break;
        }
    }
    // DENIED AND DISBELIEVED. The sentence doubles and the Mission remembers
    // the lie. A confession is never doubled; a denial that walked is not a
    // lie the bench caught.
    out.doubled = out.plea == Plea::NotGuilty && out.judgment != Judgment::Spared;
    return out;
}

// ---------------------------------------------------------------------------
// the sentence, in integers
// ---------------------------------------------------------------------------

std::int32_t cellNights(std::uint64_t draw) noexcept {
    const std::int32_t hours = heldHours(draw);
    return std::max(1, (hours + kHoursPerNight / 2) / kHoursPerNight);
}

SentenceTerms sentenceTerms(const HearingState& hearing, std::int32_t purse) noexcept {
    SentenceTerms out;
    if (!hearing.judged() || hearing.judgment == Judgment::None) {
        return out;
    }
    out.served = true;
    out.judgment = hearing.judgment;
    out.band = hearing.band;
    out.doubled = hearing.doubled;
    const ChargeSheet& sheet = hearing.sheet;
    const std::int32_t twice = out.doubled ? 2 : 1;

    // WHAT THE SHAPE OF THE ANSWER IS. Four shapes under seven names: the
    // fine (FINED), the cell (HELD), the yard (BOUND) and the rope; THE HAND
    // is the cell or the yard with the hand taken beside it, by the PAPER
    // band the score fell in, and COMMUTED is the yard at the price of a
    // life. Section 4.1's table, one row each.
    bool fined = false;
    bool cell = false;
    bool yard = false;
    std::int32_t yardDays = 0;
    switch (out.judgment) {
        case Judgment::Spared:
            // Walked out clean. Coin 0, clock 0, no prior.
            out.releaseHere = true;
            return out;
        case Judgment::Fined:
            fined = true;
            out.releaseHere = true;
            break;
        case Judgment::Held:
            fined = true;
            cell = true;
            break;
        case Judgment::Bound:
            yard = true;
            yardDays = kBoundDays * twice;
            break;
        case Judgment::TheHand:
            out.hand = true;
            if (out.band == Judgment::Bound) {
                // Under the last line: the hand, plus BOUND's five days.
                yard = true;
                yardDays = kBoundDays * twice;
            } else {
                // The hand, plus HELD's coin and nights.
                fined = true;
                cell = true;
            }
            break;
        case Judgment::Commuted:
            // The rope does not un-take the hand; twelve days bondsworn to
            // the Mission; the fine forgiven. Never doubled.
            out.hand = true;
            yard = true;
            yardDays = kCommutedDays;
            break;
        case Judgment::TheRope:
            out.rope = true;
            return out;
        case Judgment::None:
            break;
    }

    // COIN. The shipped fine, doubled if the plea failed, capped at the purse
    // -- nobody is put in debt -- and the shortfall worked off, a day per
    // four Royals, up to seven. The tenure ruling's own "payable in Royals
    // or in yourself".
    if (fined) {
        out.fineAsked = fineFor(sheet.heatAtArrest, sheet.unitsSeized) * twice;
        out.finePaid = std::min(std::max(0, purse), out.fineAsked);
        out.shortfall = out.fineAsked - out.finePaid;
        out.shortfallDays =
            std::min(kShortfallDaysMax,
                     (out.shortfall + kShortfallRoyalsPerDay - 1) / kShortfallRoyalsPerDay);
    }
    // THE CELL. The shipped one-to-three nights off the arrest's own draw,
    // exactly where they always were -- rounded to WHOLE nights once, here,
    // so what the page promises and what the clock skips are one number --
    // and twice that for a lie.
    if (cell) {
        out.cellHours = cellNights(sheet.draw) * kHoursPerNight * twice;
    }
    // THE YARD. BOUND's days, COMMUTED's days, and the fine's shortfall --
    // all bondsworn to the Mission.
    out.bondDays = yardDays + out.shortfallDays;
    out.hours = out.cellHours + out.bondDays * 24;
    out.days = out.hours / 24;
    out.mends = out.days >= 1;
    if (!fined) {
        // The yard's answers forgive the fine outright; nothing is asked.
        out.fineAsked = 0;
        out.finePaid = 0;
        out.shortfall = 0;
    }

    // STANDING. A conviction is a justice event: the roofs warm to whoever
    // the Watch corrects and the mirror halves it onto the Watch -- but not
    // for a fine, which is a charge and not a correction. The Flame
    // remembers a lie, blesses the yard's work, and remembers mercy.
    if (out.judgment != Judgment::Fined) {
        out.roofsDelta = kConvictionRoofsGain;
    }
    if (out.doubled) {
        out.templeDelta -= kLieTempleCost;
    }
    if (yard && out.judgment != Judgment::Commuted) {
        out.templeDelta += kBoundTempleGain;
    }
    if (out.judgment == Judgment::Commuted) {
        out.templeDelta += kCommutedTempleGain;
    }
    return out;
}

}  // namespace granadad::sim
