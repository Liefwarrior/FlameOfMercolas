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

    // --- S8: what he is to you ------------------------------------------------

    /// How many times this person has put the player on the floor. Zero for
    /// everybody who never has, which is nearly everybody.
    std::int32_t rivalWins = 0;
    /// The rung or the house-title he answers to because of it, or "".
    std::string rivalTitle;
    /// The trade house he founded over the player's body, or "".
    std::string rivalHouse;

    // --- #79: what does not have words ----------------------------------------

    /// True for a body that answers in stage directions rather than sentences.
    ///
    /// ADDED WHEN THE WHOLE WARD BECAME ADDRESSABLE. The Docks holds cats,
    /// dogs, strays and mice as well as people, and greet.beast is authored in
    /// the owner's own barks.json for exactly that -- "(it watches you
    /// sidelong)". What is NOT authored, and would be absurd, is a cat with an
    /// opinion about the harbourmaster's ledger: every other topic on the list
    /// assumes somebody who can hold a conversation. So a beast greets, and the
    /// only thing you may do next is stop bothering it.
    bool beast = false;
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
    /// S8. Ask the man who has had you on the floor what he wants now. The one
    /// topic on this list that only exists because of something that happened
    /// TO the player rather than something they did. APPENDED, for the reason
    /// on Buy: the ordinal is folded into which authored row a topic speaks
    /// from, so an insert in the middle would move every existing speaker's
    /// lines.
    Rival = 21,
    /// #82. "TELL ME ABOUT..." -- the door into the topic tree. Speaks
    /// nothing itself; choosing it swaps the list for the five category
    /// branches (or however many of them have anything behind them).
    Ask = 22,
    /// #82. One branch of the tree, picked off the category list. `arg` names
    /// which (see kAskLocation etc.) -- one kind for five branches, because a
    /// branch is a fact about WHICH LIST the director shows next and not a
    /// fact that needs its own row-selection axis.
    Category = 23,
    /// #82. Ask about a named landmark. Speaks through
    /// location.<id>.<family>.<attitude>.<band>, most specific first, down to
    /// the bare location.<id> -- see topicChain(). The same dockhand who
    /// greets you differently at four in the morning answers a question about
    /// the Weighhouse differently too.
    Location = 24,
    /// #82. Ask about a named thing. Same chain as Location, keyed thing.<id>.
    Thing = 25,
    /// #82. Step back up one level of the tree. Never closes the
    /// conversation -- Leave still owns that, and Back never appears at the
    /// root, where there is nowhere left to step back TO.
    Back = 26,
    /// S6. Take a job off somebody who hands them out. The payload is the
    /// contract's own id, or one of two negative sentinels -- see
    /// kContractBlockedPayload and kContractNothingPayload -- which is a
    /// topic on purpose, because a player has to be able to ask before they
    /// can be told no, or told there is simply nothing tonight. APPENDED,
    /// for the reason on Buy: the ordinal is folded into which authored row
    /// a topic speaks from.
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

/// #82. The five branches of the "TELL ME ABOUT" tree, named the same short
/// lower-case way a faction id is: carried on TopicKind::Category's own
/// `arg`, so a caller outside dialogue.cpp (a scripted capture, a test) can
/// find a branch by name instead of guessing the string this file happens to
/// use today.
inline constexpr std::string_view kAskLocation = "location";
inline constexpr std::string_view kAskPerson = "person";
inline constexpr std::string_view kAskThing = "thing";
inline constexpr std::string_view kAskWork = "work";
inline constexpr std::string_view kAskQuest = "quest";

/// What standing somebody a drink costs the player. The same two coin a drink
/// costs across the Gull's bar -- named here rather than reached for out of
/// tavern.hpp, because the dialogue layer must not know what a tavern is. A
/// test pins the two to each other, so a change to one is a change to both.
inline constexpr std::int32_t kBoughtDrinkCost = 2;

/// #81. TopicKind::TakeContract's payload when a broker will not deal with
/// the player AT ALL yet -- brokerWillTalk() is false. Answered out of
/// contract.blocked.
inline constexpr std::int32_t kContractBlockedPayload = -1;
/// #81. TopicKind::TakeContract's payload when the broker WOULD deal with
/// the player and simply has nothing of theirs on the board right now --
/// everything taken, paid, or not drawn tonight. Kept distinct from
/// kContractBlockedPayload so choose() answers out of contract.none rather
/// than contract.blocked: a broker who likes you fine and has nothing to
/// give you tonight is not saying the same sentence as one who will not
/// deal with you at all. contract.none has been authored since S6 and was
/// unreachable content until this payload gave a topic a reason to ask for
/// it -- see the note in buildTopics() at the S6 THE WORK section.
inline constexpr std::int32_t kContractNothingPayload = -2;

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

/// #82. Which list the director is currently showing. Root is everything that
/// was always a flat topic; the other six are the "TELL ME ABOUT" tree's own
/// levels -- one for the category branches, one per branch. Exposed so a
/// test can assert the LEVEL rather than infer it from which labels happen
/// to be on screen.
enum class DialogueMenu : std::uint8_t {
    Root = 0,
    Category = 1,
    Location = 2,
    Person = 3,
    Thing = 4,
    Work = 5,
    Quest = 6,
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
    /// The SAME registry, shared rather than borrowed. Anything that keeps a
    /// FactionLedger of its own -- the nemesis book does -- has to hold the
    /// owner rather than a pointer into a director that may be copied or moved,
    /// which is exactly the bug FactionLedger::attach's note describes.
    [[nodiscard]] std::shared_ptr<const FactionRegistry> factionsShared() const noexcept {
        return factions_;
    }
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
    /// #82. Which level of the TELL ME ABOUT tree topics() is currently
    /// showing. Root until "TELL ME ABOUT..." is chosen.
    [[nodiscard]] DialogueMenu menu() const noexcept { return menu_; }

    // --- #82: the register ---------------------------------------------------

    /// Dials the register for whatever is said or asked NEXT. Rebuilds the
    /// topic list immediately when a conversation is open -- the whole point
    /// of a tone selector is that a topic can appear or vanish the instant
    /// this changes, not on the next thing said. See toned() and
    /// toneAttitude() in the .cpp for the two ways it actually reaches
    /// anything.
    void setTone(Tone tone) noexcept;
    [[nodiscard]] Tone tone() const noexcept { return tone_; }

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

    // --- #82: the "TELL ME ABOUT" tree ---------------------------------------
    //
    // buildTopics() builds the ROOT list, unchanged in shape from before #82
    // except that PERSONAL, HISTORY, WARD TALK and MASTERY moved out of it and
    // into the branches below -- confirmed dead in every consumer outside
    // dialogue.cpp itself before they moved. QUEST and QUESTBEAT did NOT move:
    // too much already reaches for them at the root (scripted captures, the
    // crime and faction suites), so the QUEST branch below is a deliberate
    // MIRROR that shows the same topics a second way rather than a relocation
    // -- asking there is exactly as valid as asking at the root, and choosing
    // either one is the exact same Topic, dispatched by the exact same
    // choose() case.

    /// The category list: LOCATION / PERSON / THING / WORK / QUEST, each shown
    /// only when it has an answer behind it -- the same "no topic leads to
    /// nothing" law every branch below applies to itself.
    void buildCategoryTopics();
    void buildLocationTopics();
    void buildPersonTopics();
    void buildThingTopics();
    void buildWorkTopics();
    /// The mirror described above: THE VANISHED CLERK and any active
    /// Talk/Alms/Tally stage this speaker is the party to -- everything
    /// buildTopics()'s own active-stage loop shows at the root, MINUS
    /// Oath/Teach/Forge, which stay verbs and never appear here.
    void buildQuestTopics();
    /// Appends the "(BACK)" topic every non-root level ends on.
    void pushBack();
    /// Rebuilds whichever level menu_ currently names. The single call site
    /// every state-changing topic reaches for after choose() -- a rung
    /// climbed or a stage finished has to refresh the list the player is
    /// ACTUALLY looking at, root or three branches deep, and menu_ is always
    /// the fact of which one that is.
    void rebuildCurrentLevel();

    [[nodiscard]] bool personAvailable() const;
    [[nodiscard]] bool locationAvailable() const;
    [[nodiscard]] bool thingAvailable() const;
    [[nodiscard]] bool workAvailable() const;
    [[nodiscard]] bool questAvailable() const;
    /// The bark key one authored micro-history may actually be TOLD from, or
    /// "" when there is nothing to say -- both parties still have to resolve
    /// in the registry, and the table has to be that history's OWN
    /// (gossip.<id>), not a fall-through to the generic gossip table, which
    /// is a separate topic (WardTalk) and not this history's story. Shared
    /// between personAvailable() and buildPersonTopics() so the two can never
    /// quietly disagree about which of a notable's tellable histories are
    /// actually tellable -- see the note on personAvailable() itself for the
    /// bug this closed.
    [[nodiscard]] std::string historyBarkKey(const History& history) const;
    /// The chain quest.<questId>[.rumor.<notableId>] everybody who has heard
    /// of the clerk speaks from -- shared between questAvailable() and
    /// buildQuestTopics() so the two can never quietly disagree about what
    /// "available" means.
    [[nodiscard]] std::vector<std::string> vanishedClerkChain() const;

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
    /// #82. Widens a fallback chain with this exchange's tone-tagged variant
    /// of each candidate, immediately ahead of the candidate itself, so a
    /// register with nothing authored for a given key degrades to exactly
    /// that key's untagged line -- never to silence, and never to a LESS
    /// specific key jumping the queue. A no-op, chain in, chain out, when
    /// tone_ is NORMAL; that is what keeps every line spoken before #82
    /// unchanged unless the player actually reaches for POLITE or BLUNT.
    [[nodiscard]] std::vector<std::string> toned(const std::vector<std::string>& chain) const;
    /// #82. The MOMENTARY attitude a dialled register reads as, for GATING
    /// only -- whether a topic is on the table right now. Never the ledger's
    /// own number, which moves only through a recorded Deed; see the .cpp.
    [[nodiscard]] Attitude toneAttitude() const noexcept;
    /// #82. Which Deed hearing somebody out records, by the current register.
    [[nodiscard]] Deed toneListenDeed() const noexcept;
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
    /// #82. The register dialled for the NEXT exchange. Never persisted, never
    /// hashed as anything other than "what the current tick's replayed inputs
    /// set it to" -- see hashInto()'s own note.
    Tone tone_ = Tone::Normal;
    std::string greeting_;
    std::string greetingKey_;
    std::string lastLine_;
    std::vector<Topic> topics_;
    /// #82. Which level topics_ currently holds. Reset to Root by open() and
    /// close(); moved only by choosing Ask/Category/Back.
    DialogueMenu menu_ = DialogueMenu::Root;
    std::int32_t playerCoin_ = 0;
    /// Bumped by every conversation opened. Hashed, so replaying the same
    /// actions reproduces the same rotation through the authored rows.
    std::int32_t conversations_ = 0;
};

}  // namespace granadad::sim
