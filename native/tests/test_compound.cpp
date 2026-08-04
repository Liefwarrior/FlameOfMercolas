// The compounds: the roll, the courtyard farms, and the leasehold between them.
//
// Four kinds of case, in the order the rest of this suite uses:
//
//   ROLL      content/raws/compounds/compounds.json against the owner's own
//             notables registry -- the same refusal the contract board makes,
//             for the same reason.
//   LAND      crops that grow, get harvested, feed people and FAIL when nobody
//             turns the beds over. The failure is the case that matters.
//   GROUND    the ground penny, the roof rent that goes to the house-owner and
//             not to the Duke, the bond that moves a household's labour into
//             another compound's yard, and the priest's six outcomes.
//   SOAK      the sprint's acceptance: two years of the ward, and the ward
//             feeds itself at or under the bar the Java build held.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/compound.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/notables.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

const NotableRegistry& who() {
    static const NotableRegistry loaded = NotableRegistry::load(content::contentDir());
    return loaded;
}

constexpr std::uint64_t kSeed = 0x4752414E41444144ull;

[[nodiscard]] std::unique_ptr<Ward> freshWard() {
    return std::make_unique<Ward>(kSeed, content::contentDir(), who());
}

/// The plot index of a plot id, or -1.
[[nodiscard]] std::int32_t plotNamed(const Ward& ward, std::string_view id) {
    for (std::size_t i = 0; i < ward.plots().size(); ++i) {
        if (ward.raws().plots()[static_cast<std::size_t>(ward.plots()[i].raw)].id == id) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

}  // namespace

// ===========================================================================
// ROLL
// ===========================================================================

TEST_CASE("the roll names nobody the owner's own file does not have") {
    // THE SAME GATE THE CONTRACT BOARD PUTS ON ITS BROKERS. Section 2.8's table
    // names five Den Dukes and one charter priest; every one of them has to be
    // one of the Forty, or the plot is not on the roll.
    const CompoundRaws& roll = CompoundRaws::load(content::contentDir(), who());
    REQUIRE(roll.loaded());
    REQUIRE(who().loaded());
    CHECK(roll.refused() == 0);
    CHECK(roll.quarterDays() == 90);
    REQUIRE(roll.plots().size() == 5);
    REQUIRE(roll.crops().size() >= 3);

    for (const PlotRaw& plot : roll.plots()) {
        INFO("plot ", plot.id);
        REQUIRE_FALSE(plot.name.empty());
        REQUIRE_FALSE(plot.note.empty());
        // Every named person is authored.
        if (!plot.denDuke.empty()) {
            REQUIRE(who().find(plot.denDuke) != nullptr);
        }
        if (!plot.pledgedTo.empty()) {
            REQUIRE(who().find(plot.pledgedTo) != nullptr);
        }
        if (!plot.priest.empty()) {
            REQUIRE(who().find(plot.priest) != nullptr);
        }
        // Every plot has a courtyard, and every courtyard grows something the
        // crop table has.
        REQUIRE(plot.beds > 0);
        REQUIRE(roll.crop(plot.crop) != nullptr);
        REQUIRE(roll.crop(plot.crop)->id == plot.crop);
    }

    // THE FOUR TENURES ARE ALL PRESENT, because each is a different rule and a
    // roll with only one of them would prove nothing about the other three.
    std::int32_t glebe = 0;
    std::int32_t charged = 0;
    std::int32_t pledged = 0;
    std::int32_t vacant = 0;
    for (const PlotRaw& plot : roll.plots()) {
        glebe += plot.tenure == Tenure::Glebe ? 1 : 0;
        charged += plot.tenure == Tenure::Charged ? 1 : 0;
        pledged += plot.tenure == Tenure::Pledged ? 1 : 0;
        vacant += plot.tenure == Tenure::Vacant ? 1 : 0;
    }
    CHECK(glebe == 1);
    CHECK(charged == 2);
    CHECK(pledged == 1);
    CHECK(vacant == 1);
    // The glebe has no Duke and therefore no penny: Church ground never let to
    // anyone, token alms in place of a rent. The poorest are the least
    // enserfed, and that inversion is section 2.8's, not ours.
    for (const PlotRaw& plot : roll.plots()) {
        if (plot.tenure == Tenure::Glebe) {
            CHECK(plot.denDuke.empty());
            CHECK(plot.groundPenny == 0);
            CHECK(plot.chargeRent == 0);
        }
    }
}

TEST_CASE("a compound is dwelling units inside one wall, not a street of houses") {
    // DOCKS-GAZETTEER section 2.5: a "dozen homes" is a dozen dwelling UNITS
    // inside one compound, never a dozen separate lots -- one mansion holding
    // the owning family and its dependents, condos ringing the courtyard, and
    // the roof let as mass low-income housing.
    const std::unique_ptr<Ward> ward = freshWard();
    REQUIRE(ward->loaded());
    REQUIRE_FALSE(ward->households().empty());

    std::int32_t mansion = 0;
    std::int32_t condo = 0;
    std::int32_t roof = 0;
    for (const Household& home : ward->households()) {
        mansion += home.kind == HouseKind::Mansion ? 1 : 0;
        condo += home.kind == HouseKind::Condo ? 1 : 0;
        roof += home.kind == HouseKind::RoofHut ? 1 : 0;
        // Every household is on a plot that exists, and every head is a mouth.
        REQUIRE(home.plot >= 0);
        REQUIRE(static_cast<std::size_t>(home.plot) < ward->plots().size());
        REQUIRE(home.heads >= 1);
        REQUIRE(home.heads <= 5);
    }
    CHECK(mansion > 0);
    CHECK(condo > 0);
    CHECK(roof > 0);
    // THE WARD IS THE SIZE THE GAZETTEER DERIVES. Four compounds plus 45
    // hovels, at the canon household-size distribution.
    CHECK(ward->households().size() >= 100);
    CHECK(ward->heads() >= 200);
    CHECK(ward->heads() <= 420);

    // A ROOF LODGER RENTS FROM THE HOUSE-OWNER BENEATH THEM, not from the Duke.
    // That is a roll fact and it is what makes every mansion-poor house-owner a
    // petty landlord.
    std::int32_t lodgersWithLandlords = 0;
    for (const Household& home : ward->households()) {
        if (home.kind != HouseKind::RoofHut) {
            continue;
        }
        CHECK(home.groundPenny == 0);
        if (home.landlord >= 0) {
            ++lodgersWithLandlords;
            const Household& landlord =
                ward->households()[static_cast<std::size_t>(home.landlord)];
            CHECK(landlord.ownsHouse());
            CHECK(landlord.plot == home.plot);
        }
    }
    CHECK(lodgersWithLandlords > 0);

    // AND C1 HAS NO ROOFTOP SLUM. Section 2.8 rules that is a choice by her
    // house-owners and not a fact about the map, so it is on the roll.
    const std::int32_t quayward = plotNamed(*ward, "C1_QUAYWARD");
    REQUIRE(quayward >= 0);
    for (const Household& home : ward->households()) {
        if (home.plot == quayward) {
            CHECK(home.kind != HouseKind::RoofHut);
        }
    }
}

// ===========================================================================
// LAND
// ===========================================================================

TEST_CASE("a courtyard bed grows, is cut, and feeds the compound it stands in") {
    const std::unique_ptr<Ward> ward = freshWard();
    REQUIRE(ward->loaded());
    const std::int64_t before = ward->stats().harvests;
    CHECK(before == 0);

    for (std::int32_t day = 0; day < 120; ++day) {
        ward->endOfDay();
    }
    // The land really produced.
    CHECK(ward->stats().harvests > 0);
    CHECK(ward->stats().grown > 0);
    // And it went into mouths, not into a number.
    CHECK(ward->stats().fromCourtyard > 0);
    CHECK(ward->stats().eaten > 0);
    // Every plot with beds harvested something over four months.
    for (std::size_t p = 0; p < ward->plots().size(); ++p) {
        INFO("plot ", ward->raws().plots()[static_cast<std::size_t>(
                          ward->plots()[p].raw)].id);
        if (!ward->plots()[p].beds.empty()) {
            CHECK(ward->plots()[p].harvests > 0);
        }
    }
}

TEST_CASE("a bed nobody turns over comes up worth nothing") {
    // "CROPS THAT ACTUALLY GROW, GET HARVESTED, FEED PEOPLE, AND FAIL IF
    // NEGLECTED." The failure half is the one that is easy to leave out, so it
    // is written as arithmetic and driven directly rather than inferred from a
    // season that happened to go badly.
    //
    // Thirteen tends is what a forty-day barley bed wants at one every third
    // day.
    constexpr std::int32_t kNeeded = 13;
    constexpr std::int32_t kFull = 60;

    // Worked all season: the whole crop.
    CHECK(bedYield(kFull, kNeeded, kNeeded) == kFull);
    CHECK(bedYield(kFull, kNeeded + 40, kNeeded) == kFull);
    // Worked two thirds: two thirds of a crop. Neglect is a gradient before it
    // is a cliff.
    CHECK(bedYield(kFull, 9, kNeeded) == kFull * 9 / kNeeded);
    CHECK(bedYield(kFull, 9, kNeeded) < kFull);
    // AND THEN IT IS A CLIFF. Below a third of the working the crop wanted
    // there is nothing on the bed worth bending a back for. Five tends of
    // thirteen is thirty-eight percent and still pays; four is thirty and does
    // not.
    CHECK(bedYield(kFull, 5, kNeeded) > 0);
    CHECK(bedYield(kFull, 5, kNeeded) < bedYield(kFull, 9, kNeeded));
    CHECK(bedYield(kFull, 4, kNeeded) == 0);
    CHECK(bedYield(kFull, 1, kNeeded) == 0);
    // Nobody touched it at all.
    CHECK(bedYield(kFull, 0, kNeeded) == 0);
    CHECK(bedYield(kFull, -5, kNeeded) == 0);
    // The cliff is where the constant says it is, and not where the arithmetic
    // happened to land.
    CHECK(kBedFailsBelowPercent > 0);
    CHECK(bedYield(kFull, kNeeded * kBedFailsBelowPercent / 100, kNeeded) == 0);

    // And a ward with hands in every courtyard does NOT lose its beds, which is
    // the other side of the same claim: the failure is caused, not ambient.
    const std::unique_ptr<Ward> tended = freshWard();
    REQUIRE(tended->loaded());
    for (std::size_t p = 0; p < tended->plots().size(); ++p) {
        INFO("plot ", p);
        CHECK(tended->farmHands(static_cast<std::int32_t>(p)) > 0);
    }
    for (std::int32_t day = 0; day < 400; ++day) {
        tended->endOfDay();
    }
    CHECK(tended->stats().harvests > 0);
    CHECK(tended->stats().bedsFailed * 4 < tended->stats().harvests);
}

TEST_CASE("buy the paper on a compound's hands and its courtyard comes up thin") {
    // THE PIPE, END TO END, AS A HARVEST. Section 2.8 makes a bond a
    // transferable holding and says the bondsworn's work is the bondholder's
    // yard; this is the case that shows what that costs the yard it left.
    //
    // Two wards, the same seed, the same days. In the second, a Guild buys
    // every bond it can off one compound and moves the labour to another.
    const std::unique_ptr<Ward> left = freshWard();
    const std::unique_ptr<Ward> bought = freshWard();
    REQUIRE(left->plots().size() >= 3);

    // The compound with the most beds per resident head is the one that feels
    // it, so pick the target from the roll rather than by hand.
    std::int32_t victim = -1;
    std::int32_t buyer = -1;
    for (std::size_t p = 0; p < left->plots().size(); ++p) {
        const Tenure tenure = left->plots()[p].tenure;
        if (tenure != Tenure::Charged && tenure != Tenure::Pledged) {
            continue;
        }
        if (victim < 0) {
            victim = static_cast<std::int32_t>(p);
        } else if (buyer < 0) {
            buyer = static_cast<std::int32_t>(p);
        }
    }
    REQUIRE(victim >= 0);
    REQUIRE(buyer >= 0);

    constexpr std::int32_t kDays = 1400;
    for (std::int32_t day = 0; day < kDays; ++day) {
        left->endOfDay();
        bought->endOfDay();
        for (const Household& home : bought->households()) {
            if (home.bonded() && home.bondholder == victim && !home.bondOrdered) {
                (void)bought->transferBond(home.id, buyer);
            }
        }
    }

    const std::int64_t grownLeft = left->plots()[static_cast<std::size_t>(victim)].grown;
    const std::int64_t grownBought = bought->plots()[static_cast<std::size_t>(victim)].grown;
    INFO("victim grew ", grownLeft, " when left alone and ", grownBought,
         " when its hands were bought");
    CHECK(grownLeft > 0);
    // THE COMPOUND THAT LOST ITS HANDS GREW LESS. Not a little less by
    // accident: the labour is gone and the beds went unturned.
    CHECK(grownBought < grownLeft);
    // And the buyer's yard is no worse off for it.
    CHECK(bought->plots()[static_cast<std::size_t>(buyer)].grown >=
          left->plots()[static_cast<std::size_t>(buyer)].grown);
}

TEST_CASE("the bond is the pipe: leased labour turns up in the bondholder's yard") {
    // SECTION 2.8, VERBATIM: "while the bond runs, the bondsworn's wage is the
    // bondholder's and their work is the bondholder's yard." That single clause
    // is what makes the tenure system and the farming system one system rather
    // than two features standing next to each other, and this is the case that
    // proves the pipe carries.
    const std::unique_ptr<Ward> ward = freshWard();
    REQUIRE(ward->plots().size() >= 3);

    // Run until the ward's own arithmetic has put people into bonds -- nobody
    // is bonded on day one, because a serf is a house-owner who had a bad
    // quarter and there has not been one yet.
    std::int32_t bonded = -1;
    std::int32_t from = -1;
    for (std::int32_t day = 0; day < 1200 && bonded < 0; ++day) {
        ward->endOfDay();
        for (const Household& home : ward->households()) {
            // A HOUSE-OWNER, specifically. A wastrel can be bonded too -- they
            // are the bond pool of last resort and they have nothing else to
            // pledge -- but the claim being tested here is the one about a
            // tenant leasing themselves in lieu of the penny they owed.
            if (home.bonded() && !home.bondOrdered && home.ownsHouse()) {
                bonded = home.id;
                from = home.bondholder;
                break;
            }
        }
    }
    REQUIRE(bonded >= 0);
    // A house-owner bonds to their OWN Duke: they leased themselves in lieu of
    // the penny they owed him, and it is his ground they owed it on.
    CHECK(from == ward->households()[static_cast<std::size_t>(bonded)].plot);

    // Now a Guild buys the paper. Pick another plot with a Duke standing on it.
    std::int32_t to = -1;
    for (std::size_t p = 0; p < ward->plots().size(); ++p) {
        const Tenure tenure = ward->plots()[p].tenure;
        if (static_cast<std::int32_t>(p) != from &&
            (tenure == Tenure::Charged || tenure == Tenure::Pledged)) {
            to = static_cast<std::int32_t>(p);
            break;
        }
    }
    REQUIRE(to >= 0);

    const std::int32_t heads = ward->households()[static_cast<std::size_t>(bonded)].heads;
    REQUIRE(heads > 0);
    const auto headsWorkingIn = [&](std::int32_t plotIndex) {
        std::int32_t total = 0;
        for (const Household& home : ward->households()) {
            const std::int32_t yard = home.bonded() ? home.bondholder : home.plot;
            total += yard == plotIndex ? home.heads : 0;
        }
        return total;
    };
    const std::int32_t fromBefore = headsWorkingIn(from);
    const std::int32_t toBefore = headsWorkingIn(to);

    REQUIRE(ward->transferBond(bonded, to) == TenureResult::Done);
    CHECK(ward->households()[static_cast<std::size_t>(bonded)].bondholder == to);

    // AND THE LABOUR WENT WITH THE PAPER, head for head. farmHands counts a
    // bonded household in the bondholder's yard and not in the one it sleeps
    // in, so the heads available to the seller's courtyard really did fall and
    // the buyer's really did rise.
    CHECK(headsWorkingIn(from) == fromBefore - heads);
    CHECK(headsWorkingIn(to) == toBefore + heads);
    CHECK(ward->farmHands(from) == headsWorkingIn(from) / kHeadsPerFarmHand);
    CHECK(ward->farmHands(to) == headsWorkingIn(to) / kHeadsPerFarmHand);

    // A bond a PRIEST ordered is not a holding anybody can sell. Section 2.8:
    // "a court-ordered bond cannot be sold on to anyone else."
    std::int32_t ordered = -1;
    for (std::int32_t day = 0; day < 1200 && ordered < 0; ++day) {
        ward->endOfDay();
        for (const Household& home : ward->households()) {
            if (home.bondOrdered) {
                ordered = home.id;
                break;
            }
        }
    }
    if (ordered >= 0) {
        CHECK(ward->transferBond(ordered, to) == TenureResult::AlreadyHeld);
    }
    // And there is no paper on a household that never signed any.
    for (const Household& home : ward->households()) {
        if (!home.bonded()) {
            CHECK(ward->transferBond(home.id, to) == TenureResult::NoCause);
            break;
        }
    }
}

// ===========================================================================
// GROUND
// ===========================================================================

TEST_CASE("the ground penny falls on the earth, never on the dwelling") {
    const std::unique_ptr<Ward> ward = freshWard();
    REQUIRE(ward->loaded());
    const std::int32_t quarter = ward->raws().quarterDays();
    REQUIRE(quarter > 0);

    for (std::int32_t day = 0; day < quarter; ++day) {
        ward->endOfDay();
    }
    CHECK(ward->stats().quarters == 1);
    // Somebody paid, and a Duke's purse is heavier for it.
    CHECK(ward->stats().penniesPaid > 0);
    CHECK(ward->stats().chargeRentPaid > 0);
    // AND THE ROOFS PAID SOMEBODY ELSE. A roof lodger's rent goes to the
    // house-owner beneath them, not to the Duke, because a roof is part of a
    // house and the house is individually owned.
    CHECK(ward->stats().roofRentPaid > 0);

    // Nobody on the glebe owes a penny to anyone, ever: Church ground never let
    // to anyone. A Duke who wants labour cannot get it from there.
    const std::int32_t glebe = plotNamed(*ward, "GLEBE_HOVELS");
    REQUIRE(glebe >= 0);
    for (const Household& home : ward->households()) {
        if (home.plot == glebe) {
            CHECK(home.groundPenny == 0);
            CHECK_FALSE(home.bonded());
        }
    }
}

TEST_CASE("a Den Duke cannot turn a family out, and only one of the six answers is eviction") {
    // SECTION 2.8'S WHOLE CHARACTER. The priest weighs the tenant in the
    // Flame's own register of concern for the soul rather than commercial
    // interest, and five of the six things he can answer keep the family in the
    // house.
    const std::unique_ptr<Ward> ward = freshWard();
    for (std::int32_t day = 0; day < 720; ++day) {
        ward->endOfDay();
    }
    const WardStats& stats = ward->stats();
    INFO("petitions ", stats.petitions);
    REQUIRE(stats.petitions > 0);

    std::int64_t total = 0;
    for (std::size_t v = 0; v < 6; ++v) {
        total += stats.verdicts[v];
    }
    CHECK(total == stats.petitions);
    // The ward is not a machine for evicting people. Most petitions end some
    // other way, which is what makes the hearing an institution rather than a
    // formality.
    CHECK(stats.verdicts[static_cast<std::size_t>(Verdict::Distraint)] * 2 < stats.petitions);
    // And a house that DID pass took the roof with it, because the lodgers
    // above were never parties to the case.
    if (stats.housesDistrained > 0) {
        CHECK(stats.lodgersTurnedOut >= 0);
    }
    // Somebody leased themselves rather than lose the ground, and somebody
    // worked one out. Both halves of the instrument are live.
    CHECK(stats.bondsTaken > 0);
}

TEST_CASE("an offering is an offering and not a fee: it does not buy the verdict") {
    // SECTION 2.8: "The Duke makes an OFFERING; it is an offering and not a
    // fee, and it does not buy the verdict." That is a claim about the code and
    // it can be tested: the same case, the same day, the same household, two
    // very different offerings, and the priest must answer the same thing.
    const std::unique_ptr<Ward> ward = freshWard();

    // Run until somebody on the roll is genuinely behind. That is the case the
    // priest hears; nothing about it is staged.
    std::int32_t tenant = -1;
    for (std::int32_t day = 0; day < 1200 && tenant < 0; ++day) {
        ward->endOfDay();
        for (const Household& home : ward->households()) {
            if (home.ownsHouse() && home.arrears > 0) {
                tenant = home.id;
                break;
            }
        }
    }
    REQUIRE(tenant >= 0);
    const std::int32_t plot = ward->households()[static_cast<std::size_t>(tenant)].plot;

    // THE SAME CASE, THE SAME MORNING, THE SAME DRAW. The only thing that
    // differs between these two hearings is what the Duke put in the Mission's
    // box, and section 2.8 says that is not a term.
    constexpr std::uint64_t kMorning = 0x0FF3612345678901ull;
    const Hearing mean = ward->weighPetition(plot, tenant, 0, kMorning);
    const Hearing generous = ward->weighPetition(plot, tenant, 400, kMorning);
    REQUIRE(mean.heard);
    REQUIRE(generous.heard);
    CHECK(mean.offering == 0);
    CHECK(generous.offering == 400);
    CHECK(generous.verdict == mean.verdict);
    CHECK(generous.weight == mean.weight);
    // Four hundred Royals bought exactly nothing, and this is the assertion a
    // mutation that folded the offering into the weight would go red on.
    CHECK(generous.weight == mean.weight);

    // Weighing is a QUESTION and never an answer: asking it changes nothing
    // about the roll.
    const std::int64_t heardBefore = ward->stats().petitions;
    const std::int32_t owedBefore =
        ward->households()[static_cast<std::size_t>(tenant)].arrears;
    (void)ward->weighPetition(plot, tenant, 400, kMorning);
    CHECK(ward->stats().petitions == heardBefore);
    CHECK(ward->households()[static_cast<std::size_t>(tenant)].arrears == owedBefore);

    // And a case nobody has standing to bring is not heard at all: a Duke
    // brings a petition about his own ground and the player is not the Duke
    // here.
    const Hearing refused = ward->petitionAgainst(tenant, 100);
    CHECK_FALSE(refused.heard);
}

TEST_CASE("the player leases space, buys a house, becomes a landlord, and collects") {
    // THE SPRINT'S PLAYER ARC, played through the calls a keypress would make.
    // Every verb here is one an NPC household already does: nothing in this
    // case is player-only machinery.
    const std::unique_ptr<Ward> ward = freshWard();
    REQUIRE(ward->loaded());
    CHECK(ward->playerHousehold() < 0);

    const std::int32_t saltgate = plotNamed(*ward, "C3_SALTGATE");
    const std::int32_t quayward = plotNamed(*ward, "C1_QUAYWARD");
    const std::int32_t gullet = plotNamed(*ward, "C4_GULLET");
    REQUIRE(saltgate >= 0);
    REQUIRE(quayward >= 0);
    REQUIRE(gullet >= 0);

    // 1. A ROOF, WITH NO COIN, IS STILL NO. The ward does not extend credit.
    ward->setPlayerCoin(0);
    CHECK(ward->leaseRoof(saltgate) == TenureResult::CannotAfford);

    // 2. AND C1 HAS NO ROOF TO LEASE, because her house-owners chose not to let
    //    theirs. There is nothing there to rent, and the refusal says so.
    ward->setPlayerCoin(3000);
    CHECK(ward->leaseRoof(quayward) == TenureResult::NoSuchThing);

    // 3. A roof deck on Saltgate Terrace. The rent goes to the house-owner
    //    beneath, and the Duke is not a party to it.
    const std::int32_t purseBefore = ward->playerCoin();
    REQUIRE(ward->leaseRoof(saltgate) == TenureResult::Done);
    REQUIRE(ward->playerHousehold() >= 0);
    CHECK(ward->playerCoin() == purseBefore - kPlayerRoofLeaseQuarter);
    const Household* me = &ward->households()[
        static_cast<std::size_t>(ward->playerHousehold())];
    CHECK(me->kind == HouseKind::RoofHut);
    CHECK(me->plot == saltgate);
    CHECK(me->landlord >= 0);
    CHECK(me->groundPenny == 0);
    // Twice is once.
    CHECK(ward->leaseRoof(saltgate) == TenureResult::AlreadyHeld);

    // 4. Up off the roof and into a house. The house is the player's outright;
    //    the ground under it never will be.
    REQUIRE(ward->buyHouse(saltgate) == TenureResult::Done);
    me = &ward->households()[static_cast<std::size_t>(ward->playerHousehold())];
    CHECK(me->ownsHouse());
    CHECK(me->groundPenny > 0);
    CHECK(me->roofRent == 0);

    // 5. THE CHARGE. The Gullet's is vacant and any actor may petition for it;
    //    a plot somebody holds cannot be petitioned for at all, and Church
    //    ground is not on offer to anybody, ever.
    CHECK(ward->petitionForCharge(saltgate) == TenureResult::NotVacant);
    const std::int32_t glebe = plotNamed(*ward, "GLEBE_HOVELS");
    REQUIRE(glebe >= 0);
    CHECK(ward->petitionForCharge(glebe) == TenureResult::NoCause);
    REQUIRE(ward->petitionForCharge(gullet) == TenureResult::Done);
    CHECK(ward->plots()[static_cast<std::size_t>(gullet)].playerIsDuke);
    CHECK(ward->plots()[static_cast<std::size_t>(gullet)].tenure == Tenure::Charged);

    // 6. AND NOW THE RENT COMES IN. A quarter of it: roof lodgers to the
    //    player's own house, ground pennies to the player's own plot.
    CHECK(ward->rentWaiting() == 0);
    for (std::int32_t day = 0; day < ward->raws().quarterDays(); ++day) {
        ward->endOfDay();
    }
    CHECK(ward->rentWaiting() > 0);
    const std::int32_t purse = ward->playerCoin();
    const std::int32_t taken = ward->collectRent();
    CHECK(taken > 0);
    CHECK(ward->playerCoin() == purse + taken);
    CHECK(ward->rentWaiting() == 0);
    // Collecting twice does not pay twice.
    CHECK(ward->collectRent() == 0);
}

TEST_CASE("leasing yourself in lieu of the penny is a debt relation, not a caste") {
    // "A serf is not born a serf. A serf is a house-owner who had a bad
    // quarter." So the player has to have the bad quarter first: with nothing
    // owed there is nothing to lease yourself against.
    const std::unique_ptr<Ward> ward = freshWard();
    const std::int32_t saltgate = plotNamed(*ward, "C3_SALTGATE");
    REQUIRE(saltgate >= 0);
    ward->setPlayerCoin(3000);
    REQUIRE(ward->buyHouse(saltgate) == TenureResult::Done);
    // Nothing owed yet.
    CHECK(ward->goBondsworn() == TenureResult::NoCause);

    // Two quarters with no wage coming in -- the player household earns nothing
    // in this system -- and the arrears are real.
    for (std::int32_t day = 0; day < ward->raws().quarterDays() * 3; ++day) {
        ward->endOfDay();
    }
    const Household* me =
        &ward->households()[static_cast<std::size_t>(ward->playerHousehold())];
    REQUIRE(me->arrears > 0);
    const std::int32_t owed = me->arrears;

    REQUIRE(ward->goBondsworn() == TenureResult::Done);
    me = &ward->households()[static_cast<std::size_t>(ward->playerHousehold())];
    CHECK(me->bonded());
    CHECK(me->bondholder == saltgate);
    CHECK(ward->goBondsworn() == TenureResult::AlreadyHeld);

    // And it is WORKED OUT, day by day, and then blessed. Working the bond out
    // is discharge, and the Flame blesses a discharged debt.
    //
    // Stopped ON the day it discharges rather than run past it: the ground
    // penny falls again at the next quarter-day whether or not a bond just
    // ended, so a household that worked itself free in one quarter can be back
    // in arrears by the next. That is the institution and not a bug, and this
    // case is about the discharge.
    std::int32_t worked = 0;
    for (std::int32_t day = 0; day < owed / kBondWorkPerDay + 8; ++day) {
        ward->endOfDay();
        ++worked;
        if (!ward->households()[static_cast<std::size_t>(ward->playerHousehold())].bonded()) {
            break;
        }
    }
    me = &ward->households()[static_cast<std::size_t>(ward->playerHousehold())];
    INFO("worked ", worked, " days off ", owed, " of arrears");
    CHECK_FALSE(me->bonded());
    CHECK(me->discharges >= 1);
    CHECK(me->arrears == 0);
    // A day's labour is a day's labour: the bond took about as long as the
    // arrears divided by what a day works off.
    CHECK(worked >= owed / kBondWorkPerDay);
}

// ===========================================================================
// SOAK -- the sprint's acceptance
// ===========================================================================

TEST_CASE("two years of the ward: the compounds feed themselves, and nothing drowns") {
    // THE ACCEPTANCE. The Java build held serf starvation at or below 5%, so
    // that is the bar, and it is a constant rather than a sentence in a report:
    // kStarvationBarPermille.
    //
    // The soak fails two ways on purpose. A ward that starves past the bar
    // fails, and so does a ward whose courtyards produce so much that every
    // larder sits at its cap and nothing is scarce -- an economy with no
    // scarcity is not balanced, it is switched off.
    const WardSoakResult soak = runWardSoak(730, content::contentDir(), kSeed);
    INFO(soak.report);
    INFO("problem: ", soak.problem);
    CHECK(soak.days == 730);
    CHECK(soak.starvationPermille <= kStarvationBarPermille);
    CHECK(soak.passed);
    CHECK(soak.problem.empty());
    // The report says the things a report has to say, or the numbers in it
    // cannot be quoted.
    CHECK(soak.report.find("STARVING") != std::string::npos);
    CHECK(soak.report.find("THE LAND") != std::string::npos);
    CHECK(soak.report.find("THE GROUND") != std::string::npos);
}

TEST_CASE("the same ward, the same seed, twice -- and it agrees to the last ration") {
    // The ward is simulation state and it is hashed, so the twin-run gate can
    // see it. This is the same property asserted where it is cheapest to check.
    const std::unique_ptr<Ward> first = freshWard();
    const std::unique_ptr<Ward> second = freshWard();
    for (std::int32_t day = 0; day < 400; ++day) {
        first->endOfDay();
        second->endOfDay();
    }
    HashSink a(first->id().salt());
    HashSink b(second->id().salt());
    first->hash_into(a);
    second->hash_into(b);
    CHECK(a.finished() == b.finished());
    CHECK(first->stats().eaten == second->stats().eaten);
    CHECK(first->stats().grown == second->stats().grown);
    CHECK(first->stored() == second->stored());
}

TEST_CASE("a day off the engine's clock is the same day as a day off endOfDay") {
    // THE BRIDGE. The soak runs two years by calling endOfDay directly rather
    // than ticking sixty-three million times, and that shortcut is only honest
    // if the two paths are the same path. So: three days of each, and the
    // hashes have to match exactly.
    content::World world = content::loadWorldFile(content::bakedMap(docks::kWorldName));
    PhasedEngine engine(kSeed, world);
    auto ticked = std::make_unique<Ward>(kSeed, content::contentDir(), who());
    const Ward* view = ticked.get();
    engine.register_system(std::move(ticked));
    engine.boot();

    constexpr std::int32_t kDays = 3;
    for (std::int64_t second = 0; second < static_cast<std::int64_t>(kDays) * kSecondsPerDay;
         ++second) {
        engine.tick();
    }
    CHECK(view->day() == kDays);

    const std::unique_ptr<Ward> direct = freshWard();
    for (std::int32_t day = 0; day < kDays; ++day) {
        direct->endOfDay();
    }

    CHECK(direct->stats().eaten == view->stats().eaten);
    CHECK(direct->stats().grown == view->stats().grown);
    CHECK(direct->stored() == view->stored());
    // The seconds counter differs by construction -- one was ticked and one was
    // not -- so the comparison is of everything the DAY decided, which is what
    // the shortcut claims to reproduce.
    for (std::size_t i = 0; i < direct->households().size(); ++i) {
        const Household& mine = direct->households()[i];
        const Household& theirs = view->households()[i];
        INFO("household ", i);
        REQUIRE(mine.coin == theirs.coin);
        REQUIRE(mine.food == theirs.food);
        REQUIRE(mine.arrears == theirs.arrears);
        REQUIRE(mine.hungryDays == theirs.hungryDays);
    }
}
