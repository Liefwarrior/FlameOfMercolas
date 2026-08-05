#include "granadad/sim/contraband.hpp"

#include <algorithm>

#include "granadad/sim/social.hpp"

namespace granadad::sim {

namespace {

constexpr std::uint8_t kStashMagic0 = 'G';
constexpr std::uint8_t kStashMagic1 = 'B';
constexpr std::uint8_t kStashVersion = 1;

/// The skill a hand gets better at by handling a thing. MIXTURES is the owner's
/// own id for the apothecary craft and covers all three of the ward's chemical
/// trades; FIELDCRAFT is what a knife beside a carcass is, and the Java build's
/// cull verb charges exactly the same skill for exactly the same act;
/// CRACKSMANSHIP is what carrying somebody else's plate is.
constexpr std::string_view kMixtureSkill = "mixtures";
constexpr std::string_view kFieldSkill = "fieldcraft";

[[nodiscard]] constexpr bool inRange(Contraband good) noexcept {
    return static_cast<std::size_t>(good) < kContrabandCount;
}

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

}  // namespace

// ---------------------------------------------------------------------------
// the goods
// ---------------------------------------------------------------------------

std::string_view contrabandSymbol(Contraband good) noexcept {
    switch (good) {
        case Contraband::Scalp:
            return "scalp";
        case Contraband::Dust:
            return "dust";
        case Contraband::Moonshine:
            return "moonshine";
        case Contraband::Flower:
            return "flower";
        case Contraband::Artifact:
            return "artifact";
    }
    return "";
}

std::string_view contrabandLabel(Contraband good) noexcept {
    switch (good) {
        case Contraband::Scalp:
            return "SCALPS";
        case Contraband::Dust:
            return "DUST";
        case Contraband::Moonshine:
            return "QUAYFIRE";
        case Contraband::Flower:
            return "FLOWER";
        case Contraband::Artifact:
            return "PIECES";
    }
    return "?";
}

std::string_view contrabandLabelFor(Contraband good, std::int32_t count) noexcept {
    if (count != 1) {
        return contrabandLabel(good);
    }
    switch (good) {
        case Contraband::Scalp:
            return "SCALP";
        case Contraband::Artifact:
            return "PIECE";
        // Dust, quayfire and flower are measured, not counted. "1 DUST" is the
        // same English as "3 DUST" and neither takes an S.
        case Contraband::Dust:
        case Contraband::Moonshine:
        case Contraband::Flower:
            break;
    }
    return contrabandLabel(good);
}

bool contrabandFromSymbol(std::string_view symbol, Contraband& out) noexcept {
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        const Contraband good = static_cast<Contraband>(i);
        if (contrabandSymbol(good) == symbol) {
            out = good;
            return true;
        }
    }
    return false;
}

std::int32_t contrabandValue(Contraband good) noexcept {
    switch (good) {
        // A bounty, not a price: the ward pays it to be rid of the vermin and
        // it is deliberately the smallest number here. Nobody gets rich on rats.
        case Contraband::Scalp:
            return 4;
        // The most valuable thing on the list and the lightest, which is the
        // whole reason it is worth running.
        case Contraband::Dust:
            return 16;
        // Cheap, and it weighs like the drink it is.
        case Contraband::Moonshine:
            return 7;
        case Contraband::Flower:
            return 10;
        // A named piece. Worth three bales of spirit and impossible to explain.
        case Contraband::Artifact:
            return 24;
    }
    return 0;
}

std::int32_t contrabandHeat(Contraband good) noexcept {
    switch (good) {
        // Legal. A watchman who finds scalps on you has found a man doing the
        // ward a favour.
        case Contraband::Scalp:
            return 0;
        case Contraband::Dust:
            return 9;
        case Contraband::Moonshine:
            return 4;
        case Contraband::Flower:
            return 6;
        // Somebody has reported this missing, by name.
        case Contraband::Artifact:
            return 12;
    }
    return 0;
}

std::int32_t contrabandWeight(Contraband good) noexcept {
    switch (good) {
        case Contraband::Scalp:
            return 2;
        case Contraband::Dust:
            return 3;
        // A jar. This is what makes a spirit run a thing you can SEE somebody
        // doing, and dust a thing you cannot.
        case Contraband::Moonshine:
            return 24;
        case Contraband::Flower:
            return 8;
        case Contraband::Artifact:
            return 10;
    }
    return 0;
}

std::string_view contrabandSkill(Contraband good) noexcept {
    switch (good) {
        case Contraband::Scalp:
            return kFieldSkill;
        case Contraband::Dust:
        case Contraband::Moonshine:
        case Contraband::Flower:
            return kMixtureSkill;
        case Contraband::Artifact:
            return kThieverySkill;
    }
    return {};
}

bool contrabandLegal(Contraband good) noexcept {
    return good == Contraband::Scalp;
}

bool contrabandNeedsSanction(Contraband good) noexcept {
    // DECISIONS.md, the tenure ruling: the Church "sanctions the redemption of
    // a scalp". One conscience under blood money, and it is the only thing on
    // this list a priest has an opinion about.
    return good == Contraband::Scalp;
}

// ---------------------------------------------------------------------------
// the sack
// ---------------------------------------------------------------------------

std::int32_t Stash::count(Contraband good) const noexcept {
    return inRange(good) ? counts_[static_cast<std::size_t>(good)] : 0;
}

std::int32_t Stash::units() const noexcept {
    std::int32_t total = 0;
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        total += counts_[i];
    }
    return total;
}

std::int32_t Stash::weight() const noexcept {
    std::int32_t drams = 0;
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        drams += counts_[i] * contrabandWeight(static_cast<Contraband>(i));
    }
    return drams;
}

std::int32_t Stash::illicitUnits() const noexcept {
    std::int32_t total = 0;
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        const Contraband good = static_cast<Contraband>(i);
        if (!contrabandLegal(good)) {
            total += counts_[i];
        }
    }
    return total;
}

std::int32_t Stash::illicitWeight() const noexcept {
    std::int32_t drams = 0;
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        const Contraband good = static_cast<Contraband>(i);
        if (!contrabandLegal(good)) {
            drams += counts_[i] * contrabandWeight(good);
        }
    }
    return drams;
}

std::int32_t Stash::heatIfSearched() const noexcept {
    std::int32_t heat = 0;
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        const Contraband good = static_cast<Contraband>(i);
        heat += counts_[i] * contrabandHeat(good);
    }
    return heat;
}

std::int32_t Stash::roomLeft() const noexcept {
    return std::max(0, kStashDrams - weight());
}

std::int32_t Stash::add(Contraband good, std::int32_t units) {
    if (!inRange(good) || units <= 0) {
        return 0;
    }
    const std::int32_t perUnit = std::max(1, contrabandWeight(good));
    const std::int32_t byWeight = roomLeft() / perUnit;
    const std::int32_t byCount = std::max(0, kStashUnits - this->units());
    const std::int32_t fits = std::min({units, byWeight, byCount});
    if (fits <= 0) {
        return 0;
    }
    counts_[static_cast<std::size_t>(good)] += fits;
    return fits;
}

std::int32_t Stash::take(Contraband good, std::int32_t units) {
    if (!inRange(good) || units <= 0) {
        return 0;
    }
    std::int32_t& held = counts_[static_cast<std::size_t>(good)];
    const std::int32_t took = std::min(units, held);
    held -= took;
    return took;
}

std::int32_t Stash::seizeIllicit() {
    std::int32_t taken = 0;
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        if (!contrabandLegal(static_cast<Contraband>(i))) {
            taken += counts_[i];
            counts_[i] = 0;
        }
    }
    return taken;
}

void Stash::clear() noexcept {
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        counts_[i] = 0;
    }
}

std::vector<std::uint8_t> Stash::encode() const {
    std::vector<std::uint8_t> out;
    out.push_back(kStashMagic0);
    out.push_back(kStashMagic1);
    out.push_back(kStashVersion);
    out.push_back(static_cast<std::uint8_t>(kContrabandCount));
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        putI32(out, counts_[i]);
    }
    return out;
}

bool Stash::decode(const std::vector<std::uint8_t>& bytes, Stash& out) {
    if (bytes.size() < 4 || bytes[0] != kStashMagic0 || bytes[1] != kStashMagic1 ||
        bytes[2] != kStashVersion || bytes[3] != static_cast<std::uint8_t>(kContrabandCount)) {
        return false;
    }
    Stash parsed;
    std::size_t cursor = 4;
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        if (!takeI32(bytes, cursor, parsed.counts_[i]) || parsed.counts_[i] < 0) {
            return false;
        }
    }
    out = parsed;
    return true;
}

void Stash::hashInto(HashSink& sink) const {
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        sink.put_int(static_cast<std::uint32_t>(counts_[i]));
    }
}

}  // namespace granadad::sim
