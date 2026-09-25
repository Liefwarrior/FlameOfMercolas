#pragma once

// THE RUNG PLATE -- the ward says so, the moment it starts calling you something.
//
// ---------------------------------------------------------------------------
// WHAT THIS IS
// ---------------------------------------------------------------------------
// The Legend (sim/legend.hpp) is five tracks the ward remembers you by, each
// with a rung title and a next threshold, and until this pass a rung rising
// was SILENT: the Character tile read differently the next time somebody
// opened it, and that was the whole of the moment. Oblivion's equivalent --
// the level-up prose, the "your skill has increased" cadence -- is a big part
// of why progression there FEELS like something, and it is the part this
// project's long game had none of.
//
// So a rung gets a plate. Not the skill toast (pull.hpp's top-left chime is a
// tick, and a rung is bigger than a tick): a short prose card in the terminal
// register, held longer, centred high in the play space where the eye already
// is, three lines at most --
//
//     THE WIRE  --  LIGHT FINGERS                       the track, the new title
//     The rope hands know your face now. That is not nothing.   what the ward says
//     There is no rung above that one. Mind how you wear it.    only at the top
//
// The head is legend.cpp's own vocabulary (the track name, the title -- the
// same twenty words the casebook page prints), and the prose is AUTHORED, one
// row per rung per track in content/raws/barks/legend_barks.json, keyed
// `legend.<track>.<rung>` with `legend.top` under a topped-out track. NO CODE
// WRITES A SPOKEN LINE: a key nobody authored draws an empty row, never a
// sentence a programmer chose.
//
// ---------------------------------------------------------------------------
// RENDER STATE, NEVER HASHED -- the pull lane's own discipline
// ---------------------------------------------------------------------------
// Legend holds no state and is a pure function of counters the sim already
// hashes, so the only honest way to know a rung ROSE is to remember what it
// was and look again: LegendRiseWatch diffs the five rungs once a step on the
// render side, exactly as SkillRiseWatch diffs the skill levels. Nothing here
// writes to the simulation, nothing here is encoded, and test_rung_plate.cpp
// proves the room's and the ward's digests are byte-identical with the plate
// up, drawn, dismissed and queued.
//
// ---------------------------------------------------------------------------
// THE RULES THE PLATE KEEPS (Session::stepRungPlate owns them; stated here)
// ---------------------------------------------------------------------------
//   * It shows on a FREE screen only. A page, a conversation, the court's
//     plates and custody, the death and rope ceremonies, and a bouncer's
//     WARNING all outrank it -- the alert row's own priority rule. A rise
//     under any of those QUEUES and shows when the way is clear; a plate that
//     is up when one of them takes the screen is dismissed, which is what
//     "dismissable by any page key" means in practice.
//   * Two rungs in one step, or a second while one is up: the second WAITS
//     its turn. Queued, never dropped.
//   * A skill toast and a rung plate, same step or not: THE PLATE WINS. A new
//     toast waits behind it (the toast's own queue holds while a plate is
//     up), and one already on screen when the plate lands goes dark rather
//     than share the corner -- a rung and its matching skill are sometimes
//     the same event said twice, and the plate has already told it.
//   * Its own cue, heavier than the toast's chime -- audio::SoundId::LegendRung,
//     the Kenney heavy bell -- played when the plate actually shows.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/sim/barks.hpp"
#include "granadad/sim/legend.hpp"

namespace granadad::render {

/// One rung taken on one track: what the ward called you and what it calls
/// you now. `from` and `to` are legend.hpp's rung numbers (0..kLegendRungs);
/// a rise can skip a rung (a case closing is thirty points at once) and says
/// so rather than inventing the one it skipped.
struct LegendRise {
    sim::LegendTrack track = sim::LegendTrack::Wire;
    std::int32_t from = 0;
    std::int32_t to = 0;
};

/// Diffs a Legend's five rungs between calls. The first call SEEDS and
/// reports nothing -- a session loaded with rungs already on the sheet has
/// not just earned them. RISES ONLY: a rung that falls (heat piling onto THE
/// LAW, a contract failed) is the sheet's business, and a fall followed by
/// the same rise again is reported again, honestly, because the ward is
/// calling you that again.
class LegendRiseWatch {
public:
    [[nodiscard]] std::vector<LegendRise> diff(const sim::Legend& legend);
    [[nodiscard]] bool seeded() const noexcept { return seeded_; }

private:
    std::array<std::int32_t, sim::kLegendTracks> rungs_{};
    bool seeded_ = false;
};

/// The word the bark keys spell a track with: wire, roofs, flame, trade, law.
[[nodiscard]] std::string_view legendTrackKey(sim::LegendTrack track) noexcept;

/// The track a `--rung=` word names, or false for a word that is none of the
/// five. The same five words legendTrackKey prints, case-folded.
[[nodiscard]] bool legendTrackFromKey(std::string_view word, sim::LegendTrack& out) noexcept;

/// "legend.wire.1" -- the authored prose row for a rung of a track.
[[nodiscard]] std::string legendRungKey(sim::LegendTrack track, std::int32_t rung);

/// The one row a topped-out track gets under its own.
inline constexpr std::string_view kLegendTopKey = "legend.top";

/// The plate's three lines. `head` is never empty for a real rise; `prose` is
/// empty exactly when no row is authored for the key (which test_rung_plate
/// pins never happens); `top` is empty below the top rung.
struct RungPlateText {
    std::string head;
    std::string prose;
    std::string top;
    [[nodiscard]] bool empty() const noexcept { return head.empty(); }
};

/// "THE WIRE  --  LIGHT FINGERS": the track's own name and the new title,
/// straight off legend.cpp's tables, two cells of air and the project's dash
/// between them.
[[nodiscard]] std::string rungPlateHead(const LegendRise& rise);

/// The whole plate for a rise, the prose read out of the authored sheet.
[[nodiscard]] RungPlateText rungPlateFor(const LegendRise& rise, const sim::BarkTables& barks);

/// How long the plate holds at full strength once it is up: five seconds,
/// twice the EVENT tier's kPlateHoldSteps, because it is three lines of prose
/// and not one name. Steps, never seconds (anim.hpp's own rule).
inline constexpr std::int32_t kRungPlateHoldSteps = 300;

/// The longest a prose row may be and still draw WHOLE on one line of the
/// plate at 320x180, the smallest window this game runs at: the frame's 64
/// cells less the two border cells, the two cells of padding and a cell of
/// margin either side. Pinned by test_rung_plate against every authored row.
inline constexpr int kRungPlateRowCells = 56;

/// Where the plate SEATS vertically, as a share of the spare height above and
/// below it, out of 100. A quarter: high, under the compass band, clear of
/// the reticle and the aim prompt that hangs off it at every pinned size.
inline constexpr int kRungPlateSeat = 25;

/// What drawRungPlate draws from. Session composes it from its own plate
/// state; a test hands one in built by hand.
struct RungPlateState {
    std::string_view head;
    std::string_view prose;
    std::string_view top;
    /// 0 (gone) .. 1 (full strength). The caller's own EasedToggle.
    float fade = 0.0F;
    /// Where the plate is in its own rise, signed: -1 fully below its seat
    /// (the instant of the rung), 0 seated, +1 above it (the end of the fade)
    /// -- the announce plates' contract, so the plate rises THROUGH its seat
    /// and is gone rather than sliding back the way it came.
    float drift = 0.0F;
};

/// The pane's outer rectangle at rest (drift 0), on a frame of this size, for
/// this text -- and whether it draws at all. Pure: what drawRungPlate lays
/// out, with no framebuffer, so a case can pin the geometry rather than read
/// it off pixels.
struct RungPlateBox {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool draws = false;
};
[[nodiscard]] RungPlateBox rungPlateBox(int width, int height, const RungPlateState& state);

/// The plate over a finished frame: a PanelFrame in the terminal register
/// (panel.hpp's own rules and edges), the head knocked out of an inverted fill
/// in the body accent, the prose in the prose ink, the top row dim. Sized to
/// its content, centred, seated high. Empty text or a zero fade draws nothing.
void drawRungPlate(Framebuffer& target, const RungPlateState& state);

}  // namespace granadad::render
