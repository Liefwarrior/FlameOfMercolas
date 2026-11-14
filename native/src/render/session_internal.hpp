#pragma once

// Shared by the session*.cpp translation units and nothing else: the helpers that two or more of
// the files Session is defined across use. `Session` is still ONE class, declared in
// granadad/render/session.hpp; its member functions are defined across session*.cpp, and this
// header carries what used to sit in the one file's anonymous namespaces. Never include it from a
// public header.

#include "granadad/render/session.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <utility>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render/signage_renderer.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/build_info.hpp"
#include "granadad/sim/docks.hpp"
// TIME-AND-TENURE BUILD: the compound sign footprints, for the one question
// the tier-3 correlation was ever going to answer -- whose ground is under
// the feet. See plotIndexUnderfoot().
#include "granadad/sim/docks_signs.hpp"
#include "granadad/sim/path_finder.hpp"
#include "granadad/sim/stealth.hpp"

namespace granadad::render {

namespace session_internal {

constexpr float kPi = 3.14159265358979323846F;

/// CASE WATCH. Holds Session::watchDepth_ raised for one recorded verb's own
/// body, so a verb a verb calls internally (interact()'s closing examine(),
/// a step-pump courier) records nothing -- the replay's own top-level call
/// will make those again. See setWatchRecorder in session.hpp.
struct WatchDepthGuard {
    int& depth;
    explicit WatchDepthGuard(int& d) noexcept : depth(d) { ++depth; }
    ~WatchDepthGuard() noexcept { --depth; }
    WatchDepthGuard(const WatchDepthGuard&) = delete;
    WatchDepthGuard& operator=(const WatchDepthGuard&) = delete;
};

/// TASK #82. "DAY 2 08:14" out of a Casebook::heardAt() value -- the
/// dateline a detective's log actually carries. NEGATIVE (never yet heard)
/// prints nothing rather than "DAY 0 00:00", which would read as a real
/// timestamp for an entry that has none.
///
/// DAY ONE, NOT DAY ZERO. sim::kSecondsPerDay divides the count exactly the
/// way the ward's own compound.cpp already does; +1 is the one translation
/// from a zero-based count to the ordinal a player reads ("the first day"),
/// applied here and nowhere inside the simulation, which never counts days
/// at all -- see Casebook::heardAt's own comment on why the raw value is
/// deliberately not day-and-hour already.
[[nodiscard]] inline std::string formatCaseDay(std::int64_t heardAtSeconds) {
    if (heardAtSeconds < 0) {
        return {};
    }
    const std::int64_t day = heardAtSeconds / sim::kSecondsPerDay + 1;
    const int hour = static_cast<int>((heardAtSeconds / 3600) % 24);
    const int minute = static_cast<int>((heardAtSeconds / 60) % 60);
    std::string out = "DAY " + std::to_string(day) + " ";
    if (hour < 10) {
        out += '0';
    }
    out += std::to_string(hour);
    out += ':';
    if (minute < 10) {
        out += '0';
    }
    out += std::to_string(minute);
    return out;
}

// ---------------------------------------------------------------------------
// the tiled Menu's four panels (plus the keys page and a live conversation)
// share one cursor/page navigation shape, wrapped or paged over a list whose
// length changes every call -- the SAME three or four lines were repeated
// once per list. Pulled out once, here: the per-list state still lives on
// Session (characterCursor_/mapCursor_/lettersCursor_/caseCursor_/
// topicCursor_ and their *Page_ twins), these just take it by reference so
// each call site stays one line instead of a paragraph.
// ---------------------------------------------------------------------------

/// Wraps `cursor` by `delta` into [0, count) and sets `page` to the page it
/// falls on -- moveTopicCursor()'s own shape, once per list this widget can
/// be navigating. `count` of zero parks both at zero rather than dividing by
/// it (unreachable for every list this build has today, since none of them
/// can ever be empty while its own overlay is open, but a caller has no way
/// to prove that from here).
inline void wrapCursorAndPage(int& cursor, int& page, int delta, int count) noexcept {
    if (count <= 0) {
        cursor = 0;
        page = 0;
        return;
    }
    cursor = ((cursor + delta) % count + count) % count;
    page = topicPageOf(cursor);
}

/// Steps `page` forward one and puts `cursor` on its first row --
/// nextTopicPage()'s own shape. A single page is a no-op: the row already
/// showing has nowhere else to turn to.
inline void advancePage(int& page, int& cursor, std::size_t total) noexcept {
    const int pages = topicPageCount(total);
    if (pages <= 1) {
        return;
    }
    page = (page + 1) % pages;
    cursor = std::min(static_cast<int>(total) - 1, page * kTopicPageSize);
}

/// SPELLS BUILD: moved up from the HUD-line helpers' own anonymous namespace
/// below, unchanged -- the Grimoire page and the quick bar (earlier in this
/// file) speak the same upper-case menu furniture the HUD lines do.
[[nodiscard]] inline std::string upperAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return out;
}

/// SHIP NOTE MOVE 3. The opening hint, GENERATED from the live bindings in
/// the live device's vocabulary -- which closes the S3 verification gap the
/// constructor stated out loud (a hand-written "TAB YOUR NOTES..." that went
/// stale the moment Menu was rebound off Tab).
///
/// UI-EA (LANE HUD): LIVE VERBS ONLY, AND FEWER OF THEM. The old band sold
/// "< > MORE PAGES" on the street, where PagePrev/PageNext do nothing at all
/// (flow map violation #11 -- a dead control advertised in the first ten
/// seconds of play). The tutor tier names the three verbs a stranger can
/// actually press where they stand: the notes, the map, the hand. Keycap
/// then verb, no filler words -- "J NOTES  M MAP  E USE" on the shipped
/// keyboard, the pad's own buttons the moment a pad speaks.
[[nodiscard]] inline std::string openingHintLine(const ControlSettings& controls, InputDevice device) {
    // NINE AND THE STICKS: the hand joins the band -- the owner's own
    // sentence is "press LMB and hit whoever is in front of me", so SWING is
    // one of the first things a stranger reads. The keyboard keeps M MAP (the
    // owner's own direct shortcut); a pad has no map button -- the map is a
    // page of NOTES there -- so the pad band does not advertise one.
    std::string out = std::string(promptLabel(controls, Action::Menu, device)) + " NOTES  ";
    if (device != InputDevice::Pad) {
        out += std::string(promptLabel(controls, Action::Map, device)) + " MAP  ";
    }
    out += std::string(promptLabel(controls, Action::Interact, device)) + " USE  " +
           std::string(promptLabel(controls, Action::Attack, device)) + " SWING";
    return out;
}

// ---------------------------------------------------------------------------
// FAST TRAVEL (TRAVEL lane) -- the ward map's second commit verb
// ---------------------------------------------------------------------------
/// The nearest standable cell to (x, y) on `band`, searched in a fixed
/// widening square so the answer is a pure function of the request -- the
/// demo's own arrival rule (demo.cpp's nearestStandable), restated here
/// because a travel lands a body the same way a Cut beat lands one: on real
/// ground, never inside geometry. False when nothing within eight tiles will
/// hold a body, which is a refusal and not a fudge.
[[nodiscard]] inline bool travelStandable(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                                   std::int32_t band, std::int32_t* outX, std::int32_t* outY) {
    if (tiles.standable(x, y, band)) {
        *outX = x;
        *outY = y;
        return true;
    }
    for (std::int32_t ring = 1; ring <= 8; ++ring) {
        for (std::int32_t dy = -ring; dy <= ring; ++dy) {
            for (std::int32_t dx = -ring; dx <= ring; ++dx) {
                if (dx != ring && dx != -ring && dy != ring && dy != -ring) {
                    continue;
                }
                if (tiles.standable(x + dx, y + dy, band)) {
                    *outX = x + dx;
                    *outY = y + dy;
                    return true;
                }
            }
        }
    }
    return false;
}

/// "02:14" -- the clock as the arrival line speaks it. The wait page says
/// whole hours because it deals in them; a walk lands mid-hour and says so.
[[nodiscard]] inline std::string travelClockText(int secondOfDay) {
    const int hour = ((secondOfDay / 3600) % 24 + 24) % 24;
    const int minute = (secondOfDay / 60) % 60;
    std::string text = (hour < 10 ? "0" : "") + std::to_string(hour) + ":";
    text += (minute < 10 ? "0" : "") + std::to_string(minute);
    return text;
}

}  // namespace session_internal

using namespace session_internal;

}  // namespace granadad::render
