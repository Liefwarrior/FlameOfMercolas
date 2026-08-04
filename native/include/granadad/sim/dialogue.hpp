#pragma once

// A conversation: topics, not a single greeting.
//
// WHAT THIS IS FOR. Walking up to somebody and pressing a key used to produce
// one hardcoded sentence chosen by the speaker's ROLE. That is a barkline, not
// a conversation, and nothing social can be built on it. This opens a surface
// with a greeting that knows who you are, and a list of things you may ask
// about that is assembled from the owner's authored content and from what this
// particular person is allowed to know.
//
// THE DESIGN LAW IT OBEYS. DOCKS-GAZETTEER section 5.3: "People TELL the
// Wielder things. The investigation is never persuasion -- it is knowing WHERE
// to ask... No dialogue-skill checks exist; the gate is geographic and
// social-topological." So NO TOPIC IS EVER GATED BY A ROLL. A topic is on the
// list because the person in front of you is a party to that story, or because
// content/raws/rumors/rumors.json licenses them to repeat it. Skill decides
// exactly one thing in this file, and it is the price of a mug of ale.
//
// WHAT IT DOES NOT DO. It writes no dialogue. Every spoken line in the game
// comes out of content/raws/barks/barks.json, chosen by an authored key. The
// only strings this file composes are the player-facing LABELS on the topic
// list ("ASK ABOUT THE VANISHED CLERK"), which are menu furniture and not
// anybody's voice.
//
// SEPARATION. The director knows nothing about a tavern, a world or an Actor.
// It is handed a Speaker -- a flat description of who is talking -- and it
// hands back a Reply describing what was said and what the WORLD now owes:
// coin to move, an offence to report. Whoever owns the room applies those. That
// is what lets the whole social layer be tested with no map loaded, and what
// will let the next building reuse it without inheriting the Gull.
//
// NO FLOATS, NO DRAWS. Which authored row of a table is spoken is a pure
// function of who you are talking to and how many times you have done it
// before.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/barks.hpp"
#include "granadad/sim/barter.hpp"
#include "granadad/sim/contract.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/questline.hpp"
#include "granadad/sim/social.hpp"
#include "granadad/sim/spellbook.hpp"
#include "granadad/sim/spellforge.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// who is talking
// ---------------------------------------------------------------------------

/// Everything the dialogue layer needs to know about somebody. Flat on purpose:
/// see the header on why this is not an Actor.
struct Speaker {
    std::int32_t actorId = 0;
    std::string name;
    std::string epithet;
    /// A content/raws/names/notables.json id, or empty for somebody the raws
    /// never named. A notable has a personal table and a knowledge domain; a
    /// hired hand has the generic tables and is no less worth talking to.
    std::string notableId;
    JobFamily family = JobFamily::Serf;
    /// The trade they will talk shop about, and how far along they are.
    std::string skillId;
    std::int32_t skillLevel = 0;
    /// Their STREETWISE, which is what a haggle is fought with.
    std::int32_t haggleSkill = 0;
    /// How likely they are to notice a hand in their purse. Their own
    /// streetwise, unless the caller knows better.
    std::int32_t awareness = 0;
    /// Coin on them. What a lift would find.
    std::int32_t purse = 0;
    /// True when they have something to sell.
    bool trades = false;
    Goods goods = Goods::Drink;
    std::int32_t basePrice = 0;
    /// A mood.* override key when they are in no state for pleasantries --
    /// "mood.downed" for somebody on the floor. Empty for the usual case.
    std::string moodKey;

    // --- S4: who they are to the guilds --------------------------------------

    /// The faction they belong to, or "". DERIVED from the owner's raws -- the
    /// room knows their job family and factions.json says which faction claims
    /// that family's jobs. Deeds done to them are heard by this faction.
    std::string factionId;
    /// The faction they can sign the player onto, or "". Belonging to one and
    /// recruiting for it are different facts: every patron in the Gull is a
    /// dockhand and exactly one of them speaks for the gang.
    std::string recruitsFor;
    /// True when this person teaches craftings out of the spell raws, and will
    /// compose one with you once the ladder has opened the workshop.
    bool teaches = false;

    // --- S5: who they are to the roofs ---------------------------------------

    /// True when this person buys property that is not yours to sell. Exactly
    /// one body in the Gull does, and belonging to the Skyrunners is not
    /// enough: a cutpurse is not a fence, and the difference is the whole of
    /// what the guild's second rung is worth.
    bool buysStolen = false;
    /// True when leaning on this person is a thing that could work. False for
    /// the ones who would simply hit you: a bouncer, a watchman, and anybody
    /// whose job is to be leaned on for a living.
    bool leanable = false;
};

// ---------------------------------------------------------------------------
// topics
// ---------------------------------------------------------------------------

enum class TopicKind : std::uint8_t {
    /// Their own business. personal.<notableId>, or the generic table.
    Personal = 0,
    /// What the ward is saying. The generic gossip table.
    WardTalk = 1,
    /// One authored micro-history they are party to, or licensed to repeat.
    History = 2,
    /// The vanished clerk. Everybody has heard; almost nobody has looked.
    Quest = 3,
    /// Shop talk, out of the authored mastery tables for their trade.
    Mastery = 4,
    /// Open a haggle.
    Trade = 5,
    /// Stand them a drink out of your own purse.
    BuyDrinkFor = 6,
    /// A hand in their purse.
    PickPocket = 7,
    /// End it.
    Leave = 8,
    /// Buy the thing at whatever price is currently on the table. Appended
    /// after Leave on purpose: the ordinal is folded into which authored row a
    /// topic speaks from, so inserting in the middle would move every existing
    /// speaker's lines.
    Buy = 9,
    /// Sign on with a guild -- or take the Mission's oath, which is the same
    /// verb wearing the questline's own label. APPENDED, for the reason above.
    Join = 10,
    /// Ask for the next rung.
    Advance = 11,
    /// The beat of a questline this person is the party to.
    QuestBeat = 12,
    /// Be taught a crafting out of content/raws/spells/spells.json.
    Learn = 13,
    /// Open the workbench and compose one.
    Forge = 14,
    /// Sell them what was not yours to sell. APPENDED, for the reason on Buy:
    /// the ordinal is folded into which authored row a topic speaks from.
    Fence = 15,
    /// Lean on them for coin.
    Lean = 16,
    /// Ask your own guild for the one thing a top rung is actually for.
    Favour = 17,
    /// S6. Take a job off somebody who hands them out. The payload is the
    /// contract's own id, or -1 when the broker will not talk to you yet --
    /// which is a topic on purpose, because a player has to be able to ask
    /// before they can be told no. APPENDED, for the reason on Buy: the ordinal
    /// is folded into which authored row a topic speaks from.
    TakeContract = 18,
    /// Hand the goods over and be paid.
    TurnIn = 19,
    /// Ask a priest of the Flame to sign for what is in your sack.
    /// DECISIONS.md's tenure ruling: the Church "sanctions the redemption of a
    /// scalp", so the ward's own bounty is redeemed under a priest's mark and
    /// not otherwise.
    Sanction = 20,
};

[[nodiscard]] std::string_view topicKindName(TopicKind kind) noexcept;

/// What standing somebody a drink costs the player. The same two coin a drink
/// costs across the Gull's bar -- named here rather than reached for out of
/// tavern.hpp, because the dialogue layer must not know what a tavern is. A
/// test pins the two to each other, so a change to one is a change to both.
inline constexpr std::int32_t kBoughtDrinkCost = 2;

struct Topic {
    TopicKind kind = TopicKind::Leave;
    /// What the player reads. ASCII, upper case, short enough for the panel.
    std::string label;
    /// The authored barks.json key this topic speaks from. Empty for the verbs,
    /// which do something instead of saying something.
    std::string barkKey;
    /// History index for TopicKind::History, coin for the verbs that cost it,
    /// stage index for the questline beats.
    std::int32_t payload = 0;
    /// A questline id, or a faction id, or empty. The one string a topic needs
    /// to know WHICH ladder or WHICH line it is about -- a payload integer
    /// would have made the topic depend on load order.
    std::string arg;
};

/// What choosing a topic produced.
struct Reply {
    bool ok = false;
    TopicKind kind = TopicKind::Leave;
    /// Somebody's authored voice, or a short report of what just happened.
    std::string line;
    std::int32_t dispositionBefore = 0;
    std::int32_t dispositionAfter = 0;
    Attitude attitude = Attitude::Neutral;
    /// Coin the WORLD must move. Negative leaves the player's purse.
    std::int32_t coinDelta = 0;
    /// The house has to be told: a hand in a purse, in a room with people in it.
    bool offence = false;
    /// The conversation is over.
    bool closes = false;
    /// True when a haggle is now waiting for a number.
    bool haggling = false;
    /// True when the workbench is now open and waiting for a composition.
    bool forging = false;
    /// TopicKind::Buy declares an INTENT and nothing more -- the director does
    /// not know what a cellar is. Whoever owns the counter resolves it and
    /// fills in the line and the coin.
    bool wantsPurchase = false;
    /// A questline moved. What the journal just wrote, or empty -- so the HUD
    /// can say "the journal is longer" without reading the journal.
    std::string journalLine;
    /// True when this reply put the player on a rung they were not on.
    bool ranked = false;
    /// S6. The contract this reply moved, or -1. The room does not need it --
    /// the coin is in coinDelta like every other payment -- but a HUD that
    /// wants to say which job just closed does, and so does a test.
    std::int32_t contractId = -1;
    /// S5. The act the world has to be told about, and whether it happened in
    /// front of anybody. kCrimeCount means "nothing criminal happened" -- the
    /// dialogue layer does not know what a taproom is, so whoever owns the room
    /// decides who saw it and reports back.
    Crime crime = Crime::Lift;
    bool criminal = false;
};

// ---------------------------------------------------------------------------
// the director
// ---------------------------------------------------------------------------

/// Owns the authored content, the ledger, the player's skills and the haggle,
/// and runs one conversation at a time.
class DialogueDirector {
public:
    /// Attaches an EMPTY faction registry, so a default-constructed director is
    /// answerable rather than a null dereference waiting to happen.
    DialogueDirector();
    /// Reads barks, notables, histories, rumor domains, the skill vocabulary,
    /// the faction registry and its ladders, every authored questline and the
    /// spell shelf. NEVER throws.
    [[nodiscard]] static DialogueDirector load(const std::filesystem::path& contentDir);

    [[nodiscard]] const BarkTables& barks() const noexcept { return barks_; }
    [[nodiscard]] const NotableRegistry& notables() const noexcept { return notables_; }
    [[nodiscard]] SocialLedger& ledger() noexcept { return ledger_; }
    [[nodiscard]] const SocialLedger& ledger() const noexcept { return ledger_; }
    [[nodiscard]] SkillTrack& skills() noexcept { return skills_; }
    [[nodiscard]] const SkillTrack& skills() const noexcept { return skills_; }
    [[nodiscard]] const Haggle& haggle() const noexcept { return haggle_; }

    // --- S4: the guilds, the lines and the book -----------------------------

    [[nodiscard]] const FactionRegistry& factions() const noexcept { return *factions_; }
    [[nodiscard]] FactionLedger& standings() noexcept { return standings_; }
    [[nodiscard]] const FactionLedger& standings() const noexcept { return standings_; }
    [[nodiscard]] const QuestBook& quests() const noexcept { return quests_; }
    [[nodiscard]] QuestJournal& journal() noexcept { return journal_; }
    [[nodiscard]] const QuestJournal& journal() const noexcept { return journal_; }
    [[nodiscard]] const Spellbook& spellbook() const noexcept { return spellbook_; }
    [[nodiscard]] Grimoire& grimoire() noexcept { return grimoire_; }
    [[nodiscard]] const Grimoire& grimoire() const noexcept { return grimoire_; }

    // --- S5: the trade on the roofs -----------------------------------------

    [[nodiscard]] CrimeLedger& crimes() noexcept { return crimes_; }
    [[nodiscard]] const CrimeLedger& crimes() const noexcept { return crimes_; }

    /// THE ONE CALL SITE EVERY CRIMINAL ACT GOES THROUGH, wherever it happened
    /// -- a topic on this list, a box cracked upstairs, a bale carried out of a
    /// door, a body landing on a roof. It moves all four things an act should
    /// move and nothing else can move them: the tally, the Watch's heat, the
    /// roofs' opinion of you and -- by the mirror ranks.json already declares
    /// -- the garrison's. An act that reached one of those and missed the
    /// others is the bug this shape exists to make impossible.
    void noteCrime(Crime crime, bool witnessed);

    /// One more of whatever a counted stage might be counting. Named by string
    /// because the STAGE names it, in the owner's own raws, and no C++ here
    /// knows the list.
    void noteTally(std::string_view counter);

    /// The registry index of the Skyrunners, or -1.
    [[nodiscard]] std::int32_t roofsIndex() const noexcept;

    // --- S6: the work, and what is in the sack -------------------------------

    [[nodiscard]] ContractBoard& contracts() noexcept { return board_; }
    [[nodiscard]] const ContractBoard& contracts() const noexcept { return board_; }
    /// The authored templates, or an empty set when the raws were not found.
    [[nodiscard]] const ContractRaws& contractRaws() const noexcept { return *contractRaws_; }
    /// Posts a night's work. Called by whoever owns the clock -- the room does
    /// it when the doors open, exactly where it restocks the cellar.
    void postContracts(std::int32_t day, std::uint64_t worldSeed);
    /// Whether this broker will talk about work to this player at all. Public
    /// because the room's own scripted runs and the tests both need to ask.
    [[nodiscard]] bool brokerWillTalk(const ContractBroker& broker) const noexcept;

    /// What the world says the player is carrying. Set before choose(), so the
    /// director can refuse a round it cannot pay for. Not hashed here -- the
    /// purse is the room's state and the room hashes it.
    void setPlayerCoin(std::int32_t coin) noexcept { playerCoin_ = coin; }
    [[nodiscard]] std::int32_t playerCoin() const noexcept { return playerCoin_; }

    // --- one conversation ---------------------------------------------------

    /// Opens on somebody. Returns false when there is nothing to talk with.
    bool open(const Speaker& speaker, std::int32_t secondOfDay);
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] const Speaker& speaker() const noexcept { return speaker_; }
    [[nodiscard]] const std::vector<Topic>& topics() const noexcept { return topics_; }
    /// The greeting they opened with -- from greet.<family>.<attitude>.<band>,
    /// so a hostile docker at four in the morning is a different sentence from
    /// a warm one at noon. This is where standing becomes AUDIBLE.
    [[nodiscard]] const std::string& greeting() const noexcept { return greeting_; }
    /// The last thing said, greeting included.
    [[nodiscard]] const std::string& lastLine() const noexcept { return lastLine_; }
    [[nodiscard]] Attitude attitude() const noexcept { return attitude_; }
    /// The authored key the greeting came out of. Exposed so a test can assert
    /// the CONTENT changed and not merely the number behind it.
    [[nodiscard]] const std::string& greetingKey() const noexcept { return greetingKey_; }

    /// Picks a topic off the list.
    Reply choose(std::size_t index);

    // --- haggling -----------------------------------------------------------

    [[nodiscard]] bool isHaggling() const noexcept { return haggle_.active(); }
    /// The player names a number.
    Reply offerPrice(std::int32_t coins);
    /// The player takes the price on the table.
    Reply takeAsking();
    /// The player leaves the counter.
    Reply endHaggle();

    // --- the workbench ------------------------------------------------------
    //
    // Shaped exactly like the haggle above, and for the same reason: composing
    // a crafting is a conversation with rounds, not a modal dialog. The bench
    // is simulation state; the surface only draws it.

    [[nodiscard]] bool isForging() const noexcept { return bench_.active; }
    [[nodiscard]] const ForgeBench& bench() const noexcept { return bench_; }
    /// Walks the cursor between MOVES / SHAPE / HOW MUCH / HOW LONG / ACROSS.
    void moveForgeField(std::int32_t delta);
    /// Changes the value under the cursor.
    void adjustForge(std::int32_t delta);
    /// Says "make it". Refused compositions are refused OUT LOUD, out of the
    /// authored forge.refused table, and leave the bench open to be fixed.
    Reply commitForge();
    /// Puts the tools down.
    Reply endForge();

    void hashInto(HashSink& sink) const;

private:
    void buildTopics();
    [[nodiscard]] std::int32_t rowIndexFor(TopicKind kind, std::int32_t payload) const noexcept;
    [[nodiscard]] std::string speak(const std::vector<std::string>& chain, TopicKind kind,
                                    std::int32_t payload);
    [[nodiscard]] Reply reply(TopicKind kind, std::string line);
    [[nodiscard]] Reply settleHaggle(HaggleOutcome outcome);

    /// Records a deed against the person AND against the guild that claims
    /// them. One call site, so a deed can never reach one ledger and miss the
    /// other.
    void recordDeed(Deed deed);
    /// The registry index of the speaker's own faction, or -1.
    [[nodiscard]] std::int32_t speakerFaction() const noexcept;
    /// The authored chain for a faction verb: faction.<id>.<verb>, then the
    /// generic faction.<verb>.
    [[nodiscard]] std::vector<std::string> factionChain(std::string_view factionId,
                                                        std::string_view verb) const;
    /// Applies one stage's rewards and moves the journal on.
    [[nodiscard]] Reply completeStage(const Questline& line, std::int32_t stageIndex,
                                      TopicKind kind);
    /// One more person stood a drink. Counts toward any counted stage that is
    /// currently wanted, and toward none that is not.
    void noteAlmsGiven();
    /// The broker this notable is, or nullptr. Three of the Forty are one.
    [[nodiscard]] const ContractBroker* brokerFor(std::string_view notableId) const noexcept;
    /// True when the player is carrying goods a taken contract cannot be paid
    /// for without the Flame's mark.
    [[nodiscard]] bool wantsSanction() const noexcept;

    BarkTables barks_;
    NotableRegistry notables_;
    SocialLedger ledger_;
    SkillTrack skills_;
    Haggle haggle_;
    /// Shared rather than held, so a copy of the director and its ledger cannot
    /// end up pointing at two different registries. See FactionLedger::attach.
    std::shared_ptr<const FactionRegistry> factions_;
    FactionLedger standings_;
    QuestBook quests_;
    QuestJournal journal_;
    Spellbook spellbook_;
    Grimoire grimoire_;
    CrimeLedger crimes_;
    /// Shared for the same reason the faction registry is: the director is
    /// built by a factory that returns by value.
    std::shared_ptr<const ContractRaws> contractRaws_;
    ContractBoard board_;
    ForgeBench bench_;
    /// Which authored line the bench was opened for, and at which stage. Held
    /// across the composition because a bench is a conversation with rounds and
    /// the stage it satisfies must not be re-derived from a topic list that has
    /// been rebuilt underneath it.
    std::string forgeQuestId_;
    std::int32_t forgeStage_ = 0;

    bool open_ = false;
    Speaker speaker_;
    std::int32_t secondOfDay_ = 0;
    std::int32_t talkIndex_ = 0;
    Attitude attitude_ = Attitude::Neutral;
    std::string greeting_;
    std::string greetingKey_;
    std::string lastLine_;
    std::vector<Topic> topics_;
    std::int32_t playerCoin_ = 0;
    /// Bumped by every conversation opened. Hashed, so replaying the same
    /// actions reproduces the same rotation through the authored rows.
    std::int32_t conversations_ = 0;
};

}  // namespace granadad::sim
