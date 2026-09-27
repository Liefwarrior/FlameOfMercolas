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
//     Ropewynd locks its boxes twice these days. That is you.   what the ward says
//     There is nothing above that. Mind how you wear it.        only at the top
//     CRACKSMANSHIP RISES TO 2                          the same step's skill, dim
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
//   * EVERYTHING ELSE STANDS DOWN WHILE IT HOLDS. The alert row (the amber
//     line at the foot of the frame: a cracked box's take, a leap's length,
//     a watchman's word across the table) is news of its own, so it is not
//     cut the way the toast is -- its four-second hold would be lost whole
//     under a five-second plate -- and not deferred either, since a leap's
//     length said five seconds after the leap is a notice out of its
//     moment. It steps DOWN instead: to the plate's own register (the minor
//     scale, never a tier above the plate) and to half weight on the plate's
//     own fade, so the two cross rather than snap; and its clock STOPS under
//     the plate and resumes when the plate lifts, capped at two seconds, so
//     the take is shown whole and not five seconds late for long (a leap's
//     length is a MOMENT whose clock runs under the plate; a watchman's
//     demand is never capped at all). The voice
//     row (a passer-by's bark, a farewell) rides the same weight. The
//     announce slot (NEW LEAD, a named place crossed) holds its clock and
//     waits behind the plate. The plate is the one NOTICE on the frame at
//     full strength while it holds; the compass, the aim prompt and the
//     corner stacks are the HUD's furniture and stay. A bouncer's WARNING
//     rides the alert row too and dismisses the plate outright, so the one
//     line the player must not miss is never the one dimmed. AND NOTHING
//     COMES BACK UNTIL THE PLATE HAS LEFT: the stand-down follows the plate
//     ON SCREEN (its drawn fade), not its hold, so the lift is the plate's
//     too -- the rows and the slot return once it has gone, and a line said
//     with the rung finishes its capped two seconds in the small type and
//     never swells. The weight is on the INK: the rows' backing keeps the
//     row's own ease, so fine print over a lit wall is still print.
//   * A SKILL THAT LEVELS ON THE SAME STEP AS THE RUNG is the plate's own
//     dim last row ("SKYRUNNING RISES TO 2" under the prose), not a toast
//     after it: the same climb, told once, on the card. A skill that levels
//     later in the hold queues and starts only once the plate has lifted all
//     the way. The smallest window (320x180) has no row to spend between the
//     stat stack and the aim prompt and drops the tick row; the Character
//     tile has the level.
//   * THE WATCH'S WORD OUTRANKS IT. A street watchman's HALT, SHEATHE or
//     OFFENCE rides the alert row through say() and an ignored halt ends in
//     an arrest at reach: a demand on the row dismisses the plate the way a
//     bouncer's WARNING does, for as long as the row holds it. So does a
//     page's close tail: a rung earned under a page waits for the tail
//     rather than rising unseen beneath it.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/anim.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/panel.hpp"
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

/// The plate's lines. `head` is never empty for a real rise; `prose` is
/// empty exactly when no row is authored for the key (which test_rung_plate
/// pins never happens); `top` is empty below the top rung; `tick` is the
/// skill toast's own label ("SKYRUNNING RISES TO 2") when a skill levelled on
/// the same step as the rung, folded in by Session::stepPull, and empty
/// otherwise.
struct RungPlateText {
    std::string head;
    std::string prose;
    std::string top;
    std::string tick;
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

/// THE LIFT. The rise keeps the page ease (eight steps: a notice landing); the
/// lift takes three times that with twice the drift, because eight steps and
/// three units read as switched off after a five-second hold that opened on a
/// heavy bell -- the director's finding on the third pass.
inline constexpr std::int32_t kRungPlateLiftSteps = 3 * kPageEaseSteps;
inline constexpr int kRungPlateLiftUnits = 6;

/// The most of its own hold the alert row keeps once the plate lifts: two
/// seconds. A news line's clock stops under the plate (Session::step), so a
/// line said with the rung is shown whole after the lift; a moment's clock
/// runs under it, and whatever is still on the row when the plate goes --
/// news or moment, a demand excepted -- keeps this much and finishes small.
inline constexpr std::int32_t kRungPlateRowAfterSteps = 120;

/// THE PLATE'S REGISTER: the terminal register (panelMetric, the minor
/// scale) like every composed page -- except where the minor scale has
/// bottomed out at one under a HUD at two or more (640x360): the plate is a
/// notice in the play space, not a page, and a card in 6-pixel caps under a
/// 12-pixel compass was the smallest type on the screen at its own moment
/// (both critics, fourth pass). There, and only there, it takes the glance
/// scale: a 64-cell frame at scale 2 is exactly 640 pixels, so the widest
/// card still fits with its margin, and the band between the floor and the
/// aim prompt holds it with a row to spare. AND NEVER FEWER THAN 64 CELLS
/// ACROSS: a squarer window (1440x1080, 720x720) at the height's register
/// clipped the row's last beat with the cell drawer's mark, so the register
/// steps down a scale until the frame holds the 64 cells the sheet was
/// authored to -- the same move 640x360 makes in the other direction.
[[nodiscard]] PanelMetric rungPlateMetric(int width, int height) noexcept;

/// The plate's FLOOR, in rows of the glance scale: the lowest pixel of the
/// announce plate's slot under the compass ribbon at scale 1 (31). The
/// top-right stat stack at 320x180 reaches two rows lower when all four of
/// its rows are up, which is why the smallest window keeps five units
/// between its keyline and this floor (rungPlateBox). The seat never rises
/// past it, and the tick row is spent only where the frame has a row between
/// this floor and the aim prompt's fence to spend on it.
inline constexpr int kRungPlateFloorRows = 31;

/// The longest a prose row may be and still draw WHOLE on one line of the
/// plate at 320x180, the smallest window this game runs at: the frame's 64
/// cells less the two border cells, the two cells of gutter either side
/// (kRungPlateGutterCells) and a cell of margin either side. Pinned by
/// test_rung_plate against every authored row.
inline constexpr int kRungPlateRowCells = 56;

/// The air either side of the widest row, inside the pane's borders, in
/// cells: two, at every size, the composed pages' own gutter. One was the
/// critic's finding -- a row all but touching its keyline on a plate that
/// keeps a whole row of air between its head and its prose.
inline constexpr int kRungPlateGutterCells = 2;

/// The alert row's weight under a plate at full strength: half. The rule is
/// stated at the top of this file.
inline constexpr float kRungPlateAlertWeight = 0.5F;

/// The factor the alert row's own fade is multiplied by for a plate at this
/// fade (0 gone .. 1 fully up): 1 with no plate, kRungPlateAlertWeight under
/// one at full strength, the straight line between -- the row dims as the
/// plate rises and recovers as it lifts, on the plate's own ease.
[[nodiscard]] float alertWeightUnderRungPlate(float plateFade) noexcept;

// PINNED AS NUMBERS, not as the symbols the code spends. The auditor's
// finding on the third pass: every geometry pin used kRungPlateGutterCells by
// name, so the critic's "one cell" could come back with the suite green.
static_assert(kRungPlateGutterCells == 2, "the critic's finding was one cell; the fix is two");
static_assert(kRungPlateRowCells == 64 - 2 - 2 * kRungPlateGutterCells - 2,
              "56 glyphs + two gutters a side + two borders is the 64-cell frame less its margin");
static_assert(kRungPlateAlertWeight == 0.5F, "half weight is half");
static_assert(kRungPlateLiftSteps > kPageEaseSteps, "the lift is longer than the rise");

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
    /// The folded skill tick, dim, last. Drawn only where the frame has a row
    /// to spend on it (rungPlateBox knows).
    std::string_view tick;
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
