#include "granadad/sim/questline.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "granadad/sim/barks.hpp"

namespace granadad::sim {

namespace {

[[nodiscard]] std::string stringField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return {};
    }
    // Folded the same way every other authored string in this project is, so a
    // hand edit that reintroduces a smart quote cannot put a blank column in
    // the middle of an objective.
    return foldToAscii(found->get<std::string>());
}

[[nodiscard]] std::int32_t intField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_number_integer()) {
        return 0;
    }
    return found->get<std::int32_t>();
}

[[nodiscard]] bool boolField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    return found != node.end() && found->is_boolean() && found->get<bool>();
}

void put_string(HashSink& sink, std::string_view text) {
    sink.put_int(static_cast<std::uint32_t>(text.size()));
    for (const char c : text) {
        sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(c)));
    }
}

}  // namespace

StageKind stageKindOf(std::string_view raw) noexcept {
    if (raw == "oath") {
        return StageKind::Oath;
    }
    if (raw == "alms") {
        return StageKind::Alms;
    }
    if (raw == "talk") {
        return StageKind::Talk;
    }
    if (raw == "teach") {
        return StageKind::Teach;
    }
    if (raw == "forge") {
        return StageKind::Forge;
    }
    return StageKind::Unknown;
}

std::string_view stageKindName(StageKind kind) noexcept {
    switch (kind) {
        case StageKind::Oath:
            return "oath";
        case StageKind::Alms:
            return "alms";
        case StageKind::Talk:
            return "talk";
        case StageKind::Teach:
            return "teach";
        case StageKind::Forge:
            return "forge";
        case StageKind::Unknown:
            break;
    }
    return "?";
}

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

std::filesystem::path questRawsDir(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "quests";
}

QuestBook QuestBook::load(const std::filesystem::path& contentDir) {
    QuestBook out;
    const std::filesystem::path dir = questRawsDir(contentDir);
    std::error_code error;
    if (!std::filesystem::is_directory(dir, error)) {
        return out;
    }

    // Collected and SORTED before anything is parsed. Directory iteration order
    // is a property of the filesystem, and a book whose contents depended on it
    // would hash differently on two machines with the same content.
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(dir, error)) {
        if (entry.is_regular_file(error) && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& path : files) {
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
        // TOLD APART BY SHAPE. The owner's quests.json carries a `quests` array
        // of lines whose advance conditions this build cannot evaluate; it has
        // no top-level `stages` and is skipped here for that reason and no
        // other. See the header.
        const auto stages = document.find("stages");
        if (stages == document.end() || !stages->is_array()) {
            continue;
        }
        Questline line;
        line.id = stringField(document, "id");
        if (line.id.empty()) {
            continue;
        }
        line.title = stringField(document, "title");
        line.faction = stringField(document, "faction");
        line.giver = stringField(document, "giver");

        for (const nlohmann::json& node : *stages) {
            if (!node.is_object()) {
                continue;
            }
            QuestStage stage;
            stage.key = stringField(node, "key");
            stage.kind = stageKindOf(stringField(node, "kind"));
            if (stage.key.empty() || stage.kind == StageKind::Unknown) {
                // A stage this build could not resolve would be a wall the
                // player walks into with no way past. Dropped, loudly enough
                // that the line's stage count is visibly short in a test.
                continue;
            }
            stage.party = stringField(node, "party");
            stage.label = stringField(node, "label");
            stage.objective = stringField(node, "objective");
            stage.log = stringField(node, "log");
            stage.barkKey = stringField(node, "barkKey");
            stage.count = std::max(0, intField(node, "count"));
            stage.grantsRank = std::max(0, intField(node, "grantsRank"));
            stage.standing = intField(node, "standing");
            stage.terminal = boolField(node, "terminal");
            line.stages.push_back(std::move(stage));
        }
        if (line.stages.empty()) {
            continue;
        }
        out.lines_.push_back(std::move(line));
    }

    std::sort(out.lines_.begin(), out.lines_.end(),
              [](const Questline& a, const Questline& b) { return a.id < b.id; });
    out.lines_.erase(
        std::unique(out.lines_.begin(), out.lines_.end(),
                    [](const Questline& a, const Questline& b) { return a.id == b.id; }),
        out.lines_.end());
    return out;
}

const Questline* QuestBook::find(std::string_view id) const noexcept {
    const auto found = std::lower_bound(
        lines_.begin(), lines_.end(), id,
        [](const Questline& line, std::string_view probe) { return line.id < probe; });
    if (found == lines_.end() || found->id != id) {
        return nullptr;
    }
    return &*found;
}

// ---------------------------------------------------------------------------
// the journal
// ---------------------------------------------------------------------------

const QuestProgress* QuestJournal::rowFor(std::string_view questId) const noexcept {
    const auto found = std::lower_bound(
        rows_.begin(), rows_.end(), questId,
        [](const QuestProgress& row, std::string_view probe) { return row.questId < probe; });
    if (found == rows_.end() || found->questId != questId) {
        return nullptr;
    }
    return &*found;
}

QuestProgress& QuestJournal::entryFor(std::string_view questId) {
    const auto at = std::lower_bound(
        rows_.begin(), rows_.end(), questId,
        [](const QuestProgress& row, std::string_view probe) { return row.questId < probe; });
    if (at != rows_.end() && at->questId == questId) {
        return *at;
    }
    QuestProgress fresh;
    fresh.questId = std::string(questId);
    return *rows_.insert(at, std::move(fresh));
}

void QuestJournal::start(std::string_view questId) {
    if (questId.empty()) {
        return;
    }
    entryFor(questId).started = true;
}

bool QuestJournal::started(std::string_view questId) const noexcept {
    const QuestProgress* row = rowFor(questId);
    return row != nullptr && row->started;
}

bool QuestJournal::done(std::string_view questId) const noexcept {
    const QuestProgress* row = rowFor(questId);
    return row != nullptr && row->done;
}

std::int32_t QuestJournal::stage(std::string_view questId) const noexcept {
    const QuestProgress* row = rowFor(questId);
    return row == nullptr ? 0 : row->stage;
}

std::int32_t QuestJournal::counter(std::string_view questId) const noexcept {
    const QuestProgress* row = rowFor(questId);
    return row == nullptr ? 0 : row->counter;
}

std::int32_t QuestJournal::stagesDone(std::string_view questId) const noexcept {
    const QuestProgress* row = rowFor(questId);
    if (row == nullptr) {
        return 0;
    }
    // A finished line has its last stage behind it as well as its index.
    return row->done ? row->stage + 1 : row->stage;
}

void QuestJournal::bumpCounter(std::string_view questId, std::int32_t delta) {
    if (questId.empty() || delta == 0) {
        return;
    }
    QuestProgress& row = entryFor(questId);
    row.counter = std::max(0, row.counter + delta);
}

bool QuestJournal::advance(const Questline& line) {
    if (line.stages.empty()) {
        return false;
    }
    QuestProgress& row = entryFor(line.id);
    if (row.done) {
        return false;
    }
    row.started = true;
    const std::size_t at = static_cast<std::size_t>(std::max(0, row.stage));
    if (at >= line.stages.size()) {
        row.done = true;
        return false;
    }
    const QuestStage& stage = line.stages[at];
    if (!stage.log.empty()) {
        log_.push_back(stage.log);
    }
    // The counter belongs to the stage that used it, not to the line.
    row.counter = 0;
    if (stage.terminal || at + 1 >= line.stages.size()) {
        row.done = true;
        return true;
    }
    row.stage = static_cast<std::int32_t>(at + 1);
    return true;
}

void QuestJournal::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(rows_.size()));
    for (const QuestProgress& row : rows_) {
        put_string(sink, row.questId);
        sink.put_int(static_cast<std::uint32_t>(row.stage));
        sink.put_int(static_cast<std::uint32_t>(row.counter));
        sink.put_byte(row.started ? 1U : 0U);
        sink.put_byte(row.done ? 1U : 0U);
    }
    // The journal's own prose is hashed by LENGTH and count rather than byte by
    // byte: the lines come out of the raws, so two runs that reached the same
    // stages wrote the same words, and hashing the words as well would only be
    // hashing the content file twice.
    sink.put_int(static_cast<std::uint32_t>(log_.size()));
    for (const std::string& line : log_) {
        sink.put_int(static_cast<std::uint32_t>(line.size()));
    }
}

}  // namespace granadad::sim
