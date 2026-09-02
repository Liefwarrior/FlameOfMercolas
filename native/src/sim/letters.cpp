#include "granadad/sim/letters.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "granadad/sim/barks.hpp"

namespace granadad::sim {

namespace {

[[nodiscard]] std::string stringField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return {};
    }
    // Same fold every authored string in this build goes through, so a hand
    // edit that reintroduces a smart quote or an em-dash cannot put a
    // control byte in front of a player.
    return foldToAscii(found->get<std::string>());
}

[[nodiscard]] std::vector<std::string> bodyField(const nlohmann::json& node) {
    std::vector<std::string> out;
    const auto found = node.find("body");
    if (found == node.end() || !found->is_array()) {
        return out;
    }
    for (const nlohmann::json& paragraph : *found) {
        if (paragraph.is_string()) {
            out.push_back(foldToAscii(paragraph.get<std::string>()));
        }
    }
    return out;
}

}  // namespace

std::filesystem::path letterRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "quests" / "bloodletter_letters.json";
}

std::filesystem::path missionSheetLetterRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "quests" / "mission_sheet_letters.json";
}

std::filesystem::path evictionLetterRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "quests" / "eviction_letters.json";
}

LetterRaws LetterRaws::load(const std::filesystem::path& contentDir) {
    return loadFile(letterRawsPath(contentDir));
}

LetterRaws LetterRaws::loadFile(const std::filesystem::path& path) {
    LetterRaws out;
    std::ifstream file(path);
    if (!file) {
        // SILENT, deliberately, and for the same reason casebook.cpp's own
        // loader is: a content edit must not be able to stop the game
        // booting.
        return out;
    }
    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return out;
    }
    const auto letters = root.find("letters");
    if (letters == root.end() || !letters->is_array()) {
        return out;
    }
    for (const nlohmann::json& node : *letters) {
        if (!node.is_object()) {
            continue;
        }
        Letter letter;
        letter.id = stringField(node, "id");
        if (letter.id.empty()) {
            continue;
        }
        letter.lead = stringField(node, "lead");
        letter.from = stringField(node, "from");
        letter.to = stringField(node, "to");
        letter.dateline = stringField(node, "dateline");
        letter.salutation = stringField(node, "salutation");
        letter.body = bodyField(node);
        letter.closing = stringField(node, "closing");
        letter.signature = stringField(node, "signature");
        // COURIER CASE. Absent reads false, so every letter authored before
        // the flag existed keeps exactly the gate it always had.
        const auto handed = node.find("handed");
        letter.handed = handed != node.end() && handed->is_boolean() && handed->get<bool>();
        out.letters_.push_back(std::move(letter));
    }
    // AUTHORED ORDER IS KEPT, same as casebook.cpp's own leads_ -- nothing
    // here sorts, so Maell's three letters come back in the order he wrote
    // them.
    return out;
}

std::int32_t LetterRaws::indexOf(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < letters_.size(); ++i) {
        if (letters_[i].id == id) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

std::vector<std::int32_t> LetterRaws::forLead(std::string_view leadId) const {
    std::vector<std::int32_t> out;
    for (std::size_t i = 0; i < letters_.size(); ++i) {
        if (letters_[i].lead == leadId) {
            out.push_back(static_cast<std::int32_t>(i));
        }
    }
    return out;
}

}  // namespace granadad::sim
