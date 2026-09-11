#pragma once

// The court: the Flame's bench, the plea, the weighing and the judgment.
//
// THE RULING (Eli, 2026-09-02, verbatim, binding): "If the player is tagged
// as a criminal it should be like Daggerfall where you can go to court and
// you can face jail or execution (game over)." Sequenced as its own build
// after combat. Combat landed the hook and stopped: crime, heat, arrest,
// Sentence::Condemned as an inert status, no court.
//
// WHAT THIS FILE IS. RULES ONLY, beside watch.hpp and for the same reason:
// no room, no actor, no page. The Watch is the arresting arm and the hangman
// (watch.hpp: what the paper ASKS for, Eli's 2026-07-14 sentence verbatim);
// the Flame is the bench (Father Maell at the Mission, the same priest whose
// civil hearing Ward::weighPetition already holds). What an arrest WITH PAPER
// resolves to is no longer an inert sentence and a clock jump: the officer
// takes you to the Mission, the charge is read off the ledger, you plead, the
// priest weighs, and the answer is one of seven. A paperless search at the
// door (Sentence::Fined) never reaches the bench and is untouched.
//
// TWO PLAYER-END PATHS, NEVER CONFLATED. Combat defeat is the nemesis ruling
// (promoted-against, coin taken, quay revive, YOU LIVE). Court execution --
// THE ROPE -- is the one true game over, the only place the player ends.
// Losing a fight never ends the game; letting the law convict you can.
//
// SAME-ROLL DISCIPLINE, DECLARED. Tavern::applyArrest spends exactly ONE
// drawForPlayerAction() and it always did; the nights (heldHours) read
// `draw % 49` off the low residue exactly where they were. The court adds no
// draw and no stream: I DID NOT reads a declared band ABOVE the nights --
// bits 16 and up, `(draw >> 16) % 21 - 10`, compound.cpp's own "the priest is
// a man" jitter -- and I DID IT reads nothing (S9: no chance is involved; the
// same man with the same record gets the same answer every time). The draw
// is spent AT THE ARREST and stored on the hashed hearing; the plea reads a
// band off it later, as an ordinary stepped input. The sim never waits on a
// page for a roll.
//
// WEIGHARRAIGNMENT IS WEIGHPETITION, ONE FOR ONE IN SHAPE (compound.cpp): a
// base of 24 (the Flame's default is mercy, "or the institution is a
// formality with six names"), pleads, against, a jitter of up to a tenth of
// the scale off one owned draw, bands. Every term is an integer, every term
// is named, and every term is printed in the check block. COIN BUYS NOTHING
// AT THE BENCH: there is no offering row and no bribe. What you gave at the
// Mission's door before you were taken -- temple standing -- is the only
// currency the Flame reads, and THE DOOR is that term.
//
// NO FLOATS. NO UNORDERED CONTAINERS. Everything the court records lives on
// CrimeLedger (crime.hpp), hashed and codec'd, so a run saved between the
// arrest and the plea reopens at the bench and the twin-run gate sees all of
// it. This file is the arithmetic; crime.hpp is the record.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/watch.hpp"

namespace granadad::sim {

/// crime.hpp's six acts. Declared opaque here so the charge sheet can name
/// the one line it reads without this header owning the vocabulary.
enum class Crime : std::uint8_t;

/// The six acts, counted on the sheet since the last sentence. crime.hpp
/// static_asserts its own kCrimeCount against this, so the two cannot drift.
inline constexpr std::size_t kSheetCrimes = 6;

// ---------------------------------------------------------------------------
// the plea and the answer
// ---------------------------------------------------------------------------

/// Append-only: the ordinal is hashed and written into the ledger's encoding.
enum class Plea : std::uint8_t {
    None = 0,
    /// I DID IT. Draw-free, never doubled, never spared. A confession is
    /// weighed as it is given.
    Guilty = 1,
    /// I DID NOT. The arrest draw's own band, plus or minus ten: spared at
    /// the top, and DOUBLED below it -- Daggerfall's gamble exactly.
    NotGuilty = 2,
    /// I HAVE NOTHING TO SAY. The one row a commuted man gets before the
    /// bench a second time. Mercy is given once.
    NoPlea = 3,
};

[[nodiscard]] std::string_view pleaName(Plea plea) noexcept;

/// The bench's answer. Append-only, hashed, codec'd.
///
/// NOT Sentence (watch.hpp), and deliberately so: Sentence is what the Watch
/// ASKS for and CrimeLedger::lastSentence keeps recording it; the court's
/// answer is its own type because the two disagree by design.
enum class Judgment : std::uint8_t {
    None = 0,
    /// Walked out clean. I DID NOT at the top of the band, and nothing else.
    Spared = 1,
    /// The fine, and no cell. Best case for a confession.
    Fined = 2,
    /// A cell: the shipped one-to-three nights, doubled on a failed denial.
    Held = 3,
    /// The fine forgiven and five days of labour in the Mission's yard --
    /// ACTORS-SPEC's unbuilt PLEAD verb made real, and the slot Daggerfall's
    /// banishment would have taken. One district exists; "banished from the
    /// Docks" is a game with no map.
    Bound = 4,
    /// A Skyrunner's first: the hand, permanent, plus HELD's coin and nights.
    /// SPARABLE -- at 38 and above the priest overrules the sergeant and it is
    /// HELD with the hand spared. A court that cannot overrule the sergeant
    /// is a formality.
    TheHand = 5,
    /// The rope tier's mercy: the hand, twelve days bondsworn, condemned for
    /// the rest of the run (recognised at kCondemnedRecognisePermille), and
    /// the blood SERVED -- murderer_ clears. Given ONCE.
    Commuted = 6,
    /// THE ONE TRUE GAME OVER. A witnessed murder (a watchman included) or a
    /// Skyrunner's second arrest with paper, under the mercy line -- never
    /// theft, never repeat violence alone.
    TheRope = 7,
};

[[nodiscard]] std::string_view judgmentName(Judgment judgment) noexcept;

// ---------------------------------------------------------------------------
// the lines
// ---------------------------------------------------------------------------

/// PAPER tier (the ask is a cell) and THE HAND tier: SPARED at the top on a
/// denial only, FINED, HELD, and BOUND below the last line.
inline constexpr std::int32_t kSparedLine = 55;
inline constexpr std::int32_t kFinedLine = 38;
inline constexpr std::int32_t kHeldLine = 14;
/// THE ROPE tier has two answers and nothing else: COMMUTED at or above this
/// line, THE ROPE below it. A witnessed murder is never SPARED -- the corpse
/// is on the roster and the witnesses are named.
inline constexpr std::int32_t kMercyLine = 24;

/// THE FLAME: weighPetition's own zero point. The Flame's default is mercy.
inline constexpr std::int32_t kFlameBase = 24;
/// CONFESSED: what I DID IT is worth. Draw-free.
inline constexpr std::int32_t kConfessedTerm = 6;
/// THE PRIEST IS A MAN: the band a denial reads off the arrest draw, plus or
/// minus this. compound.cpp's own `draw % 21 - 10`, carved off the bits above
/// the nights so no sentence-length residue moves.
inline constexpr std::int32_t kPriestBand = 10;
inline constexpr int kPriestBandShift = 16;

/// The one-line names the check block prints. Fixed literals, never
/// composed, so the copy census can read every one of them off this header.
inline constexpr std::string_view kTermFlame = "THE FLAME";
inline constexpr std::string_view kTermTongue = "TONGUE";
inline constexpr std::string_view kTermDoor = "THE DOOR";
inline constexpr std::string_view kTermWard = "THE WARD";
inline constexpr std::string_view kTermTakenBefore = "TAKEN BEFORE";
inline constexpr std::string_view kTermHeat = "HEAT";
inline constexpr std::string_view kTermBlood = "BLOOD";
inline constexpr std::string_view kTermSawIt = "SAW IT";
inline constexpr std::string_view kTermRoofs = "THE ROOFS";
inline constexpr std::string_view kTermSecondRung = "THE SECOND RUNG";
inline constexpr std::string_view kTermRopeOnce = "THE ROPE ONCE";
inline constexpr std::string_view kTermConfessed = "CONFESSED";
inline constexpr std::string_view kTermPriestIsAMan = "THE PRIEST IS A MAN";

// ---------------------------------------------------------------------------
// the charge sheet
// ---------------------------------------------------------------------------

/// What the Watch lays on the table. DRAW-FREE and written AT THE ARREST off
/// the ledger (CrimeLedger::charge); nothing between the arrest and the plea
/// changes it. Every plea input the priest reads -- the tongue, the door, the
/// ward -- is captured here too, so the weighing is a pure function of this
/// one hashed record and the page can print it any time.
struct ChargeSheet {
    bool written = false;
    /// WHAT THE PAPER ASKS FOR: the shipped Sentence ladder's own answer, the
    /// murder override included. Held = PAPER (a cell), Maimed = THE HAND,
    /// Condemned = THE ROPE. Fined never reaches the bench.
    Sentence tier = Sentence::None;
    /// Blood on the record: murderer_. The sheet names the killing over any
    /// theft, and the tier is the rope.
    bool blood = false;
    /// The one line the sheet names when there is no blood: the highest-heat
    /// act with a count since the last sentence.
    bool hasWorst = false;
    Crime worst = static_cast<Crime>(0);
    /// tallies_ - servedTallies_, per act: "TWO LIFTS AND A CRACKED BOX".
    std::int32_t since[kSheetCrimes] = {};
    std::int32_t heatAtArrest = 0;
    std::int32_t unitsSeized = 0;
    /// Who saw the killing (slewWitnesses_). The rope tier's own term.
    std::int32_t witnesses = 0;
    /// Convictions before this one (arrests_) -- TAKEN BEFORE.
    std::int32_t priors = 0;
    bool skyrunner = false;
    /// The ladder's own rope: a Skyrunner with paper and a prior. THE SECOND
    /// RUNG weighs whether or not there is blood beside it.
    bool secondRung = false;
    /// A commuted man before the bench again: THE ROPE ONCE.
    bool condemnedBefore = false;
    /// Mercy was given once already. A rope hearing with this set has no
    /// plea and one answer.
    bool commutedBefore = false;
    /// The plea inputs, read draw-free at the arrest.
    std::int32_t streetwise = 0;
    std::int32_t templeStanding = 0;
    std::int32_t reputation = 0;
    /// THE ONE OWNED DRAW the arrest spent. The nights read its low residue;
    /// the denial reads the band above them.
    std::uint64_t draw = 0;
};

// ---------------------------------------------------------------------------
// the weighing
// ---------------------------------------------------------------------------

/// One line of the priest's arithmetic. Positive favours the accused.
struct ArraignmentTerm {
    std::string_view name;
    std::int32_t value = 0;
    /// The count behind a counted term (SAW IT: how many), 0 otherwise. The
    /// page composes "THREE SAW IT"; the number is here so it need not guess.
    std::int32_t count = 0;
};

/// What the priest answered, and every line of why.
struct Arraignment {
    /// False when the sheet never reaches the bench (Fined, or unwritten), or
    /// the plea was Plea::None.
    bool heard = false;
    /// The plea as WEIGHED. A commuted man's Guilty or NotGuilty is coerced to
    /// NoPlea: he gets the one row.
    Plea plea = Plea::None;
    /// The terms in the order the check block prints them, zero-valued ones
    /// omitted (THE FLAME always leads). Empty on NoPlea: the priest speaks
    /// and nothing is weighed.
    std::vector<ArraignmentTerm> terms;
    /// The sum before the plea's own term -- weighPetition's `weight`.
    std::int32_t weight = 0;
    /// CONFESSED (+6) on Guilty; THE PRIEST IS A MAN (the band) on NotGuilty.
    std::int32_t pleaTerm = 0;
    /// weight + pleaTerm. What the lines are read against.
    std::int32_t scored = 0;
    /// The PAPER ladder's band the score fell in (Spared/Fined/Held/Bound),
    /// which THE HAND tier substitutes from. None on the rope tier.
    Judgment band = Judgment::None;
    Judgment judgment = Judgment::None;
    /// A denial disbelieved: the sentence doubles (nights, fine, bond days)
    /// and the Mission remembers the lie. Never on a confession, never on the
    /// rope tier.
    bool doubled = false;
};

/// THE PRIEST WEIGHS. Copied one for one in shape from Ward::weighPetition:
/// a base, pleads, against, the band, the lines. Rules only: it weighs and
/// returns, and changes nothing at all.
///
/// PUBLIC ON PURPOSE, for the reason weighPetition is: the rule is the
/// interesting thing and it should be reachable without a room around it.
/// It is also the only honest way to prove the claims this build makes --
/// that I DID IT is the same answer under every draw, that I DID NOT moves
/// by exactly the band and doubles below it, that a witnessed murder is never
/// SPARED -- because each proof is the SAME sheet weighed twice.
[[nodiscard]] Arraignment weighArraignment(const ChargeSheet& sheet, Plea plea);

/// The PAPER ladder alone, for the page's line row and for tests: SPARED
/// (a denial only) / FINED / HELD / BOUND.
[[nodiscard]] Judgment paperBand(std::int32_t scored, Plea plea) noexcept;

// ---------------------------------------------------------------------------
// the hearing, as the ledger keeps it
// ---------------------------------------------------------------------------

enum class HearingStage : std::uint8_t {
    /// No hearing open.
    None = 0,
    /// Taken with paper; the sheet is written and the bench has not answered.
    Arraigned = 1,
    /// The plea was made and the judgment passed; the sentence is not yet
    /// served. What serving it does -- coin, clock, ledger, the rope -- is the
    /// sentence's own step, after this one.
    Judged = 2,
};

/// The hearing between the arrest and the sentence. HASHED and CODEC'D on
/// CrimeLedger (crime.hpp) -- Tavern::lastArrest_ is neither, and the
/// per-run hole that left in playerWeapon_ is the one this build does not
/// repeat. The page is render-only over this record.
struct HearingState {
    HearingStage stage = HearingStage::None;
    ChargeSheet sheet;
    /// Who laid the paper: the arresting officer's name, for the page's first
    /// line. Hashed as bytes, the way the equipped crafting's id is.
    std::string officer;
    Plea plea = Plea::None;
    Judgment judgment = Judgment::None;
    Judgment band = Judgment::None;
    std::int32_t weight = 0;
    std::int32_t scored = 0;
    bool doubled = false;

    [[nodiscard]] bool pending() const noexcept { return stage != HearingStage::None; }
    [[nodiscard]] bool awaitingPlea() const noexcept { return stage == HearingStage::Arraigned; }
    [[nodiscard]] bool judged() const noexcept { return stage == HearingStage::Judged; }
};

// ---------------------------------------------------------------------------
// the sentence, in integers
// ---------------------------------------------------------------------------
//
// SENTENCES LANE. What a judgment COSTS, before anything is applied: the
// coin, the clock, the hand, the standing. Rules only, weighArraignment's
// own shape -- a pure function of the hearing and the purse, so the page can
// print "TWO NIGHTS. 17 ROYALS." before the room moves a thing, and a test
// can prove every branch's integers without a room around them. What
// applying it does is Tavern::serveSentence (the coin, skipHours, the
// mirror, the temple, the body) and CrimeLedger::sentence (the record),
// called AFTER the skip so the heat it leaves is not cooled to nothing.
//
// JAIL IS THE SHIPPED skipHours, WIDENED THROUGH SYSTEMS THAT EXIST. The
// days pass on the world clock: heat cools through them and is then set to
// kHeatAfterSentence; every taken job past its due night fails at the next
// doors-open refresh (ContractBoard::expireStale); the ward's roll runs its
// days (Ward::advanceToDay off Tavern::dayNumber, the Session's own tail),
// so a quarter-day that falls inside a sentence charges the ground penny
// like any other; the roofs warm to a conviction and the S4 mirror halves
// it onto the Watch; the temple remembers a lie and blesses the yard's
// work; the body mends over a day or more. THE NEMESIS DOES NOT RISE: the
// man who had you taken gained nothing -- losing to the law is not losing a
// fight, and the two ledgers stay apart, which is the canon's whole point.
//
// THE BOND, MIRRORED AND NOT REUSED. The tenure ruling's own instrument
// (compound.hpp: Household::bondholder, Ward::goBondsworn) is a debt
// relation between a household ON THE ROLL and the Den Duke of its plot,
// worked off against arrears in the Duke's yard; the Mission's ground is the
// glebe, "never let to anyone", the player has no household on the roll
// until they buy a house, and a court's bond is owed to the Flame and not to
// a Duke. So BOUND is that instrument's honest mirror: days of labour on the
// world clock, no fine, the Flame's regard for the work -- and it prints
// nothing extra, the clock jump is the whole of it (section 6.4).

/// BOUND: days of labour in the Mission's yard. Doubled on a disbelieved
/// denial. The slot Daggerfall's banishment would have taken.
inline constexpr std::int32_t kBoundDays = 5;
/// COMMUTED: the rope passed, and the price of a life in days. Long enough
/// to lose every open contract, on purpose. Never doubled -- the rope tier
/// has nothing under it to double.
inline constexpr std::int32_t kCommutedDays = 12;
/// A fine the purse cannot pay is worked off: one day bondsworn per this
/// many Royals of shortfall, rounded UP the way the tenure ruling's bond
/// works arrears off (nobody owes a fraction of a day), and no more than
/// kShortfallDaysMax however short the purse. "Payable in Royals or in
/// yourself." Nobody is put in debt: the remainder past the cap is forgiven.
inline constexpr std::int32_t kShortfallRoyalsPerDay = 4;
inline constexpr std::int32_t kShortfallDaysMax = 7;
/// What the Flame does with your standing at its door. "You lied to the
/// Flame's face. The Mission will remember which."
inline constexpr std::int32_t kLieTempleCost = 8;
/// BOUND's work was the Flame's.
inline constexpr std::int32_t kBoundTempleGain = 4;
/// And a life spared is the Flame's mercy, remembered.
inline constexpr std::int32_t kCommutedTempleGain = 8;
/// A conviction is a justice event: the roofs warm to whoever the Watch
/// corrects, and the mirror the ladders declare halves it onto the Watch
/// (factions.json's own note). SPARED and FINED move nothing.
inline constexpr std::int32_t kConvictionRoofsGain = 4;

/// What a judgment costs. Every field an integer, every one printable.
struct SentenceTerms {
    /// False when there is nothing to serve: no judgment on the hearing.
    bool served = false;
    Judgment judgment = Judgment::None;
    /// The PAPER ladder's band under a HAND-tier answer, which decides
    /// whether THE HAND comes with HELD's nights or BOUND's days.
    Judgment band = Judgment::None;
    /// A denial disbelieved: the nights, the fine and the bond days double.
    bool doubled = false;
    /// COIN. fineFor(heatAtArrest, unitsSeized), doubled if the plea failed;
    /// what the purse could pay; the rest, and the days it is worked off in.
    /// Forgiven outright (all zero) on SPARED, BOUND, COMMUTED and the hand
    /// at BOUND's band.
    std::int32_t fineAsked = 0;
    std::int32_t finePaid = 0;
    std::int32_t shortfall = 0;
    std::int32_t shortfallDays = 0;
    /// THE CELL: heldHours off the arrest's own draw (24..72, the shipped
    /// one-to-three nights), doubled if the plea failed. HELD, and THE HAND
    /// at HELD's band.
    std::int32_t cellHours = 0;
    /// BONDSWORN: BOUND's five (doubled), COMMUTED's twelve, and the
    /// shortfall's days -- all of them labour in the Mission's yard, all of
    /// them the Flame's.
    std::int32_t bondDays = 0;
    /// The whole of the clock, cell and bond together, and the days in it.
    std::int32_t hours = 0;
    std::int32_t days = 0;
    /// The hand comes off: THE HAND and COMMUTED ("the rope does not un-take
    /// the hand").
    bool hand = false;
    /// THE ROPE. Nothing above applies; the run ends.
    bool rope = false;
    /// Where the body is turned loose: SPARED and FINED walk out of the
    /// Mission's door where they stand (a fine's shortfall is worked in the
    /// Mission's own yard and ends at the same door); everything that cost a
    /// cell ends on the shipped Tarwalk.
    bool releaseHere = false;
    /// What the Flame and the roofs make of it.
    std::int32_t templeDelta = 0;
    std::int32_t roofsDelta = 0;
    /// The body mends over any sentence that cost a day or more -- the days
    /// did it, reviveAfterDefeat's own shape. A fine heals nothing.
    bool mends = false;
};

/// THE SENTENCE BRANCH, IN INTEGERS (section 4). Reads the judged hearing
/// and the purse it will be paid from; returns what it would cost. Rules
/// only: it changes nothing at all. `served` is false on a hearing with no
/// judgment on it.
[[nodiscard]] SentenceTerms sentenceTerms(const HearingState& hearing,
                                          std::int32_t purse) noexcept;

// ---------------------------------------------------------------------------
// the end of the run
// ---------------------------------------------------------------------------

/// HEARING PAGE LANE. How long the judgment's badge is HELD on the page
/// before the row that serves the sentence (or takes the drop) is offered,
/// in movement steps: a beat and a half, so a leaned-on ENTER cannot plead
/// and be sentenced in one breath. The cross-lane contract's own number
/// (JUSTICE-SPEC section 5); presentation reads it, the sim never does.
inline constexpr std::int32_t kJudgmentHoldSteps = 90;

/// Where the ward hangs a man: K21, the Saltgate watch-post and its gibbet,
/// DOCKS-GAZETTEER's own authored law texture. The plate names it.
inline constexpr std::string_view kRopePlace = "THE SALTGATE POST";
/// And what it hangs him FOR when there is no corpse to name: the ladder's
/// own rope, a Skyrunner's second arrest with paper.
inline constexpr std::string_view kRopeForSecondRung = "THE SECOND RUNG";

/// THE GAME'S ONLY END. Not a defeat: applyDefeat is never called, nothing
/// revives, there is no quay. The bit itself is CrimeLedger::executed()
/// (hashed, codec'd); this is the cause the plate reads -- the place, the
/// ward as the killer, the reason, the dateline -- written once at the drop
/// by the room, from state that is all hashed already, and reported the
/// way Tavern::lastDefeat() reports a rise. Afterwards A NEW MAN is a fresh
/// run through the boot path and LEAVE is the quit; there is no save, so
/// there is nothing to load and the run is genuinely over.
struct RunEnd {
    bool ended = false;
    /// HANGED AT <place>.
    std::string place;
    /// BY THE WARD. FOR <reason>. -- the victim's name for a witnessed
    /// murder, THE SECOND RUNG for the roofs' second.
    std::string reason;
    bool blood = false;
    /// THE FOURTH DAY. 23:52. -- Tavern::dayNumber() and the clock face at
    /// the drop.
    std::int32_t day = 0;
    std::int32_t secondOfDay = 0;
};

}  // namespace granadad::sim
