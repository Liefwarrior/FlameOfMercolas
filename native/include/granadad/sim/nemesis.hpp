#pragma once

// The man who put you down, and what he became for it.
//
// ELI'S OWN DESIGN, in his words: "The player respawns when killed, but the
// person who killed them should rise in political rank and retain a rivalry
// with the player. Maybe the player could be killed by a laborer in a fist
// fight but next time the player returns that person might be promoted to a
// carpenter who's the head of a new Carpenter's Guild or something."
//
// So: PUTTING THE PLAYER DOWN IS A PROMOTION EVENT. Not a scoreboard, not a
// flag on an actor -- a rise through the systems the ward already runs on.
//
// WHAT A WIN ACTUALLY MOVES, and every one of these is an existing system
// rather than a parallel one:
//
//   THE LADDER    ranks.json already hangs a rung-by-rung ladder off each of
//                 the owner's five factions, with authored TITLES. A winner
//                 climbs the ladder of the faction that claims HIS OWN JOB, and
//                 the title he answers to afterwards is read out of that file
//                 by index. Nothing here invents a rank name.
//   THE WARD'S    FactionLedger::shiftInfluence is what already decides what a
//   WEIGHT        counter charges a stranger (guildPricePercent) and how long a
//                 bouncer lets you finish your drink (graceSecondsForPlayer).
//                 A winner's guild gains weight and its declared rival loses
//                 the same -- the mirror, unchanged, applied to a new cause.
//   AN            At kFoundsAtWins he founds one of the authored chapters in
//   INSTITUTION   content/raws/factions/chapters.json: a real trade house with
//                 a canon seat, a parent faction, a ROSTER of real actors
//                 enlisted out of the room, and a TOLL its members add to every
//                 price the player is quoted. That toll is permanent.
//   THE ROLL      At kTakesChargeAtWins he petitions the Flame for a vacant
//                 charge and becomes DEN DUKE of a compound -- Ward::grantCharge
//                 on the same roll, with the same consequences, as
//                 Ward::petitionForCharge gives the player. Section 2.8 says a
//                 vacant charge is a prize and "any actor -- including the
//                 player -- may petition for it". This is an actor doing it.
//   HIS MEMORY    SocialLedger, driven hostile, so he greets you out of a
//                 different authored table. He is also carrying something
//                 heavier every time (nemesisWeapon), meaning it more
//                 (nemesisIntent), and past kHuntsAtGrudge he comes looking.
//
// PERMANENCE IS THE POINT. Nothing here decays, nothing here is refunded by
// beating him afterwards: recordVictory takes the grudge down and leaves the
// rank, the chapter, the toll and the charge exactly where they are. You can
// win the rematch. You cannot un-found his guild.
//
// THE PERSISTENT-WARD DOOR, LEFT OPEN ON PURPOSE. Eli ruled 2026-07-31 that the
// variant where the city survives your death and you return as somebody else is
// NOT being built from the start -- "it's easy to change that later". So the
// PLAYER respawns as themselves and this file knows nothing about who the
// player is. Every record here is keyed on the WINNER: a stable name plus the
// room's roster id, in that order of authority, because a roster id is a fact
// about one building's cast and a name survives the building being rebuilt. The
// day a save carries a second protagonist, the book is loaded unchanged and a
// player identity is added beside it; no record in here has to be rewritten.
//
// NO FLOATS. NO UNORDERED CONTAINERS: rivals are a vector kept sorted by actor
// id and chapters are a dense vector in the raws' own sorted order.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/brawl.hpp"
#include "granadad/sim/compound.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/social.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the raws: what a risen man may found
// ---------------------------------------------------------------------------

/// One authored institution a risen nemesis may found.
///
/// AUTHORED, NOT GENERATED. A guild that appeared out of a string template
/// would be a guild with no seat, no parent and no canon behind it. Every row
/// in content/raws/factions/chapters.json names a faction the owner's own
/// factions.json has, a trade skills.json has, and a place
/// docs/design/DOCKS-GAZETTEER.md keys -- and a row that fails any of those is
/// refused at load, by name, the same way the compound roll and the contract
/// board refuse a notable the registry does not have.
struct ChapterRaw {
    std::string id;
    std::string displayName;
    /// The factions.json id this chapter rises INSIDE. A new guild is not a
    /// sixth faction -- the owner's registry is canon and has five -- it is a
    /// house within one, which is what a Craftlord's trade house actually is.
    std::string faction;
    /// The skills.json id of the trade it is a guild OF. This is what decides
    /// WHICH chapter a given winner founds: the man's own trade.
    std::string trade;
    /// The gazetteer key of its seat: "K23", "K07". Documentation for the
    /// player-facing line, and the one string that ties it to the map.
    std::string site;
    /// The compounds.json plot id whose charge its head eventually takes, or
    /// "" for a chapter with no ground ambitions.
    std::string seat;
    /// What founding it is worth to the parent faction's weight in the ward.
    std::int32_t influence = 0;
    /// The cut its members take off a stranger, in percent, added to every
    /// price the player is quoted at a counter that faction runs. THE TRADE
    /// IMPACT, and it is permanent.
    std::int32_t toll = 0;
    /// What its head answers to. Canon's own word: DOCKS-GAZETTEER section 2.8
    /// calls the men who hold working ground on these terms CRAFTLORDS.
    std::string founderTitle;
};

/// content/raws/factions/chapters.json, cross-checked against the owner's own
/// faction registry.
class ChapterRaws {
public:
    /// NEVER throws. A missing file leaves an empty set and a nemesis simply
    /// never founds anything -- he still ranks, still remembers and still comes
    /// prepared, which is the same rule every other loader in this build
    /// follows: the game must boot while a content file is being edited.
    [[nodiscard]] static ChapterRaws load(const std::filesystem::path& contentDir,
                                          const FactionRegistry& factions);

    [[nodiscard]] bool loaded() const noexcept { return !chapters_.empty(); }
    [[nodiscard]] const std::vector<ChapterRaw>& chapters() const noexcept { return chapters_; }
    [[nodiscard]] const ChapterRaw* at(std::int32_t index) const noexcept;
    /// The chapter a man of this trade would found, or -1. Trade first, and the
    /// faction only as a fallback, because the guild is a guild OF something.
    [[nodiscard]] std::int32_t forTrade(std::string_view trade,
                                        std::string_view factionId) const noexcept;
    /// Rows the file carried that the faction registry refused.
    [[nodiscard]] std::int32_t refused() const noexcept { return refused_; }

private:
    /// Ascending by id. Never reordered after load.
    std::vector<ChapterRaw> chapters_;
    std::int32_t refused_ = 0;
};

[[nodiscard]] std::filesystem::path chapterRawsPath(const std::filesystem::path& contentDir);

// ---------------------------------------------------------------------------
// the numbers
// ---------------------------------------------------------------------------

/// Wins it takes before a man founds his own house, and before he holds ground.
///
/// TWO AND THREE, and the shape is deliberate: the FIRST win costs you nothing
/// but a rung and a grudge, so a player who loses one fist fight has made an
/// enemy rather than triggered a cutscene. The second one changes the ward's
/// prices. The third one puts him on the roll, which is permanent in the sense
/// that section 2.8 means: a thing is true in Granadad when it is on the roll.
inline constexpr std::int32_t kFoundsAtWins = 2;
inline constexpr std::int32_t kTakesChargeAtWins = 3;

/// What one win is worth to his guild's weight in the ward. Small, because the
/// mirror takes the same off the rival and five wins should not flip the
/// district; the chapter's own influence is the step change.
inline constexpr std::int32_t kInfluencePerWin = 4;

/// How much he hates you per win, and how much beating him takes back off it.
/// Not symmetric on purpose: winning a rematch cools a man down, it does not
/// make him forget which of you started it.
inline constexpr std::int32_t kGrudgePerWin = 34;
inline constexpr std::int32_t kGrudgePerLoss = 20;
inline constexpr std::int32_t kGrudgeMax = 100;

/// The grudge at which he stops waiting for you to come to him.
inline constexpr std::int32_t kHuntsAtGrudge = 60;

/// Where a win leaves his opinion of you. Straight onto the hostile band --
/// social.hpp's kHostileAtOrBelow is -40, and this clears it on the FIRST win,
/// deliberately: a man who has had you on the floor does not greet you out of
/// the merely-cold table. Deeper with every win after that.
inline constexpr std::int32_t kDispositionPerWin = -45;

/// The most every founded chapter together may add to a price, in percent. A
/// guild takes a cut; it does not close the market.
inline constexpr std::int32_t kTollCap = 25;

/// The share of the player's purse the winner goes through their coat for.
/// Being put down in the Docks costs money.
inline constexpr std::int32_t kPurseTakenPercent = 25;

/// Hours the player is out of it. Long enough that the room has moved on and
/// the rest of the evening is gone, short enough that three bad nights are not
/// a week. Eli's own framing is "next time the player returns" -- coming back
/// is a thing the player does, and this is only how long the floor kept them.
inline constexpr std::int32_t kBlackoutHours = 3;

/// What the winner is carrying by the time he has won this often, and how far
/// he means to take it.
///
/// THIS IS THE "COMES PREPARED" CLAUSE AND IT IS NOT COSMETIC. Weapon::Edged
/// and Intent::Kill are exactly the two things brawl.hpp's rule watches: a
/// nemesis on his third win turns a taproom scuffle into a LETHAL fight by
/// classifyFight's own definition, which the room refuses to resolve with fist
/// rules. The man who beat you twice is not somebody you can casually swing at
/// any more, and the rule that says so was written in S2.
[[nodiscard]] Weapon nemesisWeapon(std::int32_t wins) noexcept;
[[nodiscard]] Intent nemesisIntent(std::int32_t wins) noexcept;

// ---------------------------------------------------------------------------
// the record
// ---------------------------------------------------------------------------

/// One person who has put the player on the floor, and everything they got for
/// it. SIMULATION STATE: hashed, byte-encodable, and never reset.
struct Nemesis {
    /// The room's roster id. Valid for as long as that room's cast is.
    std::int32_t actorId = 0;
    /// THE STABLE KEY. A roster id is a fact about one building's cast; the
    /// name is what a save file and a persistent-ward variant would match on.
    std::string who;
    std::string epithet;
    /// Registry index of the faction that claims his trade, or -1.
    std::int32_t faction = -1;
    /// Which rung of that faction's authored ladder he stands on. 0 is not on
    /// it at all, 1 is the first.
    std::int32_t rank = 0;
    /// The rung's authored title, straight out of ranks.json -- or the
    /// chapter's founderTitle once he has one of his own.
    std::string title;
    /// Index into ChapterRaws, or -1.
    std::int32_t chapter = -1;
    /// The actors he enlisted when he founded it. Ascending, deduplicated. A
    /// guild with no members is a letterhead.
    std::vector<std::int32_t> members;
    /// The plot on the roll he holds the charge of, or -1.
    std::int32_t plot = -1;
    std::int32_t wins = 0;
    std::int32_t losses = 0;
    std::int32_t grudge = 0;
    std::int32_t firstWinDay = 0;
    std::int32_t lastWinDay = 0;

    /// He has stopped waiting at his post.
    [[nodiscard]] bool hunts() const noexcept { return grudge >= kHuntsAtGrudge; }
    [[nodiscard]] Weapon weapon() const noexcept { return nemesisWeapon(wins); }
    [[nodiscard]] Intent intent() const noexcept { return nemesisIntent(wins); }
    [[nodiscard]] bool foundedAHouse() const noexcept { return chapter >= 0; }
    [[nodiscard]] bool holdsGround() const noexcept { return plot >= 0; }
};

/// Who went down, and to whom. Filled in by whoever owns the fight.
struct Defeat {
    std::int32_t actorId = 0;
    std::string who;
    std::string epithet;
    /// The job family the ROOM says he has -- "serf", "trade", "watch". The
    /// faction is derived from it through the owner's own factions.json and is
    /// never tabulated here, which is the same rule Tavern::factionOf follows.
    std::string jobPrefix;
    /// His trade, a skills.json id. What kind of guild he would found.
    std::string trade;
    /// The world day. Monotonic; see Tavern::dayNumber.
    std::int32_t day = 0;
    /// What the player was carrying when they went down.
    std::int32_t playerCoin = 0;
};

/// Everything a rise has to reach. Nothing here is owned, and every one of them
/// may be null: a rise that cannot reach the roll still ranks, still founds and
/// still remembers -- it simply does not take a charge.
struct RiseWorld {
    FactionLedger* guilds = nullptr;
    SocialLedger* ledger = nullptr;
    Ward* roll = nullptr;
    /// Everybody in the room who could be enlisted, and the faction each of
    /// them belongs to. Parallel vectors in roster order, because a map here
    /// would be an ordering the raws do not own.
    std::vector<std::int32_t> presentIds;
    std::vector<std::int32_t> presentFactions;
};

/// What one defeat did. Everything a HUD line, a bark and a test need.
struct Rise {
    bool happened = false;
    std::int32_t actorId = -1;
    std::string who;
    std::int32_t wins = 0;
    /// The rung he is on now, and its authored title.
    std::int32_t rank = 0;
    std::string title;
    /// True when THIS defeat was the one that put him a rung higher.
    bool promoted = false;
    /// True when THIS defeat founded the house. Once, ever, per nemesis.
    bool founded = false;
    std::string chapterName;
    std::int32_t members = 0;
    /// True when THIS defeat put him on the ward's roll as a Den Duke.
    bool tookCharge = false;
    std::string plotName;
    /// Coin that left the player's purse into his.
    std::int32_t coinTaken = 0;
    /// The authored barks.json key his taunt should be spoken from. The
    /// dialogue layer resolves it; nothing here writes a line.
    std::string barkKey;
    /// What he actually said, once somebody with the bark tables in hand has
    /// resolved barkKey. Empty until then, and empty forever if the owner's
    /// content does not author that key -- which is the same rule every other
    /// spoken line in this build follows.
    std::string taunt;
    /// A short report for the message line. Not anybody's voice.
    std::string line;
};

// ---------------------------------------------------------------------------
// the book
// ---------------------------------------------------------------------------

/// Everybody who has ever beaten the player, and what the ward gave them.
class NemesisBook {
public:
    /// Reads the chapter raws. NEVER throws.
    [[nodiscard]] static NemesisBook load(const std::filesystem::path& contentDir,
                                          std::shared_ptr<const FactionRegistry> factions);

    /// Points the book at a registry with no raws behind it. A default book is
    /// answerable rather than a null dereference waiting to happen.
    void attach(std::shared_ptr<const FactionRegistry> factions);

    [[nodiscard]] const FactionRegistry* factions() const noexcept { return factions_.get(); }
    [[nodiscard]] const ChapterRaws& chapters() const noexcept { return chapters_; }
    [[nodiscard]] const std::vector<Nemesis>& rivals() const noexcept { return rivals_; }
    [[nodiscard]] const Nemesis* of(std::int32_t actorId) const noexcept;
    [[nodiscard]] const Nemesis* byName(std::string_view who) const noexcept;
    /// How many times the player has been put down, all told.
    [[nodiscard]] std::int32_t defeats() const noexcept { return defeats_; }
    /// The rival with the most wins, or nullptr. What the HUD names.
    [[nodiscard]] const Nemesis* worst() const noexcept;

    // --- the one call site --------------------------------------------------

    /// THE PLAYER WENT DOWN. Everything a defeat moves, moves here, and nothing
    /// else can move it: the rung, the ward's weight, the house, the roll, the
    /// memory and the purse. A defeat that reached one of those and missed the
    /// others is the bug this shape exists to make impossible -- the same
    /// reason DialogueDirector::noteCrime is one function.
    Rise recordDefeat(const Defeat& defeat, const RiseWorld& world);

    /// And the other way round: the player put one of them down. Takes the
    /// grudge off and leaves EVERYTHING ELSE STANDING. You can win the
    /// rematch; you cannot un-found his guild.
    void recordVictory(std::int32_t actorId);

    /// The extra percent a counter run by a member of `factionIndex` charges
    /// the player, because a chapter of that faction now takes a cut. Clamped
    /// to kTollCap, and 0 for a faction nobody has founded a house in.
    [[nodiscard]] std::int32_t tollPercent(std::int32_t factionIndex) const noexcept;

    /// Whether this actor is enlisted in any founded chapter.
    [[nodiscard]] bool enlisted(std::int32_t actorId) const noexcept;

    // --- persistence --------------------------------------------------------

    /// A deterministic little-endian byte string, versioned, refusing anything
    /// it does not recognise. THE SEAM A SAVE FILE USES, and the seam a
    /// persistent-ward variant grows a player identity beside.
    [[nodiscard]] std::vector<std::uint8_t> encode() const;
    [[nodiscard]] static bool decode(const std::vector<std::uint8_t>& bytes, NemesisBook& out);

    void hashInto(HashSink& sink) const;

private:
    [[nodiscard]] Nemesis& entryFor(const Defeat& defeat);
    /// The rung title `rank` on `faction`'s authored ladder, or "".
    [[nodiscard]] std::string titleFor(std::int32_t faction, std::int32_t rank) const;

    std::shared_ptr<const FactionRegistry> factions_;
    ChapterRaws chapters_;
    /// NOT a maintained invariant: entryFor() inserts a fresh rival in
    /// actorId order, but reassigns an EXISTING rival's actorId in place (a
    /// stable name meeting a new roster id) without re-sorting -- see of()'s
    /// own note on the S9 fix. Every lookup here is a linear scan for exactly
    /// that reason; nothing may binary-search this vector.
    std::vector<Nemesis> rivals_;
    std::int32_t defeats_ = 0;
};

// ---------------------------------------------------------------------------
// VERIFICATION GAPS (S8) -- what this file does NOT do
// ---------------------------------------------------------------------------
//
// COMBAT IS IN THE WORLD NOW (action-combat build, COMBAT-ACTION-SPEC.md). A
// nemesis means the player Harm from his first win, and brawl.hpp's third
// clause says beating a BLOODIED man while meaning him Harm is Lethal whatever
// is in your hands -- so every rematch turns lethal partway through. Under the
// action model a lethal fight keeps resolving in the world under lethal rules
// (blows kill, floors lifted): the player's own lethal blows land through
// Tavern::playerAttackUp, and losing one routes the same applyDefeat this book
// reads. Tavern::concedeTo remains the direct defeat seam a scripted proof
// drives; the NPC side of a lethal rematch is still wired to the shipped
// refuse-and-latch behaviour until the presentation lane repoints the scripted
// arc onto the new verbs. A model changed, not a rule bent.
//
// VERIFICATION GAP (S8): HE HUNTS, HE DOES NOT AMBUSH. Past kHuntsAtGrudge a
// rival crosses the room to wherever the player is and stands there. He never
// throws the first punch: starting a fight is still the player's verb, and an
// NPC who opens one wants an aggression model this build does not have.
//
// VERIFICATION GAP (S8): ONE ROOM. A nemesis rises out of the Gilded Gull's
// fourteen and nowhere else, because the Gull is still the only staffed
// building in the district. His trade house has a canon SEAT in
// docs/design/DOCKS-GAZETTEER.md section 3 and nobody can walk to it.
//
// VERIFICATION GAP (S8): THE MEMBERS DO NOT KNOW. A founded chapter enlists
// real actors by id and the roster is hashed, but nothing an enlisted member
// does is different for it -- no greeting, no price, no schedule. The toll is
// charged against the parent FACTION rather than against the member, so a
// chapter's weight reaches a counter its members do not personally keep.
//
// VERIFICATION GAP (S8): NOTHING IS WRITTEN TO DISK. encode()/decode() are
// proven by round trip and nothing calls them, which is the same gap
// SocialLedger has carried since S3 and for the same reason.

}  // namespace granadad::sim
