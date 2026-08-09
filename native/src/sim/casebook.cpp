#include "granadad/sim/casebook.hpp"

#include <algorithm>
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
    // Folded the same way every authored string in this build is, so a hand
    // edit that reintroduces a smart quote cannot punch a blank column through
    // the middle of a clue.
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

std::string_view leadStateName(LeadState state) noexcept {
    switch (state) {
        case LeadState::Unheard:
            return "unheard";
        case LeadState::Open:
            return "open";
        case LeadState::Cold:
            return "cold";
        case LeadState::Followed:
            return "followed";
    }
    return "unheard";
}

std::filesystem::path casebookRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "quests" / "casebook.json";
}

CasebookRaws CasebookRaws::load(const std::filesystem::path& contentDir) {
    CasebookRaws out;
    std::ifstream file(casebookRawsPath(contentDir));
    if (!file) {
        // SILENT, deliberately, and for the same reason every other loader in
        // this build is: a content edit must not be able to stop the game
        // booting. An empty trail is a game with no case in it, which is a
        // visible and diagnosable state; a crash on startup is not.
        return out;
    }
    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return out;
    }

    const auto caseNode = root.find("case");
    if (caseNode != root.end() && caseNode->is_object()) {
        out.title_ = stringField(*caseNode, "title");
        out.hook_ = stringField(*caseNode, "hook");
        out.close_ = stringField(*caseNode, "close");
        const auto bands = caseNode->find("dreadBands");
        if (bands != caseNode->end() && bands->is_array()) {
            for (const nlohmann::json& band : *bands) {
                if (!band.is_object()) {
                    continue;
                }
                DreadBand row;
                row.at = intField(band, "at");
                row.label = stringField(band, "label");
                if (!row.label.empty()) {
                    out.dread_.push_back(std::move(row));
                }
            }
        }
    }
    // Ascending by threshold, so dreadLabel can walk it once. Stable, because
    // two bands at one threshold must resolve the same way in every run.
    std::stable_sort(out.dread_.begin(), out.dread_.end(),
                     [](const DreadBand& a, const DreadBand& b) { return a.at < b.at; });

    const auto leads = root.find("leads");
    if (leads == root.end() || !leads->is_array()) {
        return out;
    }
    for (const nlohmann::json& node : *leads) {
        if (!node.is_object()) {
            continue;
        }
        Lead lead;
        lead.id = stringField(node, "id");
        if (lead.id.empty()) {
            continue;
        }
        lead.place = stringField(node, "place");
        lead.brief = stringField(node, "short");
        lead.what = stringField(node, "what");
        lead.who = stringField(node, "who");
        lead.found = stringField(node, "found");
        lead.detail = stringField(node, "detail");
        lead.dread = intField(node, "dread");
        lead.start = boolField(node, "start");
        lead.deadEnd = boolField(node, "deadEnd");
        lead.close = boolField(node, "close");
        const auto site = node.find("site");
        if (site != node.end() && site->is_object()) {
            lead.site.x = intField(*site, "x");
            lead.site.y = intField(*site, "y");
            lead.site.band = intField(*site, "band");
        }
        const auto opens = node.find("opens");
        if (opens != node.end() && opens->is_array()) {
            for (const nlohmann::json& one : *opens) {
                if (one.is_string()) {
                    lead.opens.push_back(foldToAscii(one.get<std::string>()));
                }
            }
        }
        out.leads_.push_back(std::move(lead));
    }
    // AUTHORED ORDER IS KEPT. Nothing sorts the leads: the file's order is the
    // order the casebook lists them in, which is the order the trail is meant
    // to read in, and a sort here would be a silent editorial decision.
    return out;
}

std::int32_t CasebookRaws::indexOf(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < leads_.size(); ++i) {
        if (leads_[i].id == id) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

std::vector<std::int32_t> CasebookRaws::openedBy(std::int32_t leadIndex) const {
    std::vector<std::int32_t> out;
    if (leadIndex < 0 || static_cast<std::size_t>(leadIndex) >= leads_.size()) {
        return out;
    }
    const std::string& id = leads_[static_cast<std::size_t>(leadIndex)].id;
    for (std::size_t i = 0; i < leads_.size(); ++i) {
        for (const std::string& opened : leads_[i].opens) {
            if (opened == id) {
                out.push_back(static_cast<std::int32_t>(i));
                break;
            }
        }
    }
    return out;
}

std::string_view CasebookRaws::dreadLabel(std::int32_t dread) const noexcept {
    std::string_view label;
    for (const DreadBand& band : dread_) {
        if (dread >= band.at) {
            label = band.label;
        }
    }
    return label;
}

// ---------------------------------------------------------------------------
// the notes
// ---------------------------------------------------------------------------

void Casebook::begin(const CasebookRaws& raws, std::int64_t nowSeconds) {
    raws_ = &raws;
    state_.assign(raws.leads().size(), static_cast<std::uint8_t>(LeadState::Unheard));
    heardAt_.assign(raws.leads().size(), -1);
    dread_ = 0;
    closed_ = false;
    for (std::size_t i = 0; i < raws.leads().size(); ++i) {
        if (raws.leads()[i].start) {
            state_[i] = static_cast<std::uint8_t>(LeadState::Open);
            heardAt_[i] = nowSeconds;
        }
    }
}

LeadState Casebook::state(std::int32_t lead) const noexcept {
    if (lead < 0 || static_cast<std::size_t>(lead) >= state_.size()) {
        return LeadState::Unheard;
    }
    return static_cast<LeadState>(state_[static_cast<std::size_t>(lead)]);
}

std::int64_t Casebook::heardAt(std::int32_t lead) const noexcept {
    if (lead < 0 || static_cast<std::size_t>(lead) >= heardAt_.size()) {
        return -1;
    }
    return heardAt_[static_cast<std::size_t>(lead)];
}

std::vector<std::int32_t> Casebook::known() const {
    std::vector<std::int32_t> out;
    for (std::size_t i = 0; i < state_.size(); ++i) {
        if (static_cast<LeadState>(state_[i]) != LeadState::Unheard) {
            out.push_back(static_cast<std::int32_t>(i));
        }
    }
    return out;
}

std::int32_t Casebook::readCount() const noexcept {
    std::int32_t count = 0;
    for (const std::uint8_t one : state_) {
        const LeadState what = static_cast<LeadState>(one);
        if (what == LeadState::Cold || what == LeadState::Followed) {
            ++count;
        }
    }
    return count;
}

std::int32_t Casebook::coldCount() const noexcept {
    std::int32_t count = 0;
    for (const std::uint8_t one : state_) {
        if (static_cast<LeadState>(one) == LeadState::Cold) {
            ++count;
        }
    }
    return count;
}

std::int32_t Casebook::nextOpen() const noexcept {
    for (std::size_t i = 0; i < state_.size(); ++i) {
        if (static_cast<LeadState>(state_[i]) == LeadState::Open) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

bool Casebook::hear(std::int32_t lead, std::int64_t nowSeconds) {
    if (lead < 0 || static_cast<std::size_t>(lead) >= state_.size()) {
        return false;
    }
    if (static_cast<LeadState>(state_[static_cast<std::size_t>(lead)]) != LeadState::Unheard) {
        return false;
    }
    state_[static_cast<std::size_t>(lead)] = static_cast<std::uint8_t>(LeadState::Open);
    heardAt_[static_cast<std::size_t>(lead)] = nowSeconds;
    return true;
}

LookResult Casebook::look(std::int32_t tileX, std::int32_t tileY, std::int32_t band,
                          std::int64_t nowSeconds) {
    LookResult out;
    if (raws_ == nullptr) {
        out.line = "NOTHING HERE WORTH WRITING DOWN.";
        return out;
    }
    const std::vector<Lead>& leads = raws_->leads();

    // THE NEAREST LEAD IN REACH, and the state decides what happens rather than
    // the distance -- so standing at a site you have already read tells you
    // what you read, which is the answer a player wants when they come back to
    // check something.
    std::int32_t best = -1;
    std::int32_t bestDistance = 0;
    for (std::size_t i = 0; i < leads.size(); ++i) {
        const Lead& lead = leads[i];
        if (lead.site.band != band) {
            continue;
        }
        const std::int32_t dx = lead.site.x - tileX;
        const std::int32_t dy = lead.site.y - tileY;
        const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (distance > kLookRangeTiles) {
            continue;
        }
        // Ties break on the earlier authored index, because the vector is never
        // reordered -- two runs cannot disagree about which lead is nearer.
        if (best < 0 || distance < bestDistance) {
            best = static_cast<std::int32_t>(i);
            bestDistance = distance;
        }
    }
    if (best < 0) {
        out.line = "NOTHING HERE WORTH WRITING DOWN.";
        return out;
    }

    const Lead& lead = leads[static_cast<std::size_t>(best)];
    const LeadState was = state(best);
    out.lead = best;

    if (was == LeadState::Unheard) {
        // YOU CAN STAND ON A CLUE AND NOT SEE IT. This is the gazetteer's own
        // design law made physical: the gate is knowing where to ask, so a site
        // nobody has pointed you at reads as an ordinary corner of the ward.
        // It is also what makes the trail an investigation rather than a tour --
        // a player who walks the whole district on day one collects nothing.
        out.line = "NOTHING HERE WORTH WRITING DOWN.";
        return out;
    }
    if (was != LeadState::Open) {
        out.line = lead.found;
        return out;
    }

    // A LOOK THAT LANDS.
    out.found = true;
    out.line = lead.found;
    dread_ = std::min<std::int32_t>(100, dread_ + std::max<std::int32_t>(0, lead.dread));
    for (const std::string& id : lead.opens) {
        if (hear(raws_->indexOf(id), nowSeconds)) {
            ++out.opened;
        }
    }
    // COLD IS A LEAD THAT POINTS NOWHERE, not a lead whose destinations you had
    // already been given. The trail CONVERGES -- Harl's yard, Brann's gray
    // ledger, the dogs on Kennel Row and Tarry Jek all end up pointing at the
    // Drowned Hold, which is what corroboration IS -- so the second and third
    // of those open no NEW entry and are still very much followed. What makes a
    // lead cold is that it points nowhere at all, checked against the author's
    // own deadEnd flag by a case rather than trusted to agree with it.
    state_[static_cast<std::size_t>(best)] =
        static_cast<std::uint8_t>(lead.opens.empty() ? LeadState::Cold : LeadState::Followed);
    if (lead.close) {
        closed_ = true;
    }
    return out;
}

void Casebook::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(state_.size()));
    for (const std::uint8_t one : state_) {
        sink.put_byte(one);
    }
    // TASK #82. The dateline goes in the hash too, and for the same reason
    // every other field here does: two runs given the same inputs have to
    // agree about it, and folding it in is how a divergence in WHEN a lead
    // was recorded -- not just whether -- would actually be caught rather
    // than trusted.
    sink.put_int(static_cast<std::uint32_t>(heardAt_.size()));
    for (const std::int64_t at : heardAt_) {
        sink.put_long(static_cast<std::uint64_t>(at));
    }
    sink.put_int(static_cast<std::uint32_t>(dread_));
    sink.put_byte(closed_ ? 1U : 0U);
    // The raws themselves go in too, so a hand edit to casebook.json is a
    // different world and says so, instead of two machines with two different
    // content trees agreeing on a hash they should not agree on.
    if (raws_ != nullptr) {
        put_string(sink, raws_->title());
        sink.put_int(static_cast<std::uint32_t>(raws_->leads().size()));
        for (const Lead& lead : raws_->leads()) {
            put_string(sink, lead.id);
            sink.put_int(static_cast<std::uint32_t>(lead.site.x));
            sink.put_int(static_cast<std::uint32_t>(lead.site.y));
            sink.put_int(static_cast<std::uint32_t>(lead.site.band));
        }
    }
}

}  // namespace granadad::sim
