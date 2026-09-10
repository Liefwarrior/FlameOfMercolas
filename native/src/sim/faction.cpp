#include "granadad/sim/faction.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace granadad::sim {

namespace {

/// Version byte on the encoded ledger. decode() refuses anything else rather
/// than reinterpreting somebody else's bytes.
constexpr std::uint8_t kLedgerCodecVersion = 1;

[[nodiscard]] nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return nlohmann::json{};
    }
    std::ostringstream text;
    text << file.rdbuf();
    nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded()) {
        return nlohmann::json{};
    }
    return document;
}

[[nodiscard]] std::string stringField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return {};
    }
    return found->get<std::string>();
}

[[nodiscard]] std::int32_t intField(const nlohmann::json& node, const char* key,
                                    std::int32_t fallback = 0) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_number_integer()) {
        return fallback;
    }
    return found->get<std::int32_t>();
}

void readStrings(const nlohmann::json& node, const char* key, std::vector<std::string>& out) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_array()) {
        return;
    }
    for (const nlohmann::json& row : *found) {
        if (row.is_string()) {
            std::string value = row.get<std::string>();
            if (!value.empty()) {
                out.push_back(std::move(value));
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void put_string(HashSink& sink, std::string_view text) {
    sink.put_int(static_cast<std::uint32_t>(text.size()));
    for (const char c : text) {
        sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(c)));
    }
}

void put_i32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    out.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 24) & 0xFFU));
}

[[nodiscard]] std::int32_t take_i32(const std::vector<std::uint8_t>& bytes, std::size_t& at) {
    const std::uint32_t bits = static_cast<std::uint32_t>(bytes[at]) |
                               (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
                               (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
                               (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    at += 4;
    return static_cast<std::int32_t>(bits);
}

}  // namespace

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

std::filesystem::path factionRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "factions" / "factions.json";
}

std::filesystem::path factionRankRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "factions" / "ranks.json";
}

FactionRegistry FactionRegistry::load(const std::filesystem::path& contentDir) {
    FactionRegistry out;

    const nlohmann::json registry = readJson(factionRawsPath(contentDir));
    if (!registry.is_object()) {
        return out;
    }
    const auto rows = registry.find("factions");
    if (rows == registry.end() || !rows->is_array()) {
        return out;
    }
    for (const nlohmann::json& node : *rows) {
        if (!node.is_object()) {
            continue;
        }
        Faction faction;
        faction.id = stringField(node, "id");
        if (faction.id.empty()) {
            continue;
        }
        faction.displayName = stringField(node, "displayName");
        readStrings(node, "memberJobs", faction.memberJobs);
        out.factions_.push_back(std::move(faction));
    }
    // Sorted by id, which is also the numbering factions.json's own note
    // specifies: dockhands=0, merchants=1, skyrunners=2, temple=3, watch=4.
    std::sort(out.factions_.begin(), out.factions_.end(),
              [](const Faction& a, const Faction& b) { return a.id < b.id; });
    out.factions_.erase(std::unique(out.factions_.begin(), out.factions_.end(),
                                    [](const Faction& a, const Faction& b) { return a.id == b.id; }),
                        out.factions_.end());
    out.ladders_.assign(out.factions_.size(), FactionLadder{});

    // The ladders. A ladder for a faction the owner's registry does not have is
    // DROPPED rather than allowed to invent one -- the registry is canon and
    // this file only hangs rungs off it.
    const nlohmann::json ranks = readJson(factionRankRawsPath(contentDir));
    if (!ranks.is_object()) {
        return out;
    }
    const auto ladders = ranks.find("ladders");
    if (ladders == ranks.end() || !ladders->is_array()) {
        return out;
    }
    for (const nlohmann::json& node : *ladders) {
        if (!node.is_object()) {
            continue;
        }
        const std::int32_t index = out.indexOf(stringField(node, "faction"));
        if (index < 0) {
            continue;
        }
        FactionLadder ladder;
        ladder.skill = stringField(node, "skill");
        ladder.baseInfluence =
            std::clamp(intField(node, "baseInfluence"), kInfluenceMin, kInfluenceMax);
        readStrings(node, "rivals", ladder.rivals);

        const auto rungs = node.find("ranks");
        if (rungs != node.end() && rungs->is_array()) {
            for (const nlohmann::json& rung : *rungs) {
                if (!rung.is_object()) {
                    continue;
                }
                FactionRank rank;
                rank.title = stringField(rung, "title");
                if (rank.title.empty()) {
                    continue;
                }
                rank.standing = intField(rung, "standing");
                rank.skillLevel = intField(rung, "skillLevel");
                readStrings(rung, "unlocks", rank.unlocks);
                ladder.ranks.push_back(std::move(rank));
            }
        }
        // A ladder whose rungs get CHEAPER as they go up would let a player
        // skip one by ranking past it. Made monotonic here rather than trusted:
        // each rung costs at least what the one below it did.
        for (std::size_t i = 1; i < ladder.ranks.size(); ++i) {
            ladder.ranks[i].standing =
                std::max(ladder.ranks[i].standing, ladder.ranks[i - 1].standing);
            ladder.ranks[i].skillLevel =
                std::max(ladder.ranks[i].skillLevel, ladder.ranks[i - 1].skillLevel);
        }
        out.ladders_[static_cast<std::size_t>(index)] = std::move(ladder);
    }
    return out;
}

std::int32_t FactionRegistry::indexOf(std::string_view id) const noexcept {
    if (id.empty()) {
        return -1;
    }
    const auto found = std::lower_bound(
        factions_.begin(), factions_.end(), id,
        [](const Faction& faction, std::string_view probe) { return faction.id < probe; });
    if (found == factions_.end() || found->id != id) {
        return -1;
    }
    return static_cast<std::int32_t>(found - factions_.begin());
}

const Faction* FactionRegistry::find(std::string_view id) const noexcept {
    return at(indexOf(id));
}

const Faction* FactionRegistry::at(std::int32_t index) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= factions_.size()) {
        return nullptr;
    }
    return &factions_[static_cast<std::size_t>(index)];
}

const FactionLadder* FactionRegistry::ladder(std::int32_t index) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= ladders_.size()) {
        return nullptr;
    }
    const FactionLadder& found = ladders_[static_cast<std::size_t>(index)];
    return found.ranks.empty() ? nullptr : &found;
}

std::int32_t FactionRegistry::factionForJobPrefix(std::string_view prefix) const noexcept {
    if (prefix.empty()) {
        return -1;
    }
    for (std::size_t i = 0; i < factions_.size(); ++i) {
        for (const std::string& job : factions_[i].memberJobs) {
            // "trade" matches "trade.stallkeep" and never "trader.something":
            // the dot is required, so a prefix can only ever match a family.
            if (job.size() > prefix.size() && job.compare(0, prefix.size(), prefix) == 0 &&
                job[prefix.size()] == '.') {
                return static_cast<std::int32_t>(i);
            }
        }
    }
    return -1;
}

std::vector<std::int32_t> FactionRegistry::rivalsOf(std::int32_t index) const {
    std::vector<std::int32_t> out;
    const FactionLadder* found = ladder(index);
    if (found == nullptr) {
        return out;
    }
    for (const std::string& rival : found->rivals) {
        const std::int32_t other = indexOf(rival);
        if (other >= 0 && other != index) {
            out.push_back(other);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// deeds
// ---------------------------------------------------------------------------

std::int32_t factionDeedWeight(Deed deed) noexcept {
    // A guild hears about it secondhand, so every weight is smaller than what
    // the same deed does to the person it was done to -- and the SIGNS match,
    // because a guild does not thank you for robbing one of its own.
    switch (deed) {
        case Deed::Spoke:
            return 0;
        case Deed::BoughtDrink:
            return 2;
        case Deed::PaidAsking:
            return 2;
        case Deed::HaggledFair:
            return 1;
        case Deed::HaggledHard:
            return -1;
        case Deed::Lowballed:
            return -2;
        case Deed::WalkedOut:
            return -1;
        case Deed::Robbed:
            return -8;
        case Deed::Struck:
            return -10;
        case Deed::DrewSteel:
            return -15;
        case Deed::Listened:
            return 0;
        // #82. A guild does not hear secondhand HOW you asked -- only what
        // came of asking, which is every other row in this table.
        case Deed::SpokePolitely:
        case Deed::SpokeBluntly:
            return 0;
        // ACTION-COMBAT BUILD: a killing is the heaviest thing a guild hears
        // secondhand, below drawn steel and, like every row, smaller in
        // magnitude than what it did to the person it was done to.
        case Deed::Slew:
            return -20;
    }
    return 0;
}

std::string_view ladderResultName(LadderResult result) noexcept {
    switch (result) {
        case LadderResult::Granted:
            return "granted";
        case LadderResult::NoLadder:
            return "no ladder";
        case LadderResult::AlreadyThere:
            return "already there";
        case LadderResult::NeedsStanding:
            return "needs standing";
        case LadderResult::NeedsSkill:
            return "needs skill";
        case LadderResult::NotAMember:
            return "not a member";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// the ledger
// ---------------------------------------------------------------------------

void FactionLedger::attach(std::shared_ptr<const FactionRegistry> registry) {
    registry_ = std::move(registry);
    rows_.clear();
    if (registry_ == nullptr) {
        return;
    }
    rows_.resize(registry_->size());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const FactionLadder* ladder = registry_->ladder(static_cast<std::int32_t>(i));
        rows_[i].influence = ladder == nullptr ? 0 : ladder->baseInfluence;
    }
}

bool FactionLedger::inRange(std::int32_t index) const noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < rows_.size();
}

bool FactionLedger::isMember(std::int32_t index) const noexcept {
    return inRange(index) && rows_[static_cast<std::size_t>(index)].member;
}

std::int32_t FactionLedger::rank(std::int32_t index) const noexcept {
    return inRange(index) ? rows_[static_cast<std::size_t>(index)].rank : 0;
}

std::int32_t FactionLedger::standing(std::int32_t index) const noexcept {
    return inRange(index) ? rows_[static_cast<std::size_t>(index)].standing : 0;
}

std::int32_t FactionLedger::influence(std::int32_t index) const noexcept {
    return inRange(index) ? rows_[static_cast<std::size_t>(index)].influence : 0;
}

std::string_view FactionLedger::rankTitle(std::int32_t index) const noexcept {
    const std::int32_t rung = rank(index);
    if (rung <= 0 || registry_ == nullptr) {
        return {};
    }
    const FactionLadder* ladder = registry_->ladder(index);
    if (ladder == nullptr || static_cast<std::size_t>(rung) > ladder->ranks.size()) {
        return {};
    }
    return ladder->ranks[static_cast<std::size_t>(rung - 1)].title;
}

const FactionRank* FactionLedger::nextRung(std::int32_t index) const noexcept {
    if (registry_ == nullptr) {
        return nullptr;
    }
    const FactionLadder* ladder = registry_->ladder(index);
    if (ladder == nullptr) {
        return nullptr;
    }
    const std::size_t next = static_cast<std::size_t>(std::max(0, rank(index)));
    if (next >= ladder->ranks.size()) {
        return nullptr;
    }
    return &ladder->ranks[next];
}

bool FactionLedger::unlocked(std::int32_t index, std::string_view token) const noexcept {
    if (registry_ == nullptr || token.empty()) {
        return false;
    }
    const FactionLadder* ladder = registry_->ladder(index);
    if (ladder == nullptr) {
        return false;
    }
    const std::int32_t held = rank(index);
    // Every rung at or below the one held, so a token granted low down cannot
    // be lost by climbing past a rung that forgot to re-declare it.
    for (std::int32_t rung = 0; rung < held && static_cast<std::size_t>(rung) < ladder->ranks.size();
         ++rung) {
        for (const std::string& unlock : ladder->ranks[static_cast<std::size_t>(rung)].unlocks) {
            if (unlock == token) {
                return true;
            }
        }
    }
    return false;
}

std::int32_t FactionLedger::highestRankedFaction() const noexcept {
    std::int32_t best = -1;
    std::int32_t bestRank = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        // Ties break on the lower index, which is the raws' own order, so two
        // runs cannot disagree about which ladder the HUD names.
        if (rows_[i].rank > bestRank) {
            bestRank = rows_[i].rank;
            best = static_cast<std::int32_t>(i);
        }
    }
    return best;
}

void FactionLedger::seedStanding(std::int32_t index, std::int32_t standing) {
    if (!inRange(index)) {
        return;
    }
    // Direct, clamped, NO mirror -- see the header. A seed is an authored
    // starting fact, not a deed anybody heard about.
    rows_[static_cast<std::size_t>(index)].standing =
        std::clamp(standing, kFactionStandingMin, kFactionStandingMax);
}

void FactionLedger::addStanding(std::int32_t index, std::int32_t delta) {
    if (!inRange(index) || delta == 0) {
        return;
    }
    FactionStanding& row = rows_[static_cast<std::size_t>(index)];
    row.standing = std::clamp(row.standing + delta, kFactionStandingMin, kFactionStandingMax);
    if (registry_ == nullptr) {
        return;
    }
    // The mirror ledger: half the opposite, onto every declared rival. Half
    // rather than all, because pleasing the Watch is not the same act as
    // pleasing the roofs in reverse -- it is only heard that way.
    const std::int32_t mirrored = -(delta / 2);
    if (mirrored == 0) {
        return;
    }
    for (const std::int32_t rival : registry_->rivalsOf(index)) {
        FactionStanding& other = rows_[static_cast<std::size_t>(rival)];
        other.standing =
            std::clamp(other.standing + mirrored, kFactionStandingMin, kFactionStandingMax);
    }
}

void FactionLedger::recordDeed(std::int32_t index, Deed deed) {
    addStanding(index, factionDeedWeight(deed));
}

void FactionLedger::shiftInfluence(std::int32_t index, std::int32_t delta) {
    if (!inRange(index) || delta == 0) {
        return;
    }
    FactionStanding& row = rows_[static_cast<std::size_t>(index)];
    row.influence = std::clamp(row.influence + delta, kInfluenceMin, kInfluenceMax);
    if (registry_ == nullptr) {
        return;
    }
    for (const std::int32_t rival : registry_->rivalsOf(index)) {
        FactionStanding& other = rows_[static_cast<std::size_t>(rival)];
        other.influence = std::clamp(other.influence - delta, kInfluenceMin, kInfluenceMax);
    }
}

LadderResult FactionLedger::checkRung(std::int32_t index, std::int32_t rung,
                                      const SkillTrack& skills) const {
    if (registry_ == nullptr) {
        return LadderResult::NoLadder;
    }
    const FactionLadder* ladder = registry_->ladder(index);
    if (ladder == nullptr || rung < 0 || static_cast<std::size_t>(rung) >= ladder->ranks.size()) {
        return LadderResult::NoLadder;
    }
    const FactionRank& want = ladder->ranks[static_cast<std::size_t>(rung)];
    if (standing(index) < want.standing) {
        return LadderResult::NeedsStanding;
    }
    // A ladder with no skill named is a ladder measured only in standing. A
    // ladder naming a skill the raws do not define reads level 0, which fails
    // every rung above the first -- loudly, rather than silently passing.
    if (!ladder->skill.empty() && skills.level(ladder->skill) < want.skillLevel) {
        return LadderResult::NeedsSkill;
    }
    return LadderResult::Granted;
}

LadderResult FactionLedger::check(std::int32_t index, const SkillTrack& skills) const {
    if (!inRange(index)) {
        return LadderResult::NoLadder;
    }
    if (!isMember(index)) {
        return checkRung(index, 0, skills);
    }
    const std::int32_t next = rank(index);
    if (registry_ != nullptr) {
        const FactionLadder* ladder = registry_->ladder(index);
        if (ladder != nullptr && static_cast<std::size_t>(next) >= ladder->ranks.size()) {
            return LadderResult::AlreadyThere;
        }
    }
    return checkRung(index, next, skills);
}

LadderResult FactionLedger::join(std::int32_t index, const SkillTrack& skills) {
    if (!inRange(index)) {
        return LadderResult::NoLadder;
    }
    if (isMember(index)) {
        return LadderResult::AlreadyThere;
    }
    const LadderResult verdict = checkRung(index, 0, skills);
    if (verdict != LadderResult::Granted) {
        return verdict;
    }
    FactionStanding& row = rows_[static_cast<std::size_t>(index)];
    row.member = true;
    row.rank = 1;
    shiftInfluence(index, kInfluencePerRank);
    return LadderResult::Granted;
}

LadderResult FactionLedger::advance(std::int32_t index, const SkillTrack& skills) {
    if (!inRange(index)) {
        return LadderResult::NoLadder;
    }
    if (!isMember(index)) {
        return LadderResult::NotAMember;
    }
    const std::int32_t next = rank(index);
    if (registry_ != nullptr) {
        const FactionLadder* ladder = registry_->ladder(index);
        if (ladder != nullptr && static_cast<std::size_t>(next) >= ladder->ranks.size()) {
            return LadderResult::AlreadyThere;
        }
    }
    const LadderResult verdict = checkRung(index, next, skills);
    if (verdict != LadderResult::Granted) {
        return verdict;
    }
    rows_[static_cast<std::size_t>(index)].rank = next + 1;
    shiftInfluence(index, kInfluencePerRank);
    return LadderResult::Granted;
}

// ---------------------------------------------------------------------------
// persistence
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> FactionLedger::encode() const {
    std::vector<std::uint8_t> out;
    out.push_back(kLedgerCodecVersion);
    put_i32(out, static_cast<std::int32_t>(rows_.size()));
    for (const FactionStanding& row : rows_) {
        out.push_back(row.member ? 1U : 0U);
        put_i32(out, row.rank);
        put_i32(out, row.standing);
        put_i32(out, row.influence);
    }
    return out;
}

bool FactionLedger::decode(const std::vector<std::uint8_t>& bytes, FactionLedger& out) {
    if (bytes.size() < 5 || bytes[0] != kLedgerCodecVersion) {
        return false;
    }
    std::size_t at = 1;
    const std::int32_t count = take_i32(bytes, at);
    if (count < 0) {
        return false;
    }
    if (bytes.size() != at + static_cast<std::size_t>(count) * 13U) {
        return false;
    }
    out.rows_.assign(static_cast<std::size_t>(count), FactionStanding{});
    for (std::int32_t i = 0; i < count; ++i) {
        FactionStanding& row = out.rows_[static_cast<std::size_t>(i)];
        row.member = bytes[at++] != 0;
        row.rank = take_i32(bytes, at);
        row.standing = take_i32(bytes, at);
        row.influence = take_i32(bytes, at);
    }
    return true;
}

void FactionLedger::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(rows_.size()));
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const FactionStanding& row = rows_[i];
        // The faction's own id goes in beside the numbers. Two worlds whose
        // raws list different factions must not hash the same because their
        // rows happen to line up by position.
        if (registry_ != nullptr) {
            const Faction* faction = registry_->at(static_cast<std::int32_t>(i));
            put_string(sink, faction == nullptr ? std::string_view{} : faction->id);
        } else {
            put_string(sink, std::string_view{});
        }
        sink.put_byte(row.member ? 1U : 0U);
        sink.put_int(static_cast<std::uint32_t>(row.rank));
        sink.put_int(static_cast<std::uint32_t>(row.standing));
        sink.put_int(static_cast<std::uint32_t>(row.influence));
    }
}

// ---------------------------------------------------------------------------
// what influence does to a price
// ---------------------------------------------------------------------------

std::int32_t guildPricePercent(const FactionLedger& ledger, std::int32_t index) noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= ledger.size()) {
        return 0;
    }
    std::int32_t percent = 0;
    if (ledger.unlocked(index, "price")) {
        // The member's rate. Deeper rungs sell cheaper, which is what makes a
        // rung worth climbing rather than worth having.
        percent -= 8 + 4 * ledger.rank(index);
    } else {
        // A guild with the ward in its pocket prices strangers up; a weak one
        // cannot. Fifty is par, so a faction at its authored base influence
        // barely moves a price at all until the player has moved it.
        percent += (ledger.influence(index) - 50) / 5;
    }
    percent -= ledger.standing(index) / 10;
    return std::clamp(percent, -40, 40);
}

}  // namespace granadad::sim
