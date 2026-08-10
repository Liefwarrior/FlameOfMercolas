#include "granadad/sim/radiant_quest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "granadad/sim/barks.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/rng.hpp"

namespace granadad::sim {

namespace {

[[nodiscard]] std::string stringField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return {};
    }
    // Folded the same way every other authored string in this project is, so a
    // hand edit that reintroduces a smart quote cannot put a blank column in
    // the middle of a brief.
    return foldToAscii(found->get<std::string>());
}

[[nodiscard]] std::int32_t intField(const nlohmann::json& node, const char* key,
                                    std::int32_t fallback = 0) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_number_integer()) {
        return fallback;
    }
    return found->get<std::int32_t>();
}

[[nodiscard]] std::string upperAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

/// contrabandLabelFor is HUD vocabulary -- ASCII, upper case, meant for a
/// corner of the screen. Mid-sentence in a brief that is otherwise ordinary
/// prose, "3 SCALPS" reads as a shout for no reason, which is exactly the
/// class of thing the standing quality bar rules out. Lower-cased, "3
/// scalps" sits in a sentence the way any other counted noun does.
[[nodiscard]] std::string lowerAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/// docks::placeLabelAt is ALSO HUD vocabulary -- every entry in kPlaces and
/// every bandFallbackLabel is ASCII SHOUTING CASE, the same shape
/// contrabandLabelFor is and for the same reason (it is meant for a corner of
/// the screen). Substituted verbatim into a brief this is the identical bug
/// the comment above just fixed for {good}, one line later: "working THE
/// GILDED GULL" reads as a shout with no reason to be one. This renders the
/// same label the way it would read in a sentence a person wrote -- title
/// case, with "the" kept as the lower-case article it already is in
/// "the Weighhouse" and every other hand-authored site name in
/// contracts.json's own sites map, rather than shouted like the rest of the
/// word. Never applied to the stored row.giverPlace/targetPlace themselves --
/// those stay the ward's own canonical label, exactly as
/// test_radiant_quest.cpp pins them against docks::placeLabelAt -- only to
/// what goes into the composed sentence, the same split row.good/{good}
/// already draws.
[[nodiscard]] std::string placeProse(std::string_view label) {
    std::string out;
    out.reserve(label.size());
    std::string word;
    auto flushWord = [&out, &word]() {
        if (word.empty()) {
            return;
        }
        for (char& c : word) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (word != "the") {
            word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
        }
        out += word;
        word.clear();
    };
    for (const char c : label) {
        if (c == ' ' || c == '-') {
            flushWord();
            out.push_back(c);
        } else {
            word.push_back(c);
        }
    }
    flushWord();
    return out;
}

/// A raws string ("serf", "watch", ...) resolved to the WardType it names, or
/// false when it names nothing -- including a beast. RADIANT OBJECTIVES ARE
/// PEOPLE WORK: a dog does not commission a fetch and a mouse does not carry
/// word, so wardTypeFromName never resolves to a type isPerson() refuses.
[[nodiscard]] bool wardTypeFromName(std::string_view name, WardType& out) noexcept {
    for (std::size_t i = 0; i < kWardTypeCount; ++i) {
        const auto type = static_cast<WardType>(i);
        if (isPerson(type) && wardTypeName(type) == name) {
            out = type;
            return true;
        }
    }
    return false;
}

/// The authored WardType list at `key`, folded to the types the raws actually
/// name and isPerson() allows. An unrecognised or non-person entry is simply
/// not carried forward -- dropping one bad name should not cost the whole
/// list, but see RadiantRaws::load, which refuses the WHOLE template when the
/// filtered list comes back empty.
[[nodiscard]] std::vector<WardType> wardTypeArray(const nlohmann::json& node, const char* key) {
    std::vector<WardType> out;
    const auto found = node.find(key);
    if (found == node.end() || !found->is_array()) {
        return out;
    }
    for (const nlohmann::json& row : *found) {
        if (!row.is_string()) {
            continue;
        }
        WardType type = WardType::Serf;
        if (wardTypeFromName(foldToAscii(row.get<std::string>()), type)) {
            out.push_back(type);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

/// The authored Contraband list at `key`, folded the same way -- an
/// unrecognised symbol is dropped rather than refusing the whole template, so
/// a typo in a five-entry list does not cost the other four.
[[nodiscard]] std::vector<Contraband> contrabandArray(const nlohmann::json& node,
                                                       const char* key) {
    std::vector<Contraband> out;
    const auto found = node.find(key);
    if (found == node.end() || !found->is_array()) {
        return out;
    }
    for (const nlohmann::json& row : *found) {
        if (!row.is_string()) {
            continue;
        }
        Contraband good = Contraband::Scalp;
        if (contrabandFromSymbol(foldToAscii(row.get<std::string>()), good)) {
            out.push_back(good);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

/// Replaces every occurrence of `token` with `value`. Small and linear, the
/// same helper contract.cpp carries under its own name -- a brief is one or
/// two sentences and there are six tokens at most.
void substitute(std::string& text, std::string_view token, std::string_view value) {
    std::size_t at = text.find(token);
    while (at != std::string::npos) {
        text.replace(at, token.size(), value);
        at = text.find(token, at + value.size());
    }
}

/// The salt every board draw hangs off. Named, never positional -- renaming
/// this re-rolls every board there has ever been. See rng.hpp's own note on
/// stream_salt.
const std::uint64_t kBoardSalt = stream_salt("radiant.quest.board");

void put_string(HashSink& sink, std::string_view text) {
    sink.put_int(static_cast<std::uint32_t>(text.size()));
    for (const char c : text) {
        sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(c)));
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// the vocabulary
// ---------------------------------------------------------------------------

std::string_view radiantKindName(RadiantKind kind) noexcept {
    switch (kind) {
        case RadiantKind::Fetch:
            return "fetch";
        case RadiantKind::Deliver:
            return "deliver";
    }
    return "?";
}

bool radiantKindFromSymbol(std::string_view symbol, RadiantKind& out) noexcept {
    if (symbol == "fetch") {
        out = RadiantKind::Fetch;
        return true;
    }
    if (symbol == "deliver") {
        out = RadiantKind::Deliver;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

std::filesystem::path radiantRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "quests" / "radiant_quests.json";
}

std::filesystem::path radiantRawsDir(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "quests";
}

std::vector<std::filesystem::path> radiantRawsFiles(const std::filesystem::path& contentDir) {
    std::vector<std::filesystem::path> files;
    const std::filesystem::path owner = radiantRawsPath(contentDir);
    std::error_code error;
    if (std::filesystem::is_regular_file(owner, error)) {
        // THE OWNER'S FILE IS ALWAYS FIRST -- see barkRawsFiles(), the same
        // rule borrowed for the same reason: on a duplicate template id the
        // first file loaded wins, so nothing added later can shadow one of
        // radiant_quests.json's own six.
        files.push_back(owner);
    }
    std::vector<std::filesystem::path> extras;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(radiantRawsDir(contentDir), error)) {
        if (!entry.is_regular_file(error) || entry.path().extension() != ".json") {
            continue;
        }
        if (entry.path().filename() == owner.filename()) {
            continue;
        }
        // The quests directory is shared with quests.json, casebook.json,
        // flame_disciple.json, skyrunner_tenant.json and
        // bloodletter_letters.json -- every one of them a different schema
        // with no top-level "templates" array. They are still handed to
        // load() below; a document with no "templates" array simply
        // contributes nothing, the same way a bark table file with no
        // "tables" array would not.
        extras.push_back(entry.path());
    }
    // Sorted before use: directory iteration order is a property of the
    // filesystem, and a template set that depended on it would differ between
    // two machines carrying identical content.
    std::sort(extras.begin(), extras.end());
    files.insert(files.end(), extras.begin(), extras.end());
    return files;
}

RadiantRaws RadiantRaws::load(const std::filesystem::path& contentDir) {
    RadiantRaws out;
    for (const std::filesystem::path& path : radiantRawsFiles(contentDir)) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            continue;
        }
        std::ostringstream text;
        text << file.rdbuf();
        const nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
        if (document.is_discarded() || !document.is_object()) {
            continue;
        }

        const auto templates = document.find("templates");
        if (templates == document.end() || !templates->is_array()) {
            continue;
        }
        for (const nlohmann::json& node : *templates) {
            if (!node.is_object()) {
                continue;
            }
            RadiantTemplate row;
            row.id = stringField(node, "id");
            if (row.id.empty()) {
                continue;
            }
            // AN ID SEEN BEFORE IS DROPPED. radiant_quests.json is always the
            // first file this loop reads, so this is what makes "the owner's
            // file always wins" true rather than aspirational.
            bool duplicate = false;
            for (const RadiantTemplate& already : out.templates_) {
                if (already.id == row.id) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            if (!radiantKindFromSymbol(stringField(node, "kind"), row.kind)) {
                // A template naming a kind this build cannot evaluate is a
                // template authoring an objective that cannot be finished --
                // questline.hpp's own rule for its stage vocabulary, applied
                // here.
                continue;
            }
            row.verb = upperAscii(stringField(node, "verb"));
            row.brief = stringField(node, "brief");
            row.giverTypes = wardTypeArray(node, "giverTypes");
            row.targetTypes = wardTypeArray(node, "targetTypes");
            row.unitsMin = std::max(1, intField(node, "unitsMin", 1));
            row.unitsMax = std::max(row.unitsMin, intField(node, "unitsMax", row.unitsMin));
            row.payPerUnit = std::max(0, intField(node, "payPerUnit"));
            row.payFlat = std::max(0, intField(node, "payFlat"));
            if (row.kind == RadiantKind::Fetch) {
                row.goods = contrabandArray(node, "goods");
            }
            // REFUSED BY NAME. A template with nobody to want it, nobody to
            // ask it of, no verb, no brief, or -- for a fetch -- nothing to
            // fetch, is not an objective. Dropping it here is what makes
            // every generated row name somebody and something that actually
            // exists.
            if (row.giverTypes.empty() || row.targetTypes.empty() || row.verb.empty() ||
                row.brief.empty()) {
                continue;
            }
            if (row.kind == RadiantKind::Fetch && row.goods.empty()) {
                continue;
            }
            // BUG (fixed): a template with no pay authored used to load
            // anyway -- payPerUnit/payFlat default to 0, and refresh()'s own
            // std::max(1, ...) floor then silently paid a flat single coin
            // for it forever, regardless of units or effort. That is not a
            // job anybody would post; it is the same "refused by name" case
            // as an empty goods list, just on the pay axis instead of the
            // goods axis. Every one of this build's real templates already
            // authors a positive pay -- pinned by "a radiant template
            // naming a kind..." in test_radiant_quest.cpp -- so this closes
            // a gap a future template could fall into silently rather than
            // one any of today's seventeen were relying on.
            if (row.kind == RadiantKind::Fetch && row.payPerUnit <= 0) {
                continue;
            }
            if (row.kind == RadiantKind::Deliver && row.payFlat <= 0) {
                continue;
            }
            out.templates_.push_back(std::move(row));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// the board
// ---------------------------------------------------------------------------

const RadiantObjective* RadiantBoard::find(std::int32_t id) const noexcept {
    for (const RadiantObjective& row : rows_) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

namespace {

/// True when `type` is one of `allowed`. `allowed` is short (one to a
/// handful of entries, authored by hand) so a linear scan is the right tool
/// and not a set.
[[nodiscard]] bool typeAllowed(const std::vector<WardType>& allowed, WardType type) noexcept {
    for (const WardType candidate : allowed) {
        if (candidate == type) {
            return true;
        }
    }
    return false;
}

/// Every living, visible person in `ward` whose type is in `allowed` and
/// whose actor id is not `exclude` -- ascending by actor id, because
/// actors() already is. What a template's giverTypes/targetTypes list
/// resolves to AT THE MOMENT the board asks, which is the whole reason this
/// is built fresh per refresh() rather than cached at load: the ward's own
/// roster does not change shape but who is alive and on their feet can.
[[nodiscard]] std::vector<std::int32_t> eligiblePersons(const WardPopulation& ward,
                                                        const std::vector<WardType>& allowed,
                                                        std::int32_t exclude) {
    std::vector<std::int32_t> pool;
    const std::vector<WardActor>& actors = ward.actors();
    for (const WardActor& actor : actors) {
        if (actor.id == exclude || actor.dead || !actor.visible()) {
            continue;
        }
        if (!isPerson(actor.type) || !typeAllowed(allowed, actor.type)) {
            continue;
        }
        pool.push_back(actor.id);
    }
    return pool;
}

}  // namespace

void RadiantBoard::refresh(std::int32_t day, std::uint64_t worldSeed, const RadiantRaws& raws,
                           const WardPopulation& ward) {
    if (day == day_) {
        return;
    }
    day_ = day;
    rows_.clear();
    if (!raws.loaded()) {
        return;
    }

    const CounterRandomSource source(worldSeed, kBoardSalt);
    CounterRandomSource today = source;
    today.begin_tick(static_cast<std::uint64_t>(day < 0 ? 0 : day));

    const std::vector<RadiantTemplate>& templates = raws.templates();
    for (std::int32_t slot = 0; slot < kRadiantObjectivesPerDay; ++slot) {
        const auto key = static_cast<std::uint64_t>(slot);

        const RadiantTemplate& tmpl =
            templates[static_cast<std::size_t>(today.draw(key, 0) % templates.size())];

        const std::vector<std::int32_t> giverPool =
            eligiblePersons(ward, tmpl.giverTypes, /*exclude=*/-1);
        if (giverPool.empty()) {
            // Nobody of the type this template wants is alive and on their
            // feet tonight. The honest answer is one fewer objective on the
            // board, not a job that names somebody who is not there.
            continue;
        }
        const std::int32_t giverId =
            giverPool[static_cast<std::size_t>(today.draw(key, 1) % giverPool.size())];

        const std::vector<std::int32_t> targetPool =
            eligiblePersons(ward, tmpl.targetTypes, /*exclude=*/giverId);
        if (targetPool.empty()) {
            continue;
        }
        const std::int32_t targetId =
            targetPool[static_cast<std::size_t>(today.draw(key, 2) % targetPool.size())];

        const WardActor* giver = ward.byId(giverId);
        const WardActor* target = ward.byId(targetId);
        if (giver == nullptr || target == nullptr) {
            // Cannot happen given eligiblePersons only ever returns ids
            // ward.actors() itself just enumerated -- kept as a refusal
            // rather than an assert because a raws-driven generator must
            // never crash the session it is offering work to.
            continue;
        }
        const WardIdentity& giverWho = ward.identity(giverId);
        const WardIdentity& targetWho = ward.identity(targetId);

        Contraband good = Contraband::Scalp;
        std::int32_t units = 0;
        if (tmpl.kind == RadiantKind::Fetch) {
            good = tmpl.goods[static_cast<std::size_t>(today.draw(key, 3) % tmpl.goods.size())];
            const std::int32_t span = tmpl.unitsMax - tmpl.unitsMin + 1;
            units = tmpl.unitsMin +
                   static_cast<std::int32_t>(today.draw(key, 4) % static_cast<std::uint64_t>(span));
        }

        RadiantObjective row;
        row.id = day * kRadiantObjectivesPerDay + slot;
        row.templateId = tmpl.id;
        row.kind = tmpl.kind;
        row.verb = tmpl.verb;
        row.giverActorId = giverId;
        row.giverName = giverWho.name;
        row.giverPlace =
            std::string(docks::placeLabelAt(giver->x, giver->y, giver->band));
        row.targetActorId = targetId;
        row.targetName = targetWho.name;
        row.targetPlace =
            std::string(docks::placeLabelAt(target->x, target->y, target->band));
        row.good = good;
        row.units = units;
        row.postedOnDay = day;
        row.pay = tmpl.kind == RadiantKind::Fetch
                      ? std::max(1, units * tmpl.payPerUnit)
                      : std::max(1, tmpl.payFlat);

        // EVERY PROPER NOUN IN THE OUTPUT IS SOMEBODY OR SOMEWHERE REAL. The
        // template says "{giver}, {giverPlace}, wants {units} {good}"; what
        // goes in is a name this board just drew off the live roster and a
        // place that body is actually standing in right now.
        std::string brief = tmpl.brief;
        substitute(brief, "{giver}", row.giverName);
        substitute(brief, "{giverPlace}", placeProse(row.giverPlace));
        substitute(brief, "{target}", row.targetName);
        substitute(brief, "{targetPlace}", placeProse(row.targetPlace));
        substitute(brief, "{units}", std::to_string(units));
        substitute(brief, "{good}",
                  tmpl.kind == RadiantKind::Fetch ? lowerAscii(contrabandLabelFor(good, units))
                                                  : std::string{});
        row.brief = std::move(brief);

        rows_.push_back(std::move(row));
    }
    std::sort(rows_.begin(), rows_.end(),
             [](const RadiantObjective& a, const RadiantObjective& b) { return a.id < b.id; });
}

void RadiantBoard::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(day_));
    sink.put_int(static_cast<std::uint32_t>(rows_.size()));
    for (const RadiantObjective& row : rows_) {
        sink.put_int(static_cast<std::uint32_t>(row.id));
        put_string(sink, row.templateId);
        sink.put_byte(static_cast<std::uint32_t>(row.kind));
        sink.put_int(static_cast<std::uint32_t>(row.giverActorId));
        sink.put_int(static_cast<std::uint32_t>(row.targetActorId));
        sink.put_byte(static_cast<std::uint32_t>(row.good));
        sink.put_int(static_cast<std::uint32_t>(row.units));
        sink.put_int(static_cast<std::uint32_t>(row.pay));
    }
}

}  // namespace granadad::sim
