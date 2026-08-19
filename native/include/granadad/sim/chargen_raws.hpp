#pragma once

// The Daggerfall flow's CONTENT, read off the owner's own files.
//
// docs/design/CHARGEN-DAGGERFALL-DRAFT.md is the source of truth: the nine
// callings, the ten ward questions, the twelve biography questions and the
// three-axis tally that turns answers into a calling. This file loads that
// content out of content/raws/chargen/*.json and REFUSES it loudly when it
// does not check out -- it never invents a number, a skill, a faction or a
// person. The owner line-edits the doc; a sync pass moves his edits into the
// JSON; nothing here has an opinion about the values.
//
// THREE LOADERS, ONE VALIDATION STANCE. Each loader takes the registries the
// content must resolve against (the SAME SkillTrack, FactionRegistry and
// NotableRegistry every other system reads) and refuses AT LOAD, BY NAME, on
// anything those registries do not know -- the discipline faction.hpp's own
// header states for ranks.json ("a ladder naming a faction the owner's file
// does not have is refused at load, by name"), turned all the way up: here a
// single bad id refuses the whole file, because a chargen that silently
// dropped one authored effect would hand a player a different childhood than
// the one the owner wrote. loaded() is false and errors() says exactly what
// was wrong and where. NEVER throws -- a missing file is an empty registry,
// the same must-still-boot rule every raws loader in this build keeps.
//
// THE EFFECT VOCABULARY IS CLOSED. Seven kinds, every one of them a lever
// that exists in this build today (the doc's own "no vaporware" rule):
// skillDelta, coinDelta, factionStanding, actorDisposition, heat, hpMax,
// daggerPoints. An unknown kind in the JSON refuses the file exactly like an
// unknown skill id does -- the same closed-vocabulary stance sim/social.hpp's
// Deed enum takes, applied to content.
//
// ZERO-SUM REPUTATION, ENFORCED AT LOAD. The doc's biography lever table
// rules that faction standings move by DIRECT ROW WRITES at chargen -- not
// through FactionLedger::addStanding, whose rival mirror would double-count
// an authored spread -- and that the bookkeeping is therefore enforced at
// authoring time: every answer's faction deltas sum to exactly zero. This
// loader is where "authoring time" becomes a check instead of a hope.
//
// PURE AND DRAW-FREE. tallyQuiz() and accumulateBiography() are pure
// functions of the loaded content and the chosen answers: no RNG, no clock,
// no state. Same answers, same verdict, same effects, every time -- the same
// idempotence contract Chargen::apply() documents for the sheet arithmetic
// these effects ride beside.
//
// NO FLOATS. Every delta, every tally count and every dagger point is a
// small integer; the dagger multiplier is Q8 (social.hpp).

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/attributes.hpp"
#include "granadad/sim/chargen.hpp"

namespace granadad::sim {

class SkillTrack;
class FactionRegistry;
class NotableRegistry;

// ---------------------------------------------------------------------------
// the three axes
// ---------------------------------------------------------------------------

/// The doc's own codes. What the letters MEAN (the register each measures) is
/// content/raws/chargen/questions.json's "axes" block -- the approved trio
/// THE HAND / THE MUDLARK / THE DISCIPLE -- never this enum's business.
enum class ChargenAxis : std::uint8_t { A = 0, B = 1, C = 2 };

inline constexpr std::size_t kChargenAxisCount = 3;

[[nodiscard]] std::string_view chargenAxisCode(ChargenAxis axis) noexcept;
/// std::nullopt for anything that is not exactly "A", "B" or "C".
[[nodiscard]] std::optional<ChargenAxis> chargenAxisFromCode(std::string_view code) noexcept;

/// One axis identity card: the code, the stable id and the name the quiz
/// screen's meters wear.
struct ChargenAxisIdentity {
    ChargenAxis axis = ChargenAxis::A;
    std::string id;
    std::string name;
};

// ---------------------------------------------------------------------------
// the callings
// ---------------------------------------------------------------------------

/// One of the ward's nine trades: a full pre-authored sheet in exactly the
/// shape sim::Chargen spends by hand -- the chargenPreset idea companions.cpp
/// reads for DEVIN and GABRI, generalized into a registry a roster screen can
/// list. Skill ids are content/raws/skills/skills.json's own, checked at load.
struct CallingTemplate {
    std::string id;
    std::string name;
    /// The roster row's own line ("You carry what the ward eats...").
    std::string oneLine;
    /// The doc's citation for where this trade is real -- documentation, read
    /// by nobody mechanical.
    std::string anchor;
    ChargenAxis dominantAxis = ChargenAxis::A;
    /// Empty for a pure calling (Dockhand, Roof-Tenant, Almsbearer).
    std::optional<ChargenAxis> secondaryAxis;
    /// The sheet. Sizes validated against chargen.hpp's own slot counts
    /// (3/3/6); a SHORT minor row is legal and unpenalized -- Gabri's own
    /// sheet ships five of six, and the mudlark's is honestly thin.
    std::vector<std::string> primary;
    std::vector<std::string> major;
    std::vector<std::string> minor;
    /// Indexed by AttributeId. Sums to exactly kAttributeBonusPool, no value
    /// over kAttributeBonusPerAttributeCap -- validated at load, because the
    /// review screen re-spends these through Chargen's own refusing setter
    /// and an unspendable sheet would strand the player there.
    std::array<std::int32_t, kAttributeCount> attributeSpend{};

    /// Designates the whole sheet and spends the whole pool into `sheet`
    /// through Chargen's own public, refusing calls -- the same arithmetic a
    /// hand-built custom sheet goes through, so a calling can never hold a
    /// combination a player could not have clicked together. Meant for a
    /// FRESH Chargen; returns false (sheet in a partial state, caller
    /// discards it) if anything refused, which loaded content never does.
    [[nodiscard]] bool designateInto(Chargen& sheet, const SkillTrack& raws) const;
};

/// content/raws/chargen/callings.json. Refuses the whole file, by name, on:
/// an unknown or duplicate skill id, THE FLAME on any tier, overfull tiers,
/// a spend that does not sum to the pool or exceeds the per-attribute cap,
/// a duplicate calling id, or a bad axis code.
class CallingRegistry {
public:
    [[nodiscard]] static CallingRegistry load(const std::filesystem::path& contentDir,
                                              const SkillTrack& skills);
    /// The seam the refusal tests use: same validation, caller's file.
    [[nodiscard]] static CallingRegistry loadFromFile(const std::filesystem::path& file,
                                                      const SkillTrack& skills);

    [[nodiscard]] bool loaded() const noexcept { return !callings_.empty() && errors_.empty(); }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return errors_; }
    [[nodiscard]] const std::vector<CallingTemplate>& callings() const noexcept {
        return callings_;
    }
    [[nodiscard]] const CallingTemplate* find(std::string_view id) const noexcept;

private:
    /// The raws' own authored order -- the order the roster screen lists.
    std::vector<CallingTemplate> callings_;
    std::vector<std::string> errors_;
};

// ---------------------------------------------------------------------------
// the quiz
// ---------------------------------------------------------------------------

struct QuizAnswer {
    ChargenAxis axis = ChargenAxis::A;
    std::string text;
};

/// Exactly three answers, one per axis -- validated, because a question that
/// could not score some axis would bend the whole tally.
struct QuizQuestion {
    std::string prompt;
    std::vector<QuizAnswer> answers;
};

/// One dominant axis's row of the verdict table: the calling a pure score
/// earns, and the calling each possible SECONDARY axis steers to.
struct QuizVerdictRow {
    std::string pure;
    /// Indexed by the secondary ChargenAxis. The dominant axis's own slot is
    /// empty and never read.
    std::array<std::string, kChargenAxisCount> withSecondary;
};

/// content/raws/chargen/questions.json. Refuses, by name, on: axes that are
/// not exactly A/B/C once each, a question without exactly one answer per
/// axis, a verdict naming a calling the CallingRegistry does not have, or a
/// verdict whose calling's OWN axis codes disagree with the table cell it
/// sits in (dockhand can only ever be the A-pure verdict -- transcription
/// drift refuses rather than quietly mis-sorting a childhood).
class ChargenQuiz {
public:
    [[nodiscard]] static ChargenQuiz load(const std::filesystem::path& contentDir,
                                          const CallingRegistry& callings);
    [[nodiscard]] static ChargenQuiz loadFromFile(const std::filesystem::path& file,
                                                  const CallingRegistry& callings);

    [[nodiscard]] bool loaded() const noexcept { return !questions_.empty() && errors_.empty(); }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return errors_; }
    [[nodiscard]] const std::vector<ChargenAxisIdentity>& axes() const noexcept { return axes_; }
    [[nodiscard]] const std::vector<QuizQuestion>& questions() const noexcept {
        return questions_;
    }
    /// An axis count at or above which the verdict is the PURE calling.
    [[nodiscard]] std::int32_t pureAt() const noexcept { return pureAt_; }
    [[nodiscard]] const QuizVerdictRow& verdictRow(ChargenAxis axis) const noexcept {
        return verdicts_[static_cast<std::size_t>(axis)];
    }

private:
    std::vector<ChargenAxisIdentity> axes_;
    std::vector<QuizQuestion> questions_;
    std::array<QuizVerdictRow, kChargenAxisCount> verdicts_{};
    std::int32_t pureAt_ = 6;
    std::vector<std::string> errors_;
};

/// What the ten answers added up to.
struct QuizTally {
    /// Indexed by ChargenAxis. Sums to the number of questions answered.
    std::array<std::int32_t, kChargenAxisCount> counts{};
    ChargenAxis dominant = ChargenAxis::A;
    /// True when the dominant axis reached pureAt().
    bool pure = false;
    /// The verdict card's calling id. Always a real CallingRegistry id --
    /// the quiz loader proved every table cell at load.
    std::string calling;
};

/// The tally, pure and draw-free: same answers in, same verdict out, always.
/// `chosenAnswers` is one answer index per question, in question order.
/// std::nullopt when the sizes do not line up or an index is out of range --
/// a caller bug, refused rather than mis-scored.
///
/// TIE FOR DOMINANT (doc section 3.3): the tied axis the LAST question's
/// answer scored wins; if that answer scored an axis not in the tie, fixed
/// order A > B > C. Deterministic, and the last question is the one written
/// to carry that weight.
[[nodiscard]] std::optional<QuizTally> tallyQuiz(const ChargenQuiz& quiz,
                                                 const std::vector<std::int32_t>& chosenAnswers);

/// What a set of per-axis counts MEANS -- dominant axis, pure or not, and the
/// calling it lands on. tallyQuiz() is this function plus the counting, and
/// calls it, so there is exactly one implementation of the three rules that
/// decide a verdict (the last-answer tie-break, the pure threshold, and the
/// ">= earlier axis" secondary).
///
/// EXPOSED BECAUSE A PARTIAL TALLY IS A REAL QUESTION. The creation screen shows
/// the player which trade the ward is currently heading toward while they are
/// still answering -- the owner's complaint was spending choices blind, and
/// "if it judged you now: THE NETTER" is the most direct answer there is. That
/// needs the verdict rules over an INCOMPLETE count, which tallyQuiz cannot
/// give (it refuses a short answer vector, correctly, because a real verdict off
/// a half-answered quiz would be a lie). The alternative was a second copy of
/// these rules in the render layer.
///
/// `lastAxis` is the axis the most recent answer scored, for the tie-break;
/// std::nullopt when nothing has been answered yet.
[[nodiscard]] QuizTally tallyFromCounts(const ChargenQuiz& quiz,
                                        const std::array<std::int32_t, kChargenAxisCount>& counts,
                                        std::optional<ChargenAxis> lastAxis);

// ---------------------------------------------------------------------------
// the biography, and the closed effect vocabulary
// ---------------------------------------------------------------------------

/// CLOSED. Everything a biography answer (or a custom-path advantage) may do
/// to a fresh character. Append-only if it ever grows; nothing here is
/// hashed by ordinal today, but the JSON kind strings are load-bearing.
enum class ChargenEffectKind : std::uint8_t {
    /// `target` is a skill id; `amount` is added onto the designated start
    /// before SkillTrack::setLevel -- the doc's own mechanism line.
    SkillDelta = 0,
    /// `amount` over kPlayerStartingCoin. No target.
    CoinDelta = 1,
    /// `target` is a faction id; `amount` is a DIRECT row write at chargen
    /// (never addStanding -- see the file header on zero-sum and the mirror).
    FactionStanding = 2,
    /// `target` is a notables.json id; `amount` goes through
    /// SocialLedger::seed, the entry point built for exactly this.
    ActorDisposition = 3,
    /// `amount` of starting heat, over 0. No target.
    Heat = 4,
    /// `amount` over the player's base hpMax. No target.
    HpMax = 5,
    /// `amount` of dagger points -- the custom path's one currency. No
    /// target. Unused by the biography, priced by the advantage shop.
    DaggerPoints = 6,
};

[[nodiscard]] std::string_view chargenEffectKindId(ChargenEffectKind kind) noexcept;
/// std::nullopt for a kind id the closed vocabulary does not have -- the
/// loader's loud refusal starts here.
[[nodiscard]] std::optional<ChargenEffectKind> chargenEffectKindFromId(
    std::string_view id) noexcept;

struct ChargenEffect {
    ChargenEffectKind kind = ChargenEffectKind::CoinDelta;
    /// Empty for the kinds that take none.
    std::string target;
    std::int32_t amount = 0;
};

struct BiographyAnswer {
    std::string text;
    /// The doc's own parenthetical, where it carries one ("Pure flavor, and a
    /// lie the biography records."). Documentation, read by nobody mechanical.
    std::string note;
    std::vector<ChargenEffect> effects;
};

struct BiographyQuestion {
    /// "B1".."B12", the doc's own numbering, so a refusal can say where.
    std::string id;
    std::string prompt;
    std::vector<BiographyAnswer> answers;
};

/// content/raws/chargen/biography.json. Refuses the whole file, by name, on:
/// an unknown effect kind, an unknown skill/faction/notable target, a target
/// on a kind that takes none (or none on a kind that needs one), THE FLAME as
/// a skillDelta target, or any answer whose faction deltas do not sum to
/// exactly zero -- the zero-sum bookkeeping the doc pushes to authoring time.
class BiographyRegistry {
public:
    [[nodiscard]] static BiographyRegistry load(const std::filesystem::path& contentDir,
                                                const SkillTrack& skills,
                                                const FactionRegistry& factions,
                                                const NotableRegistry& notables);
    [[nodiscard]] static BiographyRegistry loadFromFile(const std::filesystem::path& file,
                                                        const SkillTrack& skills,
                                                        const FactionRegistry& factions,
                                                        const NotableRegistry& notables);

    [[nodiscard]] bool loaded() const noexcept { return !questions_.empty() && errors_.empty(); }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return errors_; }
    [[nodiscard]] const std::vector<BiographyQuestion>& questions() const noexcept {
        return questions_;
    }

private:
    std::vector<BiographyQuestion> questions_;
    std::vector<std::string> errors_;
};

// ---------------------------------------------------------------------------
// the accumulator
// ---------------------------------------------------------------------------

/// Every effect a finished chargen carries out of the door, summed and
/// sorted. The DEFAULT is a character nothing happened to -- every delta
/// zero, the dagger neutral -- so a CreationResult that never went near the
/// biography (DEVIN, GABRI) applies as a no-op through the same seam.
///
/// This is a VALUE, not simulation state: it rides the confirmed
/// CreationResult to whoever boots the Session, is applied once through the
/// engine's own public setters, and is done -- the same calculator-not-ledger
/// stance chargen.hpp documents for Chargen itself.
struct ChargenEffects {
    /// Ascending by skill id, one entry per skill, deltas summed.
    std::vector<std::pair<std::string, std::int32_t>> skillDeltas;
    std::int32_t coinDelta = 0;
    /// Ascending by faction id, deltas summed. Zero-sum per authored answer;
    /// sums of zero-sums are zero-sum.
    std::vector<std::pair<std::string, std::int32_t>> factionStandings;
    /// Ascending by notable id, deltas summed.
    std::vector<std::pair<std::string, std::int32_t>> dispositionSeeds;
    std::int32_t heat = 0;
    std::int32_t hpMaxDelta = 0;
    std::int32_t daggerPoints = 0;

    [[nodiscard]] bool operator==(const ChargenEffects& other) const = default;
};

/// Pure: same registry, same answers, same ChargenEffects, every time.
/// `chosenAnswers` is one answer index per question, in question order.
/// std::nullopt on a size mismatch or an out-of-range index, refused rather
/// than half-accumulated.
[[nodiscard]] std::optional<ChargenEffects> accumulateBiography(
    const BiographyRegistry& biography, const std::vector<std::int32_t>& chosenAnswers);

/// Folds one more effect list (a custom-path advantage or disadvantage) into
/// an accumulator -- the same summing/sorting discipline accumulateBiography
/// uses, exposed so the advantage shop speaks the identical vocabulary.
void accumulateEffects(ChargenEffects& into, const std::vector<ChargenEffect>& effects);

/// Applies the skill deltas onto `track`: level(id) + delta through
/// SkillTrack::setLevel, exactly once, AFTER the sheet has designated the
/// starts -- the doc's "added onto the designated start" line. Returns how
/// many ids the track knew, the same honesty-count contract
/// CompanionTemplate::applyStartingSkills keeps. NOT idempotent across
/// repeated calls (deltas are deltas); the boot seam calls it once, and the
/// purity that matters lives in accumulateBiography above.
std::int32_t applySkillDeltas(SkillTrack& track, const ChargenEffects& effects);

// ---------------------------------------------------------------------------
// dagger points -> Q8
// ---------------------------------------------------------------------------

/// The custom path's clamp on the one currency (doc section 5, draft).
inline constexpr std::int32_t kDaggerPointsMin = -8;
inline constexpr std::int32_t kDaggerPointsMax = 12;

/// The doc's own line, integerized: multiplier = 1.0 + 0.05 x P, in Q8,
/// rounded half-up, over P clamped to [kDaggerPointsMin, kDaggerPointsMax].
/// P = 0 lands EXACTLY on kDaggerNeutralQ8 -- the untouched-dagger guarantee.
[[nodiscard]] std::int32_t daggerMultiplierQ8ForPoints(std::int32_t points) noexcept;

[[nodiscard]] std::filesystem::path callingRawsPath(const std::filesystem::path& contentDir);
[[nodiscard]] std::filesystem::path quizRawsPath(const std::filesystem::path& contentDir);
[[nodiscard]] std::filesystem::path biographyRawsPath(const std::filesystem::path& contentDir);

}  // namespace granadad::sim
