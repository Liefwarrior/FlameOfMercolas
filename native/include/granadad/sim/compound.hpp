#pragma once

// The compounds: walled courtyard farms, the ground under them, and the two
// ways a Trojian pays for ground.
//
// WHAT TROJIAN HOUSING IS, and it is not a suburb. DOCKS-GAZETTEER section 2.5:
// residential space is organised into walled COMPOUNDS -- one large lot with a
// courtyard farm at the centre, ringed by condo-like apartments and mansions
// belonging to owning families, built up where land is scarce, with the roof of
// a tall compound walled off and let as mass low-income housing. A "dozen
// homes" is a dozen dwelling UNITS inside one compound, never a dozen lots.
//
// WHAT THE GROUND IS, and it is not property. Section 2.8: the ground of
// Granadad is a CHARGE, a trust the Priests of the Flame hold and let out, and
// nobody at any tier owns the earth. Three tiers:
//
//   THE FLAME      holds every plot and will not sell one.
//   THE DEN DUKE   holds the charge on a plot -- the compound, its wall, its
//                  gate, its courtyard -- pays the Flame a charge-rent each
//                  quarter, and lets the ground under each house to that
//                  house's owner.
//   THE OWNER      owns the house outright, standing on ground they will never
//                  own, and pays the Den Duke a GROUND PENNY each quarter.
//
// A family can own its house free and clear and still owe the man who rents the
// earth beneath it, and that man does not own the earth either. The Church's
// register of who holds what is THE ROLL, and a thing is true in Granadad when
// it is on the roll.
//
// AND THE ROOFTOP TIER IS A THIRD LANDLORD. A rooftop lodger rents from the
// house-owner BENEATH them, not from the Duke: a roof is part of a house and
// the house is individually owned. Three consequences, and all three are
// modelled here. Every mansion-poor house-owner is a petty landlord. Roof
// income is what makes a struggling owner look solvent enough to bear a raised
// penny. And when a house is taken for arrears the roof goes with it, and the
// roof people were never parties to the case.
//
// THE SAME INSTRUMENT, THREE USES -- the leasehold. A house-owner who cannot
// find the penny may lease their own LABOUR to their Den Duke until the arrears
// are worked off: they become BONDSWORN, the Duke becomes their BONDHOLDER,
// their wage is the bondholder's and their work is the bondholder's yard, and
// in return the bondholder owes KEEP, board and biscuit set low. That is what
// Trojian serfdom is -- not a caste and not a birth, but a debt relation
// dressed as a tenancy. A serf is a house-owner who had a bad quarter.
//
// THAT LAST CLAUSE IS THE LOAD-BEARING ONE IN THIS FILE. "Their work is the
// bondholder's yard" means a bondsworn head's farm labour leaves the compound
// it lives in and turns up in the bondholder's courtyard. So a Duke who buys
// bonds gets beds tended and the compound he bought them from does not, and the
// crops there come up thin. The tenure system and the farming system are not
// two features next to each other; the bond is the pipe between them, and the
// soak is what proves the pipe carries.
//
// NO FLOATS. Every ration, every coin, every day is an integer. No unordered
// container: plots, houses, households and beds are dense vectors in the roll's
// own order, and every walk over them is by ascending index.
//
// WHAT IS NOT MODELLED, said out loud rather than implied. There is no walking
// here: a household is a record on the roll, not a body on a tile, and no
// actor in the Gilded Gull is one of these households. The compounds are the
// ward's ECONOMY and the Gull is the ward's ROOM, and S7 does not join them.
// See the VERIFICATION GAP notes at the foot of this header for the full list.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/engine.hpp"
#include "granadad/sim/rng.hpp"
#include "granadad/sim/system_id.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

class NotableRegistry;

// ---------------------------------------------------------------------------
// tenure
// ---------------------------------------------------------------------------

/// How a plot is held. Section 2.8's own four, and the ordinal is hashed.
enum class Tenure : std::uint8_t {
    /// Church ground never let to anyone, squatted and worked directly, token
    /// alms in place of a penny. The 45 hovels. The poorest are the least
    /// enserfed: a Duke who wants labour cannot get it from the glebe.
    Glebe = 0,
    /// A Den Duke holds the charge and pays the Flame for it.
    Charged = 1,
    /// The charge itself is pledged to a creditor. If the Duke defaults, the
    /// creditor becomes Den Duke -- C2, and the reason the Widow's paper is
    /// load-bearing rather than decorative.
    Pledged = 2,
    /// The Flame has not re-let it. A vacant charge is a prize, and any actor
    /// -- including the player -- may petition for it.
    Vacant = 3,
};

[[nodiscard]] std::string_view tenureName(Tenure tenure) noexcept;

/// What kind of dwelling a household is in. Decides who its landlord is, and
/// whether the courtyard larder is any of its business.
enum class HouseKind : std::uint8_t {
    /// The owning family and its dependent households, inside the compound's
    /// mansion. Reman-engineered; the wealth gradient rides the material.
    Mansion = 0,
    /// One household per condo unit, ringing the courtyard. Owns the house.
    Condo = 1,
    /// A tent or a mud hut on the roof deck, rented from the house-owner
    /// beneath. Cloth, leather, mudbrick; cheap and flammable. NOT the Duke's
    /// tenant, and NOT fed by the courtyard.
    RoofHut = 2,
    /// Nothing at all. Section 2.8: "Wastrels own nothing, so nothing can be
    /// taken from them; the only thing they can pledge is themselves. They are
    /// the bond pool of last resort, and at the ward's starvation margin that
    /// is not a figure of speech."
    ///
    /// THIS IS WHERE THE WARD'S HUNGER LIVES, and it is where canon puts it. A
    /// wastrel owes no penny and no rent because there is nothing to owe it
    /// on, eats nothing out of a courtyard that is not theirs, and earns what
    /// the quay pays for a day nobody planned. The Mission's night-soup and a
    /// Duke who is short of hands are the only two things between them and the
    /// worst of it -- and both of those are modelled.
    Wastrel = 3,
};

[[nodiscard]] std::string_view houseKindName(HouseKind kind) noexcept;

// ---------------------------------------------------------------------------
// what a priest can answer
// ---------------------------------------------------------------------------

/// Section 2.8's six outcomes, and only one of them is eviction. The ordinal is
/// hashed and written into reports; append only.
enum class Verdict : std::uint8_t {
    /// The petition refused and the Duke warned; his standing with the Flame
    /// falls.
    Dismissed = 0,
    /// One quarter's grace, arrears frozen.
    Stay = 1,
    /// The priest permanently reduces the ground penny. The Duke eats it
    /// forever. The Flame shaving a Duke's income is the sharpest instrument in
    /// the ward.
    Abatement = 2,
    /// The tenant keeps the house and the Duke takes the labour. A
    /// court-ordered bond cannot be sold on to anyone else.
    BondOrdered = 3,
    /// The house passes to the Duke. The family is ROOFED -- turned out onto a
    /// roof deck or into the Gullet. The lodgers above them go too.
    Distraint = 4,
    /// Available only AGAINST the Duke, when his own arrears and conduct bottom
    /// out: the Flame strips the plot's charge and re-lets it. This is how a
    /// tenant, or a player, brings a case rather than only defends one.
    ChargeRevoked = 5,
};

[[nodiscard]] std::string_view verdictName(Verdict verdict) noexcept;

// ---------------------------------------------------------------------------
// the numbers
// ---------------------------------------------------------------------------
//
// ONE PLACE, so the balance can be tuned without hunting call sites -- the same
// shape the Java build's FoodEconomy used, and for the same reason. Every one
// of these is a game-balance number and none of them is canon.

/// What one head eats in a day. The unit of the whole food economy.
inline constexpr std::int32_t kRationsPerHeadPerDay = 1;

/// Days between one turning-over of a bed and the next before the crop starts
/// to suffer. A bed wants working every third day.
inline constexpr std::int32_t kTendEveryDays = 3;

/// Beds one pair of hands gets round in a day.
inline constexpr std::int32_t kBedsPerHandPerDay = 2;

/// Heads it takes to free up one pair of hands for the courtyard. Not everybody
/// in a household farms: there are children, there is the job that pays the
/// penny, and there is the half of the ward that works the quay.
///
/// THIS IS THE DIAL THE BOND TURNS. At twelve a fully-resident compound has
/// comfortable slack; sell its heads into a bond on another plot and the beds
/// stop getting turned over, which is exactly the pressure section 2.8
/// describes and the reason the two systems are one system.
inline constexpr std::int32_t kHeadsPerFarmHand = 12;

/// Fraction of the tending a crop wanted, in percent, below which a bed is not
/// worth the carrying. Neglect does not scale smoothly all the way down: a bed
/// nobody turned over is weeds, and there is a point where a farmer walks past
/// it rather than bending his back for what is on it.
inline constexpr std::int32_t kBedFailsBelowPercent = 34;

/// What a bed gives at harvest.
///
/// FULL for a bed worked every kTendEveryDays for the whole season, NOTHING for
/// one worked less than kBedFailsBelowPercent of that, and the fraction in
/// between. This is "crops fail if neglected" written as arithmetic so a case
/// can drive it directly rather than inferring it from a season.
[[nodiscard]] std::int32_t bedYield(std::int32_t yieldPerBed, std::int32_t tends,
                                    std::int32_t needed) noexcept;

/// What a household keeps in its own larder at most. A compound atrium holds
/// far more; a family's shelf does not.
inline constexpr std::int32_t kHouseholdLarderCap = 12;

/// What the shared courtyard larder holds. Sized for a compound, not a family.
inline constexpr std::int32_t kCourtyardLarderCap = 900;

/// Coin a working head brings home in a day, by where it sleeps.
///
/// The gradient is the ward's own: an owning family has the wage that bought
/// the house, and a roof lodger has whatever the quay paid today. The roof is
/// where the ward's starvation margin actually lives, and this is why.
inline constexpr std::int32_t kWagePerOwnerHead = 3;
inline constexpr std::int32_t kWagePerRoofHead = 3;
/// And what a man with no address gets for a day nobody planned.
inline constexpr std::int32_t kWagePerWastrelHead = 1;

/// Royals a household pays for one ration at a counter.
inline constexpr std::int32_t kRationPrice = 2;

/// Rations the quay lands into the ward's market every day, and the most the
/// counters will hold. Money still gates every mouthful: the import stocks the
/// market, it does not feed anybody.
inline constexpr std::int32_t kMarketImportPerDay = 195;
inline constexpr std::int32_t kMarketStockCap = 460;

/// Rations the Mission hands out in a day, to the hungriest first.
///
/// DELIBERATELY THIN. The whole point is a real margin: night-soup keeps the
/// reachable poor alive between bad quarters, and there is not enough of it for
/// everyone. Section 2.8's glebe pays "token alms in place of a penny" and this
/// is the other side of that ledger.
inline constexpr std::int32_t kAlmsPerDay = 16;

/// Rations a bondholder owes a bondsworn household in a day. "Board and
/// biscuit, set low" -- section 2.8's own words, and it is set low.
inline constexpr std::int32_t kKeepPerHousehold = 2;

/// Days of hunger before a household is counted STARVING. Not one: a family
/// misses a meal without anybody calling it starvation, and a rate that counted
/// single hungry days would report the ward's ordinary Thursday as a famine.
inline constexpr std::int32_t kStarvingAfterDays = 3;

/// Arrears at which a house-owner may go bondsworn rather than lose the house,
/// and the arrears one day of bonded labour works off.
inline constexpr std::int32_t kBondOfferedAt = 55;
inline constexpr std::int32_t kBondWorkPerDay = 2;

/// What a Duke books against a wastrel he takes in. A wastrel owns nothing and
/// therefore owes nothing, so there is no arrears figure to work off -- the
/// bond is struck against the keep advanced, and this is the price of it. The
/// bond pool of last resort is still a bond and it still discharges.
inline constexpr std::int32_t kWastrelBondArrears = 60;

/// Arrears at which a Den Duke will bring a petition against a tenant.
inline constexpr std::int32_t kPetitionAt = 120;

/// Quarters of arrears a Duke lets run before the Flame counts him behind on
/// his own charge. Used only in the report and in the revocation test.
inline constexpr std::int32_t kDukeArrearsRevokedAt = 240;

/// A Den Duke's standing with the Flame, and the two ends of it. It moves on
/// what he does: a dismissed petition costs him, a discharged bond earns him,
/// and his own charge-arrears cost him most.
inline constexpr std::int32_t kFlameStandingStart = 50;
inline constexpr std::int32_t kFlameStandingMin = 0;
inline constexpr std::int32_t kFlameStandingMax = 100;

/// What the player pays to lease a roof deck for a quarter, and what a house
/// costs to buy outright. Both in Royals.
inline constexpr std::int32_t kPlayerRoofLeaseQuarter = 14;
inline constexpr std::int32_t kHousePriceCondo = 240;
inline constexpr std::int32_t kHousePriceMansion = 900;

// ---------------------------------------------------------------------------
// the roll, as the owner's file has it
// ---------------------------------------------------------------------------

struct CropRaw {
    std::string id;
    std::string label;
    std::int32_t growDays = 1;
    std::int32_t yieldPerBed = 0;
};

struct PlotRaw {
    std::string id;
    std::string name;
    Tenure tenure = Tenure::Charged;
    /// notables.json id, or empty for a vacant charge and for the glebe.
    std::string denDuke;
    /// notables.json id of whoever holds the paper on the charge. C2 only.
    std::string pledgedTo;
    /// The plot's charter priest. In the Docks, Father Maell.
    std::string priest;
    std::int32_t chargeRent = 0;
    std::int32_t groundPenny = 0;
    std::int32_t roofRent = 0;
    std::int32_t beds = 0;
    std::string crop;
    std::int32_t mansionHouseholds = 0;
    std::int32_t condos = 0;
    std::int32_t roofHuts = 0;
    /// Households with no house, no roof and nothing to pledge but themselves.
    std::int32_t wastrels = 0;
    std::int32_t dukeCoin = 0;
    std::string note;
};

/// content/raws/compounds/compounds.json, cross-checked against the owner's own
/// notables registry.
///
/// NOTHING HERE INVENTS A PERSON. Every denDuke, pledgedTo and priest id is
/// looked up in notables.json at load and the plot is REFUSED if the registry
/// does not have it -- the same rule the contract board follows, for the same
/// reason: the roll must not be able to grow a forty-third notable quietly.
class CompoundRaws {
public:
    [[nodiscard]] static CompoundRaws load(const std::filesystem::path& contentDir,
                                           const NotableRegistry& who);

    [[nodiscard]] bool loaded() const noexcept { return !plots_.empty(); }
    [[nodiscard]] const std::vector<PlotRaw>& plots() const noexcept { return plots_; }
    [[nodiscard]] const std::vector<CropRaw>& crops() const noexcept { return crops_; }
    [[nodiscard]] const CropRaw* crop(std::string_view id) const noexcept;
    [[nodiscard]] std::int32_t quarterDays() const noexcept { return quarterDays_; }
    /// Plots the file carried that the notables registry refused.
    [[nodiscard]] std::int32_t refused() const noexcept { return refused_; }

private:
    std::vector<PlotRaw> plots_;
    std::vector<CropRaw> crops_;
    std::int32_t quarterDays_ = 90;
    std::int32_t refused_ = 0;
};

// ---------------------------------------------------------------------------
// what the roll holds
// ---------------------------------------------------------------------------

/// One bed in a courtyard farm.
///
/// A CROP IS NOT A TIMER. `age` is how long since it was sown and `tends` is
/// how many days somebody actually worked it; at harvest the yield is scaled by
/// the second against what the crop needed. A bed nobody turned over comes up
/// worth nothing and has to be sown again, which is a lost cycle and not a
/// lost day.
struct CropBed {
    std::int32_t age = 0;
    std::int32_t tends = 0;
    /// What the last harvest off this bed actually gave. Reporting only.
    std::int32_t lastYield = 0;
};

struct Household {
    std::int32_t id = 0;
    /// Index into Ward::plots().
    std::int32_t plot = 0;
    HouseKind kind = HouseKind::Condo;
    /// Mouths. Drawn from content/raws/actors/household.json's own weights.
    std::int32_t heads = 1;
    std::int32_t coin = 0;
    /// The family's own shelf.
    std::int32_t food = 0;

    // --- the ground ---------------------------------------------------------
    /// The quarterly ground penny this household owes its Den Duke. Zero on the
    /// glebe and zero for a roof lodger, who owes the house-owner instead.
    std::int32_t groundPenny = 0;
    /// What a roof lodger owes the house-owner beneath them, quarterly.
    std::int32_t roofRent = 0;
    /// The house-owning household a roof lodger rents from, or -1.
    std::int32_t landlord = -1;
    std::int32_t arrears = 0;
    /// Consecutive quarters this household has been behind. NOT the same fact
    /// as the arrears: a family that fell short once and a family that has
    /// been short for three years owe the priest two different explanations,
    /// and section 2.8's Duke has to show "that he offered terms before he
    /// asked for the ground back". Patience is a thing men run out of, and
    /// this is the counter it runs out on.
    std::int32_t quartersBehind = 0;
    /// Quarters of grace a priest has granted. While it runs the penny does
    /// not fall and the arrears do not grow -- which is the whole of what a
    /// STAY is, and saying so in a comment while charging the penny anyway
    /// would be the kind of decoration this project keeps out of its systems.
    std::int32_t stayQuarters = 0;
    /// True once the house has been taken for arrears. A roofed family keeps
    /// its years and loses its house.
    bool roofed = false;

    // --- the bond -----------------------------------------------------------
    /// The plot whose Duke holds this household's bond, or -1 for free. While
    /// the bond runs the wage is the bondholder's and the WORK IS THE
    /// BONDHOLDER'S YARD -- which is what moves farm hands between compounds.
    std::int32_t bondholder = -1;
    /// True when a priest ordered the bond rather than the household choosing
    /// it. A court-ordered bond cannot be sold on to anyone else.
    bool bondOrdered = false;
    std::int32_t bondDaysServed = 0;
    /// How many bonds this household has worked out. The Flame blesses a
    /// discharged debt, which is cheap for the Flame and worth a great deal to
    /// the discharged.
    std::int32_t discharges = 0;

    // --- the belly ----------------------------------------------------------
    std::int32_t hungryDays = 0;
    std::int32_t starvedDays = 0;
    /// Quarters this household has kept this ground. What a tenant may plead.
    std::int32_t quartersKept = 0;
    /// True when this is the player's own household.
    bool player = false;

    [[nodiscard]] bool bonded() const noexcept { return bondholder >= 0; }
    [[nodiscard]] bool starving() const noexcept { return hungryDays >= kStarvingAfterDays; }
    /// A roof lodger is nobody's tenant on the roll and the courtyard is none
    /// of their business; a wastrel is not even that.
    [[nodiscard]] bool ownsHouse() const noexcept {
        return (kind == HouseKind::Mansion || kind == HouseKind::Condo) && !roofed;
    }
};

struct Plot {
    /// Index into CompoundRaws::plots().
    std::int32_t raw = 0;
    Tenure tenure = Tenure::Charged;
    /// The shared atrium larder the courtyard fills and the compound's
    /// house-owners eat from.
    std::int32_t larder = 0;
    /// The Den Duke's purse, and what he owes the Flame.
    std::int32_t dukeCoin = 0;
    std::int32_t dukeArrears = 0;
    std::int32_t flameStanding = kFlameStandingStart;
    /// True when the PLAYER holds this plot's charge.
    bool playerIsDuke = false;
    /// Beds, in the roll's own order.
    std::vector<CropBed> beds;
    /// Ascending household ids resident on this plot.
    std::vector<std::int32_t> residents;

    /// Beds this plot lost to neglect since the ward started.
    std::int32_t bedsFailed = 0;
    std::int32_t harvests = 0;
    /// Rations this courtyard has produced. Per plot, so a case can ask what
    /// buying the paper on a compound's hands did to that compound's harvest.
    std::int64_t grown = 0;
};

// ---------------------------------------------------------------------------
// what a day did
// ---------------------------------------------------------------------------

/// Everything the soak reports. Cumulative unless the name says otherwise.
struct WardStats {
    std::int64_t days = 0;

    // food
    std::int64_t grown = 0;
    std::int64_t eaten = 0;
    std::int64_t boughtAtMarket = 0;
    std::int64_t fromCourtyard = 0;
    std::int64_t fromAlms = 0;
    std::int64_t fromKeep = 0;
    std::int64_t importedToMarket = 0;
    std::int64_t spoiledOverCap = 0;

    // the land
    std::int64_t harvests = 0;
    std::int64_t bedsFailed = 0;
    std::int64_t bedTendsWanted = 0;
    std::int64_t bedTendsDone = 0;

    // the belly
    std::int64_t headDaysHungry = 0;
    std::int64_t headDaysStarving = 0;
    std::int64_t headDays = 0;
    /// The worst single day the ward has had, in starving heads.
    std::int32_t peakStarvingHeads = 0;

    // the ground
    std::int64_t penniesPaid = 0;
    std::int64_t penniesShort = 0;
    std::int64_t roofRentPaid = 0;
    std::int64_t chargeRentPaid = 0;
    std::int64_t quarters = 0;
    std::int64_t bondsTaken = 0;
    std::int64_t bondsDischarged = 0;
    std::int64_t petitions = 0;
    /// Indexed by Verdict.
    std::int64_t verdicts[6] = {};
    std::int64_t housesDistrained = 0;
    std::int64_t lodgersTurnedOut = 0;

    /// Starving head-days as a permille of all head-days. THE NUMBER THE
    /// SPRINT IS JUDGED ON: the Java build held serf starvation at or below
    /// 5%, so this must come in at or under 50.
    [[nodiscard]] std::int32_t starvationPermille() const noexcept;
    /// Hungry head-days as a permille. Always at least the starving rate: a
    /// hungry day is a day somebody did not eat, and three in a row is
    /// starvation.
    [[nodiscard]] std::int32_t hungerPermille() const noexcept;
};

// ---------------------------------------------------------------------------
// what a player verb answers
// ---------------------------------------------------------------------------

enum class TenureResult : std::uint8_t {
    Done = 0,
    /// No such plot, house or household.
    NoSuchThing = 1,
    /// Not enough Royals.
    CannotAfford = 2,
    /// The roll already says otherwise -- the charge is held, the house has an
    /// owner, you already have a roof.
    AlreadyHeld = 3,
    /// The Flame will not hear it: nothing owed, or nothing to hear it about.
    NoCause = 4,
    /// A charge cannot be petitioned for while somebody holds it.
    NotVacant = 5,
};

[[nodiscard]] std::string_view tenureResultName(TenureResult result) noexcept;

/// What a hearing decided, and everything the report needs to say why.
struct Hearing {
    bool heard = false;
    Verdict verdict = Verdict::Dismissed;
    /// The household the petition was against.
    std::int32_t household = -1;
    std::int32_t plot = -1;
    std::int32_t arrears = 0;
    /// The Duke's offering. It is an offering and not a fee, and it does NOT
    /// buy the verdict -- see Ward::hear on where it lands instead.
    std::int32_t offering = 0;
    /// The score the priest weighed, before the draw. Positive favours the
    /// tenant.
    std::int32_t weight = 0;
    /// Roof lodgers turned out with the family, who were never parties.
    std::int32_t lodgersTurnedOut = 0;
    std::string line;
};

// ---------------------------------------------------------------------------
// the ward
// ---------------------------------------------------------------------------

/// The compounds of the Docks, ticking.
///
/// A DAY IS THE UNIT. Nothing in a courtyard happens in a second: a bed is
/// turned over or it is not, a family eats or it does not, and a penny falls at
/// quarter-day. So Ward::tick counts seconds and does the work at the day
/// boundary, and Ward::endOfDay IS that work -- exposed so a soak can run two
/// years of the ward without ticking sixty-three million times, with a case
/// proving the two paths agree exactly.
class Ward final : public SimulationSystem {
public:
    Ward(std::uint64_t worldSeed, const std::filesystem::path& contentDir,
         const NotableRegistry& who);

    [[nodiscard]] const SystemId& id() const noexcept override { return id_; }
    [[nodiscard]] TickPhase phase() const noexcept override { return TickPhase::Actors; }
    void tick(const TickContext& context) override;
    void hash_into(HashSink& sink) const override;

    // --- the roll -----------------------------------------------------------

    [[nodiscard]] bool loaded() const noexcept { return raws_.loaded(); }
    [[nodiscard]] const CompoundRaws& raws() const noexcept { return raws_; }
    [[nodiscard]] const std::vector<Plot>& plots() const noexcept { return plots_; }
    [[nodiscard]] const std::vector<Household>& households() const noexcept {
        return households_;
    }
    [[nodiscard]] std::int32_t heads() const noexcept;
    [[nodiscard]] std::int32_t starvingHeads() const noexcept;
    [[nodiscard]] std::int32_t marketStock() const noexcept { return market_; }
    [[nodiscard]] std::int32_t stored() const noexcept;
    [[nodiscard]] std::int64_t day() const noexcept { return day_; }
    [[nodiscard]] const WardStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const Hearing& lastHearing() const noexcept { return lastHearing_; }

    /// Hands free for this plot's courtyard TODAY, counting bondsworn labour
    /// that has been leased in and not counting labour leased away. This one
    /// function is the whole pipe between the tenure system and the farm.
    [[nodiscard]] std::int32_t farmHands(std::int32_t plotIndex) const noexcept;

    /// Whether this plot's courtyard has fewer hands than beds to turn over.
    ///
    /// A DUKE TAKES BONDS BECAUSE HE NEEDS HANDS, not because somebody is
    /// desperate. He owes keep for every pair he holds, so a well-worked yard
    /// turns a bondsworn tenant away -- and a tenant with nothing left to offer
    /// but the ground is how a case gets to the Mission at all. This is the one
    /// function that decides which of the two happens.
    [[nodiscard]] bool yardIsShort(std::int32_t plotIndex) const noexcept;

    /// One whole day of the ward: the land, the wage, the meal, the market,
    /// and -- every quarter -- the ground.
    void endOfDay();

    // --- the player ---------------------------------------------------------
    //
    // EVERY ONE OF THESE IS A THING AN NPC ALREADY DOES. The player leases the
    // space NPCs lease, buys the house NPCs own, collects the rent NPCs
    // collect and brings the petition NPCs bring. There is no player-only
    // machinery in this file.

    [[nodiscard]] std::int32_t playerCoin() const noexcept { return playerCoin_; }
    void setPlayerCoin(std::int32_t coin) noexcept { playerCoin_ = coin; }
    /// The player's own household on the roll, or -1 before they have a roof.
    [[nodiscard]] std::int32_t playerHousehold() const noexcept { return playerHousehold_; }

    /// Take a roof deck on this plot for a quarter. The rent goes to the
    /// house-owner beneath, not to the Duke, because the roof is part of a
    /// house and the house is individually owned.
    TenureResult leaseRoof(std::int32_t plotIndex);

    /// Buy a house outright and stand on ground you will never own. The
    /// player becomes a house-owner: they owe the plot's Den Duke a ground
    /// penny every quarter, and they become the landlord of whatever roof huts
    /// stand on their own roof.
    TenureResult buyHouse(std::int32_t plotIndex);

    /// Petition the Flame for a VACANT charge and become Den Duke of the plot.
    /// Only ever answerable for C4, whose charge section 2.8 rules is a prize.
    TenureResult petitionForCharge(std::int32_t plotIndex);

    /// What the player's own roof lodgers and, as Duke, ground pennies have put
    /// aside since it was last collected. Moves it into the player's purse and
    /// answers with the amount.
    [[nodiscard]] std::int32_t collectRent();
    [[nodiscard]] std::int32_t rentWaiting() const noexcept { return playerRentHeld_; }

    /// Bring a petition against a tenant in arrears. `offering` is the Duke's
    /// offering to the Mission -- it is an offering and not a fee, and the
    /// priest's verdict does not move by a penny of it.
    Hearing petitionAgainst(std::int32_t householdId, std::int32_t offering);

    /// What the priest WOULD answer. Rules only: it weighs and returns, and
    /// changes nothing at all.
    ///
    /// PUBLIC ON PURPOSE, and for the same reason noticePermille and
    /// sentenceFor are: the rule is the interesting thing and it should be
    /// reachable without a room around it. It is also the only honest way to
    /// prove the claim section 2.8 makes -- "it is an offering and not a fee,
    /// and it does not buy the verdict" -- because the proof is the SAME case
    /// on the SAME morning with two different offerings, which cannot be
    /// staged through a verb that applies its own answer.
    [[nodiscard]] Hearing weighPetition(std::int32_t plotIndex, std::int32_t householdId,
                                        std::int32_t offering, std::uint64_t draw) const;

    /// Lease yourself in lieu of the penny. The player goes bondsworn to their
    /// own Den Duke: wage to the bondholder, work in the bondholder's yard,
    /// keep owed back, and the arrears come off day by day.
    TenureResult goBondsworn();

    /// Buy the paper on somebody's hands, and their output is yours.
    ///
    /// SECTION 2.8'S WHOLE GUILD MECHANIC, and it needs no other machinery:
    /// "a Guild acquires competition with it -- you do not undercut a rival,
    /// you buy the paper on his hands, and then his output is yours. A bond is
    /// a transferable holding." Moving a bond moves the labour, and the labour
    /// is what the courtyard runs on -- so a Duke who buys bonds gets beds
    /// turned over and the compound he bought them from does not.
    ///
    /// ONE BOND CANNOT BE SOLD, and it is the one a priest ordered: "a
    /// court-ordered bond cannot be sold on to anyone else."
    TenureResult transferBond(std::int32_t householdId, std::int32_t toPlotIndex);

private:
    void growDay();
    void wageDay();
    void eatDay();
    void marketDay();
    void quarterDay();
    void apply(const Hearing& hearing);
    /// Feeds one household from `source` up to `wanted`, returning what it got.
    [[nodiscard]] static std::int32_t drawFrom(std::int32_t& source, std::int32_t wanted) noexcept;

    [[nodiscard]] Household* householdAt(std::int32_t id) noexcept;
    [[nodiscard]] const Household* householdAt(std::int32_t id) const noexcept;

    SystemId id_;
    CompoundRaws raws_;
    CounterRandomSource rng_;

    std::vector<Plot> plots_;
    /// Ascending by id, and id IS the index. Never reordered.
    std::vector<Household> households_;
    /// The ward's counters, stocked by the quay and drained by everybody.
    std::int32_t market_ = 0;
    std::int32_t almsToday_ = 0;

    std::int64_t day_ = 0;
    std::int64_t seconds_ = 0;

    std::int32_t playerCoin_ = 0;
    std::int32_t playerHousehold_ = -1;
    std::int32_t playerRentHeld_ = 0;

    WardStats stats_;
    Hearing lastHearing_;
};

// ---------------------------------------------------------------------------
// the soak
// ---------------------------------------------------------------------------

/// The ward's own report: what the land gave, what the mouths took, what is on
/// the shelves, and the one number the sprint is judged on.
[[nodiscard]] std::string wardReport(const Ward& ward);

/// THE BAR. The Java build held serf starvation at or below 5%, and that is
/// what S7 is measured against, so it is a constant and not a sentence in a
/// report nobody re-reads.
inline constexpr std::int32_t kStarvationBarPermille = 50;

struct WardSoakResult {
    std::string report;
    std::int32_t starvationPermille = 0;
    std::int32_t hungerPermille = 0;
    std::int64_t days = 0;
    /// True when the ward neither starved past the bar nor drowned in surplus.
    bool passed = false;
    /// Why it did not, when it did not.
    std::string problem;
};

/// Runs the compounds for `days` and answers with the report and the verdict.
///
/// TWO WAYS TO FAIL, and the second one matters as much as the first. A ward
/// that starves past kStarvationBarPermille fails; so does a ward whose
/// courtyards produce so much that every larder sits at its cap and nothing is
/// ever scarce, because an economy with no scarcity is not balanced, it is off.
[[nodiscard]] WardSoakResult runWardSoak(std::int64_t days,
                                         const std::filesystem::path& contentDir,
                                         std::uint64_t seed);

// ---------------------------------------------------------------------------
// VERIFICATION GAPS (S7) -- what this file does NOT do
// ---------------------------------------------------------------------------
//
// VERIFICATION GAP (S7): NOBODY WALKS. A household is a row on the roll and not
// a body on a tile. No household here is an Actor, none of them is one of the
// fourteen people in the Gilded Gull, and no compound in content/maps/baked/
// docks_surface.trojsav has been bound to a plot in this file by coordinate.
// The ward's ECONOMY is simulated and the ward's GEOGRAPHY is not joined to it.
//
// VERIFICATION GAP (S7): THE WAGE IS A FAUCET. Coin enters the ward at
// kWagePerOwnerHead / kWagePerRoofHead a head a day and leaves it at the market
// counter and the Duke's gate. There is no employer, no payroll and no closed
// money invariant of the kind the Java build's BankLedger held. The food supply
// IS conserved and accounted (see WardStats: minted by harvest and import,
// sunk by eating), and the money is not.
//
// VERIFICATION GAP (S7): THE MARKET HAS NO SHOPKEEPER. kMarketImportPerDay
// rations appear on the ward's counters every day and are sold at kRationPrice
// to whoever has the coin. Which counter, whose it is, and whether a household
// can walk to it are not modelled -- the Java build's SeekFoodPolicy had all
// three and this does not.
//
// VERIFICATION GAP (S7): A PETITION IS BROUGHT, NEVER ATTENDED. Father Maell
// weighs and answers, and none of it happens in the Mission: there is no room,
// no queue, no conversation and no bark spoken at a hearing. The barks are
// authored (content/raws/barks/house_barks.json) and only the report reads them.

}  // namespace granadad::sim
