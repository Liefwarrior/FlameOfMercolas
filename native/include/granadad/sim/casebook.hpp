#pragma once

// THE BLOODLETTER TRAIL: the investigation spine of the demo.
//
// WHAT THE GAME IS ABOUT AND WHAT IT HAS NEVER HAD
//
// Nine sprints built a district that keeps its own hours, a room with sixteen
// people in it, guilds with ladders, crimes with a Watch, an economy that feeds
// itself for two years, a nemesis who climbs while you sleep and a burglary you
// can play. What none of it did was give the player the one thing the box says
// the game is: a hidden bloodletter and a trail to it. The Docks were a
// sandbox with no reason to be in them.
//
// So this is the reason. It is deliberately NOT a new subsystem competing with
// what exists -- it reads the world S1-S9 built and adds one verb.
//
// THE DESIGN LAW, TAKEN VERBATIM FROM THE GAZETTEER
//
// docs/design/DOCKS-GAZETTEER.md section 5.3: "People TELL the Wielder things.
// The investigation is never persuasion -- it is knowing WHERE to ask. No
// dialogue-skill checks exist; the gate is geographic and social-topological."
//
// That is why there is no roll anywhere in this file and no skill check
// anywhere in the trail. A lead is gated on ONE thing: having been told it
// exists. You cannot talk your way past a lead and you cannot fail one. What
// you can do is stand in the wrong ward asking the wrong people, which is what
// an investigation actually feels like.
//
// WHERE THE TRAIL LIVES
//
// content/raws/quests/casebook.json, which is a NEW authored file and not an
// edit to any existing one -- content/ is the owner's canon and read-only. Its
// words are the gazetteer's own section 5.4 and section 4.4; its coordinates
// are the tile coordinates of the MAP'S OWN authored `script_anchor` markers,
// which an earlier pass laid down and nothing has ever read:
// `clue_c1_mission_backroom`, `clue_c2_weighhouse_ledger`,
// `clue_c3_drowned_hold`, `clue_wrackhouse_salvage_anchor`,
// `clue_brann_grayledger_anchor`, `shrine_drowned_name_wall_anchor`,
// `strand_beaching_anchor` and their neighbours in
// tools/scripts/gen_docks_surface.py. Converted to global tiles by the one
// documented rule, which docks.hpp already states: local + 32, local z + 8.
//
// A case asserts every site is somewhere a body can actually stand in the baked
// world, so a coordinate that drifts is a red build and not a lead nobody can
// reach.
//
// DEAD ENDS ARE REAL AND THEY ARE THE POINT
//
// Two leads yield a clue that opens nothing. The Outfall grate is corroded shut
// FROM OUTSIDE -- so the sea, which is the obvious theory and the one the whole
// ward believes, is wrong. The King's Bond is where struck cargo is supposed to
// go and the struck line never came there. Both cost you a walk across the
// district and both are worth having: an investigation where every door opens
// onto the next door is a corridor.
//
// NO FLOATS. NO UNORDERED CONTAINERS. The state is a small vector of bytes and
// two integers, hashed with everything else, so the trail is inside the
// twin-run gate like the rest of the simulation.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

/// Where a lead is, in global tiles and a band -- the same coordinates
/// PlayerBody and TileQuery speak.
struct LeadSite {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
};

/// How close the body has to be standing to look at a lead, in tiles.
///
/// FOUR, and it is a measurement rather than a taste. The authored anchors sit
/// at a site's working heart -- the Mission's back room, the Weighhouse's
/// counter, the Drowned Hold's sagging floor -- and a shop in this ward is
/// seven by eight (gazetteer section 3.1). Four tiles is "inside the building
/// and near the thing"; two would make a player hunt for a pixel and eight
/// would let them read the Mission's flagstones from the street.
inline constexpr std::int32_t kLookRangeTiles = 4;

/// Where a lead stands in the player's own notes.
enum class LeadState : std::uint8_t {
    /// Not yet heard of. Not in the casebook and not on the map.
    Unheard = 0,
    /// Heard of, not yet looked at. This is the state that IS the game.
    Open = 1,
    /// Looked at, and it went nowhere. Kept in the book, struck through.
    Cold = 2,
    /// Looked at, and it opened something.
    Followed = 3,
};

[[nodiscard]] std::string_view leadStateName(LeadState state) noexcept;

/// One thing to go and look at. All of it authored; none of it computed.
struct Lead {
    std::string id;
    /// "MISSION OF THE FLAME" -- the place, in the words the sign uses.
    std::string place;
    /// "THE MISSION" -- the same place in a casebook row. The topic grid this
    /// borrows is three columns wide, so a long name arrives cut; the short one
    /// is AUTHORED rather than truncated at draw time, because a machine that
    /// picks where to cut a proper noun picks badly.
    std::string brief;
    /// "THE BODY, AND WHOEVER FOUND IT" -- what you are going there for.
    std::string what;
    /// notables.json id of whoever tells you, or empty for a lead that is a
    /// thing rather than a person.
    std::string who;
    /// The one line you get for standing there.
    std::string found;
    /// And the paragraph behind it, which the casebook shows when you open the
    /// entry. This is where a witness's own knowledge goes.
    std::string detail;
    /// Which leads this one puts in the book.
    std::vector<std::string> opens;
    LeadSite site;
    /// What learning this does to the ward's nerve.
    std::int32_t dread = 0;
    /// Open from the first minute of the game. Exactly one lead is.
    bool start = false;
    /// It yields a clue and opens nothing. Deliberate; see the file header.
    bool deadEnd = false;
    /// Reaching this one is the end of the surface trail.
    bool close = false;
};

/// A threshold at which the ward's own nerve is worth saying out loud.
struct DreadBand {
    std::int32_t at = 0;
    std::string label;
};

/// The authored file, loaded. NEVER throws and never refuses to boot: a missing
/// or malformed casebook.json leaves an empty trail and the game still runs,
/// which is the same contract every other raws loader in this build honours.
class CasebookRaws {
public:
    [[nodiscard]] static CasebookRaws load(const std::filesystem::path& contentDir);

    [[nodiscard]] bool loaded() const noexcept { return !leads_.empty(); }
    [[nodiscard]] const std::vector<Lead>& leads() const noexcept { return leads_; }
    [[nodiscard]] const std::vector<DreadBand>& dreadBands() const noexcept { return dread_; }
    [[nodiscard]] std::string_view title() const noexcept { return title_; }
    [[nodiscard]] std::string_view hook() const noexcept { return hook_; }
    [[nodiscard]] std::string_view close() const noexcept { return close_; }

    /// Index of a lead by id, or -1.
    [[nodiscard]] std::int32_t indexOf(std::string_view id) const noexcept;
    /// The band label for a dread score.
    [[nodiscard]] std::string_view dreadLabel(std::int32_t dread) const noexcept;

private:
    std::vector<Lead> leads_;
    std::vector<DreadBand> dread_;
    std::string title_;
    std::string hook_;
    std::string close_;
};

[[nodiscard]] std::filesystem::path casebookRawsPath(const std::filesystem::path& contentDir);

/// What one look produced.
struct LookResult {
    /// True when a lead was standing here and had not been read yet.
    bool found = false;
    /// Index into the raws' leads, or -1.
    std::int32_t lead = -1;
    /// What to put on the message line. Never empty.
    std::string line;
    /// How many leads this look put in the book.
    std::int32_t opened = 0;
};

/// THE PLAYER'S OWN NOTES. State, and nothing else: every word it prints comes
/// out of CasebookRaws.
class Casebook {
public:
    Casebook() = default;
    /// Binds to a set of raws and opens whichever lead is marked `start`.
    void begin(const CasebookRaws& raws);

    [[nodiscard]] bool active() const noexcept { return raws_ != nullptr; }
    [[nodiscard]] const CasebookRaws* raws() const noexcept { return raws_; }

    [[nodiscard]] LeadState state(std::int32_t lead) const noexcept;
    /// Every lead the player has heard of, in authored order. This is what the
    /// casebook surface lists and it is never sorted at draw time -- two runs
    /// that disagreed about the order of a menu would be two different games.
    [[nodiscard]] std::vector<std::int32_t> known() const;
    /// How many have been read, and how many of those went nowhere.
    [[nodiscard]] std::int32_t readCount() const noexcept;
    [[nodiscard]] std::int32_t coldCount() const noexcept;
    /// The ward's nerve, 0..100.
    [[nodiscard]] std::int32_t dread() const noexcept { return dread_; }
    /// True once the closing lead has been read.
    [[nodiscard]] bool closed() const noexcept { return closed_; }

    /// The first lead still waiting to be looked at, or -1. What the HUD's
    /// objective row names, so a player who puts the game down for a week is
    /// told where they were going.
    [[nodiscard]] std::int32_t nextOpen() const noexcept;

    /// LOOK AT WHAT IS HERE. The one verb. Answers what a body standing at
    /// (tileX, tileY, band) can see, opens whatever that opens, and never rolls
    /// anything.
    [[nodiscard]] LookResult look(std::int32_t tileX, std::int32_t tileY, std::int32_t band);

    /// Puts a lead in the book without looking at it -- what a clue's `opens`
    /// list does, exposed because the questline layer will want it too. Returns
    /// true when the lead was not already known.
    bool hear(std::int32_t lead);

    void hashInto(HashSink& sink) const;

private:
    const CasebookRaws* raws_ = nullptr;
    /// One LeadState per lead, in authored order. A vector and not a map, so
    /// there is nothing here whose iteration order could differ between two
    /// machines -- see the ban in ARCHITECTURE.md.
    std::vector<std::uint8_t> state_;
    std::int32_t dread_ = 0;
    bool closed_ = false;
};

}  // namespace granadad::sim
