#include "granadad/sim/social.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace granadad::sim {

namespace {

/// The ledger encoding's magic and version. A version bump makes every older
/// blob refuse to load rather than be misread -- the same loud-fail rule the
/// SkillTrackRegistry save-frame guard follows in the raws' own notes.
constexpr std::uint8_t kLedgerMagic0 = 'G';
constexpr std::uint8_t kLedgerMagic1 = 'S';
constexpr std::uint8_t kLedgerVersion = 1;

constexpr std::int32_t clampDisposition(std::int32_t value) noexcept {
    return std::clamp(value, kDispositionMin, kDispositionMax);
}

void putI32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    out.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 24) & 0xFFU));
}

[[nodiscard]] bool takeI32(const std::vector<std::uint8_t>& bytes, std::size_t& cursor,
                           std::int32_t& out) {
    if (cursor + 4 > bytes.size()) {
        return false;
    }
    const std::uint32_t bits = static_cast<std::uint32_t>(bytes[cursor]) |
                               (static_cast<std::uint32_t>(bytes[cursor + 1]) << 8) |
                               (static_cast<std::uint32_t>(bytes[cursor + 2]) << 16) |
                               (static_cast<std::uint32_t>(bytes[cursor + 3]) << 24);
    cursor += 4;
    out = static_cast<std::int32_t>(bits);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// deeds
// ---------------------------------------------------------------------------

std::string_view deedName(Deed deed) noexcept {
    switch (deed) {
        case Deed::Spoke:
            return "spoke";
        case Deed::BoughtDrink:
            return "bought a drink";
        case Deed::PaidAsking:
            return "paid asking";
        case Deed::HaggledFair:
            return "haggled fair";
        case Deed::HaggledHard:
            return "haggled hard";
        case Deed::Lowballed:
            return "lowballed";
        case Deed::WalkedOut:
            return "walked out";
        case Deed::Robbed:
            return "robbed";
        case Deed::Struck:
            return "struck";
        case Deed::DrewSteel:
            return "drew steel";
        case Deed::Listened:
            return "listened";
        case Deed::SpokePolitely:
            return "spoke politely";
        case Deed::SpokeBluntly:
            return "spoke bluntly";
    }
    return "?";
}

std::int32_t deedWeight(Deed deed) noexcept {
    switch (deed) {
        // Zero, and deliberately: saying hello is not a favour. See the talk
        // ceiling in record() for the rest of that rule.
        case Deed::Spoke:
            return 0;
        case Deed::Listened:
            return 2;
        // #82. A HAIR more than plain Listened -- politeness alone was never
        // meant to be a lever, only a nudge that can tip a standing already
        // sitting near a threshold.
        case Deed::SpokePolitely:
            return 1;
        // #82. NEGATIVE, and on purpose: unlike Spoke/Listened/SpokePolitely
        // this weight is NOT read through record()'s talk-ceiling branch, so
        // it moves disposition for real -- see record()'s own note on why the
        // three "just talk" deeds above split this way.
        case Deed::SpokeBluntly:
            return -2;
        case Deed::BoughtDrink:
            return 12;
        case Deed::PaidAsking:
            return 6;
        case Deed::HaggledFair:
            return 3;
        case Deed::HaggledHard:
            return -4;
        case Deed::Lowballed:
            return -8;
        case Deed::WalkedOut:
            return -5;
        case Deed::Robbed:
            return -45;
        case Deed::Struck:
            return -35;
        // A blade in a room full of people is the single worst thing in the
        // list, because it is the one that ends the room.
        case Deed::DrewSteel:
            return -60;
    }
    return 0;
}

std::int32_t witnessWeight(Deed deed) noexcept {
    switch (deed) {
        // Small talk with somebody else is not news -- and neither is HOW it
        // was said. A room does not turn to look at a polite word or a curt
        // one; it turns for a robbery.
        case Deed::Spoke:
        case Deed::Listened:
        case Deed::SpokePolitely:
        case Deed::SpokeBluntly:
        case Deed::HaggledFair:
            return 0;
        case Deed::BoughtDrink:
            return 3;
        case Deed::PaidAsking:
            return 1;
        case Deed::HaggledHard:
            return -1;
        case Deed::Lowballed:
            return -2;
        case Deed::WalkedOut:
            return -1;
        // Watching somebody get robbed is nearly as bad as being the one robbed.
        case Deed::Robbed:
            return -30;
        case Deed::Struck:
            return -25;
        case Deed::DrewSteel:
            return -50;
    }
    return 0;
}

Attitude attitudeFor(std::int32_t disposition) noexcept {
    if (disposition >= kKinAtOrAbove) {
        return Attitude::Kin;
    }
    if (disposition >= kFriendAtOrAbove) {
        return Attitude::Friend;
    }
    if (disposition >= kWarmAtOrAbove) {
        return Attitude::Warm;
    }
    if (disposition <= kHostileAtOrBelow) {
        return Attitude::Hostile;
    }
    if (disposition <= kColdAtOrBelow) {
        return Attitude::Cold;
    }
    return Attitude::Neutral;
}

// ---------------------------------------------------------------------------
// the ledger
// ---------------------------------------------------------------------------

const Memory* SocialLedger::memoryOf(std::int32_t actorId) const noexcept {
    const auto found = std::lower_bound(
        memories_.begin(), memories_.end(), actorId,
        [](const Memory& memory, std::int32_t probe) { return memory.actorId < probe; });
    if (found == memories_.end() || found->actorId != actorId) {
        return nullptr;
    }
    return &*found;
}

std::int32_t SocialLedger::dispositionOf(std::int32_t actorId) const noexcept {
    const Memory* memory = memoryOf(actorId);
    return memory == nullptr ? 0 : memory->disposition;
}

Memory& SocialLedger::entryFor(std::int32_t actorId) {
    const auto found = std::lower_bound(
        memories_.begin(), memories_.end(), actorId,
        [](const Memory& memory, std::int32_t probe) { return memory.actorId < probe; });
    if (found != memories_.end() && found->actorId == actorId) {
        return *found;
    }
    Memory fresh;
    fresh.actorId = actorId;
    // Inserted in place, so the vector stays ascending by id and iteration
    // order never depends on the order deeds happened to arrive in.
    return *memories_.insert(found, fresh);
}

std::int32_t SocialLedger::record(std::int32_t actorId, Deed deed) {
    Memory& memory = entryFor(actorId);
    const std::int32_t weight = deedWeight(deed);
    if (deed == Deed::Spoke || deed == Deed::Listened || deed == Deed::SpokePolitely) {
        // YOU CANNOT CHAT YOUR WAY INTO BEING LIKED. Hearing somebody out is
        // worth something, and it stops being worth anything just short of
        // WARM: past that the only currency is coin spent, favours done and
        // trouble not started. Without this, standing in front of a docker
        // pressing one key three hundred times would make you his brother.
        //
        // #82's SpokePolitely rides this SAME branch -- manners are still
        // just talk. SpokeBluntly deliberately does NOT: talking down to
        // somebody can cost you standing you already had, the same as
        // WalkedOut or Lowballed can, and both of those fall through to the
        // plain clamp below.
        memory.disposition =
            std::max(memory.disposition, std::min(memory.disposition + weight, kTalkCeiling));
    } else {
        memory.disposition = clampDisposition(memory.disposition + weight);
    }
    memory.lastDeed = deed;
    if (weight > 0) {
        ++memory.favours;
    } else if (weight < 0) {
        ++memory.injuries;
    }
    ++deedsDone_;
    return memory.disposition;
}

std::int32_t SocialLedger::witness(std::int32_t actorId, Deed deed) {
    const std::int32_t weight = witnessWeight(deed);
    if (weight == 0) {
        // No entry created: somebody who watched you buy a round and felt
        // nothing about it has not MET you, and should still greet as a
        // stranger.
        return dispositionOf(actorId);
    }
    Memory& memory = entryFor(actorId);
    memory.disposition = clampDisposition(memory.disposition + weight);
    memory.lastDeed = deed;
    // The ward's own number moves by a quarter of what one witness feels, so a
    // crowded room moves it and an empty one barely does.
    reputation_ = clampDisposition(reputation_ + (weight >= 0 ? (weight + 3) / 4
                                                              : -((-weight + 3) / 4)));
    return memory.disposition;
}

std::int32_t SocialLedger::noteConversation(std::int32_t actorId) {
    Memory& memory = entryFor(actorId);
    return ++memory.talks;
}

void SocialLedger::seed(std::int32_t actorId, std::int32_t disposition) {
    entryFor(actorId).disposition = clampDisposition(disposition);
}

// VERIFICATION GAP (S3): reputation() has exactly one consumer, and it is this
// label in the corner of the HUD. No actor reads the ward's opinion to decide
// anything -- a bouncer does not refuse a man the whole district is talking
// about, and a shopkeeper prices off HIS OWN memory of you and not the ward's.
// The number is real, moved by real witnesses and folded into the world hash;
// what it does not yet do is anything.
std::string_view SocialLedger::reputationLabel() const noexcept {
    if (reputation_ >= kFriendAtOrAbove) {
        return "THE WARD OWES YOU";
    }
    if (reputation_ >= kWarmAtOrAbove) {
        return "WELL SPOKEN OF";
    }
    if (reputation_ <= kHostileAtOrBelow) {
        return "THE WARD WANTS YOU GONE";
    }
    if (reputation_ <= kColdAtOrBelow) {
        return "TALKED ABOUT BADLY";
    }
    return kReputationUnremarkable;
}

std::vector<std::uint8_t> SocialLedger::encode() const {
    std::vector<std::uint8_t> out;
    out.push_back(kLedgerMagic0);
    out.push_back(kLedgerMagic1);
    out.push_back(kLedgerVersion);
    putI32(out, reputation_);
    putI32(out, deedsDone_);
    putI32(out, static_cast<std::int32_t>(memories_.size()));
    for (const Memory& memory : memories_) {
        putI32(out, memory.actorId);
        putI32(out, memory.disposition);
        putI32(out, memory.favours);
        putI32(out, memory.injuries);
        putI32(out, memory.talks);
        out.push_back(static_cast<std::uint8_t>(memory.lastDeed));
    }
    return out;
}

bool SocialLedger::decode(const std::vector<std::uint8_t>& bytes, SocialLedger& out) {
    if (bytes.size() < 3 || bytes[0] != kLedgerMagic0 || bytes[1] != kLedgerMagic1 ||
        bytes[2] != kLedgerVersion) {
        return false;
    }
    std::size_t cursor = 3;
    SocialLedger loaded;
    std::int32_t count = 0;
    if (!takeI32(bytes, cursor, loaded.reputation_) ||
        !takeI32(bytes, cursor, loaded.deedsDone_) || !takeI32(bytes, cursor, count) ||
        count < 0) {
        return false;
    }
    loaded.memories_.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        Memory memory;
        if (!takeI32(bytes, cursor, memory.actorId) ||
            !takeI32(bytes, cursor, memory.disposition) ||
            !takeI32(bytes, cursor, memory.favours) ||
            !takeI32(bytes, cursor, memory.injuries) || !takeI32(bytes, cursor, memory.talks)) {
            return false;
        }
        if (cursor >= bytes.size() || bytes[cursor] >= static_cast<std::uint8_t>(kDeedCount)) {
            return false;
        }
        memory.lastDeed = static_cast<Deed>(bytes[cursor]);
        ++cursor;
        // Ascending, strictly. A blob whose entries are out of order would give
        // a ledger whose binary search silently misses people.
        if (i > 0 && memory.actorId <= loaded.memories_.back().actorId) {
            return false;
        }
        loaded.memories_.push_back(memory);
    }
    if (cursor != bytes.size()) {
        return false;
    }
    out = std::move(loaded);
    return true;
}

void SocialLedger::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(memories_.size()));
    for (const Memory& memory : memories_) {
        sink.put_int(static_cast<std::uint32_t>(memory.actorId));
        sink.put_int(static_cast<std::uint32_t>(memory.disposition));
        sink.put_int(static_cast<std::uint32_t>(memory.favours));
        sink.put_int(static_cast<std::uint32_t>(memory.injuries));
        sink.put_int(static_cast<std::uint32_t>(memory.talks));
        sink.put_byte(static_cast<std::uint32_t>(memory.lastDeed));
    }
    sink.put_int(static_cast<std::uint32_t>(reputation_));
    sink.put_int(static_cast<std::uint32_t>(deedsDone_));
}

// ---------------------------------------------------------------------------
// skills
// ---------------------------------------------------------------------------

std::int32_t usesForLevel(std::int32_t level) noexcept {
    // 4 uses for the first level, 6 for the second, and so on. A dead-flat
    // curve makes level 50 as cheap as level 1; anything steeper than linear
    // makes the first hour of play feel like nothing is happening.
    return 4 + 2 * std::max(0, level);
}

std::filesystem::path skillRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "skills" / "skills.json";
}

SkillTrack SkillTrack::load(const std::filesystem::path& contentDir) {
    SkillTrack out;
    std::ifstream file(skillRawsPath(contentDir), std::ios::binary);
    if (!file) {
        return out;
    }
    std::ostringstream text;
    text << file.rdbuf();
    const nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return out;
    }
    const auto skills = document.find("skills");
    if (skills == document.end() || !skills->is_array()) {
        return out;
    }
    for (const nlohmann::json& node : *skills) {
        if (!node.is_object()) {
            continue;
        }
        const auto id = node.find("id");
        if (id == node.end() || !id->is_string()) {
            continue;
        }
        Entry entry;
        entry.id = id->get<std::string>();
        if (entry.id.empty()) {
            continue;
        }
        const auto display = node.find("displayName");
        entry.displayName =
            display != node.end() && display->is_string() ? display->get<std::string>() : entry.id;

        const auto governing = node.find("governingAttribute");
        entry.governingAttribute = governing != node.end() && governing->is_string()
                                        ? attributeFromRaw(governing->get<std::string>())
                                        : std::nullopt;
        const auto aptitude = node.find("aptitudeTier");
        entry.aptitudeTier = aptitude != node.end() && aptitude->is_string()
                                  ? aptitudeTierFromRaw(aptitude->get<std::string>())
                                  : AptitudeTier::Trained;

        out.entries_.push_back(std::move(entry));
    }
    std::sort(out.entries_.begin(), out.entries_.end(),
              [](const Entry& a, const Entry& b) { return a.id < b.id; });
    return out;
}

const SkillTrack::Entry* SkillTrack::find(std::string_view id) const noexcept {
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), id,
        [](const Entry& entry, std::string_view probe) { return entry.id < probe; });
    if (found == entries_.end() || found->id != id) {
        return nullptr;
    }
    return &*found;
}

SkillTrack::Entry* SkillTrack::findMutable(std::string_view id) noexcept {
    // Same lookup as find(), once: cast the const away from the const overload
    // rather than repeating the lower_bound and its comparator a second time.
    return const_cast<Entry*>(static_cast<const SkillTrack&>(*this).find(id));
}

std::int32_t SkillTrack::level(std::string_view id) const noexcept {
    const Entry* entry = find(id);
    return entry == nullptr ? 0 : entry->level;
}

std::optional<AttributeId> SkillTrack::governingAttribute(std::string_view id) const noexcept {
    const Entry* entry = find(id);
    return entry == nullptr ? std::nullopt : entry->governingAttribute;
}

AptitudeTier SkillTrack::aptitudeTier(std::string_view id) const noexcept {
    const Entry* entry = find(id);
    return entry == nullptr ? AptitudeTier::Trained : entry->aptitudeTier;
}

bool SkillTrack::setLevel(std::string_view id, std::int32_t level) noexcept {
    Entry* entry = findMutable(id);
    if (entry == nullptr) {
        return false;
    }
    entry->level = std::clamp(level, 0, 100);
    entry->uses = 0;
    return true;
}

void SkillTrack::setAdvanceMultiplierQ8(std::int32_t q8) noexcept {
    advanceMultiplierQ8_ = std::clamp(q8, kDaggerMinQ8, kDaggerMaxQ8);
}

std::int32_t SkillTrack::scaledUsesForLevel(std::int32_t level) const noexcept {
    // Integer throughout, truncating: charge = flat * 256 / dagger. At the
    // neutral 256 this is EXACTLY usesForLevel(level) -- u * 256 / 256 has no
    // remainder to lose -- which is the whole default-changes-nothing
    // guarantee the dagger's header makes. Floored at 1: even a 3.0x dagger
    // pays at least one use per level. No overflow: usesForLevel caps at 204
    // (level 100) and 204 * 256 is far inside int32.
    return std::max(1, usesForLevel(level) * kDaggerNeutralQ8 / advanceMultiplierQ8_);
}

bool SkillTrack::use(std::string_view id, std::int32_t effort) noexcept {
    Entry* entry = findMutable(id);
    if (entry == nullptr || effort <= 0) {
        return false;
    }
    entry->uses += effort;
    bool levelled = false;
    // THE ONE PLACE THE DAGGER BITES. usesForLevel() itself is untouched;
    // the scaled charge is consulted exactly where the flat one used to be.
    while (entry->level < 100 && entry->uses >= scaledUsesForLevel(entry->level)) {
        entry->uses -= scaledUsesForLevel(entry->level);
        ++entry->level;
        levelled = true;
    }
    if (entry->level >= 100) {
        entry->uses = 0;
    }
    return levelled;
}

void SkillTrack::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(entries_.size()));
    for (const Entry& entry : entries_) {
        sink.put_int(static_cast<std::uint32_t>(entry.level));
        sink.put_int(static_cast<std::uint32_t>(entry.uses));
    }
    // The dagger. A HASH-STRUCTURE CHANGE, on purpose and stated: a leveling
    // speed the twin-run gate cannot see is one it does not protect. Committed
    // after the entries so the entry block's own layout is undisturbed.
    sink.put_int(static_cast<std::uint32_t>(advanceMultiplierQ8_));
}

}  // namespace granadad::sim
