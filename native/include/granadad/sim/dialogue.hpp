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
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/barks.hpp"
#include "granadad/sim/barter.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/social.hpp"
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
    /// History index for TopicKind::History, coin for the verbs that cost it.
    std::int32_t payload = 0;
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
    /// TopicKind::Buy declares an INTENT and nothing more -- the director does
    /// not know what a cellar is. Whoever owns the counter resolves it and
    /// fills in the line and the coin.
    bool wantsPurchase = false;
};

// ---------------------------------------------------------------------------
// the director
// ---------------------------------------------------------------------------

/// Owns the authored content, the ledger, the player's skills and the haggle,
/// and runs one conversation at a time.
class DialogueDirector {
public:
    DialogueDirector() = default;
    /// Reads barks, notables, histories, rumor domains and the skill
    /// vocabulary. NEVER throws.
    [[nodiscard]] static DialogueDirector load(const std::filesystem::path& contentDir);

    [[nodiscard]] const BarkTables& barks() const noexcept { return barks_; }
    [[nodiscard]] const NotableRegistry& notables() const noexcept { return notables_; }
    [[nodiscard]] SocialLedger& ledger() noexcept { return ledger_; }
    [[nodiscard]] const SocialLedger& ledger() const noexcept { return ledger_; }
    [[nodiscard]] SkillTrack& skills() noexcept { return skills_; }
    [[nodiscard]] const SkillTrack& skills() const noexcept { return skills_; }
    [[nodiscard]] const Haggle& haggle() const noexcept { return haggle_; }

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

    void hashInto(HashSink& sink) const;

private:
    void buildTopics();
    [[nodiscard]] std::int32_t rowIndexFor(TopicKind kind, std::int32_t payload) const noexcept;
    [[nodiscard]] std::string speak(const std::vector<std::string>& chain, TopicKind kind,
                                    std::int32_t payload);
    [[nodiscard]] Reply reply(TopicKind kind, std::string line);
    [[nodiscard]] Reply settleHaggle(HaggleOutcome outcome);

    BarkTables barks_;
    NotableRegistry notables_;
    SocialLedger ledger_;
    SkillTrack skills_;
    Haggle haggle_;

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
