#pragma once

// Radiant work: jobs that are GENERATED but never invented.
//
// WHAT THE SPRINT WAS ASKED FOR, and the line it has to walk. "Radiant means
// generated, repeatable and varied, but they must not feel like slot-machine
// filler -- each contract should reference real places, real named NPCs and the
// real economy." Those two sentences pull in opposite directions and the whole
// design of this file is the answer to the tension:
//
//   THE SHAPE IS GENERATED. Which broker is offering, which of the Forty wants
//   it, whose ground it comes off, how many, by which night, for how much --
//   all drawn, all from the same counter-based chain everything else in this
//   simulation draws from, all reproducible from (worldSeed, day, slot).
//
//   EVERY NAME IN IT IS AUTHORED. A patron is a notables.json id or the offer
//   is refused at load. A site is the site that notable is actually bound to.
//   The brief is a template out of content/raws/contracts/contracts.json with
//   those names substituted in, so "Squall keeps a back room at the bathhouse
//   and the back room keeps a smell; it comes off Foreman Hemp's ground at the
//   Ropewalk" is a sentence the ward could have said about itself. Not one
//   proper noun in this file's output was chosen by a programmer.
//
//   AND THE PAY IS THE WARD'S OWN ECONOMY. A contract is priced through exactly
//   the guildPricePercent S4 already uses for a mug of ale -- the broker's
//   guild, its influence over the district and what it thinks of you -- so
//   climbing a ladder makes the work pay better in the same units that make
//   drink cheaper, rather than through a second economy nobody can see.
//
// A CONTRACT IS NOT A QUESTLINE. QuestBook owns authored, hand-written, ordered
// stages with a beginning and an end; this owns work that repeats forever and
// is never the same twice. The Skyrunner line GRADUATES into this: its last
// stage makes you a Cutpurse, and a Cutpurse is somebody Finch will give a job
// to. They share nothing but the room they happen in, deliberately -- a
// generated stage in an authored line would rot the line, and an authored line
// of generated jobs is a contradiction.
//
// NO FLOATS. NO UNORDERED CONTAINERS: offers are a vector in the file's order,
// live contracts a vector sorted by id, and every draw is the pure
// (seed, day, slot, index) chain.
//
// NO BYTE CODEC, and that is a disclosure rather than an oversight -- the same
// one QuestJournal makes. A board is regenerable from (day, seed, standings);
// only which contracts were TAKEN is not, and that is four integers and a
// string per row that a save writes the same way the ledgers already show. The
// board is hashed, so the twin-run gate protects it either way.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/contraband.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

/// Somebody who hands work out. Three of them, and the difference between them
/// is the whole texture of the board: the Watch pays a bounty, the house buys
/// for its own cellar, and the roofs broker everything else.
struct ContractBroker {
    /// A content/raws/names/notables.json id.
    std::string id;
    /// A content/raws/factions/factions.json id. What the pay is priced by.
    std::string faction;
    /// What the topic list calls this broker's work.
    std::string label;
    /// What it takes before this one says a word: "none", "acquaintance" or
    /// "member". A gradient rather than a switch -- a public bounty anybody may
    /// claim, a landlord who has to not dislike you, and a guild that has sworn
    /// you in, which is exactly what the Skyrunner line graduates a player into.
    std::string needs;
};

/// One authored template. Everything a generated contract is, before the draw
/// decides which of the authored names go in it.
struct ContractOffer {
    std::string id;
    std::string broker;
    Contraband good = Contraband::Scalp;
    /// The word the label opens with: TAKE, RUN, CARRY, MOVE, FETCH.
    std::string verb;
    std::int32_t unitsMin = 1;
    std::int32_t unitsMax = 1;
    std::int32_t payPerUnit = 0;
    /// Nights it stands. One is tonight.
    std::int32_t days = 1;
    /// The template. {patron} {patronSite} {source} {sourceSite} {thing}
    /// {units} {good}.
    std::string brief;
    /// notables.json ids, ascending. Never empty for a loaded offer.
    std::vector<std::string> patrons;
    std::vector<std::string> sources;
    /// What a FETCH is fetching, when the offer names things.
    std::vector<std::string> things;
};

/// One of the Forty, as a job needs to talk about them: their authored name and
/// the ward's own word for where they are.
///
/// Copied out of the NotableRegistry at load rather than looked up later, so the
/// board can compose a brief without holding a second reference to the owner's
/// registry -- and so an id that is not in that registry cannot reach a brief at
/// all, because it never got a row here.
struct ContractPerson {
    std::string id;
    std::string name;
    std::string place;
};

/// The board's authored half.
class ContractRaws {
public:
    /// Reads content/raws/contracts/contracts.json. NEVER throws -- a missing
    /// file leaves an empty board and every broker has nothing to say, which is
    /// the rule every raws loader in this build follows.
    ///
    /// REFUSES BY NAME. A broker, patron or source this file names that the
    /// owner's notables.json does not have is dropped, and an offer left with
    /// no patron or no source is dropped whole. That is what keeps a generated
    /// job from ever naming somebody who does not exist.
    [[nodiscard]] static ContractRaws load(const std::filesystem::path& contentDir,
                                           const NotableRegistry& notables,
                                           const FactionRegistry& factions);

    [[nodiscard]] bool loaded() const noexcept { return !offers_.empty(); }
    [[nodiscard]] const std::vector<ContractOffer>& offers() const noexcept { return offers_; }
    [[nodiscard]] const std::vector<ContractBroker>& brokers() const noexcept {
        return brokers_;
    }
    [[nodiscard]] const ContractBroker* broker(std::string_view id) const noexcept;
    /// How many offers this broker has templates for.
    [[nodiscard]] std::size_t offersFor(std::string_view brokerId) const noexcept;

    /// The ward's own name for an authored site id. Falls back to the id with
    /// its key prefix stripped and its underscores opened out, so a site the
    /// table has not been told about still reads as English.
    [[nodiscard]] std::string siteName(std::string_view siteId) const;

    /// Everybody any offer here can name, with their authored name and place.
    [[nodiscard]] const ContractPerson* person(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<ContractPerson>& people() const noexcept { return people_; }

private:
    std::vector<ContractBroker> brokers_;
    std::vector<ContractOffer> offers_;
    /// Ascending by site id.
    std::vector<std::pair<std::string, std::string>> sites_;
    /// Ascending by notable id.
    std::vector<ContractPerson> people_;
};

[[nodiscard]] std::filesystem::path contractRawsPath(const std::filesystem::path& contentDir);

// ---------------------------------------------------------------------------
// one job
// ---------------------------------------------------------------------------

enum class ContractState : std::uint8_t {
    /// On the board and not yet anybody's.
    Offered = 0,
    /// Taken. The clock is running.
    Taken = 1,
    /// Delivered and paid.
    Paid = 2,
    /// The night it was wanted by has gone.
    Expired = 3,
    /// The Watch took what it was for.
    Seized = 4,
};

[[nodiscard]] std::string_view contractStateName(ContractState state) noexcept;

struct Contract {
    /// Unique for the life of a board. day * kOffersPerDay + slot.
    std::int32_t id = 0;
    std::string offerId;
    /// notables.json ids.
    std::string broker;
    std::string patron;
    std::string source;
    /// What the topic list reads. ASCII, upper case, menu furniture.
    std::string label;
    /// The composed job, in the ward's own words.
    std::string brief;
    Contraband good = Contraband::Scalp;
    std::int32_t units = 0;
    std::int32_t pay = 0;
    std::int32_t postedOnDay = 0;
    /// The last day it can still be turned in.
    std::int32_t dueOnDay = 0;
    ContractState state = ContractState::Offered;
    /// Whether the Flame has signed for what this is asking for. Only ever
    /// wanted by the goods contrabandNeedsSanction names.
    bool sanctioned = false;

    [[nodiscard]] bool live() const noexcept { return state == ContractState::Taken; }
    [[nodiscard]] bool needsSanction() const noexcept {
        return contrabandNeedsSanction(good) && !sanctioned;
    }
};

// ---------------------------------------------------------------------------
// the board
// ---------------------------------------------------------------------------

/// How many jobs are on the board on any given night.
inline constexpr std::int32_t kOffersPerDay = 4;
/// And how many a player may be holding at once. Three, so a night has a shape:
/// enough to plan a route through the ward, few enough that "which one" is a
/// decision.
inline constexpr std::int32_t kMaxTakenContracts = 3;

/// What taking one answered.
enum class TakeResult : std::uint8_t {
    Taken = 0,
    NoSuchContract = 1,
    /// Already taken, paid or gone.
    NotOffered = 2,
    /// You are already carrying three.
    HandsFull = 3,
};

[[nodiscard]] std::string_view takeResultName(TakeResult result) noexcept;

/// What handing one in answered.
enum class TurnInResult : std::uint8_t {
    Paid = 0,
    NoSuchContract = 1,
    /// Not taken, or not this broker's.
    NotYours = 2,
    /// You do not have the goods.
    Short = 3,
    /// The night it was wanted by has gone.
    Late = 4,
    /// A scalp is redeemed under the Flame's mark or it is not redeemed.
    /// DECISIONS.md's tenure ruling: the Church "sanctions the redemption of a
    /// scalp".
    NeedsSanction = 5,
};

[[nodiscard]] std::string_view turnInResultName(TurnInResult result) noexcept;

struct Settlement {
    TurnInResult result = TurnInResult::NoSuchContract;
    std::int32_t pay = 0;
    std::int32_t unitsTaken = 0;
};

/// Everything on offer tonight, everything taken, and what became of it.
class ContractBoard {
public:
    /// Shared rather than borrowed, for exactly the reason FactionLedger::attach
    /// is: the director that owns this is built by a factory that returns by
    /// value.
    void attach(std::shared_ptr<const ContractRaws> raws);
    [[nodiscard]] const ContractRaws* raws() const noexcept { return raws_.get(); }

    /// Posts a fresh night's work.
    ///
    /// DETERMINISTIC AND PURE: everything drawn comes from the counter chain
    /// bound to (worldSeed, day), so the same day of the same world always
    /// offers the same four jobs, on any machine, whether or not the player was
    /// ever in the room. Contracts already TAKEN survive -- a job with three
    /// nights on it is not cancelled because the sun came up.
    void refresh(std::int32_t day, std::uint64_t worldSeed, const FactionLedger& standings);

    [[nodiscard]] std::int32_t day() const noexcept { return day_; }
    [[nodiscard]] const std::vector<Contract>& contracts() const noexcept { return rows_; }
    [[nodiscard]] const Contract* find(std::int32_t id) const noexcept;

    /// Ids this broker is offering right now, ascending.
    [[nodiscard]] std::vector<std::int32_t> offeredBy(std::string_view brokerId) const;
    /// Ids this broker is waiting on, ascending.
    [[nodiscard]] std::vector<std::int32_t> takenBy(std::string_view brokerId) const;
    [[nodiscard]] std::int32_t takenCount() const noexcept;
    [[nodiscard]] std::int32_t paidCount() const noexcept { return paid_; }
    [[nodiscard]] std::int32_t failedCount() const noexcept { return failed_; }
    [[nodiscard]] std::int32_t coinEarned() const noexcept { return earned_; }

    TakeResult take(std::int32_t id);

    /// Hands the goods over. Takes them OUT of the stash and answers with the
    /// pay, or with the reason there is none.
    Settlement turnIn(std::int32_t id, Stash& stash, std::int32_t day);

    /// The Flame signs for what is in the sack. Marks every taken contract
    /// whose goods want a mark. Returns how many it marked.
    std::int32_t sanction();

    /// The Watch emptied the sack. Every taken contract that wanted what was in
    /// it fails, because a job you cannot deliver is a job you have lost --
    /// which is what makes an arrest cost something beyond the night.
    std::int32_t seizeFor(const Stash& before);

    void hashInto(HashSink& sink) const;

private:
    [[nodiscard]] Contract* rowFor(std::int32_t id) noexcept;
    void expireStale();

    std::shared_ptr<const ContractRaws> raws_;
    /// Ascending by id. Never reordered.
    std::vector<Contract> rows_;
    std::int32_t day_ = -1;
    std::int32_t paid_ = 0;
    std::int32_t failed_ = 0;
    std::int32_t earned_ = 0;
};

}  // namespace granadad::sim
