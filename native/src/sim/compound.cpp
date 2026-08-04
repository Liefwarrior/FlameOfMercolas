#include "granadad/sim/compound.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "granadad/sim/actor.hpp"
#include "granadad/sim/barks.hpp"
#include "granadad/sim/notables.hpp"

namespace granadad::sim {

namespace {

[[nodiscard]] std::string stringField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return {};
    }
    return foldToAscii(found->get<std::string>());
}

[[nodiscard]] std::int32_t intField(const nlohmann::json& node, const char* key,
                                    std::int32_t fallback = 0) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_number_integer()) {
        return fallback;
    }
    return found->get<std::int32_t>();
}

void put_string(HashSink& sink, std::string_view text) {
    sink.put_int(static_cast<std::uint32_t>(text.size()));
    for (const char c : text) {
        sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(c)));
    }
}

[[nodiscard]] Tenure tenureFromKey(std::string_view key) noexcept {
    if (key == "glebe") {
        return Tenure::Glebe;
    }
    if (key == "pledged") {
        return Tenure::Pledged;
    }
    if (key == "vacant") {
        return Tenure::Vacant;
    }
    return Tenure::Charged;
}

/// The salt every draw in this file hangs off. Named, never positional.
const std::uint64_t kWardSalt = stream_salt("ward.compounds");

/// THE HOUSEHOLD SIZE DISTRIBUTION IS NOT OURS. content/raws/actors/
/// household.json carries {1:20, 2:35, 3:25, 4:15, 5:5}, and DOCKS-GAZETTEER
/// section 2.5 cites exactly those weights when it derives the ward's
/// population. So the roll reads them rather than inventing a mean.
///
/// The fallback is the same five numbers, and it exists for one reason: this
/// file must not be the thing that stops the game booting while somebody is
/// editing a raw. Every other loader in this project follows the same rule.
struct HouseholdSizes {
    std::int32_t size[6] = {0, 20, 35, 25, 15, 5};
    std::int32_t total = 100;

    [[nodiscard]] std::int32_t draw(std::uint64_t roll) const noexcept {
        std::int32_t pick =
            static_cast<std::int32_t>(roll % static_cast<std::uint64_t>(std::max(1, total)));
        for (std::int32_t heads = 1; heads <= 5; ++heads) {
            pick -= size[heads];
            if (pick < 0) {
                return heads;
            }
        }
        return 1;
    }
};

[[nodiscard]] HouseholdSizes loadHouseholdSizes(const std::filesystem::path& contentDir) {
    HouseholdSizes out;
    std::error_code error;
    const std::filesystem::path path = contentDir / "raws" / "actors" / "household.json";
    if (!std::filesystem::is_regular_file(path, error)) {
        return out;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return out;
    }
    std::ostringstream text;
    text << file.rdbuf();
    const nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return out;
    }
    const auto weights = document.find("householdSizeWeights");
    if (weights == document.end() || !weights->is_object()) {
        return out;
    }
    HouseholdSizes read;
    for (std::int32_t heads = 1; heads <= 5; ++heads) {
        read.size[heads] = 0;
    }
    read.total = 0;
    for (auto row = weights->begin(); row != weights->end(); ++row) {
        if (!row.value().is_number_integer()) {
            continue;
        }
        const std::int32_t heads = std::atoi(row.key().c_str());
        const std::int32_t weight = std::max(0, row.value().get<std::int32_t>());
        if (heads >= 1 && heads <= 5) {
            read.size[heads] = weight;
            read.total += weight;
        }
    }
    return read.total > 0 ? read : out;
}

}  // namespace

// ---------------------------------------------------------------------------
// names
// ---------------------------------------------------------------------------

std::string_view tenureName(Tenure tenure) noexcept {
    switch (tenure) {
        case Tenure::Glebe:
            return "glebe";
        case Tenure::Charged:
            return "charged";
        case Tenure::Pledged:
            return "pledged";
        case Tenure::Vacant:
            return "vacant";
    }
    return "?";
}

std::string_view houseKindName(HouseKind kind) noexcept {
    switch (kind) {
        case HouseKind::Mansion:
            return "mansion";
        case HouseKind::Condo:
            return "condo";
        case HouseKind::RoofHut:
            return "roof";
        case HouseKind::Wastrel:
            return "wastrel";
    }
    return "?";
}

std::string_view verdictName(Verdict verdict) noexcept {
    switch (verdict) {
        case Verdict::Dismissed:
            return "dismissed";
        case Verdict::Stay:
            return "stay";
        case Verdict::Abatement:
            return "abatement";
        case Verdict::BondOrdered:
            return "bond";
        case Verdict::Distraint:
            return "distraint";
        case Verdict::ChargeRevoked:
            return "revoked";
    }
    return "?";
}

std::string_view tenureResultName(TenureResult result) noexcept {
    switch (result) {
        case TenureResult::Done:
            return "done";
        case TenureResult::NoSuchThing:
            return "no such thing";
        case TenureResult::CannotAfford:
            return "cannot afford";
        case TenureResult::AlreadyHeld:
            return "already held";
        case TenureResult::NoCause:
            return "no cause";
        case TenureResult::NotVacant:
            return "not vacant";
    }
    return "?";
}

std::int32_t bedYield(std::int32_t yieldPerBed, std::int32_t tends,
                      std::int32_t needed) noexcept {
    const std::int32_t wanted = std::max(1, needed);
    const std::int32_t worked = std::clamp(tends, 0, wanted);
    if (worked * 100 < wanted * kBedFailsBelowPercent) {
        // WEEDS. Neglect does not scale smoothly all the way down: below a
        // third of the working a crop wanted there is nothing on the bed worth
        // bending a back for, and the season is lost rather than thin.
        return 0;
    }
    return std::max(0, yieldPerBed) * worked / wanted;
}

std::int32_t WardStats::starvationPermille() const noexcept {
    if (headDays <= 0) {
        return 0;
    }
    return static_cast<std::int32_t>(headDaysStarving * 1000 / headDays);
}

std::int32_t WardStats::hungerPermille() const noexcept {
    if (headDays <= 0) {
        return 0;
    }
    return static_cast<std::int32_t>(headDaysHungry * 1000 / headDays);
}

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

const CropRaw* CompoundRaws::crop(std::string_view id) const noexcept {
    for (const CropRaw& row : crops_) {
        if (row.id == id) {
            return &row;
        }
    }
    return crops_.empty() ? nullptr : &crops_.front();
}

CompoundRaws CompoundRaws::load(const std::filesystem::path& contentDir,
                                const NotableRegistry& who) {
    CompoundRaws out;
    std::error_code error;
    const std::filesystem::path path = contentDir / "raws" / "compounds" / "compounds.json";
    if (!std::filesystem::is_regular_file(path, error)) {
        return out;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return out;
    }
    std::ostringstream text;
    text << file.rdbuf();
    const nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return out;
    }
    out.quarterDays_ = std::max(1, intField(document, "quarterDays", 90));

    const auto crops = document.find("crops");
    if (crops != document.end() && crops->is_array()) {
        for (const nlohmann::json& node : *crops) {
            if (!node.is_object()) {
                continue;
            }
            CropRaw row;
            row.id = stringField(node, "id");
            row.label = stringField(node, "label");
            row.growDays = std::max(1, intField(node, "growDays", 40));
            row.yieldPerBed = std::max(0, intField(node, "yieldPerBed"));
            if (!row.id.empty()) {
                out.crops_.push_back(std::move(row));
            }
        }
    }

    const auto plots = document.find("plots");
    if (plots != document.end() && plots->is_array()) {
        for (const nlohmann::json& node : *plots) {
            if (!node.is_object()) {
                continue;
            }
            PlotRaw row;
            row.id = stringField(node, "id");
            row.name = stringField(node, "name");
            row.tenure = tenureFromKey(stringField(node, "tenure"));
            row.denDuke = stringField(node, "denDuke");
            row.pledgedTo = stringField(node, "pledgedTo");
            row.priest = stringField(node, "priest");
            row.chargeRent = std::max(0, intField(node, "chargeRent"));
            row.groundPenny = std::max(0, intField(node, "groundPenny"));
            row.roofRent = std::max(0, intField(node, "roofRent"));
            row.beds = std::max(0, intField(node, "beds"));
            row.crop = stringField(node, "crop");
            row.mansionHouseholds = std::max(0, intField(node, "mansionHouseholds"));
            row.condos = std::max(0, intField(node, "condos"));
            row.roofHuts = std::max(0, intField(node, "roofHuts"));
            row.wastrels = std::max(0, intField(node, "wastrels"));
            row.dukeCoin = std::max(0, intField(node, "dukeCoin"));
            row.note = stringField(node, "note");
            if (row.id.empty()) {
                continue;
            }
            // REFUSED BY NAME. A Den Duke, a creditor or a priest the owner's
            // notables.json does not have is not a person, and a plot that
            // names one is not on the roll. This is the same gate the contract
            // board puts on its brokers, and it exists so nothing here can
            // quietly invent a forty-third notable.
            const bool dukeKnown = row.denDuke.empty() || who.find(row.denDuke) != nullptr;
            const bool creditorKnown = row.pledgedTo.empty() || who.find(row.pledgedTo) != nullptr;
            const bool priestKnown = row.priest.empty() || who.find(row.priest) != nullptr;
            if (!dukeKnown || !creditorKnown || !priestKnown) {
                ++out.refused_;
                continue;
            }
            // A charged or pledged plot with nobody holding the charge is a
            // vacant one, whatever the file says: the roll is the truth and a
            // Duke who is not named is a Duke who is not there.
            if (row.denDuke.empty() && row.tenure != Tenure::Glebe) {
                row.tenure = Tenure::Vacant;
            }
            out.plots_.push_back(std::move(row));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// building the roll
// ---------------------------------------------------------------------------

Ward::Ward(std::uint64_t worldSeed, const std::filesystem::path& contentDir,
           const NotableRegistry& who)
    : id_(SystemId::of("ward.compounds", "WARD")),
      raws_(CompoundRaws::load(contentDir, who)),
      rng_(worldSeed, kWardSalt) {
    const HouseholdSizes sizes = loadHouseholdSizes(contentDir);
    CounterRandomSource build(worldSeed, kWardSalt);
    build.begin_tick(0);

    plots_.reserve(raws_.plots().size());
    for (std::size_t p = 0; p < raws_.plots().size(); ++p) {
        const PlotRaw& raw = raws_.plots()[p];
        Plot plot;
        plot.raw = static_cast<std::int32_t>(p);
        plot.tenure = raw.tenure;
        plot.dukeCoin = raw.dukeCoin;
        plot.beds.assign(static_cast<std::size_t>(raw.beds), CropBed{});
        // STAGGERED, not planted all on one morning. A courtyard whose every
        // bed ripened on the same day would feed the compound four times a
        // year and starve it in between, which is an artefact of the model and
        // not a fact about farming.
        const CropRaw* crop = raws_.crop(raw.crop);
        const std::int32_t growDays = crop == nullptr ? 40 : crop->growDays;
        for (std::size_t b = 0; b < plot.beds.size(); ++b) {
            const std::uint64_t roll = build.draw(static_cast<std::uint64_t>(p) * 1000U + b, 0);
            plot.beds[b].age =
                static_cast<std::int32_t>(roll % static_cast<std::uint64_t>(growDays));
            // And it has been worked since it was sown, at the rate a tended
            // bed is worked. A ward that started every bed at zero tends would
            // report a famine on day one that never happened.
            plot.beds[b].tends = plot.beds[b].age / kTendEveryDays;
        }
        plots_.push_back(std::move(plot));
    }

    // The households, plot by plot, mansion then condos then roofs -- so a
    // household's id is a stable function of the roll's own order and never of
    // the order somebody happened to build them in.
    for (std::size_t p = 0; p < plots_.size(); ++p) {
        const PlotRaw& raw = raws_.plots()[p];
        Plot& plot = plots_[p];
        // The house-owners on this plot, in roll order. A roof hut is let by
        // ONE of them and not by the compound: section 2.8 is explicit that a
        // rooftop lodger rents from the house-owner beneath them, and "every
        // mansion-poor house-owner becomes a petty landlord" only means
        // something if the roofs are spread across the owners rather than all
        // hanging off whichever one happened to be built first.
        std::vector<std::int32_t> ownersHere;
        std::int32_t roofsPlaced = 0;
        const auto place = [&](HouseKind kind, std::int32_t index) {
            Household home;
            home.id = static_cast<std::int32_t>(households_.size());
            home.plot = static_cast<std::int32_t>(p);
            home.kind = kind;
            home.heads = sizes.draw(build.draw(
                static_cast<std::uint64_t>(p) * 100000U + static_cast<std::uint64_t>(index),
                1 + static_cast<std::int32_t>(kind)));
            if (kind == HouseKind::Mansion && index == 0) {
                // The owning family. Bigger, because section 2.5's mansion
                // holds the family AND its dependent households.
                home.heads = std::min(5, home.heads + 2);
            }
            if (kind == HouseKind::Wastrel) {
                // Nothing owed to anybody, because there is nothing to owe it
                // on -- and nothing can be taken from them either, which is the
                // only protection they have.
            } else if (kind == HouseKind::RoofHut) {
                home.roofRent = raw.roofRent;
                home.landlord = ownersHere.empty()
                                    ? -1
                                    : ownersHere[static_cast<std::size_t>(roofsPlaced) %
                                                 ownersHere.size()];
                ++roofsPlaced;
            } else {
                // NOBODY COLLECTS ON A VACANT CHARGE. "Rent is not paid in the
                // Gullet, but respects are, and they are paid to Mag." Church
                // ground is the same the other way round: never let to anyone,
                // token alms in place of a penny. A penny is owed to a Den
                // Duke and to nobody else, so where there is no Duke there is
                // no penny -- until somebody petitions for the charge, which
                // is what petitionForCharge does.
                const bool hasDuke =
                    raw.tenure == Tenure::Charged || raw.tenure == Tenure::Pledged;
                home.groundPenny = hasDuke ? raw.groundPenny : 0;
                ownersHere.push_back(home.id);
            }
            // A quarter's food money and a shelf with something on it. Nobody
            // starts the simulation starving; the first quarter is what the
            // ward's own arithmetic does to them. A wastrel starts with a
            // day's scraps and no purse, because that is what a wastrel is.
            home.coin = kind == HouseKind::Wastrel ? 0 : home.heads * kRationPrice * 20;
            home.food = kind == HouseKind::Wastrel
                            ? 1
                            : std::min(kHouseholdLarderCap, home.heads * 3);
            plot.residents.push_back(home.id);
            households_.push_back(home);
        };
        for (std::int32_t i = 0; i < raw.mansionHouseholds; ++i) {
            place(HouseKind::Mansion, i);
        }
        for (std::int32_t i = 0; i < raw.condos; ++i) {
            place(HouseKind::Condo, 1000 + i);
        }
        for (std::int32_t i = 0; i < raw.roofHuts; ++i) {
            place(HouseKind::RoofHut, 2000 + i);
        }
        for (std::int32_t i = 0; i < raw.wastrels; ++i) {
            place(HouseKind::Wastrel, 3000 + i);
        }
        // The atrium starts with something in it, for the same reason the
        // household shelf does.
        plot.larder = std::min(kCourtyardLarderCap, raw.beds * 6);
    }
    market_ = kMarketStockCap / 2;
}

// ---------------------------------------------------------------------------
// reading it
// ---------------------------------------------------------------------------

std::int32_t Ward::heads() const noexcept {
    std::int32_t total = 0;
    for (const Household& home : households_) {
        total += home.heads;
    }
    return total;
}

std::int32_t Ward::starvingHeads() const noexcept {
    std::int32_t total = 0;
    for (const Household& home : households_) {
        total += home.starving() ? home.heads : 0;
    }
    return total;
}

std::int32_t Ward::stored() const noexcept {
    std::int32_t total = market_;
    for (const Plot& plot : plots_) {
        total += plot.larder;
    }
    for (const Household& home : households_) {
        total += home.food;
    }
    return total;
}

Household* Ward::householdAt(std::int32_t id) noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= households_.size()) {
        return nullptr;
    }
    return &households_[static_cast<std::size_t>(id)];
}

const Household* Ward::householdAt(std::int32_t id) const noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= households_.size()) {
        return nullptr;
    }
    return &households_[static_cast<std::size_t>(id)];
}

std::int32_t Ward::farmHands(std::int32_t plotIndex) const noexcept {
    if (plotIndex < 0 || static_cast<std::size_t>(plotIndex) >= plots_.size()) {
        return 0;
    }
    // THE BOND IS THE PIPE. A resident whose bond is held by another plot's
    // Duke works THAT Duke's yard -- section 2.8, "their work is the
    // bondholder's yard" -- so their heads are counted there and not here.
    std::int32_t working = 0;
    for (const Household& home : households_) {
        const bool livesHere = home.plot == plotIndex;
        const bool worksHere = home.bonded() ? home.bondholder == plotIndex : livesHere;
        if (worksHere) {
            working += home.heads;
        }
    }
    return working / kHeadsPerFarmHand;
}

bool Ward::yardIsShort(std::int32_t plotIndex) const noexcept {
    if (plotIndex < 0 || static_cast<std::size_t>(plotIndex) >= plots_.size()) {
        return false;
    }
    const Plot& plot = plots_[static_cast<std::size_t>(plotIndex)];
    const std::int32_t capacity = farmHands(plotIndex) * kBedsPerHandPerDay;
    const std::int32_t wanted =
        (static_cast<std::int32_t>(plot.beds.size()) + kTendEveryDays - 1) / kTendEveryDays;
    return capacity < wanted;
}

// ---------------------------------------------------------------------------
// the day
// ---------------------------------------------------------------------------

void Ward::tick(const TickContext& context) {
    // A DAY IS THE UNIT and a tick is a second, so this counts and then does a
    // day's work when a day has gone by. Driving the ward off the engine and
    // driving it off endOfDay() are therefore the same thing, which is what the
    // bridge case in test_compound.cpp asserts rather than assumes.
    (void)context;
    ++seconds_;
    if (seconds_ % kSecondsPerDay == 0) {
        endOfDay();
    }
}

void Ward::endOfDay() {
    ++day_;
    ++stats_.days;
    // ORDER IS STATE. The land first, because what it gives is what the ward
    // eats today; then the wage, because a household buys with the coin it was
    // paid this morning; then the meal; then the quay, which restocks for
    // tomorrow; then, on a quarter-day, the ground.
    growDay();
    wageDay();
    eatDay();
    marketDay();
    if (raws_.quarterDays() > 0 && day_ % raws_.quarterDays() == 0) {
        quarterDay();
    }
}

void Ward::growDay() {
    for (std::size_t p = 0; p < plots_.size(); ++p) {
        Plot& plot = plots_[p];
        if (plot.beds.empty()) {
            continue;
        }
        const PlotRaw& raw = raws_.plots()[static_cast<std::size_t>(plot.raw)];
        const CropRaw* crop = raws_.crop(raw.crop);
        if (crop == nullptr) {
            continue;
        }
        // WHO IS IN THE YARD TODAY. Every hand gets round kBedsPerHandPerDay
        // beds, and a bed wants working every kTendEveryDays, so the beds a
        // compound can keep is hands x rate x interval and no more.
        std::int32_t capacity = farmHands(static_cast<std::int32_t>(p)) * kBedsPerHandPerDay;
        const std::int32_t wanted =
            (static_cast<std::int32_t>(plot.beds.size()) + kTendEveryDays - 1) / kTendEveryDays;
        stats_.bedTendsWanted += wanted;

        // ROTATING START, so the same low-index beds are not the only ones
        // ever worked when there are not enough hands for all of them. A fixed
        // walk would make the last beds of a short-handed compound permanently
        // dead, which is a bug that would look exactly like a balance decision.
        const std::size_t count = plot.beds.size();
        const std::size_t start = static_cast<std::size_t>(day_ % static_cast<std::int64_t>(count));
        for (std::size_t step = 0; step < count && capacity > 0; ++step) {
            CropBed& bed = plot.beds[(start + step) % count];
            // Only beds that are actually due. Working a bed turned over
            // yesterday is a hand wasted.
            if (bed.age % kTendEveryDays != 0) {
                continue;
            }
            ++bed.tends;
            --capacity;
            ++stats_.bedTendsDone;
        }

        const std::int32_t needed = std::max(1, crop->growDays / kTendEveryDays);
        for (CropBed& bed : plot.beds) {
            ++bed.age;
            if (bed.age < crop->growDays) {
                continue;
            }
            // THE SICKLE. What comes off a bed is what was put into it -- see
            // bedYield, which is where "crops fail if neglected" is written as
            // arithmetic rather than left to a season to demonstrate.
            const std::int32_t yield = bedYield(crop->yieldPerBed, bed.tends, needed);
            bed.lastYield = yield;
            if (yield <= 0) {
                ++plot.bedsFailed;
                ++stats_.bedsFailed;
            } else {
                ++plot.harvests;
                ++stats_.harvests;
                plot.grown += yield;
                stats_.grown += yield;
                const std::int32_t room = std::max(0, kCourtyardLarderCap - plot.larder);
                const std::int32_t stored = std::min(room, yield);
                plot.larder += stored;
                // A larder is a larder and not a warehouse. What will not fit
                // is accounted rather than quietly dropped, because a food
                // supply that leaks in silence cannot be balanced.
                stats_.spoiledOverCap += yield - stored;
            }
            bed.age = 0;
            bed.tends = 0;
        }
    }
}

void Ward::wageDay() {
    for (Household& home : households_) {
        // THE PLAYER'S PURSE IS THE PLAYER'S OWN. Nothing in this system pays
        // them a wage: what they have is what they earned in the ward -- a
        // bounty, a bale, a night's fence -- and the ground penny falls on them
        // exactly as it falls on everybody else. A player household drawing an
        // NPC wage would make the whole tenure layer a formality for the one
        // person it is meant to be a decision for.
        //
        // The BOND still runs, though, and that is the difference between
        // skipping the wage and skipping the day: a player who leased
        // themselves works the arrears off exactly as anybody else does.
        std::int32_t rate = kWagePerOwnerHead;
        if (home.kind == HouseKind::RoofHut) {
            rate = kWagePerRoofHead;
        } else if (home.kind == HouseKind::Wastrel) {
            rate = kWagePerWastrelHead;
        }
        const std::int32_t wage = home.player ? 0 : home.heads * rate;
        if (home.bonded()) {
            // WHILE THE BOND RUNS, THE WAGE IS THE BONDHOLDER'S. Section 2.8,
            // and it is the whole of what a bond IS: not a caste, a claim on
            // the earnings of somebody who had a bad quarter.
            Plot& holder = plots_[static_cast<std::size_t>(home.bondholder)];
            if (holder.playerIsDuke) {
                playerRentHeld_ += wage;
            } else {
                holder.dukeCoin += wage;
            }
            home.arrears = std::max(0, home.arrears - kBondWorkPerDay);
            ++home.bondDaysServed;
            if (home.arrears <= 0) {
                // DISCHARGE. The Flame blesses a discharged debt, which is
                // cheap for the Flame and worth a great deal to the discharged.
                holder.flameStanding = std::min(kFlameStandingMax, holder.flameStanding + 1);
                home.bondholder = -1;
                home.bondOrdered = false;
                home.bondDaysServed = 0;
                home.quartersBehind = 0;
                ++home.discharges;
                ++stats_.bondsDischarged;
            }
        } else {
            home.coin += wage;
        }
    }
}

std::int32_t Ward::drawFrom(std::int32_t& source, std::int32_t wanted) noexcept {
    const std::int32_t got = std::max(0, std::min(source, wanted));
    source -= got;
    return got;
}

void Ward::eatDay() {
    almsToday_ = kAlmsPerDay;
    std::int32_t starvingToday = 0;

    // TWO PASSES, and the order is the point. The first feeds everybody from
    // what they have, what their compound grew, what their coin buys and what
    // their bondholder owes them. The second hands out the Mission's night-soup
    // to whoever is still short -- HUNGRIEST FIRST, which cannot be decided
    // until the first pass has finished.
    std::vector<std::int32_t> shortBy(households_.size(), 0);

    for (std::size_t i = 0; i < households_.size(); ++i) {
        Household& home = households_[i];
        std::int32_t wanted = home.heads * kRationsPerHeadPerDay;
        stats_.headDays += home.heads;

        // 1. The family's own shelf.
        const std::int32_t own = drawFrom(home.food, wanted);
        wanted -= own;

        // 2. The courtyard. A COMPOUND EATS WHAT ITS COURTYARD GREW -- and a
        //    roof lodger does not, because a roof lodger is not the Duke's
        //    tenant and the atrium is not theirs. That single clause is where
        //    most of this ward's starvation lives, and it is canon's, not a
        //    balance choice: section 2.8 is explicit that the roof people are
        //    their tenants' tenants and parties to nothing.
        if (wanted > 0 && home.ownsHouse()) {
            const std::int32_t got = drawFrom(plots_[static_cast<std::size_t>(home.plot)].larder,
                                              wanted);
            wanted -= got;
            stats_.fromCourtyard += got;
            stats_.eaten += got;
        }
        stats_.eaten += own;

        // 3. The counter. Money gates every mouthful that did not come out of
        //    the ground you worked.
        if (wanted > 0) {
            const std::int32_t affordable = home.coin / kRationPrice;
            const std::int32_t got = drawFrom(market_, std::min(wanted, affordable));
            home.coin -= got * kRationPrice;
            wanted -= got;
            stats_.boughtAtMarket += got;
            stats_.eaten += got;
        }

        // 4. Keep. "In return the bondholder owes KEEP -- board and biscuit,
        //    set low." Out of the bondholder's own courtyard, because that is
        //    where a Duke's food is.
        if (wanted > 0 && home.bonded()) {
            Plot& holder = plots_[static_cast<std::size_t>(home.bondholder)];
            const std::int32_t got = drawFrom(holder.larder, std::min(wanted, kKeepPerHousehold));
            wanted -= got;
            stats_.fromKeep += got;
            stats_.eaten += got;
        }

        shortBy[i] = wanted;
    }

    // The Mission, hungriest first. Sorted by (shortfall descending, id
    // ascending) so the queue is a fact and not the order the vector happened
    // to be in.
    std::vector<std::int32_t> queue;
    queue.reserve(households_.size());
    for (std::size_t i = 0; i < households_.size(); ++i) {
        if (shortBy[i] > 0) {
            queue.push_back(static_cast<std::int32_t>(i));
        }
    }
    std::sort(queue.begin(), queue.end(), [&](std::int32_t a, std::int32_t b) {
        const std::int32_t left = shortBy[static_cast<std::size_t>(a)];
        const std::int32_t right = shortBy[static_cast<std::size_t>(b)];
        return left != right ? left > right : a < b;
    });
    for (const std::int32_t id : queue) {
        if (almsToday_ <= 0) {
            break;
        }
        const std::int32_t got =
            drawFrom(almsToday_, std::min(shortBy[static_cast<std::size_t>(id)], 2));
        shortBy[static_cast<std::size_t>(id)] -= got;
        stats_.fromAlms += got;
        stats_.eaten += got;
    }

    for (std::size_t i = 0; i < households_.size(); ++i) {
        Household& home = households_[i];
        if (shortBy[i] > 0) {
            ++home.hungryDays;
            stats_.headDaysHungry += home.heads;
        } else {
            // A FULL BELLY IS NOT AN AMNESTY, but it is a recovery. Hunger
            // walks back one day at a time, so a family that ate once after a
            // bad week is not instantly counted well again.
            home.hungryDays = std::max(0, home.hungryDays - 1);
            // And what is left over goes on the shelf against tomorrow.
            const std::int32_t spare = std::min(
                kHouseholdLarderCap - home.food,
                home.coin / kRationPrice / 4);
            if (spare > 0) {
                const std::int32_t bought = drawFrom(market_, spare);
                home.coin -= bought * kRationPrice;
                home.food += bought;
                stats_.boughtAtMarket += bought;
            }
        }
        if (home.starving()) {
            ++home.starvedDays;
            stats_.headDaysStarving += home.heads;
            starvingToday += home.heads;
        }
    }
    stats_.peakStarvingHeads = std::max(stats_.peakStarvingHeads, starvingToday);
}

void Ward::marketDay() {
    const std::int32_t room = std::max(0, kMarketStockCap - market_);
    const std::int32_t landed = std::min(room, kMarketImportPerDay);
    market_ += landed;
    stats_.importedToMarket += landed;
}

void Ward::quarterDay() {
    ++stats_.quarters;

    // 1. THE ROOFS PAY FIRST, and they pay the house-owner beneath them rather
    //    than the Duke. Roof income is what makes a struggling owner look
    //    solvent enough to bear a raised penny, so it has to land before the
    //    penny does.
    for (Household& home : households_) {
        if (home.kind != HouseKind::RoofHut || home.roofRent <= 0) {
            continue;
        }
        const std::int32_t paid = std::min(home.coin, home.roofRent);
        home.coin -= paid;
        home.arrears += home.roofRent - paid;
        stats_.roofRentPaid += paid;
        Household* landlord = householdAt(home.landlord);
        if (landlord != nullptr) {
            if (landlord->player) {
                playerRentHeld_ += paid;
            } else {
                landlord->coin += paid;
            }
        }
    }

    // 2. THE GROUND PENNY. Owed by each house-owner to the plot's Den Duke,
    //    never on the dwelling -- the dwelling is the tenant's own -- only on
    //    the earth under it.
    for (Household& home : households_) {
        if (!home.ownsHouse() || home.groundPenny <= 0) {
            continue;
        }
        ++home.quartersKept;
        if (home.bonded()) {
            // The bond IS the payment. A bondsworn tenant does not also find
            // the penny; that is the whole point of leasing yourself.
            continue;
        }
        if (home.stayQuarters > 0) {
            // A STAY IS A REAL QUARTER OF GRACE: the penny does not fall and
            // the arrears do not grow. The debt is still there when it runs out.
            --home.stayQuarters;
            continue;
        }
        const std::int32_t paid = std::min(home.coin, home.groundPenny);
        home.coin -= paid;
        const std::int32_t missed = home.groundPenny - paid;
        home.arrears += missed;
        home.quartersBehind = home.arrears > 0 ? home.quartersBehind + 1 : 0;
        stats_.penniesPaid += paid;
        stats_.penniesShort += missed;
        Plot& plot = plots_[static_cast<std::size_t>(home.plot)];
        if (plot.playerIsDuke) {
            playerRentHeld_ += paid;
        } else {
            plot.dukeCoin += paid;
        }
    }

    // 3. THE CHARGE-RENT. A Den Duke pays the Flame each quarter, and a Duke
    //    behind on his own charge pleads badly at every hearing he brings.
    for (std::size_t p = 0; p < plots_.size(); ++p) {
        Plot& plot = plots_[p];
        const PlotRaw& raw = raws_.plots()[static_cast<std::size_t>(plot.raw)];
        if (plot.tenure == Tenure::Glebe || plot.tenure == Tenure::Vacant ||
            raw.chargeRent <= 0) {
            continue;
        }
        const std::int32_t paid = std::min(plot.dukeCoin, raw.chargeRent);
        plot.dukeCoin -= paid;
        plot.dukeArrears += raw.chargeRent - paid;
        stats_.chargeRentPaid += paid;
        if (paid < raw.chargeRent) {
            plot.flameStanding = std::max(kFlameStandingMin, plot.flameStanding - 6);
        } else {
            plot.flameStanding = std::min(kFlameStandingMax, plot.flameStanding + 2);
        }
    }

    // 4. GOING BONDSWORN. A house-owner who cannot find the penny may lease
    //    their own labour to their Den Duke until the arrears are worked off.
    //    Entered voluntarily by people with one asset left, which is why this
    //    is a choice the household makes and not something done to it.
    for (Household& home : households_) {
        // The player is never bonded ON THEIR BEHALF. Leasing yourself is a
        // decision and Ward::goBondsworn is where the player makes it.
        if (home.player || !home.ownsHouse() || home.bonded() ||
            home.arrears < kBondOfferedAt) {
            continue;
        }
        const Plot& plot = plots_[static_cast<std::size_t>(home.plot)];
        if (plot.tenure == Tenure::Glebe || plot.tenure == Tenure::Vacant) {
            // NOBODY TO LEASE YOURSELF TO. The glebe has no Duke, which is
            // section 2.8's own inversion: the poorest are the least enserfed,
            // and a Duke who wants labour must go to the Gullet or the Rows.
            continue;
        }
        // AND IT TAKES TWO. A bond is a bargain and the Duke is the other
        // party to it: he owes keep for every pair of hands he holds, so he
        // takes them while his own courtyard is short of them and not one
        // more. A Duke with a well-worked yard has no use for your years, and
        // that is what leaves a tenant with nothing to offer but the ground --
        // which is how a case gets to the Mission at all.
        if (!yardIsShort(home.plot)) {
            continue;
        }
        home.bondholder = home.plot;
        home.bondOrdered = false;
        home.bondDaysServed = 0;
        ++stats_.bondsTaken;
    }

    // 4b. THE BOND POOL OF LAST RESORT. "Wastrels own nothing, so nothing can
    //     be taken from them; the only thing they can pledge is themselves."
    //     A starving wastrel and a Duke short of hands are two halves of the
    //     same bargain, and it is the ONE thing in this ward that turns hunger
    //     back into food: hands into the courtyard, crop out of it, keep on
    //     the table. There is no arrears figure to work off, because there was
    //     never any ground -- the bond is struck against the keep advanced.
    for (Household& home : households_) {
        if (home.player || home.kind != HouseKind::Wastrel || home.bonded() ||
            !home.starving()) {
            continue;
        }
        if (plots_[static_cast<std::size_t>(home.plot)].tenure == Tenure::Glebe) {
            // AND NOT OFF THE GLEBE. Section 2.8 is explicit: "a Duke who wants
            // labour cannot get it from the glebe; he must go to the Gullet, to
            // the Rows, or to his own tenants." Church ground is never let to
            // anyone, and that includes letting the people on it. It is the
            // whole of the good inversion the section names -- the poorest are
            // the least enserfed -- and it is also why the Mission's alms
            // traffic is the only thing standing between a hovel-row wastrel
            // and a hungry quarter.
            continue;
        }
        std::int32_t taker = -1;
        for (std::size_t p = 0; p < plots_.size() && taker < 0; ++p) {
            const Tenure tenure = plots_[p].tenure;
            if ((tenure == Tenure::Charged || tenure == Tenure::Pledged) &&
                yardIsShort(static_cast<std::int32_t>(p))) {
                taker = static_cast<std::int32_t>(p);
            }
        }
        if (taker < 0) {
            continue;
        }
        home.bondholder = taker;
        home.bondOrdered = false;
        home.bondDaysServed = 0;
        home.arrears = kWastrelBondArrears;
        ++stats_.bondsTaken;
    }

    // 5. THE HEARINGS. A Den Duke cannot turn a family out; he brings a
    //    petition to the Mission and the plot's charter priest hears it.
    for (std::size_t i = 0; i < households_.size(); ++i) {
        const Household& home = households_[i];
        // THE PLAYER IS NOT EXEMPT. A Duke brings a petition against whoever
        // is behind on his ground, and being the person holding the controller
        // is not a defence -- if it were, the one actor the institution is
        // meant to be a decision for would be the one actor it never touched.
        if (!home.ownsHouse() || home.bonded() || home.arrears < kPetitionAt) {
            continue;
        }
        const Plot& plot = plots_[static_cast<std::size_t>(home.plot)];
        if (plot.tenure == Tenure::Glebe || plot.tenure == Tenure::Vacant) {
            continue;
        }
        // The Duke's offering, sized to what he has. It buys him nothing at
        // the hearing -- see hear() -- and this is the case that proves it.
        const std::int32_t offering = std::min(plot.dukeCoin, 20);
        const std::uint64_t draw =
            rng_.draw(static_cast<std::uint64_t>(home.id),
                      static_cast<std::int32_t>(day_ % 4096));
        const Hearing hearing =
            weighPetition(home.plot, home.id, offering, draw);
        apply(hearing);
    }
}

// ---------------------------------------------------------------------------
// the hearing
// ---------------------------------------------------------------------------

Hearing Ward::weighPetition(std::int32_t plotIndex, std::int32_t householdId,
                            std::int32_t offering, std::uint64_t draw) const {
    Hearing out;
    const Household* home = householdAt(householdId);
    if (home == nullptr || plotIndex < 0 ||
        static_cast<std::size_t>(plotIndex) >= plots_.size()) {
        return out;
    }
    const Plot& plot = plots_[static_cast<std::size_t>(plotIndex)];
    out.heard = true;
    out.household = householdId;
    out.plot = plotIndex;
    out.arrears = home->arrears;
    out.offering = offering;

    // THE PRIEST WEIGHS, in the Flame's own register of concern for the soul
    // rather than commercial interest. Section 2.8 lists five things and every
    // one of them is a term here. Positive favours the tenant.
    // THE FLAME'S DEFAULT IS THAT THE FAMILY STAYS, and the arithmetic has to
    // start there or the institution is a formality with six names. Section
    // 2.8 gives six outcomes and only one of them is eviction; a scale whose
    // zero point is "turn them out" would produce the opposite ward.
    std::int32_t weight = 24;
    // What the tenant may plead: years kept on that ground, and actors under
    // the roof.
    weight += std::min(30, home->quartersKept * 2);
    weight += std::min(25, home->heads * 5);
    // Labour already given under a former bond.
    weight += std::min(20, home->discharges * 10);
    // What the ward brings: the Flame does not put a family on the street in a
    // hungry quarter, and a starving household IS a hungry quarter proved.
    weight += home->starving() ? 25 : 0;
    // What the Duke must show: arrears proved by the roll. Capped, because
    // past a point the debt says nothing new -- a family thirty quarters
    // behind is not thirty times worse than one three quarters behind, it is
    // a family that was never going to find the penny.
    weight -= std::min(40, home->arrears / 8);
    // AND HOW LONG HE HAS BEEN OFFERING TERMS. The Duke must show that he
    // offered terms before he asked for the ground back, and a family three
    // years behind is a family he has been offering them to for three years.
    // Without this the years a tenant has kept the ground protect them
    // forever, and the harshest outcome in the institution becomes one that
    // never happens -- which is its own kind of decoration.
    weight -= std::min(24, home->quartersBehind * 4);
    // What the Duke's own conduct costs him. A Duke behind on his own
    // charge-rent pleads badly.
    weight -= (plot.flameStanding - kFlameStandingStart) / 4;
    weight += std::min(30, plot.dukeArrears / 8);
    // AND THE OFFERING IS NOT IN THIS SUM. It is an offering and not a fee.
    // The only thing it touches is the Mission's own purse, and the case that
    // says so drives two identical hearings with different offerings and
    // requires the same verdict out of both.
    out.weight = weight;

    // A verdict is not a coin flip, but it is not a lookup table either: the
    // priest is a man, and the same case on two mornings is not guaranteed the
    // same answer. The draw shifts the weight by up to a tenth of the scale.
    const std::int32_t jitter = static_cast<std::int32_t>(draw % 21U) - 10;
    const std::int32_t scored = weight + jitter;

    // SIX OUTCOMES, AND ONLY ONE OF THEM IS EVICTION. That ratio is section
    // 2.8's, and it is the whole character of the institution.
    if (plot.dukeArrears >= kDukeArrearsRevokedAt && plot.flameStanding <= 10) {
        // Available only AGAINST the Duke, when his own arrears and conduct
        // bottom out.
        out.verdict = Verdict::ChargeRevoked;
    } else if (scored >= 55) {
        out.verdict = Verdict::Dismissed;
    } else if (scored >= 35) {
        out.verdict = Verdict::Stay;
    } else if (scored >= 18) {
        out.verdict = Verdict::Abatement;
    } else if (scored >= 0) {
        out.verdict = Verdict::BondOrdered;
    } else {
        out.verdict = Verdict::Distraint;
    }

    if (out.verdict == Verdict::Distraint) {
        // AND THE ROOF GOES WITH IT. A hearing that saves one family can turn
        // out six who never spoke: the lodgers above them were never parties
        // to the case. The Flame weighs the tenant; nobody weighs the lodgers.
        for (const Household& other : households_) {
            if (other.kind == HouseKind::RoofHut && other.landlord == householdId &&
                !other.roofed) {
                ++out.lodgersTurnedOut;
            }
        }
    }

    out.line = std::string(verdictName(out.verdict));
    return out;
}

void Ward::apply(const Hearing& hearing) {
    if (!hearing.heard) {
        return;
    }
    lastHearing_ = hearing;
    ++stats_.petitions;
    stats_.verdicts[static_cast<std::size_t>(hearing.verdict)] += 1;
    Household* home = householdAt(hearing.household);
    Plot& plot = plots_[static_cast<std::size_t>(hearing.plot)];
    if (home == nullptr) {
        return;
    }
    // The offering lands in the Mission's hands whatever the answer was. It
    // leaves the Duke's purse either way, which is what makes it an offering.
    const std::int32_t offering = std::min(plot.dukeCoin, hearing.offering);
    plot.dukeCoin -= offering;

    switch (hearing.verdict) {
        case Verdict::Dismissed:
            // The petition refused and the Duke warned.
            plot.flameStanding = std::max(kFlameStandingMin, plot.flameStanding - 8);
            break;
        case Verdict::Stay:
            // One quarter's grace, arrears frozen. Frozen is all it is: the
            // debt does not go away, it stops growing for a quarter.
            home->stayQuarters = std::max(home->stayQuarters, 1);
            break;
        case Verdict::Abatement:
            // The priest permanently reduces the ground penny. The Duke eats
            // it forever -- the sharpest instrument in the ward.
            home->groundPenny = std::max(1, home->groundPenny * 3 / 4);
            home->arrears = home->arrears / 2;
            break;
        case Verdict::BondOrdered:
            // The tenant keeps the house and the Duke takes the labour.
            home->bondholder = home->plot;
            home->bondOrdered = true;
            home->bondDaysServed = 0;
            ++stats_.bondsTaken;
            break;
        case Verdict::Distraint: {
            // The house passes to the Duke. The family is ROOFED: turned out
            // of the house and onto a roof deck, or into the Gullet.
            home->roofed = true;
            home->kind = HouseKind::RoofHut;
            home->groundPenny = 0;
            home->arrears = 0;
            home->roofRent = raws_.plots()[static_cast<std::size_t>(plot.raw)].roofRent;
            home->landlord = -1;
            ++stats_.housesDistrained;
            for (Household& other : households_) {
                if (other.kind == HouseKind::RoofHut && other.landlord == hearing.household &&
                    !other.roofed) {
                    other.landlord = -1;
                    other.roofed = true;
                    ++stats_.lodgersTurnedOut;
                }
            }
            break;
        }
        case Verdict::ChargeRevoked:
            // The Flame strips the plot's charge and re-lets it. Vacant until
            // somebody -- an actor, or the player -- petitions for it.
            plot.tenure = Tenure::Vacant;
            plot.playerIsDuke = false;
            plot.dukeArrears = 0;
            plot.flameStanding = kFlameStandingStart;
            break;
    }
}

// ---------------------------------------------------------------------------
// the player
// ---------------------------------------------------------------------------

TenureResult Ward::leaseRoof(std::int32_t plotIndex) {
    if (plotIndex < 0 || static_cast<std::size_t>(plotIndex) >= plots_.size()) {
        return TenureResult::NoSuchThing;
    }
    if (playerHousehold_ >= 0) {
        return TenureResult::AlreadyHeld;
    }
    const PlotRaw& raw = raws_.plots()[static_cast<std::size_t>(plots_[
        static_cast<std::size_t>(plotIndex)].raw)];
    if (raw.roofHuts <= 0) {
        // C1 has no rooftop slum, and section 2.8 rules that is a choice by her
        // house-owners rather than a fact about the map. There is nothing to
        // lease.
        return TenureResult::NoSuchThing;
    }
    if (playerCoin_ < kPlayerRoofLeaseQuarter) {
        return TenureResult::CannotAfford;
    }
    // A ROOF IS LET BY THE HOUSE UNDER IT. Find a house-owner on this plot and
    // rent from them; the Duke is not a party to this and never sees the coin.
    std::int32_t landlord = -1;
    for (const std::int32_t id : plots_[static_cast<std::size_t>(plotIndex)].residents) {
        const Household* home = householdAt(id);
        if (home != nullptr && home->ownsHouse()) {
            landlord = id;
            break;
        }
    }
    playerCoin_ -= kPlayerRoofLeaseQuarter;
    Household* owner = householdAt(landlord);
    if (owner != nullptr) {
        owner->coin += kPlayerRoofLeaseQuarter;
    }
    Household home;
    home.id = static_cast<std::int32_t>(households_.size());
    home.plot = plotIndex;
    home.kind = HouseKind::RoofHut;
    home.heads = 1;
    home.coin = 0;
    home.food = 2;
    home.roofRent = raw.roofRent;
    home.landlord = landlord;
    home.player = true;
    playerHousehold_ = home.id;
    plots_[static_cast<std::size_t>(plotIndex)].residents.push_back(home.id);
    households_.push_back(home);
    return TenureResult::Done;
}

TenureResult Ward::buyHouse(std::int32_t plotIndex) {
    if (plotIndex < 0 || static_cast<std::size_t>(plotIndex) >= plots_.size()) {
        return TenureResult::NoSuchThing;
    }
    const Plot& plot = plots_[static_cast<std::size_t>(plotIndex)];
    const PlotRaw& raw = raws_.plots()[static_cast<std::size_t>(plot.raw)];
    const std::int32_t price =
        raw.mansionHouseholds > 0 ? kHousePriceMansion : kHousePriceCondo;
    if (playerCoin_ < price) {
        return TenureResult::CannotAfford;
    }
    playerCoin_ -= price;
    Household* existing = householdAt(playerHousehold_);
    if (existing != nullptr) {
        // Moving up off the roof. The lease ends; the house is bought.
        existing->kind = HouseKind::Condo;
        existing->roofed = false;
        existing->roofRent = 0;
        existing->landlord = -1;
        existing->plot = plotIndex;
        existing->groundPenny = raw.groundPenny;
        return TenureResult::Done;
    }
    Household home;
    home.id = static_cast<std::int32_t>(households_.size());
    home.plot = plotIndex;
    home.kind = HouseKind::Condo;
    home.heads = 1;
    home.coin = 0;
    home.food = 3;
    home.groundPenny = raw.groundPenny;
    home.player = true;
    playerHousehold_ = home.id;
    plots_[static_cast<std::size_t>(plotIndex)].residents.push_back(home.id);
    households_.push_back(home);
    // AND EVERY ROOF HUT ON THAT PLOT WITHOUT A LANDLORD IS NOW THE PLAYER'S.
    // Section 2.8: every mansion-poor house-owner becomes a petty landlord,
    // and the player is not exempt from an institution just for being the
    // player.
    for (Household& other : households_) {
        if (other.plot == plotIndex && other.kind == HouseKind::RoofHut &&
            other.landlord < 0 && !other.player) {
            other.landlord = playerHousehold_;
        }
    }
    return TenureResult::Done;
}

TenureResult Ward::petitionForCharge(std::int32_t plotIndex) {
    if (plotIndex < 0 || static_cast<std::size_t>(plotIndex) >= plots_.size()) {
        return TenureResult::NoSuchThing;
    }
    Plot& plot = plots_[static_cast<std::size_t>(plotIndex)];
    if (plot.tenure == Tenure::Glebe) {
        // Church ground never let to anyone. There is nothing to petition for.
        return TenureResult::NoCause;
    }
    if (plot.tenure != Tenure::Vacant) {
        return TenureResult::NotVacant;
    }
    if (plot.playerIsDuke) {
        return TenureResult::AlreadyHeld;
    }
    const PlotRaw& raw = raws_.plots()[static_cast<std::size_t>(plot.raw)];
    // The Flame does not sell a plot; it re-lets a charge, and the first
    // quarter's charge-rent is what a new Duke puts up.
    if (playerCoin_ < raw.chargeRent) {
        return TenureResult::CannotAfford;
    }
    playerCoin_ -= raw.chargeRent;
    plot.tenure = Tenure::Charged;
    plot.playerIsDuke = true;
    plot.dukeArrears = 0;
    plot.flameStanding = kFlameStandingStart;
    // And the ground under every house-owner on the plot is suddenly let from
    // somebody who is standing there.
    for (Household& home : households_) {
        if (home.plot == plotIndex && home.ownsHouse() && !home.player) {
            home.groundPenny = raw.groundPenny;
        }
    }
    return TenureResult::Done;
}

std::int32_t Ward::collectRent() {
    const std::int32_t taken = playerRentHeld_;
    playerRentHeld_ = 0;
    playerCoin_ += taken;
    return taken;
}

Hearing Ward::petitionAgainst(std::int32_t householdId, std::int32_t offering) {
    Hearing out;
    const Household* home = householdAt(householdId);
    if (home == nullptr) {
        return out;
    }
    Plot& plot = plots_[static_cast<std::size_t>(home->plot)];
    if (!plot.playerIsDuke) {
        // A Duke brings a petition about HIS OWN ground. Anybody else has no
        // standing to be heard at all.
        return out;
    }
    if (home->arrears <= 0) {
        return out;
    }
    const std::uint64_t draw = rng_.draw(static_cast<std::uint64_t>(householdId) ^ 0x50455449ULL,
                                         static_cast<std::int32_t>(day_ % 4096));
    // The player's offering comes out of the player's purse; a Duke's comes out
    // of the plot's. Both are offerings and neither buys anything.
    const std::int32_t paid = std::min(playerCoin_, std::max(0, offering));
    playerCoin_ -= paid;
    plot.dukeCoin += paid;
    out = weighPetition(home->plot, householdId, paid, draw);
    apply(out);
    return out;
}

TenureResult Ward::goBondsworn() {
    Household* home = householdAt(playerHousehold_);
    if (home == nullptr) {
        return TenureResult::NoSuchThing;
    }
    if (home->bonded()) {
        return TenureResult::AlreadyHeld;
    }
    if (home->arrears <= 0) {
        // Nothing owed. A bond is a debt relation and there is no debt.
        return TenureResult::NoCause;
    }
    Plot& plot = plots_[static_cast<std::size_t>(home->plot)];
    if (plot.tenure == Tenure::Glebe || plot.tenure == Tenure::Vacant) {
        return TenureResult::NoSuchThing;
    }
    home->bondholder = home->plot;
    home->bondOrdered = false;
    home->bondDaysServed = 0;
    ++stats_.bondsTaken;
    return TenureResult::Done;
}

TenureResult Ward::transferBond(std::int32_t householdId, std::int32_t toPlotIndex) {
    Household* home = householdAt(householdId);
    if (home == nullptr || toPlotIndex < 0 ||
        static_cast<std::size_t>(toPlotIndex) >= plots_.size()) {
        return TenureResult::NoSuchThing;
    }
    if (!home->bonded()) {
        // There is no paper to buy.
        return TenureResult::NoCause;
    }
    if (home->bondOrdered) {
        // A COURT-ORDERED BOND CANNOT BE SOLD ON TO ANYONE ELSE. Section 2.8,
        // and it is the one limit on the instrument: a priest who ordered a
        // bond ordered it against a named Duke, and the tenant did not choose
        // to be anybody's holding.
        return TenureResult::AlreadyHeld;
    }
    if (home->bondholder == toPlotIndex) {
        return TenureResult::AlreadyHeld;
    }
    const Plot& buyer = plots_[static_cast<std::size_t>(toPlotIndex)];
    if (buyer.tenure == Tenure::Glebe || buyer.tenure == Tenure::Vacant) {
        // Nobody holds that ground, so nobody can hold paper on it.
        return TenureResult::NoSuchThing;
    }
    home->bondholder = toPlotIndex;
    home->bondDaysServed = 0;
    return TenureResult::Done;
}

// ---------------------------------------------------------------------------
// the report
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::string dec(std::int64_t value) {
    return std::to_string(value);
}

[[nodiscard]] std::string permilleText(std::int32_t permille) {
    return std::to_string(permille / 10) + "." + std::to_string(permille % 10) + "%";
}

[[nodiscard]] std::string pad(std::string text, std::size_t width) {
    while (text.size() < width) {
        text.push_back(' ');
    }
    return text;
}

}  // namespace

std::string wardReport(const Ward& ward) {
    const WardStats& stats = ward.stats();
    std::string out;
    out += "the compounds of the Docks -- " + dec(stats.days) + " days\n";
    out += "  plots        " + dec(static_cast<std::int64_t>(ward.plots().size())) +
           "   households " + dec(static_cast<std::int64_t>(ward.households().size())) +
           "   heads " + dec(ward.heads()) + "\n";
    out += "\n  THE LAND\n";
    for (std::size_t p = 0; p < ward.plots().size(); ++p) {
        const Plot& plot = ward.plots()[p];
        const PlotRaw& raw = ward.raws().plots()[static_cast<std::size_t>(plot.raw)];
        std::int32_t heads = 0;
        std::int32_t bonded = 0;
        std::int32_t starving = 0;
        for (const Household& home : ward.households()) {
            if (home.plot != static_cast<std::int32_t>(p)) {
                continue;
            }
            heads += home.heads;
            bonded += home.bonded() ? home.heads : 0;
            starving += home.starving() ? home.heads : 0;
        }
        out += "    " + pad(raw.name, 18) + pad(std::string(tenureName(plot.tenure)), 9) +
               "beds " + pad(dec(static_cast<std::int64_t>(plot.beds.size())), 4) + "hands " +
               pad(dec(ward.farmHands(static_cast<std::int32_t>(p))), 4) + "larder " +
               pad(dec(plot.larder), 6) + "heads " + pad(dec(heads), 5) + "bonded " +
               pad(dec(bonded), 5) + "starving " + pad(dec(starving), 5) + "harvests " +
               pad(dec(plot.harvests), 6) + "failed " + dec(plot.bedsFailed) + "\n";
    }
    out += "\n  THE FOOD\n";
    out += "    grown        " + dec(stats.grown) + "\n";
    out += "    imported     " + dec(stats.importedToMarket) + "\n";
    out += "    eaten        " + dec(stats.eaten) + "\n";
    out += "      courtyard  " + dec(stats.fromCourtyard) + "\n";
    out += "      market     " + dec(stats.boughtAtMarket) + "\n";
    out += "      keep       " + dec(stats.fromKeep) + "\n";
    out += "      alms       " + dec(stats.fromAlms) + "\n";
    out += "    over cap     " + dec(stats.spoiledOverCap) + "\n";
    out += "    stored now   " + dec(ward.stored()) + "  (market " + dec(ward.marketStock()) +
           ")\n";
    out += "    harvests     " + dec(stats.harvests) + "   beds failed " + dec(stats.bedsFailed) +
           "\n";
    out += "    bed-tends    " + dec(stats.bedTendsDone) + " of " + dec(stats.bedTendsWanted) +
           " wanted\n";
    out += "\n  THE GROUND\n";
    out += "    quarters     " + dec(stats.quarters) + "\n";
    out += "    pennies      " + dec(stats.penniesPaid) + " paid, " + dec(stats.penniesShort) +
           " short\n";
    out += "    roof rent    " + dec(stats.roofRentPaid) + "\n";
    out += "    charge-rent  " + dec(stats.chargeRentPaid) + "\n";
    out += "    bonds        " + dec(stats.bondsTaken) + " taken, " + dec(stats.bondsDischarged) +
           " discharged\n";
    out += "    petitions    " + dec(stats.petitions) + "\n";
    for (std::size_t v = 0; v < 6; ++v) {
        out += "      " + pad(std::string(verdictName(static_cast<Verdict>(v))), 12) +
               dec(stats.verdicts[v]) + "\n";
    }
    out += "    distrained   " + dec(stats.housesDistrained) + " houses, " +
           dec(stats.lodgersTurnedOut) + " lodgers turned out with them\n";
    out += "\n  THE BELLY\n";
    out += "    head-days    " + dec(stats.headDays) + "\n";
    out += "    hungry       " + dec(stats.headDaysHungry) + "  (" +
           permilleText(stats.hungerPermille()) + ")\n";
    out += "    STARVING     " + dec(stats.headDaysStarving) + "  (" +
           permilleText(stats.starvationPermille()) + ", bar is " +
           permilleText(kStarvationBarPermille) + ")\n";
    out += "    worst day    " + dec(stats.peakStarvingHeads) + " heads\n";
    return out;
}

WardSoakResult runWardSoak(std::int64_t days, const std::filesystem::path& contentDir,
                           std::uint64_t seed) {
    WardSoakResult out;
    const NotableRegistry who = NotableRegistry::load(contentDir);
    Ward ward(seed, contentDir, who);
    if (!ward.loaded()) {
        out.report = "the roll is empty: content/raws/compounds/compounds.json did not load\n";
        out.problem = "no roll";
        return out;
    }
    for (std::int64_t day = 0; day < std::max<std::int64_t>(1, days); ++day) {
        ward.endOfDay();
    }
    out.report = wardReport(ward);
    out.days = ward.stats().days;
    out.starvationPermille = ward.stats().starvationPermille();
    out.hungerPermille = ward.stats().hungerPermille();

    if (out.starvationPermille > kStarvationBarPermille) {
        out.problem = "the ward starved: " + permilleText(out.starvationPermille) + " over " +
                      permilleText(kStarvationBarPermille);
        return out;
    }
    // AND IT MUST NOT DROWN IN SURPLUS EITHER. A courtyard whose larder sits at
    // its cap for the whole soak is not a farm, it is a faucet, and an economy
    // with nothing scarce in it is not balanced.
    std::int32_t full = 0;
    for (const Plot& plot : ward.plots()) {
        full += plot.larder >= kCourtyardLarderCap ? 1 : 0;
    }
    if (!ward.plots().empty() &&
        full == static_cast<std::int32_t>(ward.plots().size())) {
        out.problem = "every courtyard larder is at its cap: nothing in this ward is scarce";
        return out;
    }
    // And the land has to be doing real work. A ward fed entirely by the quay
    // would pass the starvation bar with every bed dead.
    if (ward.stats().grown <= 0 || ward.stats().fromCourtyard * 8 < ward.stats().eaten) {
        out.problem = "the courtyards fed almost nobody: the farm is decoration";
        return out;
    }
    out.passed = true;
    return out;
}

// ---------------------------------------------------------------------------
// the hash
// ---------------------------------------------------------------------------

void Ward::hash_into(HashSink& sink) const {
    sink.put_long(static_cast<std::uint64_t>(day_));
    sink.put_long(static_cast<std::uint64_t>(seconds_));
    sink.put_int(static_cast<std::uint32_t>(market_));
    sink.put_int(static_cast<std::uint32_t>(playerCoin_));
    sink.put_int(static_cast<std::uint32_t>(playerHousehold_));
    sink.put_int(static_cast<std::uint32_t>(playerRentHeld_));
    sink.put_int(static_cast<std::uint32_t>(plots_.size()));
    for (const Plot& plot : plots_) {
        put_string(sink, raws_.plots()[static_cast<std::size_t>(plot.raw)].id);
        sink.put_byte(static_cast<std::uint32_t>(plot.tenure));
        sink.put_int(static_cast<std::uint32_t>(plot.larder));
        sink.put_int(static_cast<std::uint32_t>(plot.dukeCoin));
        sink.put_int(static_cast<std::uint32_t>(plot.dukeArrears));
        sink.put_int(static_cast<std::uint32_t>(plot.flameStanding));
        sink.put_byte(plot.playerIsDuke ? 1U : 0U);
        sink.put_int(static_cast<std::uint32_t>(plot.beds.size()));
        for (const CropBed& bed : plot.beds) {
            sink.put_int(static_cast<std::uint32_t>(bed.age));
            sink.put_int(static_cast<std::uint32_t>(bed.tends));
        }
    }
    sink.put_int(static_cast<std::uint32_t>(households_.size()));
    for (const Household& home : households_) {
        sink.put_int(static_cast<std::uint32_t>(home.id));
        sink.put_int(static_cast<std::uint32_t>(home.plot));
        sink.put_byte(static_cast<std::uint32_t>(home.kind));
        sink.put_int(static_cast<std::uint32_t>(home.heads));
        sink.put_int(static_cast<std::uint32_t>(home.coin));
        sink.put_int(static_cast<std::uint32_t>(home.food));
        sink.put_int(static_cast<std::uint32_t>(home.groundPenny));
        sink.put_int(static_cast<std::uint32_t>(home.roofRent));
        sink.put_int(static_cast<std::uint32_t>(home.landlord));
        sink.put_int(static_cast<std::uint32_t>(home.arrears));
        sink.put_int(static_cast<std::uint32_t>(home.stayQuarters));
        sink.put_int(static_cast<std::uint32_t>(home.quartersBehind));
        sink.put_int(static_cast<std::uint32_t>(home.bondholder));
        sink.put_int(static_cast<std::uint32_t>(home.bondDaysServed));
        sink.put_int(static_cast<std::uint32_t>(home.discharges));
        sink.put_int(static_cast<std::uint32_t>(home.hungryDays));
        sink.put_int(static_cast<std::uint32_t>(home.quartersKept));
        sink.put_byte(home.roofed ? 1U : 0U);
        sink.put_byte(home.bondOrdered ? 1U : 0U);
        sink.put_byte(home.player ? 1U : 0U);
    }
}

}  // namespace granadad::sim
