#include "granadad/sim/nemesis.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace granadad::sim {

namespace {

/// The byte-string version. decode() refuses anything else rather than
/// reinterpreting it -- the same rule the social ledger and the faction ledger
/// follow.
constexpr std::uint8_t kBookVersion = 1;

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

void putString(std::vector<std::uint8_t>& out, const std::string& text) {
    const std::uint32_t size = static_cast<std::uint32_t>(std::min<std::size_t>(text.size(), 255));
    out.push_back(static_cast<std::uint8_t>(size));
    for (std::uint32_t i = 0; i < size; ++i) {
        out.push_back(static_cast<std::uint8_t>(text[i]));
    }
}

[[nodiscard]] bool takeString(const std::vector<std::uint8_t>& bytes, std::size_t& at,
                              std::string& out) {
    if (at >= bytes.size()) {
        return false;
    }
    const std::size_t size = bytes[at++];
    if (at + size > bytes.size()) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data() + at), size);
    at += size;
    return true;
}

void putInt(std::vector<std::uint8_t>& out, std::int32_t value) {
    const std::uint32_t raw = static_cast<std::uint32_t>(value);
    out.push_back(static_cast<std::uint8_t>(raw & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((raw >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((raw >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((raw >> 24) & 0xFFU));
}

[[nodiscard]] bool takeInt(const std::vector<std::uint8_t>& bytes, std::size_t& at,
                           std::int32_t& out) {
    if (at + 4 > bytes.size()) {
        return false;
    }
    const std::uint32_t raw = static_cast<std::uint32_t>(bytes[at]) |
                              (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
                              (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
                              (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    at += 4;
    out = static_cast<std::int32_t>(raw);
    return true;
}

void hashString(HashSink& sink, const std::string& text) {
    sink.put_int(static_cast<std::uint32_t>(text.size()));
    for (const char character : text) {
        sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(character)));
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

std::filesystem::path chapterRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "factions" / "chapters.json";
}

ChapterRaws ChapterRaws::load(const std::filesystem::path& contentDir,
                              const FactionRegistry& factions) {
    ChapterRaws out;
    std::ifstream file(chapterRawsPath(contentDir), std::ios::binary);
    if (!file) {
        return out;
    }
    std::ostringstream text;
    text << file.rdbuf();
    const nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return out;
    }
    const auto rows = document.find("chapters");
    if (rows == document.end() || !rows->is_array()) {
        return out;
    }
    for (const nlohmann::json& node : *rows) {
        if (!node.is_object()) {
            continue;
        }
        ChapterRaw row;
        row.id = stringField(node, "id");
        row.displayName = stringField(node, "displayName");
        row.faction = stringField(node, "faction");
        row.trade = stringField(node, "trade");
        row.site = stringField(node, "site");
        row.seat = stringField(node, "seat");
        row.influence = std::max(0, intField(node, "influence"));
        row.toll = std::max(0, intField(node, "toll"));
        row.founderTitle = stringField(node, "founderTitle");
        if (row.id.empty() || row.displayName.empty()) {
            continue;
        }
        // REFUSED BY NAME. A chapter naming a faction the owner's own
        // factions.json does not have is not a house inside anything, and this
        // file must never be able to grow a sixth faction by typo. Same gate
        // the compound roll and the contract board pass through.
        if (factions.indexOf(row.faction) < 0) {
            ++out.refused_;
            continue;
        }
        out.chapters_.push_back(std::move(row));
    }
    // Ascending by id, so the index a nemesis stores is the file's own order on
    // every machine and not the order a JSON parser felt like.
    std::sort(out.chapters_.begin(), out.chapters_.end(),
              [](const ChapterRaw& left, const ChapterRaw& right) { return left.id < right.id; });
    return out;
}

const ChapterRaw* ChapterRaws::at(std::int32_t index) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= chapters_.size()) {
        return nullptr;
    }
    return &chapters_[static_cast<std::size_t>(index)];
}

std::int32_t ChapterRaws::forTrade(std::string_view trade,
                                   std::string_view factionId) const noexcept {
    // TRADE AND FACTION, then trade, then faction. kit_keeping is a cooper's
    // skill AND a watchman's, so the pair has to be tried first or a risen
    // watchman would found a cooperage.
    for (std::size_t i = 0; i < chapters_.size(); ++i) {
        if (chapters_[i].trade == trade && chapters_[i].faction == factionId) {
            return static_cast<std::int32_t>(i);
        }
    }
    // THEN TRADE, BUT NEVER ACROSS A FACTION. S9 closes the S8 review's third
    // finding: this pass used to match on trade alone, so Sella Brinewall -- a
    // DOCKHAND whose trade is streetwise -- founded The Chandlers' Row, which
    // chapters.json says belongs to the MERCHANTS, and then recordDefeat booked
    // her influence and her toll against the dockhands. A hand founded a
    // merchants' house and taxed the quay gang for it.
    //
    // A chapter with no faction of its own is still fair game on trade; one
    // that names a faction is that faction's house and nobody else's. What is
    // left over falls to the faction-only pass below, which is the right answer
    // anyway: you found a house of YOUR OWN guild.
    for (std::size_t i = 0; i < chapters_.size(); ++i) {
        if (!trade.empty() && chapters_[i].trade == trade &&
            (chapters_[i].faction.empty() || factionId.empty() ||
             chapters_[i].faction == factionId)) {
            return static_cast<std::int32_t>(i);
        }
    }
    for (std::size_t i = 0; i < chapters_.size(); ++i) {
        if (!factionId.empty() && chapters_[i].faction == factionId) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// what he is carrying
// ---------------------------------------------------------------------------

Weapon nemesisWeapon(std::int32_t wins) noexcept {
    if (wins >= 3) {
        return Weapon::Edged;
    }
    if (wins >= 2) {
        return Weapon::Blunt;
    }
    return Weapon::Fists;
}

Intent nemesisIntent(std::int32_t wins) noexcept {
    if (wins >= 3) {
        return Intent::Kill;
    }
    if (wins >= 1) {
        return Intent::Harm;
    }
    return Intent::Subdue;
}

// ---------------------------------------------------------------------------
// the book
// ---------------------------------------------------------------------------

NemesisBook NemesisBook::load(const std::filesystem::path& contentDir,
                              std::shared_ptr<const FactionRegistry> factions) {
    NemesisBook out;
    out.attach(std::move(factions));
    if (out.factions_ != nullptr) {
        out.chapters_ = ChapterRaws::load(contentDir, *out.factions_);
    }
    return out;
}

void NemesisBook::attach(std::shared_ptr<const FactionRegistry> factions) {
    factions_ = factions != nullptr ? std::move(factions)
                                    : std::make_shared<const FactionRegistry>();
}

const Nemesis* NemesisBook::of(std::int32_t actorId) const noexcept {
    // S9 CLOSES THE S8 REVIEW'S SECOND FINDING. This used to `break` on the
    // first rival whose id sorted past the one asked for, which is only correct
    // while rivals_ is sorted BY ID -- and entryFor() reassigns actorId in
    // place, without re-sorting, on exactly the path nemesis.hpp advertises as
    // "the persistent-ward door, left open on purpose": a save reloaded against
    // a bigger cast hands the same man a different id. One rival past the
    // reassigned one and of() returned nullptr for a man the book has.
    //
    // The fix is to stop pretending the list is ordered. There are a handful of
    // rivals in a ward and a linear scan over them is free; a sort that has to
    // be remembered on every write is not.
    for (const Nemesis& rival : rivals_) {
        if (rival.actorId == actorId) {
            return &rival;
        }
    }
    return nullptr;
}

const Nemesis* NemesisBook::byName(std::string_view who) const noexcept {
    for (const Nemesis& rival : rivals_) {
        if (rival.who == who) {
            return &rival;
        }
    }
    return nullptr;
}

const Nemesis* NemesisBook::worst() const noexcept {
    const Nemesis* worst = nullptr;
    for (const Nemesis& rival : rivals_) {
        // Wins first, then the grudge, then the id -- a total order, so two
        // runs never disagree about who the ward's headline enemy is.
        if (worst == nullptr || rival.wins > worst->wins ||
            (rival.wins == worst->wins && rival.grudge > worst->grudge)) {
            worst = &rival;
        }
    }
    return worst;
}

Nemesis& NemesisBook::entryFor(const Defeat& defeat) {
    // THE NAME IS THE KEY AND THE ID IS THE ADDRESS. A room rebuilt from the
    // roster hands out the same ids in the same order, but a persistent-ward
    // save reloaded against a bigger cast would not -- so a name that is
    // already in the book keeps its record and takes the new id.
    for (Nemesis& rival : rivals_) {
        if (rival.who == defeat.who) {
            rival.actorId = defeat.actorId;
            return rival;
        }
    }
    Nemesis fresh;
    fresh.actorId = defeat.actorId;
    fresh.who = defeat.who;
    fresh.epithet = defeat.epithet;
    fresh.faction = factions_->factionForJobPrefix(defeat.jobPrefix);
    fresh.firstWinDay = defeat.day;
    const auto at = std::lower_bound(
        rivals_.begin(), rivals_.end(), fresh.actorId,
        [](const Nemesis& row, std::int32_t id) { return row.actorId < id; });
    return *rivals_.insert(at, std::move(fresh));
}

std::string NemesisBook::titleFor(std::int32_t faction, std::int32_t rank) const {
    const FactionLadder* ladder = factions_->ladder(faction);
    if (ladder == nullptr || rank <= 0 ||
        static_cast<std::size_t>(rank) > ladder->ranks.size()) {
        return {};
    }
    return ladder->ranks[static_cast<std::size_t>(rank - 1)].title;
}

Rise NemesisBook::recordDefeat(const Defeat& defeat, const RiseWorld& world) {
    Rise out;
    if (defeat.who.empty()) {
        return out;
    }
    ++defeats_;
    Nemesis& entry = entryFor(defeat);
    ++entry.wins;
    entry.lastWinDay = defeat.day;
    entry.grudge = std::min(kGrudgeMax, entry.grudge + kGrudgePerWin);

    out.happened = true;
    out.actorId = entry.actorId;
    out.who = entry.who;
    out.wins = entry.wins;

    // --- the rung ---------------------------------------------------------
    //
    // ONE RUNG PER WIN, on the ladder of the faction that claims HIS OWN JOB,
    // capped by that ladder's own length. The title comes out of ranks.json by
    // index; nothing here writes a rank name.
    if (entry.faction >= 0) {
        const FactionLadder* ladder = factions_->ladder(entry.faction);
        const std::int32_t top =
            ladder == nullptr ? 0 : static_cast<std::int32_t>(ladder->ranks.size());
        if (entry.rank < top) {
            ++entry.rank;
            out.promoted = true;
        }
        const std::string title = titleFor(entry.faction, entry.rank);
        if (!title.empty()) {
            entry.title = title;
        }
        if (world.guilds != nullptr) {
            // THE MIRROR, UNCHANGED. shiftInfluence already takes the same off
            // every declared rival, so a Skyrunner rising is a garrison losing
            // weight -- which is what moves a bouncer's patience and the price
            // of a mug without one line of new machinery.
            world.guilds->shiftInfluence(entry.faction, kInfluencePerWin);
        }
    }

    // --- the house --------------------------------------------------------
    if (entry.chapter < 0 && entry.wins >= kFoundsAtWins) {
        const std::string factionId =
            entry.faction >= 0 && factions_->at(entry.faction) != nullptr
                ? factions_->at(entry.faction)->id
                : std::string();
        const std::int32_t chapter = chapters_.forTrade(defeat.trade, factionId);
        const ChapterRaw* raw = chapters_.at(chapter);
        if (raw != nullptr) {
            entry.chapter = chapter;
            entry.title = raw->founderTitle;
            out.founded = true;
            out.chapterName = raw->displayName;
            // REAL MEMBERS, ENLISTED OUT OF THE ROOM. Everybody present who
            // belongs to the parent faction and is not the founder joins it.
            // A guild with no members is a letterhead, and the S4 review's
            // complaint about a number with no consumer is the same complaint.
            for (std::size_t i = 0;
                 i < world.presentIds.size() && i < world.presentFactions.size(); ++i) {
                const std::int32_t id = world.presentIds[i];
                if (id == entry.actorId || world.presentFactions[i] != entry.faction) {
                    continue;
                }
                if (std::find(entry.members.begin(), entry.members.end(), id) ==
                    entry.members.end()) {
                    entry.members.push_back(id);
                }
            }
            std::sort(entry.members.begin(), entry.members.end());
            if (world.guilds != nullptr && entry.faction >= 0) {
                world.guilds->shiftInfluence(entry.faction, raw->influence);
            }
        }
    }
    out.members = static_cast<std::int32_t>(entry.members.size());

    // --- the roll ---------------------------------------------------------
    //
    // Section 2.8: a vacant charge is a prize and "any actor -- including the
    // player -- may petition for it". This is an actor doing it, on the same
    // roll, through a call that does the same work Ward::petitionForCharge
    // does for the player. A thing is true in Granadad when it is on the roll.
    if (entry.plot < 0 && entry.wins >= kTakesChargeAtWins && world.roll != nullptr) {
        const ChapterRaw* raw = chapters_.at(entry.chapter);
        if (raw != nullptr && !raw->seat.empty()) {
            const std::int32_t plot = world.roll->plotNamed(raw->seat);
            if (plot >= 0 && world.roll->grantCharge(plot, entry.who) == TenureResult::Done) {
                entry.plot = plot;
                out.tookCharge = true;
                out.plotName =
                    world.roll->raws()
                        .plots()[static_cast<std::size_t>(
                            world.roll->plots()[static_cast<std::size_t>(plot)].raw)]
                        .name;
            }
        }
    }

    // --- his memory -------------------------------------------------------
    if (world.ledger != nullptr) {
        // Straight onto the hostile band, deeper with every win. seed() and not
        // record(): a Deed is something the PLAYER did to somebody, and being
        // beaten by them is not one. What this is doing is setting where a man
        // stands after he has had you on the floor.
        world.ledger->seed(entry.actorId, kDispositionPerWin * entry.wins);
    }

    // --- the purse --------------------------------------------------------
    out.coinTaken = std::max(0, defeat.playerCoin) * kPurseTakenPercent / 100;

    // --- what to say ------------------------------------------------------
    //
    // A KEY, NOT A LINE. Every spoken word in this game comes out of the
    // owner's bark tables; this picks which authored table applies and the
    // dialogue layer resolves it.
    if (out.tookCharge) {
        out.barkKey = "nemesis.charge";
    } else if (out.founded) {
        out.barkKey = "nemesis.founded";
    } else if (entry.wins > 1) {
        out.barkKey = "nemesis.again";
    } else {
        out.barkKey = "nemesis.first";
    }
    out.rank = entry.rank;
    out.title = entry.title;

    std::string line = entry.who;
    if (!entry.title.empty()) {
        line += " IS NOW " + entry.title;
    } else {
        line += " STANDS OVER YOU";
    }
    if (out.founded) {
        line += " OF " + out.chapterName;
    }
    if (out.tookCharge) {
        line += " AND HOLDS " + out.plotName;
    }
    out.line = line;
    return out;
}

void NemesisBook::recordVictory(std::int32_t actorId) {
    for (Nemesis& rival : rivals_) {
        if (rival.actorId != actorId) {
            continue;
        }
        ++rival.losses;
        // THE GRUDGE COMES DOWN AND NOTHING ELSE DOES. He stops hunting you.
        // He does not stop being Craftlord of the cooperage, the toll does not
        // come off the price of a drink, and the roll does not forget who holds
        // the Gullet. Permanence is the point.
        rival.grudge = std::max(0, rival.grudge - kGrudgePerLoss);
        return;
    }
}

std::int32_t NemesisBook::tollPercent(std::int32_t factionIndex) const noexcept {
    if (factionIndex < 0) {
        return 0;
    }
    std::int32_t total = 0;
    for (const Nemesis& rival : rivals_) {
        if (rival.faction != factionIndex) {
            continue;
        }
        const ChapterRaw* raw = chapters_.at(rival.chapter);
        if (raw != nullptr) {
            total += raw->toll;
        }
    }
    return std::min(kTollCap, total);
}

bool NemesisBook::enlisted(std::int32_t actorId) const noexcept {
    for (const Nemesis& rival : rivals_) {
        if (rival.chapter < 0) {
            continue;
        }
        if (std::find(rival.members.begin(), rival.members.end(), actorId) !=
            rival.members.end()) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// persistence
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> NemesisBook::encode() const {
    std::vector<std::uint8_t> out;
    out.push_back(kBookVersion);
    putInt(out, defeats_);
    putInt(out, static_cast<std::int32_t>(rivals_.size()));
    for (const Nemesis& rival : rivals_) {
        putInt(out, rival.actorId);
        putString(out, rival.who);
        putString(out, rival.epithet);
        putInt(out, rival.faction);
        putInt(out, rival.rank);
        putString(out, rival.title);
        putInt(out, rival.chapter);
        putInt(out, rival.plot);
        putInt(out, rival.wins);
        putInt(out, rival.losses);
        putInt(out, rival.grudge);
        putInt(out, rival.firstWinDay);
        putInt(out, rival.lastWinDay);
        putInt(out, static_cast<std::int32_t>(rival.members.size()));
        for (const std::int32_t member : rival.members) {
            putInt(out, member);
        }
    }
    return out;
}

bool NemesisBook::decode(const std::vector<std::uint8_t>& bytes, NemesisBook& out) {
    if (bytes.empty() || bytes[0] != kBookVersion) {
        return false;
    }
    std::size_t at = 1;
    std::int32_t defeats = 0;
    std::int32_t count = 0;
    if (!takeInt(bytes, at, defeats) || !takeInt(bytes, at, count) || count < 0) {
        return false;
    }
    std::vector<Nemesis> rivals;
    rivals.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        Nemesis row;
        std::int32_t members = 0;
        if (!takeInt(bytes, at, row.actorId) || !takeString(bytes, at, row.who) ||
            !takeString(bytes, at, row.epithet) || !takeInt(bytes, at, row.faction) ||
            !takeInt(bytes, at, row.rank) || !takeString(bytes, at, row.title) ||
            !takeInt(bytes, at, row.chapter) || !takeInt(bytes, at, row.plot) ||
            !takeInt(bytes, at, row.wins) || !takeInt(bytes, at, row.losses) ||
            !takeInt(bytes, at, row.grudge) || !takeInt(bytes, at, row.firstWinDay) ||
            !takeInt(bytes, at, row.lastWinDay) || !takeInt(bytes, at, members) || members < 0) {
            return false;
        }
        for (std::int32_t m = 0; m < members; ++m) {
            std::int32_t id = 0;
            if (!takeInt(bytes, at, id)) {
                return false;
            }
            row.members.push_back(id);
        }
        rivals.push_back(std::move(row));
    }
    if (at != bytes.size()) {
        return false;
    }
    out.defeats_ = defeats;
    out.rivals_ = std::move(rivals);
    return true;
}

void NemesisBook::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(defeats_));
    sink.put_int(static_cast<std::uint32_t>(rivals_.size()));
    for (const Nemesis& rival : rivals_) {
        sink.put_int(static_cast<std::uint32_t>(rival.actorId));
        hashString(sink, rival.who);
        sink.put_int(static_cast<std::uint32_t>(rival.faction));
        sink.put_int(static_cast<std::uint32_t>(rival.rank));
        hashString(sink, rival.title);
        sink.put_int(static_cast<std::uint32_t>(rival.chapter));
        sink.put_int(static_cast<std::uint32_t>(rival.plot));
        sink.put_int(static_cast<std::uint32_t>(rival.wins));
        sink.put_int(static_cast<std::uint32_t>(rival.losses));
        sink.put_int(static_cast<std::uint32_t>(rival.grudge));
        sink.put_int(static_cast<std::uint32_t>(rival.firstWinDay));
        sink.put_int(static_cast<std::uint32_t>(rival.lastWinDay));
        sink.put_int(static_cast<std::uint32_t>(rival.members.size()));
        for (const std::int32_t member : rival.members) {
            sink.put_int(static_cast<std::uint32_t>(member));
        }
    }
}

}  // namespace granadad::sim
