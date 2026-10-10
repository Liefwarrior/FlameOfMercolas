#include "granadad/workbench/workbench.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#include "granadad/render/capture.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/map_view.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/contraband.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/radiant_quest.hpp"
#include "granadad/sim/ward_actors.hpp"

namespace granadad::workbench {

namespace {

using render::Framebuffer;
using render::PanelRect;
using render::Rgb;

// THE GRID. The 4x6 font at scale two: eight-by-twelve glyphs on a ten-by-
// fourteen cell, the same arithmetic hud.cpp's rowHeight() and panel.hpp's
// PanelMetric keep, so a workbench row lines up exactly like a game row.
constexpr int kTextScale = 2;
constexpr int kCellW = 5 * kTextScale;
constexpr int kCellH = 7 * kTextScale;
constexpr int kPad = 4;

// How many console lines are kept; the pane shows the newest that fit.
constexpr std::size_t kConsoleKeep = 200;

[[nodiscard]] const render::PanelInk& ink() { return render::panelInk(); }

[[nodiscard]] std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

[[nodiscard]] std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

[[nodiscard]] bool containsFolded(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

[[nodiscard]] std::vector<std::string> words(std::string_view line) {
    std::vector<std::string> out;
    std::istringstream in{std::string(line)};
    std::string word;
    while (in >> word) {
        out.push_back(word);
    }
    return out;
}

/// Everything after the first `skip` words, as typed.
[[nodiscard]] std::string restAfter(std::string_view line, int skip) {
    std::size_t at = 0;
    for (int i = 0; i < skip; ++i) {
        while (at < line.size() && std::isspace(static_cast<unsigned char>(line[at])) != 0) {
            ++at;
        }
        while (at < line.size() && std::isspace(static_cast<unsigned char>(line[at])) == 0) {
            ++at;
        }
    }
    while (at < line.size() && std::isspace(static_cast<unsigned char>(line[at])) != 0) {
        ++at;
    }
    std::string out(line.substr(at));
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())) != 0) {
        out.pop_back();
    }
    return out;
}

[[nodiscard]] bool parseInt(std::string_view text, std::int32_t& out) {
    if (text.empty()) {
        return false;
    }
    std::int64_t value = 0;
    bool negative = false;
    std::size_t at = 0;
    if (text[0] == '-' || text[0] == '+') {
        negative = text[0] == '-';
        at = 1;
    }
    if (at >= text.size()) {
        return false;
    }
    for (; at < text.size(); ++at) {
        if (std::isdigit(static_cast<unsigned char>(text[at])) == 0) {
            return false;
        }
        value = value * 10 + (text[at] - '0');
        if (value > 2000000000) {
            return false;
        }
    }
    out = static_cast<std::int32_t>(negative ? -value : value);
    return true;
}

[[nodiscard]] std::string clock(int secondOfDay) {
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d", (secondOfDay / 3600) % 24,
                  (secondOfDay / 60) % 60);
    return text;
}

[[nodiscard]] std::string hex32(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "%08llX",
                  static_cast<unsigned long long>(value & 0xFFFFFFFFull));
    return text;
}

/// Greedy word wrap to `cols` columns. A word longer than the line is cut.
[[nodiscard]] std::vector<std::string> wrap(std::string_view text, int cols) {
    std::vector<std::string> out;
    if (cols <= 0) {
        return out;
    }
    std::string line;
    for (const std::string& word : words(text)) {
        std::string piece = word;
        while (static_cast<int>(piece.size()) > cols) {
            if (!line.empty()) {
                out.push_back(line);
                line.clear();
            }
            out.push_back(piece.substr(0, static_cast<std::size_t>(cols)));
            piece = piece.substr(static_cast<std::size_t>(cols));
        }
        if (line.empty()) {
            line = piece;
        } else if (static_cast<int>(line.size() + 1 + piece.size()) <= cols) {
            line += " " + piece;
        } else {
            out.push_back(line);
            line = piece;
        }
    }
    if (!line.empty()) {
        out.push_back(line);
    }
    return out;
}

// --- drawing -----------------------------------------------------------------

[[nodiscard]] int colsIn(const PanelRect& pane) { return std::max(0, (pane.w - 2 * kPad) / kCellW); }
[[nodiscard]] int rowsIn(const PanelRect& pane) { return std::max(0, (pane.h - 2 * kPad) / kCellH); }

/// Draws `text` at (col, row) of `pane`'s text grid, cut at the pane's edge.
void text(Framebuffer& target, const PanelRect& pane, int col, int row, std::string_view line,
          const Rgb& colour) {
    const int room = colsIn(pane) - col;
    if (room <= 0 || row < 0 || row >= rowsIn(pane)) {
        return;
    }
    const std::string cut = upper(line.substr(0, std::min<std::size_t>(line.size(),
                                                                      static_cast<std::size_t>(room))));
    (void)render::drawText(target, pane.x + kPad + col * kCellW, pane.y + kPad + row * kCellH, cut,
                           colour, 1.0F, kTextScale);
}

/// The same, right-aligned on the pane's last column.
void textRight(Framebuffer& target, const PanelRect& pane, int row, std::string_view line,
               const Rgb& colour) {
    const int col = std::max(0, colsIn(pane) - static_cast<int>(line.size()));
    text(target, pane, col, row, line, colour);
}

/// A highlighted row: the accent fill under the whole row.
void rowFill(Framebuffer& target, const PanelRect& pane, int row, const Rgb& colour, float alpha) {
    if (row < 0 || row >= rowsIn(pane)) {
        return;
    }
    target.fillRect(pane.x + 1, pane.y + kPad + row * kCellH - 1, pane.w - 2, kCellH, colour, alpha);
}

void outline(Framebuffer& target, int x, int y, int w, int h, const Rgb& colour) {
    target.fillRect(x, y, w, 1, colour, 1.0F);
    target.fillRect(x, y + h - 1, w, 1, colour, 1.0F);
    target.fillRect(x, y, 1, h, colour, 1.0F);
    target.fillRect(x + w - 1, y, 1, h, colour, 1.0F);
}

/// A pane's ground, its rule and its title knocked out of an accent bar.
void chrome(Framebuffer& target, const PanelRect& pane, std::string_view title) {
    target.fillRect(pane.x, pane.y, pane.w, pane.h, ink().ground, 1.0F);
    outline(target, pane.x, pane.y, pane.w, pane.h, ink().rule);
    const std::string label = " " + upper(title) + " ";
    const int width = static_cast<int>(label.size()) * kCellW;
    target.fillRect(pane.x + kPad, pane.y + kPad - 1, width, kCellH, ink().accent, 1.0F);
    text(target, pane, 0, 0, label, ink().knockout);
}

[[nodiscard]] Rgb typeColour(sim::WardType type) {
    switch (type) {
        case sim::WardType::MilitiaWatch:
            return Rgb{0.40F, 0.62F, 0.95F};
        case sim::WardType::PriestOfTheFlame:
        case sim::WardType::DiscipleOfTheFlame:
            return Rgb{0.96F, 0.94F, 0.88F};
        case sim::WardType::Shopkeeper:
            return Rgb{0.52F, 0.82F, 0.48F};
        default:
            break;
    }
    if (!sim::isPerson(type)) {
        return Rgb{0.60F, 0.48F, 0.36F};
    }
    return Rgb{0.86F, 0.70F, 0.50F};
}

[[nodiscard]] std::string personName(const render::Session& session, std::int32_t id) {
    const std::string& name = session.people().identity(id).name;
    return name.empty() ? "SOMEBODY #" + std::to_string(id) : name;
}

[[nodiscard]] std::string errandLabel(const sim::RadiantObjective& row) {
    if (row.isFetch()) {
        return row.verb + " " + std::to_string(row.units) + " " +
               std::string(sim::contrabandLabelFor(row.good, row.units)) + " FOR " +
               upper(row.giverName);
    }
    return row.verb + " TO " + upper(row.targetName);
}

/// Where a ward body stands, in the plan's own world-tile terms.
struct Spot {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
};

[[nodiscard]] std::string jsonEscape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace

std::string_view hierarchyTabName(HierarchyTab tab) noexcept {
    switch (tab) {
        case HierarchyTab::People:
            return "people";
        case HierarchyTab::Places:
            return "places";
        case HierarchyTab::Board:
            return "board";
        case HierarchyTab::Kit:
            return "kit";
    }
    return "people";
}

Panes workbenchPanes() noexcept {
    // One fixed composition: the GAME pane is the session's own 640x360 at its
    // native size, the SCENE pane its twin above it, the lists either side.
    Panes out;
    constexpr int kTop = kCellH + 2 * kPad + 2;
    constexpr int kLeft = 340;
    constexpr int kCentre = 640;
    constexpr int kCommand = kCellH + 2 * kPad + 2;
    const int bottom = kFrameHeight - kCommand;
    out.toolbar = PanelRect{0, 0, kFrameWidth, kTop};
    out.hierarchy = PanelRect{0, kTop, kLeft, bottom - kTop};
    out.scene = PanelRect{kLeft, kTop, kCentre, 360};
    out.game = PanelRect{kLeft, kTop + 360, kCentre, bottom - kTop - 360};
    out.inspector = PanelRect{kLeft + kCentre, kTop, kFrameWidth - kLeft - kCentre, 400};
    out.console = PanelRect{kLeft + kCentre, kTop + 400, kFrameWidth - kLeft - kCentre,
                            bottom - kTop - 400};
    out.command = PanelRect{0, bottom, kFrameWidth, kCommand};
    return out;
}

// ---------------------------------------------------------------------------
// the world, held
// ---------------------------------------------------------------------------

Workbench::Workbench(render::SessionConfig config) : config_(std::move(config)) {
    reboot();
    say("GRANADAD WORKBENCH. TYPE HELP.");
}

Workbench::~Workbench() = default;

void Workbench::reboot() {
    session_ = std::make_unique<render::Session>(config_);
    playing_ = false;
    stepsRun_ = 0;
    selection_ = Selection{};
    cursor_ = 0;
    scroll_ = 0;
    sceneLog_.clear();
}

void Workbench::say(std::string line) {
    console_.push_back(std::move(line));
    if (console_.size() > kConsoleKeep) {
        console_.erase(console_.begin(),
                       console_.begin() + static_cast<std::ptrdiff_t>(console_.size() - kConsoleKeep));
    }
}

void Workbench::stepWorld(int steps) {
    if (steps <= 0) {
        return;
    }
    const std::string before = session_->lastMessage();
    session_->stepMany(sim::MoveInput{}, steps);
    stepsRun_ += steps;
    if (session_->lastMessage() != before && !session_->lastMessage().empty()) {
        say(session_->lastMessage());
    }
}

void Workbench::frame() {
    if (!playing_) {
        return;
    }
    stepWorld(speed_);
    // PLAY IS LOGGED AS STEPS, coalesced, so a scene saved after a minute of
    // watching replays the same minute.
    if (!sceneLog_.empty() && sceneLog_.back().rfind("step ", 0) == 0) {
        std::int32_t had = 0;
        if (parseInt(sceneLog_.back().substr(5), had)) {
            sceneLog_.back() = "step " + std::to_string(had + speed_);
            return;
        }
    }
    sceneLog_.push_back("step " + std::to_string(speed_));
}

// ---------------------------------------------------------------------------
// the lists
// ---------------------------------------------------------------------------

std::vector<HierarchyRow> Workbench::hierarchyRows() const {
    std::vector<HierarchyRow> rows;
    const render::Session& s = *session_;
    switch (tab_) {
        case HierarchyTab::People: {
            for (const sim::WardActor& actor : s.people().actors()) {
                HierarchyRow row;
                row.label = upper(personName(s, actor.id));
                row.note = upper(sim::wardTypeName(actor.type));
                if (!actor.visible()) {
                    row.note += actor.dead ? " DEAD" : " DOWN";
                }
                row.select = Selection{Selection::Kind::Person, actor.id};
                if (containsFolded(row.label, filter_) || containsFolded(row.note, filter_)) {
                    rows.push_back(std::move(row));
                }
            }
            break;
        }
        case HierarchyTab::Places: {
            const std::vector<render::MapPlace>& places = render::mapPlaces();
            for (std::size_t i = 0; i < places.size(); ++i) {
                HierarchyRow row;
                row.label = upper(places[i].name);
                row.note = places[i].way ? "WAY" : "DOOR";
                row.select = Selection{Selection::Kind::Place, static_cast<std::int32_t>(i)};
                if (containsFolded(row.label, filter_)) {
                    rows.push_back(std::move(row));
                }
            }
            break;
        }
        case HierarchyTab::Board: {
            for (const sim::RadiantObjective& errand :
                 s.tavern().dialogue().radiant().objectives()) {
                HierarchyRow row;
                row.label = errandLabel(errand);
                row.note = upper(sim::radiantStateName(errand.state));
                row.select = Selection{Selection::Kind::Errand, errand.id};
                if (containsFolded(row.label, filter_) || containsFolded(row.note, filter_)) {
                    rows.push_back(std::move(row));
                }
            }
            break;
        }
        case HierarchyTab::Kit: {
            const sim::ItemRegistry& items = s.tavern().items();
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto index = static_cast<std::int32_t>(i);
                const std::int32_t count = s.tavern().kit().count(index);
                if (count <= 0) {
                    continue;
                }
                HierarchyRow row;
                row.label = items.at(index)->name;
                row.note = "X" + std::to_string(count) +
                           (s.tavern().kit().isWorn(index) ? " WORN" : "");
                row.select = Selection{Selection::Kind::Item, index};
                if (containsFolded(row.label, filter_)) {
                    rows.push_back(std::move(row));
                }
            }
            break;
        }
    }
    return rows;
}

void Workbench::clampCursor() {
    const int count = static_cast<int>(hierarchyRows().size());
    cursor_ = count <= 0 ? 0 : std::clamp(cursor_, 0, count - 1);
}

bool Workbench::selectByName(Selection::Kind kind, std::string_view what) {
    const render::Session& s = *session_;
    std::int32_t id = -1;
    // "#166" is an id; anything else is a name, exact first, then contained.
    if (!what.empty() && what[0] == '#' && parseInt(what.substr(1), id)) {
        const bool real =
            (kind == Selection::Kind::Person && s.people().byId(id) != nullptr) ||
            (kind == Selection::Kind::Place && id >= 0 &&
             static_cast<std::size_t>(id) < render::mapPlaces().size()) ||
            (kind == Selection::Kind::Errand &&
             s.tavern().dialogue().radiant().find(id) != nullptr) ||
            (kind == Selection::Kind::Item && s.tavern().items().at(id) != nullptr);
        if (!real) {
            return false;
        }
        selection_ = Selection{kind, id};
        return true;
    }
    if (kind == Selection::Kind::Person) {
        const sim::WardActor* contained = nullptr;
        for (const sim::WardActor& actor : s.people().actors()) {
            const std::string name = personName(s, actor.id);
            if (lower(name) == lower(what)) {
                selection_ = Selection{kind, actor.id};
                return true;
            }
            if (contained == nullptr && containsFolded(name, what)) {
                contained = &actor;
            }
        }
        if (contained != nullptr) {
            selection_ = Selection{kind, contained->id};
            return true;
        }
        return false;
    }
    if (kind == Selection::Kind::Place) {
        const std::vector<render::MapPlace>& places = render::mapPlaces();
        int contained = -1;
        for (std::size_t i = 0; i < places.size(); ++i) {
            if (lower(places[i].name) == lower(what)) {
                selection_ = Selection{kind, static_cast<std::int32_t>(i)};
                return true;
            }
            if (contained < 0 && containsFolded(places[i].name, what)) {
                contained = static_cast<int>(i);
            }
        }
        if (contained >= 0) {
            selection_ = Selection{kind, contained};
            return true;
        }
        return false;
    }
    if (kind == Selection::Kind::Errand && parseInt(what, id) &&
        s.tavern().dialogue().radiant().find(id) != nullptr) {
        selection_ = Selection{kind, id};
        return true;
    }
    if (kind == Selection::Kind::Item) {
        const std::int32_t index = s.tavern().items().indexOf(lower(what));
        if (index >= 0) {
            selection_ = Selection{kind, index};
            return true;
        }
        for (std::size_t i = 0; i < s.tavern().items().size(); ++i) {
            if (containsFolded(s.tavern().items().items()[i].name, what)) {
                selection_ = Selection{kind, static_cast<std::int32_t>(i)};
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> Workbench::inspectorLines() const {
    std::vector<std::string> out;
    const render::Session& s = *session_;
    const sim::PlayerBody& body = s.body();
    switch (selection_.kind) {
        case Selection::Kind::Person: {
            const sim::WardActor* actor = s.people().byId(selection_.id);
            if (actor == nullptr) {
                out.push_back("NOBODY BY THAT ID.");
                break;
            }
            const sim::WardIdentity& who = s.people().identity(actor->id);
            out.push_back("NAME     " + upper(personName(s, actor->id)));
            if (!who.epithet.empty()) {
                out.push_back("CALLED   " + upper(who.epithet));
            }
            out.push_back("TRADE    " + upper(sim::wardTypeName(actor->type)));
            out.push_back("ID       " + std::to_string(actor->id) +
                          (who.notableId.empty() ? "" : "   NOTABLE " + upper(who.notableId)));
            out.push_back("AT       " + std::to_string(actor->x) + "," + std::to_string(actor->y) +
                          " BAND " + std::to_string(actor->band));
            out.push_back("         " +
                          std::string(sim::docks::placeLabelAt(actor->x, actor->y, actor->band)));
            const bool atPost = actor->band == actor->anchorBand &&
                                std::abs(actor->x - actor->anchorX) <= 4 &&
                                std::abs(actor->y - actor->anchorY) <= 4;
            out.push_back("POST     " + std::to_string(actor->anchorX) + "," +
                          std::to_string(actor->anchorY) + (atPost ? "  AT IT" : "  AWAY"));
            out.push_back("HOME     " + std::to_string(actor->homeX) + "," +
                          std::to_string(actor->homeY));
            out.push_back("COIN     " + std::to_string(actor->coin));
            out.push_back(std::string("STATE    ") +
                          (actor->dead ? "DEAD" : actor->visible() ? "ON THEIR FEET" : "DOWN"));
            const std::int32_t far = std::max(std::abs(actor->x - body.tileX()),
                                              std::abs(actor->y - body.tileY()));
            const sim::WardActor* addressed = s.wardAddresseeNow();
            out.push_back("FROM YOU " + std::to_string(far) + " TILES" +
                          (addressed != nullptr && addressed->id == actor->id ? "  E TALKS TO THEM"
                                                                             : ""));
            for (const sim::RadiantObjective& row : s.tavern().dialogue().radiant().objectives()) {
                if (row.giverActorId == actor->id) {
                    out.push_back("GIVES    " + errandLabel(row) + " (" +
                                  upper(sim::radiantStateName(row.state)) + ")");
                }
                if (row.targetActorId == actor->id) {
                    out.push_back("NAMED IN " + errandLabel(row) + " (" +
                                  upper(sim::radiantStateName(row.state)) + ")");
                }
            }
            if (const sim::ShopCounter* counter = s.tavern().shops().forKeeper(who.notableId);
                counter != nullptr) {
                out.push_back("KEEPS    " + upper(counter->name));
            }
            break;
        }
        case Selection::Kind::Place: {
            const std::vector<render::MapPlace>& places = render::mapPlaces();
            if (selection_.id < 0 || static_cast<std::size_t>(selection_.id) >= places.size()) {
                out.push_back("NO SUCH PLACE.");
                break;
            }
            const render::MapPlace& place = places[static_cast<std::size_t>(selection_.id)];
            out.push_back("PLACE    " + upper(place.name));
            out.push_back(std::string("KIND     ") + (place.way ? "A WAY" : "A DOOR"));
            out.push_back("TILES    " + std::to_string(place.x0) + "," + std::to_string(place.y0) +
                          " TO " + std::to_string(place.x1) + "," + std::to_string(place.y1) +
                          " BAND " + std::to_string(place.band));
            for (const std::string& line : wrap(place.what, 32)) {
                out.push_back("         " + upper(line));
            }
            std::vector<std::string> here;
            for (const sim::WardActor& actor : s.people().actors()) {
                if (actor.visible() && actor.band == place.band && actor.x >= place.x0 &&
                    actor.x <= place.x1 && actor.y >= place.y0 && actor.y <= place.y1) {
                    here.push_back(upper(personName(s, actor.id)));
                }
            }
            // The Gilded Gull's own cast are the TAVERN's bodies, not the
            // ward's: counted too, or the busiest room in the district reads
            // empty.
            for (const sim::Actor& actor : s.tavern().actors()) {
                if (actor.band() == place.band && actor.tileX() >= place.x0 &&
                    actor.tileX() <= place.x1 && actor.tileY() >= place.y0 &&
                    actor.tileY() <= place.y1) {
                    here.push_back(upper(actor.name()));
                }
            }
            out.push_back("HERE NOW " + std::to_string(here.size()));
            for (std::size_t i = 0; i < here.size() && i < 10; ++i) {
                out.push_back("  " + here[i]);
            }
            break;
        }
        case Selection::Kind::Errand: {
            const sim::RadiantObjective* row =
                s.tavern().dialogue().radiant().find(selection_.id);
            if (row == nullptr) {
                out.push_back("THAT ERRAND HAS LEFT THE BOARD.");
                break;
            }
            out.push_back("ERRAND   " + errandLabel(*row));
            out.push_back("STATE    " + upper(sim::radiantStateName(row->state)));
            out.push_back("GIVER    " + upper(row->giverName) + " #" +
                          std::to_string(row->giverActorId));
            out.push_back("         " + upper(sim::radiantPlacePhrase(row->giverPlace).at));
            out.push_back("TARGET   " + upper(row->targetName) + " #" +
                          std::to_string(row->targetActorId));
            out.push_back("         " + upper(sim::radiantPlacePhrase(row->targetPlace).at));
            out.push_back("PAYS     " + std::to_string(row->pay) + "C");
            for (const std::string& line : wrap(row->brief, 36)) {
                out.push_back(upper(line));
            }
            break;
        }
        case Selection::Kind::Item: {
            const sim::ItemDef* item = s.tavern().items().at(selection_.id);
            if (item == nullptr) {
                out.push_back("NO SUCH ITEM.");
                break;
            }
            out.push_back("ITEM     " + item->name + "  (" + upper(item->id) + ")");
            out.push_back("WEIGHS   " + std::to_string(item->drams) + " DRAMS");
            out.push_back("WORTH    " + std::to_string(item->royals) + " ROYALS");
            if (item->dr > 0) {
                out.push_back("TURNS    " + std::to_string(item->dr));
            }
            out.push_back("CARRIED  " + std::to_string(s.tavern().kit().count(selection_.id)) +
                          (s.tavern().kit().isWorn(selection_.id) ? "  WORN" : ""));
            for (const std::string& line : wrap(item->desc, 36)) {
                out.push_back(upper(line));
            }
            break;
        }
        case Selection::Kind::None:
        default: {
            out.push_back("THE PLAYER");
            out.push_back("AT       " + std::to_string(body.tileX()) + "," +
                          std::to_string(body.tileY()) + " BAND " + std::to_string(body.band()));
            out.push_back("         " + upper(s.placeLabel()));
            out.push_back("FACING   " + std::string(sim::compass_point(body.yaw())));
            out.push_back("COIN     " + std::to_string(s.tavern().playerCoin()));
            out.push_back("LOAD     " + std::to_string(s.tavern().loadDrams()) + " / " +
                          std::to_string(s.tavern().loadBudget()) + " DRAMS");
            out.push_back("HEAT     " +
                          std::to_string(s.tavern().dialogue().crimes().heat()));
            out.push_back("TONE     " +
                          std::string(sim::toneName(s.tavern().dialogue().tone())));
            if (s.talking()) {
                out.push_back("TALKING  " + upper(s.tavern().dialogue().speaker().name));
            }
            break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// the commands
// ---------------------------------------------------------------------------

std::string Workbench::run(std::string_view line) {
    say("> " + std::string(line));
    std::string answer = runOne(line, true);
    // A command can open a page; the paused picture shows it opened.
    session_->settleOverlaysForInspection();
    std::istringstream lines(answer);
    std::string each;
    while (std::getline(lines, each)) {
        say(each);
    }
    return answer;
}

std::string Workbench::runFile(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) {
        return "NO SUCH FILE: " + file.string() + "\n";
    }
    std::string all;
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = restAfter(line, 0);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        all += run(trimmed);
        if (quit_) {
            break;
        }
    }
    return all;
}

std::string Workbench::runOne(std::string_view line, bool logged) {
    const std::vector<std::string> w = words(line);
    if (w.empty()) {
        return {};
    }
    const std::string verb = lower(w[0]);
    render::Session& s = *session_;
    std::ostringstream out;
    const auto log = [this, logged](const std::string& command) {
        if (logged) {
            sceneLog_.push_back(command);
        }
    };
    const auto topicsList = [&s, &out] {
        const std::vector<sim::Topic>& topics = s.tavern().dialogue().topics();
        for (std::size_t i = 0; i < topics.size(); ++i) {
            out << "  " << (i + 1) << " " << topics[i].label << "\n";
        }
    };

    if (verb == "help") {
        out << "WORLD: step [N]  hour HH  play  pause  speed N\n"
               "LOOK: tab people|places|board|kit  filter [TEXT]  list  select N|person NAME|"
               "place NAME|errand ID|item ID|none  next  prev  inspect  zoom 0-3\n"
               "ACT: focus  talk  topics  topic N  leave  goto X Y [BAND]  face n|e|s|w  "
               "coin N  give ITEM [N]\n"
               "SHARE: shot PATH  pane NAME PATH  state  hash  save NAME  load NAME  echo TEXT  "
               "quit\n";
        return out.str();
    }
    if (verb == "step") {
        std::int32_t n = 1;
        if (w.size() > 1 && (!parseInt(w[1], n) || n < 1 || n > 1000000)) {
            return "STEP WANTS A COUNT FROM 1 TO 1000000.\n";
        }
        stepWorld(n);
        log("step " + std::to_string(n));
        out << "STEPPED " << n << ". " << clock(s.timeOfDay()) << ".\n";
        return out.str();
    }
    if (verb == "hour") {
        std::int32_t h = 0;
        if (w.size() < 2 || !parseInt(w[1], h) || h < 0 || h > 23) {
            return "HOUR WANTS 0 TO 23.\n";
        }
        s.skipToHour(h);
        log("hour " + std::to_string(h));
        out << "THE CLOCK READS " << clock(s.timeOfDay()) << ".\n";
        return out.str();
    }
    if (verb == "play") {
        playing_ = true;
        return "PLAYING.\n";
    }
    if (verb == "pause") {
        playing_ = false;
        return "PAUSED AT STEP " + std::to_string(stepsRun_) + ".\n";
    }
    if (verb == "speed") {
        std::int32_t n = 1;
        if (w.size() < 2 || !parseInt(w[1], n) || n < 1 || n > 600) {
            return "SPEED WANTS 1 TO 600 STEPS A FRAME.\n";
        }
        speed_ = n;
        return "SPEED " + std::to_string(n) + ".\n";
    }
    if (verb == "tab") {
        const std::string want = w.size() > 1 ? lower(w[1]) : "";
        for (const HierarchyTab tab : {HierarchyTab::People, HierarchyTab::Places,
                                       HierarchyTab::Board, HierarchyTab::Kit}) {
            if (hierarchyTabName(tab) == want) {
                tab_ = tab;
                cursor_ = 0;
                scroll_ = 0;
                return "TAB " + upper(want) + ", " + std::to_string(hierarchyRows().size()) +
                       " ROWS.\n";
            }
        }
        return "TAB WANTS PEOPLE, PLACES, BOARD OR KIT.\n";
    }
    if (verb == "filter") {
        filter_ = restAfter(line, 1);
        cursor_ = 0;
        scroll_ = 0;
        return "FILTER '" + upper(filter_) + "', " + std::to_string(hierarchyRows().size()) +
               " ROWS.\n";
    }
    if (verb == "list") {
        const std::vector<HierarchyRow> rows = hierarchyRows();
        for (std::size_t i = 0; i < rows.size() && i < 60; ++i) {
            out << (i + 1) << " " << rows[i].label << "  " << rows[i].note << "\n";
        }
        if (rows.size() > 60) {
            out << "... " << (rows.size() - 60) << " MORE. FILTER TO NARROW.\n";
        }
        return out.str();
    }
    if (verb == "select" || verb == "next" || verb == "prev") {
        bool ok = false;
        if (verb != "select") {
            const std::vector<HierarchyRow> rows = hierarchyRows();
            if (!rows.empty()) {
                cursor_ = (cursor_ + (verb == "next" ? 1 : -1) + static_cast<int>(rows.size())) %
                          static_cast<int>(rows.size());
                selection_ = rows[static_cast<std::size_t>(cursor_)].select;
                ok = true;
            }
        } else if (w.size() > 1) {
            const std::string kind = lower(w[1]);
            std::int32_t row = 0;
            if (kind == "none") {
                selection_ = Selection{};
                ok = true;
            } else if (parseInt(w[1], row)) {
                const std::vector<HierarchyRow> rows = hierarchyRows();
                if (row >= 1 && row <= static_cast<std::int32_t>(rows.size())) {
                    cursor_ = row - 1;
                    selection_ = rows[static_cast<std::size_t>(cursor_)].select;
                    ok = true;
                }
            } else if (kind == "person") {
                ok = selectByName(Selection::Kind::Person, restAfter(line, 2));
            } else if (kind == "place") {
                ok = selectByName(Selection::Kind::Place, restAfter(line, 2));
            } else if (kind == "errand") {
                ok = selectByName(Selection::Kind::Errand, restAfter(line, 2));
            } else if (kind == "item") {
                ok = selectByName(Selection::Kind::Item, restAfter(line, 2));
            }
        }
        if (!ok) {
            return "NOTHING TO SELECT THERE.\n";
        }
        // Logged RESOLVED, so a replay selects the same thing whatever the
        // list looked like when it was typed.
        static constexpr std::string_view kKinds[] = {"none", "person", "place", "errand", "item"};
        const std::string resolved =
            selection_.kind == Selection::Kind::None
                ? "select none"
                : "select " + std::string(kKinds[static_cast<int>(selection_.kind)]) + " #" +
                      std::to_string(selection_.id);
        if (selection_.kind == Selection::Kind::Errand || selection_.kind == Selection::Kind::Item) {
            log("select " + std::string(kKinds[static_cast<int>(selection_.kind)]) + " " +
                (selection_.kind == Selection::Kind::Item
                     ? s.tavern().items().at(selection_.id)->id
                     : std::to_string(selection_.id)));
        } else {
            log(resolved);
        }
        const std::vector<std::string> lines = inspectorLines();
        out << "SELECTED: " << (lines.empty() ? std::string("?") : lines.front()) << "\n";
        return out.str();
    }
    if (verb == "inspect") {
        for (const std::string& each : inspectorLines()) {
            out << each << "\n";
        }
        return out.str();
    }
    if (verb == "zoom") {
        std::int32_t z = 0;
        if (w.size() < 2 || !parseInt(w[1], z) || z < 0 || z >= render::mapZoomSteps()) {
            return "ZOOM WANTS 0 TO " + std::to_string(render::mapZoomSteps() - 1) + ".\n";
        }
        zoom_ = z;
        return "ZOOM " + std::to_string(z) + ".\n";
    }
    if (verb == "focus") {
        // PUT THE PLAYER THERE: beside a person, facing them, on a tile the
        // talk key answers them from -- the scripted lines' own placement --
        // or on a place's sign. A world-changing act, so it is logged.
        std::int32_t personId = -1;
        if (selection_.kind == Selection::Kind::Person) {
            personId = selection_.id;
        } else if (selection_.kind == Selection::Kind::Errand) {
            if (const sim::RadiantObjective* row =
                    s.tavern().dialogue().radiant().find(selection_.id)) {
                personId = row->state == sim::RadiantState::Taken && !row->isFetch()
                               ? row->targetActorId
                               : row->giverActorId;
            }
        }
        if (personId >= 0) {
            const sim::WardActor* actor = s.people().byId(personId);
            if (actor == nullptr || !actor->visible()) {
                return "THEY ARE NOT ON THEIR FEET.\n";
            }
            const Spot at{actor->x, actor->y, actor->band};
            for (std::int32_t oy = -2; oy <= 2; ++oy) {
                for (std::int32_t ox = -2; ox <= 2; ++ox) {
                    if ((ox == 0 && oy == 0) || !s.tiles().standable(at.x + ox, at.y + oy, at.band)) {
                        continue;
                    }
                    s.placeBodyAt(at.x + ox, at.y + oy, at.band);
                    const std::int32_t dx = -ox;
                    const std::int32_t dy = -oy;
                    s.body().setYaw(std::abs(dx) >= std::abs(dy)
                                        ? (dx > 0 ? sim::kFacingEast : sim::kFacingWest)
                                        : (dy > 0 ? sim::kFacingSouth : sim::kFacingNorth));
                    s.stepMany(sim::MoveInput{}, 1);
                    ++stepsRun_;
                    const sim::WardActor* answers = s.wardAddresseeNow();
                    if (answers != nullptr && answers->id == personId) {
                        log("focus");
                        return "BESIDE " + upper(personName(s, personId)) + ". E TALKS TO THEM.\n";
                    }
                }
            }
            log("focus");
            return "BESIDE " + upper(personName(s, personId)) +
                   ", BUT SOMEBODY NEARER ANSWERS E.\n";
        }
        if (selection_.kind == Selection::Kind::Place) {
            const render::MapPlace& place =
                render::mapPlaces()[static_cast<std::size_t>(selection_.id)];
            const auto ax = static_cast<std::int32_t>(std::floor(place.anchorX));
            const auto ay = static_cast<std::int32_t>(std::floor(place.anchorY));
            for (std::int32_t r = 0; r <= 3; ++r) {
                for (std::int32_t oy = -r; oy <= r; ++oy) {
                    for (std::int32_t ox = -r; ox <= r; ++ox) {
                        if (s.tiles().standable(ax + ox, ay + oy, place.band)) {
                            s.placeBodyAt(ax + ox, ay + oy, place.band);
                            log("focus");
                            return "AT " + upper(place.name) + ".\n";
                        }
                    }
                }
            }
            return "NOWHERE TO STAND AT " + upper(place.name) + ".\n";
        }
        return "SELECT A PERSON, A PLACE OR AN ERRAND FIRST.\n";
    }
    if (verb == "talk") {
        s.interact();
        log("talk");
        if (!s.talking()) {
            return "NOBODY ANSWERS.\n";
        }
        out << upper(s.tavern().dialogue().speaker().name) << ": "
            << s.tavern().dialogue().lastLine() << "\n";
        topicsList();
        return out.str();
    }
    if (verb == "topics") {
        if (!s.talking()) {
            return "NOT TALKING TO ANYBODY.\n";
        }
        topicsList();
        return out.str();
    }
    if (verb == "topic") {
        std::int32_t n = 0;
        if (!s.talking() || w.size() < 2 || !parseInt(w[1], n) || n < 1 ||
            n > static_cast<std::int32_t>(s.tavern().dialogue().topics().size())) {
            return "TOPIC WANTS A NUMBER FROM THE LIST.\n";
        }
        s.chooseTopic(static_cast<std::size_t>(n - 1));
        log("topic " + std::to_string(n));
        out << s.lastMessage() << "\n";
        if (s.talking()) {
            topicsList();
        }
        return out.str();
    }
    if (verb == "leave") {
        s.closeConversation();
        log("leave");
        return "THE TALK IS OVER.\n";
    }
    if (verb == "goto") {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t band = s.body().band();
        if (w.size() < 3 || !parseInt(w[1], x) || !parseInt(w[2], y) ||
            (w.size() > 3 && !parseInt(w[3], band))) {
            return "GOTO WANTS X Y [BAND].\n";
        }
        if (!s.tiles().standable(x, y, band)) {
            return "NOBODY CAN STAND THERE.\n";
        }
        s.placeBodyAt(x, y, band);
        log("goto " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(band));
        return "AT " + std::to_string(x) + "," + std::to_string(y) + ".\n";
    }
    if (verb == "face") {
        const std::string way = w.size() > 1 ? lower(w[1]) : "";
        const sim::Angle yaw = way == "n"   ? sim::kFacingNorth
                               : way == "e" ? sim::kFacingEast
                               : way == "s" ? sim::kFacingSouth
                               : way == "w" ? sim::kFacingWest
                                            : -1;
        if (yaw < 0) {
            return "FACE WANTS N, E, S OR W.\n";
        }
        s.body().setYaw(yaw);
        log("face " + way);
        return "FACING " + upper(way) + ".\n";
    }
    if (verb == "coin") {
        std::int32_t n = 0;
        if (w.size() < 2 || !parseInt(w[1], n) || n < 0) {
            return "COIN WANTS A PURSE.\n";
        }
        s.tavern().setPlayerCoin(n);
        log("coin " + std::to_string(n));
        return "THE PURSE HOLDS " + std::to_string(n) + "C.\n";
    }
    if (verb == "give") {
        std::int32_t n = 1;
        if (w.size() < 2 || (w.size() > 2 && (!parseInt(w[2], n) || n < 1 || n > 99))) {
            return "GIVE WANTS AN ITEM ID AND A COUNT.\n";
        }
        const std::string id = lower(w[1]);
        if (!s.tavern().giveItem(id, n)) {
            return "IT WILL NOT GO ON YOU: " + upper(id) + ".\n";
        }
        log("give " + id + " " + std::to_string(n));
        return "GIVEN " + std::to_string(n) + " " + upper(id) + ".\n";
    }
    if (verb == "shot" || verb == "pane") {
        const std::size_t pathAt = verb == "shot" ? 1 : 2;
        if (w.size() <= pathAt) {
            return verb == "shot" ? "SHOT WANTS A PATH.\n" : "PANE WANTS A NAME AND A PATH.\n";
        }
        Framebuffer frame(1, 1);
        if (verb == "shot") {
            draw(frame);
        } else {
            frame = drawPane(lower(w[1]));
        }
        if (frame.width() <= 1) {
            return "NO PANE CALLED " + upper(w[1]) + ".\n";
        }
        const std::filesystem::path path = restAfter(line, static_cast<int>(pathAt));
        std::error_code error;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), error);
        }
        if (!render::writePng(frame, path)) {
            return "COULD NOT WRITE " + path.string() + ".\n";
        }
        return "WROTE " + path.string() + ".\n";
    }
    if (verb == "state") {
        return stateJson() + "\n";
    }
    if (verb == "hash") {
        return "WORLD HASH " + hex32(s.simHash() >> 32) + hex32(s.simHash()) + " AT STEP " +
               std::to_string(stepsRun_) + ".\n";
    }
    if (verb == "save") {
        const std::string name = restAfter(line, 1);
        if (name.empty() || name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos || name.find("..") != std::string::npos) {
            return "SAVE WANTS A PLAIN NAME.\n";
        }
        std::error_code error;
        std::filesystem::create_directories(sceneDir_, error);
        const std::filesystem::path path = sceneDir_ / (name + ".wbs");
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            return "COULD NOT WRITE " + path.string() + ".\n";
        }
        file << "# granadad workbench scene -- replayed onto a fresh world by `load " << name
             << "`\n# boot: time " << config_.timeOfDay << " world " << config_.world << "\n";
        for (const std::string& each : sceneLog_) {
            file << each << "\n";
        }
        return "SAVED " + std::to_string(sceneLog_.size()) + " COMMANDS TO " + path.string() + ".\n";
    }
    if (verb == "load") {
        const std::string name = restAfter(line, 1);
        std::filesystem::path path = name;
        if (!std::filesystem::exists(path)) {
            path = sceneDir_ / (name + ".wbs");
        }
        if (!std::filesystem::exists(path)) {
            return "NO SCENE CALLED " + upper(name) + ".\n";
        }
        reboot();
        const std::string replayed = runFile(path);
        out << "LOADED " << path.string() << " ONTO A FRESH WORLD: " << sceneLog_.size()
            << " COMMANDS, STEP " << stepsRun_ << ".\n";
        (void)replayed;
        return out.str();
    }
    if (verb == "echo") {
        return restAfter(line, 1) + "\n";
    }
    if (verb == "quit") {
        quit_ = true;
        return "GOODBYE.\n";
    }
    return "NO COMMAND CALLED " + upper(verb) + ". TYPE HELP.\n";
}

std::string Workbench::stateJson() const {
    const render::Session& s = *session_;
    std::ostringstream out;
    out << "{\"day\":" << s.tavern().dialogue().radiant().day() << ",\"clock\":\""
        << clock(s.timeOfDay()) << "\",\"steps\":" << stepsRun_
        << ",\"playing\":" << (playing_ ? "true" : "false") << ",\"tab\":\""
        << hierarchyTabName(tab_) << "\",\"selection\":{\"kind\":"
        << static_cast<int>(selection_.kind) << ",\"id\":" << selection_.id
        << "},\"player\":{\"x\":" << s.body().tileX() << ",\"y\":" << s.body().tileY()
        << ",\"band\":" << s.body().band() << ",\"place\":\"" << jsonEscape(s.placeLabel())
        << "\",\"coin\":" << s.tavern().playerCoin() << ",\"talking\":"
        << (s.talking() ? "true" : "false") << "},\"inspector\":[";
    const std::vector<std::string> lines = inspectorLines();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out << (i == 0 ? "" : ",") << "\"" << jsonEscape(lines[i]) << "\"";
    }
    out << "],\"lastMessage\":\"" << jsonEscape(s.lastMessage()) << "\"}";
    return out.str();
}

// ---------------------------------------------------------------------------
// the mouse and the command line
// ---------------------------------------------------------------------------

std::string Workbench::click(int x, int y) {
    const Panes panes = workbenchPanes();
    const auto inside = [x, y](const PanelRect& r) {
        return x >= r.x && x < r.right() && y >= r.y && y < r.bottom();
    };
    if (inside(panes.hierarchy)) {
        const int row = (y - panes.hierarchy.y - kPad) / kCellH;
        if (row == 1) {
            // The tab row: four words, eight cells each.
            const int col = (x - panes.hierarchy.x - kPad) / kCellW;
            static constexpr HierarchyTab kTabs[] = {HierarchyTab::People, HierarchyTab::Places,
                                                     HierarchyTab::Board, HierarchyTab::Kit};
            const int which = col / 8;
            if (which >= 0 && which < 4) {
                return run("tab " + std::string(hierarchyTabName(kTabs[which])));
            }
            return {};
        }
        if (row >= 3) {
            const int index = scroll_ + (row - 3);
            if (index >= 0 && index < static_cast<int>(hierarchyRows().size())) {
                return run("select " + std::to_string(index + 1));
            }
        }
        return {};
    }
    if (inside(panes.scene)) {
        const render::Session& s = *session_;
        const render::MapBounds bounds = render::mapContentBounds(s.tiles());
        const int scale = render::mapZoomScale(zoom_, panes.scene, bounds);
        float cx = static_cast<float>(s.body().tileX());
        float cy = static_cast<float>(s.body().tileY());
        if (selection_.kind == Selection::Kind::Person) {
            if (const sim::WardActor* actor = s.people().byId(selection_.id)) {
                cx = static_cast<float>(actor->x);
                cy = static_cast<float>(actor->y);
            }
        }
        const render::MapViewport view = render::mapViewport(panes.scene, bounds, scale, cx, cy);
        const std::int32_t tx = view.tileOfPx(x);
        const std::int32_t ty = view.tileOfPy(y);
        const sim::WardActor* nearest = nullptr;
        std::int32_t best = 3;
        for (const sim::WardActor& actor : s.people().actors()) {
            if (!actor.visible()) {
                continue;
            }
            const std::int32_t d = std::max(std::abs(actor.x - tx), std::abs(actor.y - ty));
            if (d < best) {
                best = d;
                nearest = &actor;
            }
        }
        if (nearest != nullptr) {
            return run("select person #" + std::to_string(nearest->id));
        }
        const int place = render::mapPlaceUnder(tx, ty);
        if (place >= 0) {
            return run("select place #" + std::to_string(place));
        }
    }
    return {};
}

void Workbench::wheel(int x, int y, int notches) {
    const Panes panes = workbenchPanes();
    if (x >= panes.hierarchy.x && x < panes.hierarchy.right() && y >= panes.hierarchy.y &&
        y < panes.hierarchy.bottom()) {
        const int count = static_cast<int>(hierarchyRows().size());
        const int visible = std::max(1, rowsIn(panes.hierarchy) - 3);
        scroll_ = std::clamp(scroll_ - notches * 3, 0, std::max(0, count - visible));
        return;
    }
    if (x >= panes.scene.x && x < panes.scene.right() && y >= panes.scene.y &&
        y < panes.scene.bottom()) {
        zoom_ = std::clamp(zoom_ + notches, 0, render::mapZoomSteps() - 1);
    }
}

void Workbench::typeChar(char c) {
    if (c >= 32 && c < 127 && commandLine_.size() < 200) {
        commandLine_.push_back(c);
    }
}

void Workbench::backspace() {
    if (!commandLine_.empty()) {
        commandLine_.pop_back();
    }
}

std::string Workbench::submit() {
    const std::string line = commandLine_;
    commandLine_.clear();
    if (restAfter(line, 0).empty()) {
        return {};
    }
    return run(line);
}

// ---------------------------------------------------------------------------
// the picture
// ---------------------------------------------------------------------------

void Workbench::draw(Framebuffer& target) const {
    if (target.width() != kFrameWidth || target.height() != kFrameHeight) {
        target = Framebuffer(kFrameWidth, kFrameHeight);
    }
    target.clear(ink().ground);
    const Panes panes = workbenchPanes();
    drawToolbar(target, panes.toolbar);
    drawHierarchy(target, panes.hierarchy);
    drawScene(target, panes.scene);
    drawGame(target, panes.game);
    drawInspector(target, panes.inspector);
    drawConsole(target, panes.console);
    drawCommand(target, panes.command);
}

Framebuffer Workbench::drawPane(std::string_view pane) const {
    const Panes panes = workbenchPanes();
    const PanelRect* rect = pane == "toolbar"     ? &panes.toolbar
                            : pane == "hierarchy" ? &panes.hierarchy
                            : pane == "scene"     ? &panes.scene
                            : pane == "game"      ? &panes.game
                            : pane == "inspector" ? &panes.inspector
                            : pane == "console"   ? &panes.console
                                                  : nullptr;
    if (rect == nullptr) {
        return Framebuffer(1, 1);
    }
    Framebuffer whole(kFrameWidth, kFrameHeight);
    draw(whole);
    Framebuffer out(rect->w, rect->h);
    for (int y = 0; y < rect->h; ++y) {
        for (int x = 0; x < rect->w; ++x) {
            out.pixels()[out.index(x, y)] = whole.pixels()[whole.index(rect->x + x, rect->y + y)];
        }
    }
    return out;
}

void Workbench::drawToolbar(Framebuffer& target, const PanelRect& pane) const {
    target.fillRect(pane.x, pane.y, pane.w, pane.h, ink().ground, 1.0F);
    target.fillRect(pane.x, pane.bottom() - 1, pane.w, 1, ink().rule, 1.0F);
    const render::Session& s = *session_;
    const std::string title = " GRANADAD WORKBENCH ";
    target.fillRect(pane.x + kPad, pane.y + kPad - 1, static_cast<int>(title.size()) * kCellW,
                    kCellH, ink().accent, 1.0F);
    text(target, pane, 0, 0, title, ink().knockout);
    std::string status = "DAY " + std::to_string(s.tavern().dialogue().radiant().day()) + "  " +
                         clock(s.timeOfDay()) + "  " + (playing_ ? "PLAYING" : "PAUSED");
    if (playing_ && speed_ > 1) {
        status += " X" + std::to_string(speed_);
    }
    status += "  STEP " + std::to_string(stepsRun_) + "  " + upper(s.placeLabel());
    text(target, pane, static_cast<int>(title.size()) + 1, 0, status, ink().prose);
    const std::vector<std::string> lines = inspectorLines();
    const std::string sel =
        selection_.kind == Selection::Kind::None || lines.empty() ? "NOTHING SELECTED" : lines.front();
    textRight(target, pane, 0, sel, ink().dim);
}

void Workbench::drawHierarchy(Framebuffer& target, const PanelRect& pane) const {
    chrome(target, pane, "hierarchy");
    const std::vector<HierarchyRow> rows = hierarchyRows();
    textRight(target, pane, 0, std::to_string(rows.size()) + " ROWS", ink().dim);
    static constexpr HierarchyTab kTabs[] = {HierarchyTab::People, HierarchyTab::Places,
                                             HierarchyTab::Board, HierarchyTab::Kit};
    for (int i = 0; i < 4; ++i) {
        const std::string word = upper(hierarchyTabName(kTabs[i]));
        if (kTabs[i] == tab_) {
            target.fillRect(pane.x + kPad + i * 8 * kCellW, pane.y + kPad + kCellH - 1,
                            static_cast<int>(word.size()) * kCellW, kCellH, ink().key, 1.0F);
            text(target, pane, i * 8, 1, word, ink().knockout);
        } else {
            text(target, pane, i * 8, 1, word, ink().key);
        }
    }
    text(target, pane, 0, 2, "FILTER: " + (filter_.empty() ? std::string("--") : filter_),
         ink().dim);
    const int visible = std::max(0, rowsIn(pane) - 3);
    for (int i = 0; i < visible; ++i) {
        const int index = scroll_ + i;
        if (index >= static_cast<int>(rows.size())) {
            break;
        }
        const HierarchyRow& row = rows[static_cast<std::size_t>(index)];
        const bool selected = row.select.kind == selection_.kind && row.select.id == selection_.id;
        if (selected) {
            rowFill(target, pane, 3 + i, ink().accent, 0.28F);
        }
        const int noteCols = static_cast<int>(row.note.size());
        const int labelCols = std::max(1, colsIn(pane) - noteCols - 1);
        text(target, pane, 0, 3 + i, row.label.substr(0, static_cast<std::size_t>(labelCols)),
             selected ? ink().accent : ink().prose);
        textRight(target, pane, 3 + i, row.note, ink().dim);
    }
}

void Workbench::drawScene(Framebuffer& target, const PanelRect& pane) const {
    const render::Session& s = *session_;
    target.fillRect(pane.x, pane.y, pane.w, pane.h, Rgb{0.02F, 0.02F, 0.03F}, 1.0F);
    const render::MapBounds bounds = render::mapContentBounds(s.tiles());
    const int scale = render::mapZoomScale(zoom_, pane, bounds);
    // The plan is drawn at the PLAYER's band, the ward map's own rule, unless
    // a person is selected -- then theirs, so a body up the slope is seen on
    // its own ground.
    std::int32_t band = s.body().band();
    float cx = static_cast<float>(s.body().tileX());
    float cy = static_cast<float>(s.body().tileY());
    const sim::WardActor* picked = nullptr;
    if (selection_.kind == Selection::Kind::Person) {
        picked = s.people().byId(selection_.id);
        if (picked != nullptr) {
            band = picked->band;
            cx = static_cast<float>(picked->x);
            cy = static_cast<float>(picked->y);
        }
    } else if (selection_.kind == Selection::Kind::Place) {
        const render::MapPlace& place = render::mapPlaces()[static_cast<std::size_t>(selection_.id)];
        band = place.band;
        cx = place.anchorX;
        cy = place.anchorY;
    }
    const render::MapViewport view = render::mapViewport(pane, bounds, scale, cx, cy);
    const std::int32_t tx0 = std::max(bounds.minX, view.tileOfPx(pane.x));
    const std::int32_t tx1 = std::min(bounds.maxX, view.tileOfPx(pane.right() - 1));
    const std::int32_t ty0 = std::max(bounds.minY, view.tileOfPy(pane.y));
    const std::int32_t ty1 = std::min(bounds.maxY, view.tileOfPy(pane.bottom() - 1));
    for (std::int32_t ty = ty0; ty <= ty1; ++ty) {
        for (std::int32_t tx = tx0; tx <= tx1; ++tx) {
            Rgb tone{0.02F, 0.02F, 0.03F};
            switch (render::mapCellAt(s.tiles(), tx, ty, band)) {
                case render::MapCell::Wall:
                    tone = Rgb{0.30F, 0.27F, 0.22F};
                    break;
                case render::MapCell::Floor:
                    tone = Rgb{0.56F, 0.51F, 0.41F};
                    break;
                case render::MapCell::LowerFloor:
                    tone = Rgb{0.33F, 0.31F, 0.26F};
                    break;
                case render::MapCell::Water:
                    tone = Rgb{0.16F, 0.28F, 0.38F};
                    break;
                case render::MapCell::Void:
                default:
                    continue;
            }
            const int px = view.pxOfX(static_cast<float>(tx));
            const int py = view.pxOfY(static_cast<float>(ty));
            const int x0 = std::max(px, pane.x);
            const int y0 = std::max(py, pane.y);
            const int x1 = std::min(px + scale, pane.right());
            const int y1 = std::min(py + scale, pane.bottom());
            if (x1 > x0 && y1 > y0) {
                target.fillRect(x0, y0, x1 - x0, y1 - y0, tone, 1.0F);
            }
        }
    }
    const auto dot = [&](float wx, float wy, int size, const Rgb& colour) {
        const int px = view.pxOfX(wx + 0.5F) - size / 2;
        const int py = view.pxOfY(wy + 0.5F) - size / 2;
        const int x0 = std::max(px, pane.x);
        const int y0 = std::max(py, pane.y);
        const int x1 = std::min(px + size, pane.right());
        const int y1 = std::min(py + size, pane.bottom());
        if (x1 > x0 && y1 > y0) {
            target.fillRect(x0, y0, x1 - x0, y1 - y0, colour, 1.0F);
        }
    };
    // THE SELECTED PLACE, outlined on its footprint.
    if (selection_.kind == Selection::Kind::Place) {
        const render::MapPlace& place = render::mapPlaces()[static_cast<std::size_t>(selection_.id)];
        const int px = view.pxOfX(static_cast<float>(place.x0));
        const int py = view.pxOfY(static_cast<float>(place.y0));
        const int w = (place.x1 - place.x0 + 1) * scale;
        const int h = (place.y1 - place.y0 + 1) * scale;
        if (px >= pane.x && py >= pane.y && px + w <= pane.right() && py + h <= pane.bottom()) {
            outline(target, px, py, w, h, ink().accent);
        }
    }
    // EVERY BODY ON THIS BAND, a dot in its trade's colour.
    const int size = std::max(2, scale);
    for (const sim::WardActor& actor : s.people().actors()) {
        if (actor.band != band) {
            continue;
        }
        const Rgb colour = actor.visible() ? typeColour(actor.type) : Rgb{0.70F, 0.15F, 0.12F};
        dot(static_cast<float>(actor.x), static_cast<float>(actor.y), size, colour);
    }
    // THE SELECTION, ringed.
    if (picked != nullptr) {
        const int ring = size + 6;
        const int px = view.pxOfX(static_cast<float>(picked->x) + 0.5F) - ring / 2;
        const int py = view.pxOfY(static_cast<float>(picked->y) + 0.5F) - ring / 2;
        if (px >= pane.x && py >= pane.y && px + ring <= pane.right() && py + ring <= pane.bottom()) {
            outline(target, px, py, ring, ring, Rgb{0.95F, 0.30F, 0.25F});
            outline(target, px + 1, py + 1, ring - 2, ring - 2, Rgb{0.95F, 0.30F, 0.25F});
        }
    }
    // THE PLAYER: a yellow body and a short line where they look.
    if (s.body().band() == band) {
        const float wx = static_cast<float>(s.body().x()) / 256.0F;
        const float wy = static_cast<float>(s.body().y()) / 256.0F;
        dot(wx - 0.5F, wy - 0.5F, size + 2, Rgb{0.98F, 0.86F, 0.20F});
        const float fx = static_cast<float>(sim::forward_x_q16(s.body().yaw())) / 65536.0F;
        const float fy = static_cast<float>(sim::forward_y_q16(s.body().yaw())) / 65536.0F;
        for (int i = 1; i <= 4; ++i) {
            dot(wx - 0.5F + fx * static_cast<float>(i) * 0.6F,
                wy - 0.5F + fy * static_cast<float>(i) * 0.6F, std::max(1, size / 2),
                Rgb{0.98F, 0.86F, 0.20F});
        }
    }
    outline(target, pane.x, pane.y, pane.w, pane.h, ink().rule);
    const std::string label = " SCENE  BAND " + std::to_string(band) + "  ZOOM " +
                              std::to_string(zoom_) + " ";
    target.fillRect(pane.x + kPad, pane.y + kPad - 1, static_cast<int>(label.size()) * kCellW,
                    kCellH, ink().accent, 1.0F);
    text(target, pane, 0, 0, label, ink().knockout);
}

void Workbench::drawGame(Framebuffer& target, const PanelRect& pane) const {
    const render::Session& s = *session_;
    Framebuffer frame(s.config().width, s.config().height);
    (void)s.drawFrame(frame);
    const int w = std::min(pane.w, frame.width());
    const int h = std::min(pane.h, frame.height());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            target.pixels()[target.index(pane.x + x, pane.y + y)] = frame.pixels()[frame.index(x, y)];
        }
    }
    outline(target, pane.x, pane.y, pane.w, pane.h, ink().rule);
    const std::string label = " GAME ";
    target.fillRect(pane.x + kPad, pane.y + kPad - 1, static_cast<int>(label.size()) * kCellW,
                    kCellH, ink().accent, 1.0F);
    text(target, pane, 0, 0, label, ink().knockout);
}

void Workbench::drawInspector(Framebuffer& target, const PanelRect& pane) const {
    chrome(target, pane, "inspector");
    const std::vector<std::string> lines = inspectorLines();
    const int rows = rowsIn(pane) - 1;
    for (int i = 0; i < rows && i < static_cast<int>(lines.size()); ++i) {
        text(target, pane, 0, 1 + i, lines[static_cast<std::size_t>(i)],
             i == 0 ? ink().accent : ink().prose);
    }
}

void Workbench::drawConsole(Framebuffer& target, const PanelRect& pane) const {
    chrome(target, pane, "console");
    const int rows = rowsIn(pane) - 1;
    const int cols = colsIn(pane);
    // The newest lines that fit, each wrapped, oldest at the top.
    std::vector<std::pair<std::string, bool>> shown;
    for (auto it = console_.rbegin(); it != console_.rend() && static_cast<int>(shown.size()) < rows;
         ++it) {
        const std::vector<std::string> parts = wrap(*it, cols);
        for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
            shown.emplace_back(*part, it->rfind("> ", 0) == 0);
        }
    }
    std::reverse(shown.begin(), shown.end());
    const int skip = std::max(0, static_cast<int>(shown.size()) - rows);
    for (int i = 0; i + skip < static_cast<int>(shown.size()); ++i) {
        const auto& [line, command] = shown[static_cast<std::size_t>(i + skip)];
        text(target, pane, 0, 1 + i, line, command ? ink().key : ink().prose);
    }
}

void Workbench::drawCommand(Framebuffer& target, const PanelRect& pane) const {
    target.fillRect(pane.x, pane.y, pane.w, pane.h, ink().ground, 1.0F);
    target.fillRect(pane.x, pane.y, pane.w, 1, ink().rule, 1.0F);
    text(target, pane, 0, 0, "> " + commandLine_ + "_", ink().key);
    textRight(target, pane, 0, "SPACE PLAY  . STEP  ENTER RUN  HELP", ink().dim);
}

// ---------------------------------------------------------------------------
// the serve loop
// ---------------------------------------------------------------------------

int serveOnce(Workbench& bench, const std::filesystem::path& dir) {
    std::error_code error;
    const std::filesystem::path inbox = dir / "inbox";
    const std::filesystem::path outbox = dir / "outbox";
    const std::filesystem::path done = dir / "done";
    std::filesystem::create_directories(inbox, error);
    std::filesystem::create_directories(outbox, error);
    std::filesystem::create_directories(done, error);
    std::vector<std::filesystem::path> waiting;
    for (const auto& entry : std::filesystem::directory_iterator(inbox, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cmd") {
            waiting.push_back(entry.path());
        }
    }
    std::sort(waiting.begin(), waiting.end());
    for (const std::filesystem::path& file : waiting) {
        const std::string answer = bench.runFile(file);
        {
            std::ofstream out(outbox / (file.stem().string() + ".out"), std::ios::binary);
            out << answer;
        }
        {
            std::ofstream log(dir / "log.txt", std::ios::binary | std::ios::app);
            log << "== " << file.filename().string() << "\n" << answer;
        }
        std::filesystem::rename(file, done / file.filename(), error);
        if (error) {
            std::filesystem::remove(file, error);
        }
        if (bench.quitRequested()) {
            break;
        }
    }
    if (!waiting.empty()) {
        Framebuffer frame(kFrameWidth, kFrameHeight);
        bench.draw(frame);
        (void)render::writePng(frame, dir / "frame.png");
        std::ofstream state(dir / "state.json", std::ios::binary);
        state << bench.stateJson() << "\n";
    }
    return static_cast<int>(waiting.size());
}

}  // namespace granadad::workbench
