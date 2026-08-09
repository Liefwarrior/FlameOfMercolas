#pragma once

// TASK #81. THE RADIANT QUEST GENERATOR CORE: templated objectives that bind
// to REAL sim state AT GENERATION TIME.
//
// WHAT THIS IS, AND WHAT IT DELIBERATELY IS NOT.
//
// contract.hpp already proved the shape S6 was asked for: "generated,
// repeatable and varied, but must not feel like slot-machine filler -- each
// job should reference real places, real named NPCs and the real economy"
// (DECISIONS.md, 2026-08-04). That answer still stands and this file does not
// replace it -- a ContractBoard job is a GOODS transaction with the Watch, a
// merchant house or the roofs, and it is still the only place in this build a
// scalp or a jar of moonshine changes hands for coin.
//
// What has changed since S6 is the WARD ITSELF. Contract's patrons and
// sources are drawn from content/raws/names/notables.json -- forty-two
// authored people, bound to a SITE FIELD read once out of that same file at
// LOAD TIME. #78 through #80 then put six hundred and fifty-odd more bodies
// on the street, each with a real baked name (WardIdentity, out of the
// owner's own name pools or a notable's own file), a real job, and a real
// tile they are standing on RIGHT NOW. A generator that only ever reaches the
// Forty-Two is leaving five hundred and some named, walking, working people
// out of its own vocabulary.
//
// So this generator draws its cast from the WHOLE ward roster
// (WardPopulation::actors()/identities()), and it resolves a NAME'S PLACE by
// asking the map what is under that body's feet AT THE MOMENT THE BOARD IS
// GENERATED (sim::docks::placeLabelAt) rather than by reading a site string
// baked into the raws once and never revisited. A brief that says "find
// GOODWIFE SALLA, last seen near ROPEWYND" is true of wherever her own
// schedule actually put her that day, not of a fact the file remembered.
//
// TWO OBJECTIVE KINDS, AND THE VOCABULARY IS APPENDABLE, NOT CLOSED. Every
// kind this file supports is one a body can actually go and do in this build,
// which is the same discipline questline.hpp states for its own stage
// vocabulary ("authoring a stage against a condition nothing can evaluate
// would be authoring a quest that cannot be finished"):
//
//   fetch     bring `units` of a named trade good -- Contraband, the same
//             five-word vocabulary contract.hpp and the Watch already share --
//             from wherever `target` is standing to wherever `giver` is.
//   deliver   carry word from `giver` to `target`, no goods and no units, just
//             two real people and the two real places they are standing.
//
// EVERY PROPER NOUN IS EITHER AUTHORED OR LIVE, NEVER INVENTED. The SHAPE
// (which template, which two bodies, how many, of what) is drawn from the
// same pure counter chain the rest of this simulation draws from --
// (worldSeed, day, slot) -- so the same day of the same world always offers
// the same board on any machine. Every NAME in the output is either a string
// out of an authored template (the verb, the brief's fixed words) or a live
// WardIdentity's own baked name; nothing here calls a name generator or
// composes one out of parts. A template whose giver/target type lists resolve
// to nobody eligible tonight is simply not offered that slot -- see
// RadiantBoard::refresh -- the same refusal discipline ContractRaws::load
// applies to a broker or a patron the owner's file does not have.
//
// PROSE IS DELIBERATELY NOT THIS SPRINT'S JOB. The authored `brief` strings in
// content/raws/quests/radiant_quests.json are plain, correct English -- no
// enum leakage, no unsubstituted token, nothing that violates the standing
// "no shitty English" bar -- but they are NOT trying to be Crell's memo or
// Maell's letters. This is the mechanical engine: it proves every generated
// sentence names a body that exists and a place that body is actually
// standing in. Writing THAT sentence so it sounds like the ward is a later
// sprint's job, and it has real nouns to write around because of this file.
//
// NO FLOATS. NO UNORDERED CONTAINERS: candidate pools are built fresh each
// refresh as a std::vector in ascending actor-id order, and the board itself
// is a std::vector sorted by objective id, exactly like ContractBoard's rows.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/contraband.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the vocabulary
// ---------------------------------------------------------------------------

/// Append-only: the ordinal is hashed. See the file header for what each kind
/// asks a body to actually do.
enum class RadiantKind : std::uint8_t {
    Fetch = 0,
    Deliver = 1,
};

[[nodiscard]] std::string_view radiantKindName(RadiantKind kind) noexcept;
/// The raws symbol a template names ("fetch", "deliver") resolved to a kind,
/// or false when the string names neither.
[[nodiscard]] bool radiantKindFromSymbol(std::string_view symbol, RadiantKind& out) noexcept;

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

/// One authored template. Everything a generated objective is, before the
/// draw decides which two live bodies and which numbers go in it.
struct RadiantTemplate {
    std::string id;
    RadiantKind kind = RadiantKind::Fetch;
    /// The word the brief opens on in a topic list: FETCH, CARRY WORD.
    std::string verb;
    /// The template. {giver} {giverPlace} {target} {targetPlace} {units}
    /// {good}. `target` means "who the goods come off" for a fetch and "who
    /// the word is for" for a deliver -- the same patron/source shape
    /// contract.hpp uses, renamed because a deliver has no patron.
    std::string brief;
    /// Which WardTypes may be drawn as the giver. Person types only -- see
    /// RadiantRaws::load, which drops a beast named here rather than let a cat
    /// hand out work. Never empty for a loaded template.
    std::vector<WardType> giverTypes;
    /// Which WardTypes may be drawn as the target. Never empty for a loaded
    /// template, and never sharing a body with the giver -- see refresh().
    std::vector<WardType> targetTypes;
    /// FETCH ONLY. Which of the five contraband goods this template may ask
    /// for. Never empty for a loaded fetch template.
    std::vector<Contraband> goods;
    std::int32_t unitsMin = 1;
    std::int32_t unitsMax = 1;
    /// FETCH: pay is unitsdrawn * payPerUnit. DELIVER: pay is payFlat.
    std::int32_t payPerUnit = 0;
    std::int32_t payFlat = 0;
};

/// The board's authored half: every template this build can draw from.
class RadiantRaws {
public:
    /// Reads content/raws/quests/radiant_quests.json. NEVER throws -- a
    /// missing or malformed file leaves an empty table and the board offers
    /// nothing, which is the rule every raws loader in this build follows.
    ///
    /// REFUSES BY NAME, the same discipline ContractRaws::load applies to a
    /// broker or a patron: a template naming an unknown kind, an unrecognised
    /// WardType, an unrecognised Contraband symbol, or left with an empty
    /// giver/target/goods list after that filtering, is dropped whole. That is
    /// what keeps a generated objective from ever asking a mouse to carry a
    /// letter.
    [[nodiscard]] static RadiantRaws load(const std::filesystem::path& contentDir);

    [[nodiscard]] bool loaded() const noexcept { return !templates_.empty(); }
    [[nodiscard]] const std::vector<RadiantTemplate>& templates() const noexcept {
        return templates_;
    }

private:
    /// In authored file order. Never reordered after load -- the draw picks an
    /// INDEX, so reordering this would re-roll every board of every world.
    std::vector<RadiantTemplate> templates_;
};

[[nodiscard]] std::filesystem::path radiantRawsPath(const std::filesystem::path& contentDir);

// ---------------------------------------------------------------------------
// one generated objective
// ---------------------------------------------------------------------------

struct RadiantObjective {
    /// Unique for the life of a board. day * kRadiantObjectivesPerDay + slot,
    /// the same scheme ContractBoard uses.
    std::int32_t id = 0;
    std::string templateId;
    RadiantKind kind = RadiantKind::Fetch;
    std::string verb;

    /// The WardActor who wants this done, and the ward's own baked name for
    /// them (WardIdentity::name -- a notable's authored name, or a real draw
    /// out of the names pools; never invented here).
    std::int32_t giverActorId = -1;
    std::string giverName;
    /// Where that body was actually standing WHEN THIS BOARD WAS GENERATED --
    /// sim::docks::placeLabelAt on their live tile, not an authored site.
    std::string giverPlace;

    /// FETCH: who the goods come off. DELIVER: who the word is for.
    std::int32_t targetActorId = -1;
    std::string targetName;
    std::string targetPlace;

    /// Meaningful only when kind == Fetch.
    Contraband good = Contraband::Scalp;
    std::int32_t units = 0;

    /// The composed sentence: every {token} in the template's brief
    /// substituted with a real noun. Never contains a brace on a
    /// successfully-generated row -- see test_radiant_quest.cpp.
    std::string brief;
    std::int32_t pay = 0;
    std::int32_t postedOnDay = 0;

    [[nodiscard]] bool isFetch() const noexcept { return kind == RadiantKind::Fetch; }
};

/// How many objectives a fresh board offers. Four, matching kOffersPerDay --
/// not because the two boards share a number for any deeper reason, but
/// because four is the shape S6 already found: enough to plan a route
/// through the ward, few enough that "which one" is still a decision.
inline constexpr std::int32_t kRadiantObjectivesPerDay = 4;

// ---------------------------------------------------------------------------
// the board
// ---------------------------------------------------------------------------

/// Everything on offer today, generated from the live ward.
class RadiantBoard {
public:
    /// Posts a fresh day's objectives.
    ///
    /// DETERMINISTIC AND PURE over (worldSeed, day) for a GIVEN state of
    /// `ward` -- the same day of the same world offers the same board on any
    /// machine that has ticked the same ward to the same instant, exactly the
    /// property test_radiant_quest.cpp's RADIANCE cases pin. It is NOT pure
    /// across time the way ContractBoard::refresh is: `ward` is live state and
    /// where a body is standing changes across the day, which is the entire
    /// point of resolving giverPlace/targetPlace HERE rather than at load.
    ///
    /// A template whose giverTypes or targetTypes resolve to no eligible,
    /// visible, living person in `ward` right now is simply skipped for that
    /// slot -- fewer than four objectives on a quiet day is the honest answer,
    /// the same way ContractBoard leaves a slot unfilled when its pool is
    /// empty.
    void refresh(std::int32_t day, std::uint64_t worldSeed, const RadiantRaws& raws,
                const WardPopulation& ward);

    [[nodiscard]] std::int32_t day() const noexcept { return day_; }
    [[nodiscard]] const std::vector<RadiantObjective>& objectives() const noexcept {
        return rows_;
    }
    [[nodiscard]] const RadiantObjective* find(std::int32_t id) const noexcept;

    void hashInto(HashSink& sink) const;

private:
    std::int32_t day_ = -1;
    /// Ascending by id. Never reordered.
    std::vector<RadiantObjective> rows_;
};

}  // namespace granadad::sim
