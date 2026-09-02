#pragma once

// THE TERMINAL-PANEL VOCABULARY -- the drawing primitives every later UI phase
// composes its screens out of.
//
// docs/design/UI-REFERENCE-TERMINAL.md is the grammar, read off the owner's own
// reference frames, and it is BINDING. This file is that grammar as code: the
// `+~-~-` rules with their `+`/`◆` junctions, vertical edges that alternate
// `|` and `!` per row (interior dividers included -- that alternation is the
// texture and it is not optional), the breadcrumb/instruction header, the tab
// row with its inverted current tab and right-aligned resource readout, the
// numbered option list whose selection is an INVERTED FILL in the entity's own
// accent, the master/detail split, aligned key/value rows at a shared value
// column, the `•`/`◦` bullet hierarchy, and stippled panel grounds.
//
// ---------------------------------------------------------------------------
// FIXED LAYOUTS, NOT MODULAR. THIS WAS RULED ON.
// ---------------------------------------------------------------------------
// A Morrowind-style modular pane system -- several open at once, dragged to
// move and resize, the arrangement persisted -- was proposed and the owner
// ruled on it verbatim: "Alright well then dont make it modular" -- "but
// clean". So there is NO player-draggable pane system in here, no per-pane
// move or resize, no saved arrangement, and none of the persistence format,
// drag clamps or controller-resize scheme that would have needed. Every screen
// built on this file is a FIXED, DELIBERATELY COMPOSED layout, exactly like the
// reference frames, which are themselves fixed compositions.
//
// What IS in here is what "clean" turned out to mean, and it is a real
// requirement rather than a taste: ONE considered composition per screen, with
// ALIGNED EDGES and SHARED GUTTERS and a COMMON VALUE COLUMN for key/value
// rows, DELIBERATE EMPTINESS instead of cramming, and STABLE GEOMETRY that
// does not jump as a cursor moves between entries of different lengths.
// splitRows/splitColumns and PanelFrame exist so those four are the DEFAULT
// rather than something each screen has to remember; OptionListStyle::minRows
// exists so a pane holds its height when its content shrinks.
//
// ---------------------------------------------------------------------------
// WIDTH-AWARE FROM THE FIRST LINE -- FOR RESOLUTION, NEVER FOR DRAGGING
// ---------------------------------------------------------------------------
// The game runs at different window sizes and scales (--width, --height,
// --scale, and the 960x540 baseline docs/HUD-REAL-ESTATE.md measures at), so
// NOTHING in this file is authored at a fixed column count. Every primitive
// takes the rect it is drawing into and works out its own geometry: rules
// stretch, prose wraps, option lists pick their column count from the longest
// entry against the space available, and the value column is computed from the
// content rather than typed in. A layout responds to the WINDOW. It never
// responds to a player dragging a pane edge, because there is no such player
// and there is no such edge.
//
// The one number a caller has to get right is the METRIC, and panelMetric()
// picks it from the frame height for them.
//
// ---------------------------------------------------------------------------
// THE FONT IS NOT TOUCHED, AND THAT COST SOMETHING -- READ THIS
// ---------------------------------------------------------------------------
// The owner's brief puts the typeface out of scope in as many words: "Your
// flavor and texture is awesome, I love the font and all that." hud.cpp's
// kGlyphs table and drawText are therefore UNCHANGED by this pass, not one
// bit.
//
// The problem that leaves is that the grammar this file implements needs seven
// glyphs that 4x6 font has never had: `~` and `|` for the rules and the edges,
// `◆` for the heavier junction, and `•`/`◦` for the bullet hierarchy. Adding
// them to kGlyphs would have been editing the font.
//
// So the motif carries its OWN little glyph table in panel.cpp -- the five the
// font genuinely lacks (`~`, `|`, `◆`, `•`, `◦`), on the identical 4x6 cell at
// the identical 5-unit advance with the identical one-pixel drop shadow --
// and drawMotif() draws from it. The two the font DOES have, `!` and `+`, go
// straight through drawText, so there is exactly one of each in this build and
// they cannot drift apart.
// Nothing in hud.cpp changes, `isDrawableGlyph` still answers for the font and
// only the font (so test_copy.cpp's sweep over player-facing PROSE keeps
// meaning exactly what it meant), and the two draw interleaved in register
// because they share the cell. `|` and `!` differ by one pixel row on purpose:
// `!` is the font's own bang with its gap at row 4, `|` is the same column
// filled through. That one-pixel flicker down a border IS the texture the
// reference is made of.
//
// ---------------------------------------------------------------------------
// WHAT THIS FILE IS NOT
// ---------------------------------------------------------------------------
// It is not a widget toolkit and it holds no state. Every function is a pure
// function of its arguments plus the framebuffer it writes into, which is what
// makes a composed screen a testable artifact: planOptionList() answers "how
// many columns at this width" with no pixels involved at all, and a case can
// pin the re-columning without rendering anything.
//
// NO SIMULATION STATE. Floats are legal here for the same reason they are legal
// in framebuffer.hpp and nowhere else.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/framebuffer.hpp"

namespace granadad::render {

// ---------------------------------------------------------------------------
// the cell grid
// ---------------------------------------------------------------------------

/// The grid every composed screen is measured in: one glyph cell wide, one
/// text row tall. Five units and seven units at scale 1, which is the 4x6 font
/// plus its advance and plus the row of drop shadow under it -- the identical
/// arithmetic drawText and hud.cpp's rowHeight() already use, so text laid out
/// on this grid lands exactly where drawText puts it.
struct PanelMetric {
    int scale = 1;

    [[nodiscard]] int cellW() const noexcept { return 5 * scale; }
    [[nodiscard]] int cellH() const noexcept { return 7 * scale; }
    /// Whole cells that fit in a span of pixels. Never negative.
    [[nodiscard]] int cellsIn(int pixels) const noexcept;
    [[nodiscard]] int rowsIn(int pixels) const noexcept;
    [[nodiscard]] int widthOf(int cells) const noexcept { return cells * cellW(); }
    [[nodiscard]] int heightOf(int rows) const noexcept { return rows * cellH(); }
};

/// The metric a composed screen at this frame height draws its BODY at:
/// hudMinorScale, the register hud.hpp already reserves for "reference material
/// the player reads deliberately", which is what a panel full of rows is.
///
/// ONE METRIC PER SCREEN, and that is a deliberate narrowing of what
/// menu_view.cpp does. Titles here take their emphasis from INVERSION -- the
/// reference's own idiom, an accent fill with the text knocked out dark --
/// rather than from a second, larger glyph size, because two glyph sizes on one
/// surface means two cell grids, and two cell grids is how a column stops
/// lining up with the rule above it.
[[nodiscard]] PanelMetric panelMetric(int frameHeight) noexcept;

/// WHERE A PAGE THAT ENDED AFTER ITS CONTENT SITS IN THE WINDOW.
///
/// The pages that size to content (creation_page.cpp, casebook_page.cpp) learnt
/// to STOP at their content and were never told where to START, so they were
/// pinned to the top: at 640x360 THE DOOR was a 164px panel with two pixels
/// above it and a hundred and ninety-four of black below. That reads as a panel
/// that ran out rather than one that was placed.
///
/// This is the one place that decides, so there is one rule and not one per
/// page. It is NOT a true half: type sits low in a box that is geometrically
/// centred, and the correction is to seat it a little high --
/// `kPanelSeatAbove` of the spare above, the rest below. At 640x360 with a
/// 168px frame that is 86 above and 106 below rather than 96/96, which is the
/// difference between "centred" and "placed" and is a judgement made off
/// frames rather than off arithmetic (see docs/HUD-REAL-ESTATE.md).
///
/// A frame that already fills the window has a spare of two or three pixels and
/// comes out exactly where it always did -- this replaces the old
/// `(frameHeight - heightOf(rows)) / 2` on that path rather than sitting beside
/// it.
[[nodiscard]] int panelSeatY(int frameHeight, int panelHeight) noexcept;

/// The share of the leftover height that goes ABOVE a page that ended early,
/// out of 100. See panelSeatY.
inline constexpr int kPanelSeatAbove = 45;

/// WHERE A PAGE THAT MEASURED ITS WIDTH SITS ACROSS THE WINDOW -- the mirror of
/// panelSeatY(), the same judgement turned ninety degrees.
///
/// panelSeatY() exists because the pages that size their HEIGHT to content were
/// never told where to start; the identical defect existed sideways, one worse:
/// nothing sized the WIDTH at all, so every full-screen page drew 639px wide at
/// a 640px window whatever it held -- THE DOOR was five short rows running the
/// whole frame, a letterbox rather than a composition. The pages now measure
/// their widest row (see panelMeasureCells) and this is the one rule that
/// decides where the measured panel sits.
///
/// NOT A TRUE HALF, deliberately: `kPanelSeatLeft` of the spare goes LEFT of
/// the panel and the rest right, slightly left-weighted to mirror the vertical
/// seat's slightly-high judgement -- reading starts at the left edge, and a
/// block of ragged-right type centred by arithmetic reads as drifting toward
/// the trailing margin. Same number as the vertical share on purpose: one
/// judgement, two axes.
///
/// A panel that measured out at the whole window has a spare of a few pixels
/// and comes out exactly where the old centring put it.
[[nodiscard]] int panelSeatX(int frameWidth, int panelWidth) noexcept;

/// The share of the leftover width that goes LEFT of a measured panel, out of
/// 100. See panelSeatX.
inline constexpr int kPanelSeatLeft = 45;

/// THE WIDTH A COMPOSITION THAT MEASURED ITS ROWS ACTUALLY TAKES, in grid
/// cells -- the mirror of the height rule the pages already keep ("end after
/// your content, then be seated").
///
/// `wantCells` is the widest row the page will actually draw -- header, tab
/// row, list rows at their natural column count, the master/detail body via
/// masterDetailCellsFor -- plus its border cells. This clamps it twice:
///
///   * NEVER PAST THE WINDOW. A page whose content earns the whole frame gets
///     the whole frame BY CONSTRUCTION -- there is no full-width special case,
///     the clamp simply lands there (which is how the ward map and the
///     controls page stay exactly what they were).
///   * NEVER UNDER kPanelMeasureFloorCells. A full-screen page narrower than
///     that reads as a toast that wandered to mid-screen, and prose wrapped
///     into it stops being a paragraph.
///
/// A window too small to hold even the floor gets the window -- the floor is a
/// composition judgement, not a reason to draw past the frame.
[[nodiscard]] int panelMeasureCells(int windowCells, int wantCells) noexcept;

/// THE FLOOR A MEASURED PAGE NEVER GOES UNDER, in cells, border included.
///
/// 49 is not taste, it is this vocabulary's own narrowest legal master/detail
/// composition read back out of splitMasterDetail: an 18-cell master (the
/// floor the on-screen keyboard's ten-column grid survives 320x180 on), the
/// 3 cells of divider chrome, a 26-cell detail pane (creation's "a wrapped
/// sentence that still reads"), and the 2 border cells. Anything under that
/// could not hold the grammar's own body, so it is where "panel" stops and
/// "toast" begins.
inline constexpr int kPanelMeasureFloorCells = 18 + 3 + 26 + 2;

/// THE MEASURE PROSE-SHAPED CONTENT WRAPS TO WHEN IT IS THE WIDTH-SETTER, in
/// cells.
///
/// Prose does not get a vote in panelMeasureCells' widest-row maximum -- it
/// wraps to whatever the facts and lists settled on, which is what "natural
/// wrap" means. But a page whose MASTER LIST is itself sentences (the quiz,
/// the biography -- OptionBlocks) has nothing else to set the width, and
/// unwrapped hundred-glyph answers would ask for the whole frame back. So a
/// block list measures as min(longest entry, THIS): 46 cells is the low end
/// of the typographic 45-75 glyph measure, three lines out of a hundred-glyph
/// answer instead of one unreadable ribbon -- and comfortably above the
/// 26-cell floor that merely still reads.
inline constexpr int kPanelProseMeasureCells = 46;

// ---------------------------------------------------------------------------
// geometry
// ---------------------------------------------------------------------------

/// A rectangle in framebuffer pixels. Regions, bands and panes are all this.
struct PanelRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    [[nodiscard]] int right() const noexcept { return x + w; }
    [[nodiscard]] int bottom() const noexcept { return y + h; }
    [[nodiscard]] bool empty() const noexcept { return w <= 0 || h <= 0; }
    /// Shrinks by `dx` on each side and `dy` on top and bottom. Never inverts:
    /// an inset bigger than the rect gives an empty rect, not a negative one.
    [[nodiscard]] PanelRect inset(int dx, int dy) const noexcept;
};

/// A share of one axis of a composition: either EXACTLY this many grid units,
/// or this much WEIGHT of whatever is left over after the fixed shares.
///
/// This is the whole of the layout system, and the whole of it on purpose. A
/// composition says "a row for the tab bar, a row for the header, everything
/// else for the body, two rows for the global nav" and gets four rects back
/// that add up to the bounds and are all snapped to the grid. It resolves
/// against the CURRENT WINDOW, so there are no pixel constants in a screen.
struct Span {
    int cells = 0;
    int weight = 0;
};

[[nodiscard]] constexpr Span spanCells(int n) noexcept { return Span{n, 0}; }
[[nodiscard]] constexpr Span spanWeight(int w) noexcept { return Span{0, w}; }

/// Splits `bounds` top to bottom. `cells` spans take exactly that many ROWS;
/// `weight` spans divide the rows left over. Every band starts and ends on the
/// row grid, so a rule drawn at the top of one band lands exactly on the bottom
/// of the one above it -- which is the "aligned edges" half of clean, made
/// structural rather than left to arithmetic at each call site.
///
/// The remainder that does not divide evenly goes to the LAST weighted span, so
/// the result is a pure function of the inputs and a captured frame is
/// reproducible.
[[nodiscard]] std::vector<PanelRect> splitRows(const PanelRect& bounds, const PanelMetric& metric,
                                               const std::vector<Span>& spans);

/// The same, left to right, in CELLS. Shared gutters are spans of their own:
/// `{spanWeight(2), spanCells(1), spanWeight(3)}` is a two-pane split with a
/// one-cell gutter that stays one cell at every resolution.
[[nodiscard]] std::vector<PanelRect> splitColumns(const PanelRect& bounds,
                                                  const PanelMetric& metric,
                                                  const std::vector<Span>& spans);

// ---------------------------------------------------------------------------
// colour roles
// ---------------------------------------------------------------------------

/// The reference's colour-role table, translated to this build's palette rather
/// than copied literally -- UI-REFERENCE-TERMINAL.md's "what NOT to carry" is
/// explicit that the DISCIPLINE (one colour per role) travels and the exact
/// hues do not, because these panels sit over a first-person view lit like
/// lamplight and not over a 4X's tile grid.
struct PanelInk {
    /// Near-black. The panel ground.
    Rgb ground{0.03F, 0.03F, 0.04F};
    /// The rules, the edges, the junctions.
    Rgb rule{0.44F, 0.40F, 0.31F};
    /// Description, flavour, anything read as a sentence.
    Rgb prose{0.88F, 0.86F, 0.80F};
    /// Reference material read deliberately: datelines, unfocused crumbs.
    Rgb dim{0.58F, 0.56F, 0.48F};
    /// Keys, actions, verbs.
    Rgb key{0.96F, 0.80F, 0.34F};
    /// Mechanical benefit and numbers.
    Rgb number{0.52F, 0.82F, 0.48F};
    /// The default entity accent, for anything with no identity colour of its
    /// own to claim.
    Rgb accent{0.98F, 0.86F, 0.42F};
    /// What text knocked out of an inverted fill is drawn in.
    Rgb knockout{0.04F, 0.04F, 0.05F};
};

[[nodiscard]] const PanelInk& panelInk() noexcept;

/// Which role a line of text takes. Saves every caller a colour constant.
enum class InkRole : std::uint8_t { Prose, Dim, Key, Number, Accent, Rule };

[[nodiscard]] Rgb inkFor(InkRole role) noexcept;

// ---------------------------------------------------------------------------
// the motif glyphs the font does not have
// ---------------------------------------------------------------------------

/// See this file's header on why these live here and not in kGlyphs.
enum class Motif : std::uint8_t {
    /// `~` -- the rule's other half.
    Tilde,
    /// `|` -- the vertical edge on even rows.
    Bar,
    /// `!` -- the vertical edge on odd rows. The font has this one; it is here
    /// so the edge is one call rather than a branch at every call site.
    Bang,
    /// `+` -- the lighter junction. Also in the font, same reason.
    Plus,
    /// `◆` -- the heavier junction. Filled, so it reads as a corner and `+`
    /// reads as a crossing.
    Diamond,
    /// `•` -- an effect.
    Dot,
    /// `◦` -- a sub-item under an effect.
    Ring,
    // THE KEYCAP MOTIFS (UI-EA-SPEC sec. 5) -- the six word-savers the EA diet
    // substitutes for key WORDS: `UP DOWN` becomes two arrowheads, `ENTER`
    // becomes the return hook, `D-PAD` becomes the cross. Same 4x6 cell, same
    // advance, same shadow; NOT in the font (the font is untouched) and never
    // used in prose -- they are keycaps, and they enter label strings only
    // through the sentinel bytes below.
    /// `▲`
    ArrowUp,
    /// `▼`
    ArrowDown,
    /// `◀`
    ArrowLeft,
    /// `▶`
    ArrowRight,
    /// `⏎` -- the return hook. The word ENTER, retired.
    Return,
    /// `✚` -- the d-pad cross.
    Cross,
};

// ---------------------------------------------------------------------------
// THE MOTIF SENTINELS -- UI-EA-SPEC sec. 5, cross-lane contract (a)
// ---------------------------------------------------------------------------
// Bytes 0x01-0x06 inside ANY label string drawn by this vocabulary render as
// the keycap motifs above: the text drawers (drawCellText and its right-aligned
// and knocked-out kin, and everything built on them -- option lists, tab rows,
// commit verbs) map sentinel -> motif in place, one cell each, in the run's own
// ink. drawText itself draws nothing for them and still advances the cursor,
// so a sentinel that ever reaches the raw font is a BLANK CELL, not garbage --
// visible in a frame, harmless to a player.
//
// THE MAPPING IS THE CONTRACT and it is fixed: the promptKey choke points
// (controls.cpp -- LANE FLOW's) emit these same bytes, so device-awareness
// survives by construction: a pad still prints `A`, `B`, `X` as words. Never
// printable-character collisions: 0x01-0x06 are control bytes the font has no
// glyph for and authored prose never contains (test_copy's sweep would refuse
// them), which is what makes them safe to smuggle through std::string.
// INTEGRATION RULING: the two lanes landed contract (a) with DIFFERENT byte
// assignments -- controls.cpp's choke points emit Return=0x01, Up=0x02,
// Down=0x03, Left=0x04, Right=0x05, DPad=0x06, and that numbering is pinned
// in ~30 places across controls.cpp and three test files, against two pins
// here. The emitter's numbering wins; these constants renumber to match it.
// The values are arbitrary -- AGREEMENT is the contract.
inline constexpr char kSentinelReturn = '\x01';
inline constexpr char kSentinelArrowUp = '\x02';
inline constexpr char kSentinelArrowDown = '\x03';
inline constexpr char kSentinelArrowLeft = '\x04';
inline constexpr char kSentinelArrowRight = '\x05';
inline constexpr char kSentinelCross = '\x06';

/// The composed forms a page's own copy reaches for. `kGlyphMoveKeys` is the
/// rest-state keycap form of `ARROWS`/`UP DOWN LEFT RIGHT`; the single glyphs
/// compose feet like `"\x01 - FACE IT"`.
inline constexpr std::string_view kGlyphReturn = "\x01";
inline constexpr std::string_view kGlyphUp = "\x02";
inline constexpr std::string_view kGlyphDown = "\x03";
inline constexpr std::string_view kGlyphLeft = "\x04";
inline constexpr std::string_view kGlyphRight = "\x05";
inline constexpr std::string_view kGlyphCross = "\x06";
inline constexpr std::string_view kGlyphMoveKeys = "\x02\x03\x04\x05";
inline constexpr std::string_view kGlyphUpDown = "\x02\x03";

/// The motif a sentinel byte names, or false for any other character. Public
/// so a page measuring words in a label (the census, the token-count pins) can
/// ask the same question the drawer answers.
[[nodiscard]] bool motifForSentinel(char c, Motif& out) noexcept;

/// One motif glyph on the 4x6 cell, top-left anchored, with drawText's own
/// one-pixel drop shadow so it sits in register beside real text.
void drawMotif(Framebuffer& target, int x, int y, Motif motif, const Rgb& colour, float alpha,
               int scale);

/// A run of the `+~-~-` rule motif, `cells` cells wide, starting at (x, y).
/// Alternates `~` and `-` and puts `junction` at BOTH ends. Cells listed in
/// `interiorJunctions` (indices into the run) get a junction too -- that is
/// where a column divider meets the rule.
void drawRuleRun(Framebuffer& target, int x, int y, int cells, const PanelMetric& metric,
                 Motif junction, const Rgb& colour, float alpha,
                 const std::vector<int>& interiorJunctions = {});

/// The faint `.`/`'` field the reference fills a large otherwise-empty panel
/// with, so dead space is TEXTURED rather than a black hole.
///
/// A JUDGEMENT, NOT A RULE, and the reference says so itself: the Summon Demon
/// frame stipples its ground and the Create Homunculus frame -- the same panel
/// type -- leaves plain black. Reach for it when a piece of art or a short
/// block of prose would otherwise float in a big bordered area; skip it when
/// the content fills the space.
///
/// Deterministic from the rect's own coordinates, so a captured frame is
/// reproducible and a stippled panel does not shimmer between frames.
void drawStipple(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                 const Rgb& colour, float alpha);

/// THE ONE ALPHA AN EMPTY PANE'S GROUND FIELD IS DRAWN AT.
///
/// There used to be four -- 0.20 in the creation detail pane, 0.22 in the
/// casebook, the keys page and the map, 0.28 on PanelFrame's own ground -- and
/// nobody could have told you why any of them was the number it was. Same drift
/// the four groundAlphas were, and the same answer: pick one, and let a screen
/// that genuinely wants a different weight pass its own and say so.
///
/// 0.55 rather than the old 0.22 because the field was MEASURED and it was not
/// working: 0.77% coverage at a 21/255 luma delta over the near-black ground,
/// which is a stipple only to somebody who has been told there is one. The
/// spec's whole argument for texturing dead space is the 640x360 window, and at
/// 640x360 it was reading as flat black.
inline constexpr float kPaneStippleAlpha = 0.55F;

/// THE ONE GROUND OPACITY A FULL-SCREEN PAGE IS DRAWN AT.
///
/// There used to be four of these too -- 0.995 on the ward map, 0.99 in the
/// casebook, 0.98 in creation, 0.97 on the controls page -- each with its own
/// paragraph explaining a number nobody had measured against the others. Three
/// of those paragraphs say the SAME thing in different words ("the ward's own
/// hanging signage reads straight through"), which is how you know it is drift
/// and not four judgements.
///
/// 0.97 was demonstrably not enough. The controls page's own comment records
/// the author catching "THE GILDED GULL" through the bindings list and
/// answering it by going from the vocabulary default to 0.97 -- and two frames
/// committed AFTER that still have the sign legible across the middle of the
/// list. A lamplit street sign is high-contrast against near-black: 3% of it is
/// still a readable word. 0.5% is not, which is why the map -- the page that
/// paid for a capture and then measured it -- landed here.
///
/// NOT 1.0, deliberately. The half per cent is what keeps a page feeling like a
/// surface over a world rather than a modal box that replaced it; at 0.995 the
/// world contributes tone and never a glyph.
inline constexpr float kPageGroundAlpha = 0.995F;

/// What a BAND over the live world is drawn at -- the conversation panel, the
/// dialogue rows. A different number for a different job, and the difference is
/// the point: you are meant to still see who you are talking to. Named so the
/// next reader can tell this apart from the drift above at a glance.
inline constexpr float kBandGroundAlpha = 0.86F;

// ---------------------------------------------------------------------------
// the frame
// ---------------------------------------------------------------------------

struct FrameStyle {
    /// `+` or `◆` at the corners and junctions. Either is in register; BE
    /// CONSISTENT WITHIN A SCREEN, which is the reference's own instruction.
    Motif junction = Motif::Plus;
    /// Stipple the ground. See drawStipple's note: judgement, not rule.
    bool stipple = false;
    /// The caller's own fade -- an overlay's EasedToggle, per the project's UI
    /// conventions. 0 draws nothing at all.
    float alpha = 1.0F;
    /// How opaque the ground is over whatever is behind it. The first-person
    /// view is behind these panels and is allowed to show through a little.
    float groundAlpha = 0.94F;
    Rgb rule = panelInk().rule;
    Rgb ground = panelInk().ground;
    /// Where this frame's first content row sits in the ALTERNATION, so a
    /// frame nested inside another can stay in step with its parent's edges
    /// rather than restarting the flicker.
    int rowPhase = 0;
};

/// One bordered pane: ground, the two horizontal rules, and both vertical
/// edges alternating `|`/`!` down the rows.
///
/// PANES HOLD THEIR HEIGHT. The frame is constructed from a rect and draws
/// exactly that rect, every frame, whatever is inside it. Nothing here reflows
/// or collapses when the content shrinks, because the geometry is decided
/// before the content is looked at -- which is the point. A list whose layout
/// jumps as you arrow through it feels broken.
/// DECLARE, THEN DRAW. Interior rules and column dividers are REGISTERED
/// first and rendered together with the border, because a junction is a
/// property of two lines meeting and neither line can know about it alone.
/// Registering also means an edge is never drawn on a row a rule is about to
/// take, so nothing is overdrawn and the border reads clean at every scale --
/// which the alternative (draw border, then paint rules over it) does not,
/// because these grounds are deliberately not fully opaque.
class PanelFrame {
  public:
    PanelFrame(Framebuffer& target, const PanelRect& bounds, const PanelMetric& metric,
               const FrameStyle& style);

    /// An interior horizontal rule spending content row `r` -- the way the
    /// reference's stacked panels spend one. Call before draw().
    void addRule(int r);

    /// A vertical divider at interior cell column `c`, over content rows
    /// [first, first + count), alternating `|`/`!` IN STEP with the outer
    /// edges: the texture runs through the whole frame, not just its border.
    /// Call before draw().
    void addDivider(int c, int first, int count);

    /// Ground, stipple, every registered rule, both side edges, every divider,
    /// and a junction wherever two of them meet. Call once, last.
    void draw();

    /// Inside the edges and the two border rules: where content goes.
    [[nodiscard]] const PanelRect& interior() const noexcept { return interior_; }
    /// How many whole content rows the interior holds.
    [[nodiscard]] int rowCount() const noexcept { return rows_; }
    /// Pixel y of content row `r`.
    [[nodiscard]] int rowY(int r) const noexcept;
    /// The rect covering content rows [first, first + count), clamped to the
    /// interior. This is what a band of the composition is handed.
    [[nodiscard]] PanelRect band(int first, int count) const;

    /// Which motif the edge on content row `r` wears. Public so a screen
    /// drawing its own furniture inside the frame can stay in step.
    [[nodiscard]] Motif edgeAt(int r) const noexcept;

  private:
    struct Divider {
        int cell = 0;
        int first = 0;
        int count = 0;
    };

    Framebuffer* target_;
    PanelRect bounds_;
    PanelMetric metric_;
    FrameStyle style_;
    PanelRect interior_;
    int rows_ = 0;
    int cellsWide_ = 0;
    std::vector<int> rules_;
    std::vector<Divider> dividers_;
};

// ---------------------------------------------------------------------------
// the breadcrumb / instruction header
// ---------------------------------------------------------------------------

/// The nav path, slash-separated, leaf in the colour of the thing being looked
/// at and the rest dim. EVERY PANEL IN THIS PROJECT SHOULD CARRY ONE -- it is
/// how a deeply nested text menu stays navigable.
///
/// Where the panel is a TASK rather than a location, pass a single crumb: it is
/// drawn whole in `leafInk` as the instruction ("SELECT A TILE TO PREACH TO"),
/// which is the same row doing the same job.
///
/// Wraps into `row` when the path is longer than the width, and never draws
/// past it. Returns the rows used.
int drawBreadcrumb(Framebuffer& target, const PanelRect& row, const PanelMetric& metric,
                   const std::vector<std::string>& crumbs, const Rgb& leafInk, float alpha);

/// The cells a PATH wants on one row: every crumb whole with ` / ` between --
/// the width at which drawBreadcrumb stops truncating ancestors. A page
/// measuring itself hands this its LONGEST possible leaf (the widest lead
/// name, not the current one), because the crumb follows the cursor and the
/// cursor may not move the frame. A single crumb is an instruction and wraps;
/// its answer here is just its own length, and it is the caller's judgement
/// whether that belongs in a width maximum at all.
[[nodiscard]] int breadcrumbCells(const std::vector<std::string>& crumbs) noexcept;

// ---------------------------------------------------------------------------
// the tab row
// ---------------------------------------------------------------------------

/// One view over the same subject. `key` is what the player presses.
struct PanelTab {
    std::string key;
    std::string name;
};

/// Screen title at the left, the tabs after it, and a PERSISTENT RESOURCE
/// READOUT right-aligned -- the number you are spending is always on screen
/// while you choose.
///
/// The current tab is an INVERTED FILL in `accent` with its text knocked out
/// dark. Not a bracket and not an arrow: the fill is the affordance and it is
/// unmistakable at a glance.
///
/// WIDTH-AWARE. When the row cannot hold everything it drops from the right --
/// siblings first, never the current tab, then the title -- rather than letting
/// anything collide with the readout. The readout is the last thing to go
/// because it is the thing you are watching.
void drawTabRow(Framebuffer& target, const PanelRect& row, const PanelMetric& metric,
                std::string_view title, const std::vector<PanelTab>& tabs, int current,
                std::string_view readout, const Rgb& accent, float alpha);

/// The cells the whole tab row wants so that NOTHING is dropped -- the title,
/// every tab at its spacing, and the readout with its cell of air. The same
/// costing walk drawTabRow makes before it starts dropping siblings, exposed
/// so a page measuring its width (panelMeasureCells) never composes a frame
/// its own tab row has to shed tabs to fit.
[[nodiscard]] int tabRowCells(std::string_view title, const std::vector<PanelTab>& tabs,
                              std::string_view readout) noexcept;

/// Which tab of a drawn tab row a pixel lands on, or -1 for none (the title
/// and the readout are furniture, not views, and answer -1).
///
/// THE INVERSE OF drawTabRow, written as the same placement walk -- see
/// optionListAt on why the inverse of a layout lives beside the layout rather
/// than being re-derived by every screen that wants a mouse. Every tab's hit
/// span carries the one cell of grace either side that the current tab's
/// inverted fill paints, so the target holds still as the selection moves.
[[nodiscard]] int tabRowTabAt(const PanelRect& row, const PanelMetric& metric,
                              std::string_view title, const std::vector<PanelTab>& tabs,
                              int current, std::string_view readout, int px, int py);

// ---------------------------------------------------------------------------
// the numbered option list
// ---------------------------------------------------------------------------

/// One row of a direct-select list.
struct PanelOption {
    /// What the player presses. "1", "0", "ENTER", "F2" -- numbers for
    /// enumerable rows, letters for named slots and verbs, per the reference.
    std::string key;
    std::string label;
    /// The right-hand value in the list's own COMMON VALUE COLUMN: a cost, a
    /// state label, a bound key. Empty draws nothing and costs nothing, which
    /// is how a row that has been acquired drops its price without the column
    /// moving for every other row.
    std::string value;
    /// The entity's own accent. The selection fill takes THIS, not one global
    /// highlight hue -- the reference fills Blood's row in Blood's own green.
    Rgb accent = panelInk().accent;
    /// Which ink the value takes when the row is not selected.
    InkRole valueInk = InkRole::Number;
    /// A row that is a note or a group heading rather than a choice: drawn
    /// plain, never filled, never counted as selectable.
    bool selectable = true;
    /// Draw the LABEL in this row's own accent rather than in plain prose ink.
    ///
    /// The reference's option lists do this -- Water blue, Earth green, Fire
    /// red -- and it is what makes a thirty-row page scannable by hue before a
    /// word of it is read. Off by default, because a list whose entries have no
    /// identity of their own should not be tinted for decoration: colour is
    /// meaningful in this register, never ornamental.
    bool labelTakesAccent = false;
};

struct OptionListStyle {
    /// A ceiling, not a setting. The actual count comes from the content.
    int maxColumns = 4;
    /// The gutter between columns, in cells. Shared: every gutter on the
    /// screen should be this same number.
    int gutterCells = 2;
    /// THE PANE HOLDS ITS HEIGHT. Never fewer rows than this, whatever the
    /// list currently holds -- so a list that shortens does not drag the rule
    /// under it upwards.
    int minRows = 0;
    /// Values line up at ONE column across the whole list.
    bool alignValues = true;
    /// Print `key` at all. A list of plain names passes false.
    bool showKeys = true;
};

/// What drawOptionList worked out, without drawing anything.
struct OptionListPlan {
    int columns = 1;
    /// Rows actually used per column -- already at least style.minRows.
    int rows = 0;
    /// One column's CONTENT width in cells: key + label + value, and no more.
    /// This is what the selection fill spans and what the value column is
    /// right-aligned against, so a row's highlight hugs its text instead of
    /// running to the far side of a wide pane and leaving the value marooned at
    /// the end of a forty-cell leader.
    int columnCells = 0;
    /// Cells from one column's left edge to the next one's. The columns are
    /// SPREAD across the pane -- last column's content ends at the right edge --
    /// so the gutters are even and the block is not bunched into the left third
    /// of a wide pane. 0 when there is only one column.
    int stride = 0;
    /// The widest key, in cells. 0 when keys are off.
    int keyCells = 0;
    /// The common value column's width in cells. 0 when no entry has a value.
    int valueCells = 0;
    /// What is left for the label after the key and the value.
    int labelCells = 0;
    /// True when the list does not fit even at maxColumns and rows had to be
    /// cut -- the caller wanted a taller pane or a shorter list.
    bool overflowed = false;
};

/// COLUMN COUNT FOLLOWS CONTENT, NOT A FIXED SETTING. Four short names take two
/// columns; nine long ones take one. The count is picked from the longest entry
/// against the width available, then capped by the height available and by
/// maxColumns.
///
/// PURE. No framebuffer, no drawing -- so a case can pin the re-columning at
/// 320x180 and at 1920x1080 without rendering a pixel, which is exactly what
/// test_panel.cpp does.
[[nodiscard]] OptionListPlan planOptionList(const std::vector<PanelOption>& options,
                                            const PanelRect& rect, const PanelMetric& metric,
                                            const OptionListStyle& style);

/// Draws the list COLUMN-MAJOR: entry 0 is the top of the first column and the
/// printed keys read DOWN it, so the numbering runs CONTINUOUSLY across a
/// multi-column layout and the selection model underneath stays a single flat
/// list. That is the reference's own character sheet -- attributes 1-4 on the
/// left, skills 5-8 on the right, one unbroken hotkey sequence -- and it is
/// what keeps a dense two-column list keyboard-drivable without a cursor that
/// has to understand columns.
///
/// `selected` is an index into `options`. Out of range selects nothing.
void drawOptionList(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                    const std::vector<PanelOption>& options, int selected,
                    const OptionListStyle& style, float alpha);

/// The same, against a plan the caller worked out ITSELF -- which is how a
/// screen keeps its geometry still while the list it is showing changes.
///
/// The case that needs it: a list longer than the pane, shown a page at a time.
/// Plan against the WHOLE list (so the value column and the column count are
/// sized for the widest entry that exists, not the widest entry on this page),
/// then draw each page against that one plan. Planning per page instead makes
/// the columns and the value column jump every time the page turns, which is
/// the exact "nothing jumps as the cursor moves" rule this vocabulary exists to
/// keep.
/// Takes no OptionListStyle: everything the drawing needs -- the column count,
/// the stride, the key and value columns -- is IN the plan by then, which is
/// the point of having planned.
void drawOptionListPlanned(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                           const std::vector<PanelOption>& options, int selected,
                           const OptionListPlan& plan, float alpha);

/// HOW WIDE THE PLANNED LIST'S CONTENT ACTUALLY IS, in cells: every column at
/// its content width plus the style's own gutter between them -- the width the
/// block would take if the pane hugged it, which is exactly what a page
/// measuring itself for panelMeasureCells wants to know. The same fields the
/// drawing walks (columnCells, columns), not a second description of them.
/// Plan against a roomy rect first so the answer is the content's and not the
/// pane's.
[[nodiscard]] int optionListNaturalCells(const OptionListPlan& plan, int gutterCells) noexcept;

/// Which entry of a drawn column list a pixel lands on, or -1 for none.
///
/// THIS IS WHAT MAKES A MOUSE A FIRST-CLASS INPUT rather than a bolt-on. The
/// geometry a list is drawn at is already a pure function of (rect, metric,
/// plan) -- drawOptionListPlanned does nothing else -- so the inverse is a pure
/// function too, and it belongs beside the drawing rather than re-derived by
/// every screen that wants to be clickable. A screen that draws a list with
/// this vocabulary gets hover and click for free and cannot get them subtly out
/// of register with what it drew, which is exactly the bug a hand-rolled
/// hit-test in each screen would eventually ship.
///
/// `count` is how many entries were actually drawn (a page, not the whole
/// list); the index returned is into that same run.
[[nodiscard]] int optionListAt(const PanelRect& rect, const PanelMetric& metric,
                               const OptionListPlan& plan, int count, int px, int py) noexcept;

// ---------------------------------------------------------------------------
// the KEY GRID -- a block of single-glyph keys, laid out like a keyboard
// ---------------------------------------------------------------------------

/// WHY THIS EXISTS, in one line: a keyboard is a BLOCK and drawOptionList lays
/// out a LIST.
///
/// The on-screen keyboard shipped as a thirty-row option list capped at six
/// columns, and inherited planOptionList's spread rule -- the one that makes
/// the last column of a wide pane end at its right edge so the gutters come out
/// even. That rule is right for four callings and wrong for thirty letters: at
/// 640x360 the six columns came out at a ten-cell stride carrying one glyph
/// each, so the alphabet read as five sparse vertical strings with 63.6% of the
/// frame dead beneath it. It looked like a list that happened to contain
/// letters.
///
/// So the grid gets its own rule and the list keeps its own. The difference is
/// one line of arithmetic and it is the whole point: THE ADVANCE IS THE WIDEST
/// A KEY CAN AFFORD, NOT THE PANE DIVIDED BY THE COLUMNS. Keys sit a cell
/// apart, every advance is equal, and the block ends where its last key ends
/// instead of being stretched to the margin.
///
/// EVERYTHING ELSE IS THE REGISTER IT ALWAYS WAS: the block sits in a framed,
/// headed pane, the selection is an inverted fill in the entity's accent (never
/// an arrow), and the commit verb is at the foot of the detail pane restating
/// its cost. This is a layout rule, not a second grammar.
///
/// READING ORDER IS THE DRAW ORDER. Unlike drawOptionList -- which runs
/// column-major so a numbered list's hotkeys read down a column -- a grid of
/// letters has no numbers and must read ACROSS. Entry 0 is the top left and
/// entry `columns` is the start of the second row, so a caller's cursor index
/// IS the grid position and there is no transpose to keep in step.
struct KeyGridStyle {
    /// EXACT, not a ceiling. The caller's cursor walks a fixed rectangle
    /// (left/right wraps a row, up/down wraps a column) so the drawn grid must
    /// have the shape that cursor believes in, or the fill lands on a different
    /// glyph than the pane names.
    int columns = 10;
    /// Cells of air inside a key's cap, each side, when the pane can afford it
    /// -- a cap wider than its glyph reads as a key TOP rather than as one
    /// highlighted character.
    ///
    /// ZERO, AND THAT WAS MEASURED. On a one-glyph grid every cell of padding
    /// also doubles the horizontal advance, and pulling the alphabet apart is
    /// the exact defect this whole struct exists to end: at 640x360 a padded
    /// cap puts the columns twenty pixels apart against seven-pixel rows, and
    /// the block goes back to reading as ten vertical strings. Frames of both,
    /// at the default render size, said so. So the fill hugs its glyph -- the
    /// same rule drawOptionList's own fill already keeps -- and a caller whose
    /// keys are words rather than letters can afford to raise it.
    int padCells = 0;
    /// Cells between one cap and the next. Squeezed to nothing before the
    /// column count is ever touched -- see planKeyGrid.
    int gapCells = 1;
    /// Blank rows between key rows. Zero: a keyboard's rows are adjacent, and
    /// the caps' own vertical padding is the row of drop shadow under the font.
    int rowGapRows = 0;
};

/// What drawKeyGrid worked out, without drawing anything.
struct KeyGridPlan {
    int columns = 0;
    int rows = 0;
    /// One key's fill width in cells -- glyph plus whatever padding the pane
    /// could afford. This is what the inverted fill spans.
    int capCells = 0;
    /// Cells from one key's left edge to the next one's, and it is CONSTANT.
    int strideCells = 0;
    /// Rows from one key row's top to the next one's.
    int strideRows = 1;
    /// Where the glyph sits inside its cap.
    int padCells = 0;
    /// False when the pane cannot hold `columns` keys even with the padding
    /// squeezed out, or cannot hold the rows. The caller falls back rather than
    /// drawing a grid of the wrong shape.
    bool usable = false;
};

/// PURE. Same contract as planOptionList: no framebuffer, so a case can pin the
/// block's shape at 320x180 and at 1920x1080 without rendering a pixel.
[[nodiscard]] KeyGridPlan planKeyGrid(const std::vector<PanelOption>& keys, const PanelRect& rect,
                                      const PanelMetric& metric, const KeyGridStyle& style);

/// `selected` is an index into `keys`, in reading order. Out of range selects
/// nothing.
void drawKeyGrid(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                 const std::vector<PanelOption>& keys, int selected, const KeyGridPlan& plan,
                 float alpha);

/// Which key a pixel lands on, or -1. The inverse of drawKeyGrid, written as
/// the same walk for the same reason optionListAt is.
[[nodiscard]] int keyGridAt(const PanelRect& rect, const PanelMetric& metric,
                            const KeyGridPlan& plan, int count, int px, int py) noexcept;

/// HOW WIDE THE PLANNED BLOCK ACTUALLY IS, in cells: the last cap ends the
/// block, so it is the stride times the columns minus the trailing gap. The
/// key grid's own measure for panelMeasureCells -- ten one-glyph keys at a
/// one-cell gap answer 19, which is why THE NAME stops asking for a whole
/// frame. 0 for a plan that is not usable.
[[nodiscard]] int keyGridNaturalCells(const KeyGridPlan& plan) noexcept;

// ---------------------------------------------------------------------------
// the BLOCK list -- a numbered list whose entries are sentences
// ---------------------------------------------------------------------------

/// WHY THIS EXISTS, in one line: the reference's option lists are lists of
/// NAMES (`Water`, `Earth`, `Homunculus Theory`) and some of ours are lists of
/// SENTENCES.
///
/// The chargen quiz and the biography both ask a question and offer answers a
/// hundred glyphs long. Run through drawOptionList they clip to their column --
/// which is precisely the defect the owner hit: `1 YOU TOLD THE.` is not a
/// choice anybody can make. Wrapping them instead is not a different grammar,
/// it is the SAME grammar at a different scale: still numbered, still
/// direct-select, still an inverted fill in the entity's accent for the
/// selection -- the fill simply spans the whole wrapped block rather than one
/// row.
///
/// GEOMETRY IS STILL STABLE. Every block's height comes from its own text and
/// nothing else, so moving the cursor between them moves no block by a pixel.
struct OptionBlockStyle {
    /// Blank rows between one block and the next.
    int gapRows = 1;
    /// Print the key in a hanging column to the left of the wrapped text, so
    /// continuation lines indent under the text and not under the number.
    bool showKeys = true;
    /// Never fewer rows than this per block -- so a one-line answer beside a
    /// three-line one still reads as an equal-weight choice rather than a
    /// footnote.
    int minRows = 1;
};

/// Where one entry of a block list sits. `rows` is 0 for an entry that did not
/// fit the pane -- the index is still handed back so a caller's selection model
/// and this vector never disagree about what entry 4 is.
struct OptionBlock {
    PanelRect rect;
    int rows = 0;
    /// The wrapped label, already shouted and broken to the text column.
    std::vector<std::string> lines;
};

/// PURE. Same contract as planOptionList and for the same reason: a case can
/// pin how a 130-glyph answer breaks at 320x180 and at 1920x1080 without
/// rendering a pixel, and a mouse hit-test can be exact without a framebuffer.
[[nodiscard]] std::vector<OptionBlock> planOptionBlocks(const std::vector<PanelOption>& options,
                                                        const PanelRect& rect,
                                                        const PanelMetric& metric,
                                                        const OptionBlockStyle& style);

void drawOptionBlocks(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                      const std::vector<PanelOption>& options, int selected,
                      const OptionBlockStyle& style, float alpha);

/// Which block a pixel lands in, or -1. See optionListAt on why the inverse of
/// a layout lives beside the layout.
[[nodiscard]] int optionBlockAt(const std::vector<OptionBlock>& blocks, int px, int py) noexcept;

// ---------------------------------------------------------------------------
// bars
// ---------------------------------------------------------------------------

/// One measured quantity, drawn as shape AND figure at once -- the reference's
/// own character sheet: `Strength 8 ###......`, the filled run in the stat's own
/// colour and the REMAINDER AS A DOTTED TRACK rather than an empty gap. Same
/// instinct as the stippled art grounds: emptiness is textured, not blank.
///
/// This is also the honest answer to "show the consequence" that
/// UI-REFERENCE-TERMINAL.md asks for by name -- a player watching an answer move
/// a skill should SEE the bar move, which beats a line of text reporting a
/// delta.
struct PanelBar {
    std::string label;
    /// Printed after the bar. The exact figure beside the shape.
    std::string value;
    std::int32_t filled = 0;
    std::int32_t total = 0;
    /// EVERY STAT CARRIES ITS OWN COLOUR, so a sheet is scannable by hue before
    /// a word of it is read.
    Rgb accent = panelInk().accent;
};

/// Labels in one column, bars `barCells` wide at a shared left edge, values in
/// one column after them. Rows past the bottom of `rect` are not drawn.
/// Returns the rows used.
int drawBars(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
             const std::vector<PanelBar>& bars, int barCells, float alpha);

// ---------------------------------------------------------------------------
// aligned key/value rows
// ---------------------------------------------------------------------------

struct PanelFact {
    std::string label;
    std::string value;
    InkRole valueInk = InkRole::Prose;
};

/// The value column every row in this block shares: one past the longest label,
/// clamped so it never eats more than half the pane however long one label
/// gets. Exposed so two blocks in the same composition can be handed the SAME
/// column and line up with each other, which is the point of having a common
/// value column at all.
[[nodiscard]] int factValueColumn(const std::vector<PanelFact>& facts, const PanelRect& rect,
                                  const PanelMetric& metric);

/// Labels left, values at `valueColumn` cells in. Facts, not paragraphs.
/// Pass -1 for `valueColumn` to have it computed from these facts alone.
void drawFacts(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
               const std::vector<PanelFact>& facts, int valueColumn, float alpha);

// ---------------------------------------------------------------------------
// prose, effects and the bullet hierarchy
// ---------------------------------------------------------------------------

enum class Bullet : std::uint8_t {
    /// A plain line. Flavour.
    None,
    /// `•` -- an effect.
    Dot,
    /// `◦` -- a sub-item under an effect, indented.
    Ring,
};

/// One line of a prose block, before wrapping.
///
/// THE ORDER IS FLAVOUR -> NAMED EFFECT -> NUMBER, and the colour does the
/// sorting: plain lines take prose ink, a named effect takes the subject's
/// accent for its NAME and green for its BODY, and a terse number-led bullet is
/// all green with no prose at all. Do not pad a terse effect into a sentence to
/// match a neighbour, and do not compress an effect that needs a clause.
struct PanelLine {
    Bullet bullet = Bullet::None;
    /// Accent-coloured. Ends in a colon when a body follows it. Empty for a
    /// plain or a number-led line.
    std::string name;
    std::string body;
    /// Which ink the body takes. Prose for flavour, Number for a mechanical
    /// consequence.
    InkRole bodyInk = InkRole::Prose;
    /// The accent the NAME takes. The subject's own, per the reference: `•
    /// Blood:` renders red against a green effect body, and flattening the two
    /// to one colour loses the distinction on purpose made.
    Rgb nameInk = panelInk().accent;
};

/// Wraps every line to the pane and draws it, bullets hanging in their own
/// column so wrapped continuations indent under the text rather than under the
/// bullet. Stops at the bottom of `rect` rather than drawing past it. Returns
/// the rows used.
int drawProse(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
              const std::vector<PanelLine>& lines, float alpha);

/// HOW TALL THAT BLOCK WANTS TO BE, in rows, at `rect`'s WIDTH. The rect's
/// height is deliberately ignored: this is what a caller asks BEFORE it decides
/// how much room to give the block.
///
/// PURE, and the same walk drawProse makes rather than a second description of
/// it -- see planOptionList and optionListAt for the same argument. The case it
/// exists for: a detail pane that must pin its CONSEQUENCE block immediately
/// above the commit verb and let the flavour above it take whatever is left,
/// so that a pane one row short drops the end of a paragraph rather than the
/// line naming what a clue opened.
[[nodiscard]] int measureProse(const PanelRect& rect, const PanelMetric& metric,
                               const std::vector<PanelLine>& lines);

// ---------------------------------------------------------------------------
// the master/detail split
// ---------------------------------------------------------------------------

/// Narrow numbered list on the left, rich detail pane on the right.
struct MasterDetail {
    PanelRect master;
    PanelRect detail;
    /// The interior cell column the divider sits on. Hand it to
    /// PanelFrame::divider() and to PanelFrame::ruleOn() so the junctions land
    /// on it.
    int dividerCell = 0;
    /// False when the interior was too narrow to hold both, in which case
    /// `master` is the whole interior and `detail` is empty. A caller composes
    /// its narrow-window fallback off this rather than guessing a breakpoint.
    bool split = true;
};

/// `masterShare` is the master's share of the interior width out of 100. It is
/// clamped so the master is never narrower than `minMasterCells` nor wider than
/// two thirds -- a master wide enough to re-column is still a legitimate
/// composition, a detail pane squeezed to nothing is not -- and the whole split
/// COLLAPSES (split == false) when the detail pane would come out narrower than
/// `minDetailCells`. That collapse is the honest answer at a small window,
/// rather than two panes too thin to read; the caller composes its one-pane
/// fallback off `split` instead of guessing a pixel breakpoint of its own.
[[nodiscard]] MasterDetail splitMasterDetail(const PanelRect& interior, const PanelMetric& metric,
                                             int masterShare, int minMasterCells,
                                             int minDetailCells);

/// THE INVERSE OF THE SPLIT: the narrowest interior, in cells, whose
/// splitMasterDetail at this share hands the master at least `masterCells` and
/// the detail at least `detailCells`.
///
/// This is what makes a measured page's split a property of its OWN width
/// rather than the window's: the page measures what each half needs, asks this
/// for the interior, and the split it then makes of that interior holds both
/// halves whole by construction -- the master's rows uncut, the detail at the
/// held width its bodyHoldRows-style floors were judged against. Note the
/// share REDISTRIBUTES whatever it is given (a 50-share master takes half of
/// any interior, not its content width), so the answer is often wider than
/// master + chrome + detail -- that surplus is the fixed share doing its job,
/// and the alternative is a share that responds to content, which is the drift
/// the fixed composition rule exists to end.
///
/// Written as a closed form CHECKED AGAINST THE REAL CLAMP -- the last step
/// walks splitMasterDetail's own arithmetic upward until both halves hold, so
/// this cannot quietly disagree with the split it is the inverse of.
[[nodiscard]] int masterDetailCellsFor(int masterShare, int minMasterCells, int masterCells,
                                       int detailCells) noexcept;

/// THE NARROWEST INTERIOR A FULL-WIDTH PAGE EVER HAD, in cells. hudMinorScale
/// steps up with the frame height, so THE BIGGEST WINDOW IS THE NARROWEST IN
/// CELLS: 1920x1080 has 76 across and 74 inside the border, against 960x540's
/// 94. Every held-height floor in this build -- CreationPage::bodyHoldRows,
/// the casebook's kDetailHoldRows -- was judged against detail panes the
/// full-width compositions dealt, and the narrowest of those was dealt HERE.
inline constexpr int kPanelNarrowestFullInteriorCells = 74;

/// THE WIDTH A MEASURED PAGE HOLDS ITS DETAIL PANE AT, at least: what
/// splitMasterDetail would deal this share at the narrowest full-width
/// interior the game runs.
///
/// The detail pane is the half the cursor SWAPS, so its content gets no vote
/// in a width measure (the mirror of "it may not set the height on its own")
/// -- it takes what the master's share arithmetic leaves it, floored by THIS.
/// The floor is not taste: wrap rows only grow as a pane narrows, and every
/// bodyHoldRows-style height floor was judged against panes at least this
/// wide -- so a measure that never deals a narrower detail than the narrowest
/// window would have lets every existing hold keep its promise, with no
/// re-judging of seven steps and two views. Pinned against splitMasterDetail
/// itself in test_panel.cpp.
[[nodiscard]] int panelHeldDetailCells(int masterShare, int minMasterCells) noexcept;

/// The commit action at the FOOT of a detail pane, restating what it costs --
/// `e - Establish (Cost: 200*)`. The irreversible act sits adjacent to the
/// information justifying it, not parked in a global bar.
///
/// STATE CHANGES THE VERB, not the button's enabled-ness: when the thing is
/// already owned the caller passes "R - RECANT" and an empty cost, and this
/// draws that. There is no greyed-out disabled state in this vocabulary,
/// deliberately.
///
/// Drawn on the LAST row of `pane`, so it holds still while the cursor moves
/// through entries whose detail is a different number of lines long. When the
/// verb and the restatement together do not fit the pane's width, the
/// RESTATEMENT moves to the row above rather than being cut -- a cost that
/// arrives as "(TAKES IT OFF WHATEV.." is not doing the job a restated cost
/// exists to do, and the verb is the half that must not move.
void drawCommitVerb(Framebuffer& target, const PanelRect& pane, const PanelMetric& metric,
                    std::string_view verb, std::string_view cost, const Rgb& accent, float alpha);

/// THE COMMIT BEAT -- UI-EA-SPEC sec. 3 rule 5, cross-lane contract (b).
///
/// An ImpactPulse rendered on the inverted fill: for the few steps after a
/// commit lands (FACE IT, TRAVEL, REBIND, BEGIN, quit-confirm), the commit row
/// flashes as a solid accent fill with the verb knocked out dark, easing back
/// to the plain drawCommitVerb render as `pulse` decays to zero. Draw the
/// plain verb first, then call this over it; at pulse 0 it draws nothing at
/// all, so an unwired page costs nothing and changes nothing.
///
/// PAGES render it off a `commitPulse` field on their state; LANE FLOW arms
/// that field from the routing's own ImpactPulse at the commit press. Same
/// last-row placement rule as drawCommitVerb, so the flash lands exactly on
/// the row the verb holds.
void drawCommitPulse(Framebuffer& target, const PanelRect& pane, const PanelMetric& metric,
                     std::string_view verb, std::string_view cost, const Rgb& accent, float alpha,
                     float pulse);

// ---------------------------------------------------------------------------
// text, on the grid
// ---------------------------------------------------------------------------

/// drawText, but positioned in CELLS from the rect's left edge and ROWS from
/// its top, and clipped to the rect. The one call a composed screen makes for
/// ordinary text, so nothing in a screen has to do glyph arithmetic.
/// Returns the cells drawn.
int drawCellText(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric, int cell,
                 int row, std::string_view text, const Rgb& colour, float alpha);

/// The same, right-aligned so the LAST glyph lands on `cell` counted from the
/// rect's right edge.
int drawCellTextRight(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                      int cellFromRight, int row, std::string_view text, const Rgb& colour,
                      float alpha);

/// An inverted fill behind `cells` cells starting at `cell` on `row`: the
/// accent as a solid block, for text to be knocked out of. THE selection
/// idiom, and also the status-badge idiom (`SUCCESS`), which is the same shape
/// doing the same job.
void drawInvertedFill(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                      int cell, int row, int cells, const Rgb& accent, float alpha);

/// drawCellText for text sitting ON an inverted fill -- WITHOUT the drop
/// shadow, and that is the whole reason it exists.
///
/// drawText gives every glyph a one-pixel dark shadow offset down and right.
/// Over this build's near-black panels that shadow is invisible and it is what
/// keeps a pale line legible against a pale sky. Over a BRIGHT ACCENT FILL with
/// DARK knocked-out text it is a second dark copy of every glyph, one pixel
/// out, and at hudMinorScale the two run together: the first capture of the
/// converted controls page had a selected row and a current tab that were
/// legibly SHAPED and not legibly READABLE (docs/frames/panes/keys-960x540.png,
/// first version).
///
/// The font is not touched to fix it, per the brief. Instead the run is drawn
/// once into a scratch framebuffer -- the real font, the real drawText -- in
/// white on black, and only the pixels the INK pass lit are blitted back. The
/// shadow, being near-black on black, does not survive the threshold. One small
/// allocation per knocked-out run, and knocked-out runs are rare: a current
/// tab, a selected row, a subject badge, a status verdict.
int drawCellTextKnockout(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                         int cell, int row, std::string_view text, const Rgb& colour, float alpha);

}  // namespace granadad::render
