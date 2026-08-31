#pragma once

// TASK #80: THE ORIGIN-SELECT AND CUSTOMIZE FLOW -- A NEW GAME'S FIRST TWO
// SCREENS.
//
// BG3 and Divinity: Original Sin 2 both open the same way: pick a ready-made
// origin or start from nothing, then a second screen where that choice
// becomes a name and a handful of spent points. Eli named the two origins
// himself -- DEVIN (secretive) and GABRI (no-nonsense) -- alongside a fully
// custom third path, and asked for this build's own chunky pixel
// presentation rather than a parchment sheet: see task #80's own text.
//
// THIS FILE IS UI/FLOW, HOSTING TWO OTHER PIECES OF WORK THAT LANDED FROM
// SIBLING TASKS WHILE THIS ONE WAS BEING BUILT:
//
//   DEVIN AND GABRI ARE FIXED SHEETS, NOT POINT-BOUGHT. content/raws/
//   companions/devin.json and gabri.json are hand-authored character sheets,
//   read through sim::CompanionTemplate (companions.hpp) -- exact starting
//   skill levels and derived MGT/AGI/VIG/WIT, a real bio and epithet in
//   their own voice. companions.hpp's own header names this exact file as
//   "the remaining integration work, named and left for whoever does it
//   next": choosing DEVIN or GABRI shows their fixed sheet, read-only, and
//   confirming carries the CompanionTemplate itself in CreationResult.
//
//   CUSTOM IS THE INTERACTIVE ONE, AND sim::Chargen (chargen.hpp) PLUS
//   sim::AttributeBlock (attributes.hpp) ARE THE ARITHMETIC. The
//   Primary/Major/Minor skill sheet and the attribute bonus pool, already
//   built, already proved against the real content/raws/skills/skills.json
//   SkillTrack. This file does not reimplement any of that arithmetic; it
//   holds ONE sim::Chargen for the CUSTOM path, turns its state into rows a
//   player can read and step through with LEFT/RIGHT and ENTER, and hands
//   the finished sheet back in CreationResult for a caller to apply() onto a
//   real SkillTrack. chargen.hpp's own header names this file too: "the
//   arithmetic a BG3/DOS2-style origin-template screen... will eventually
//   stand on top of, built and proved first so that screen has real numbers
//   to show rather than numbers invented to match it."
//
//   WHAT YOU LOOK LIKE IS sim::appearanceOptions() (appearance.hpp) -- eleven
//   of WardType's sixteen values, the same tag-queried vocabulary
//   render::ActorSheet draws the ward's own six hundred out of. DEVIN and
//   GABRI carry a FIXED look off their own CompanionTemplate::appearanceType
//   (a read-only LOOK row, shown only when the raw actually authors one --
//   Gabri's does, Devin's does not yet, see companions.hpp); CUSTOM gets a
//   real LEFT/RIGHT picker over the same eleven, right under NAME, because a
//   custom character choosing how they present is exactly as much "who you
//   are" as what to call yourself.
//
// Neither sibling's content is edited here -- companions.hpp, chargen.hpp,
// attributes.hpp and appearance.hpp are read through their own public
// accessors only. This file's own contribution is the ONE THING none of them
// could be: the three-way switch a player actually presses keys against, and
// the row layout that makes a fixed sheet and a spendable one look like the
// same kind of screen.
//
// DRAWN THROUGH THE EXISTING WIDGET, NOT A NEW ONE. Every other page this
// build has grown -- the casebook, the keys page, options, the pause menu,
// the character sheet -- is DialogueViewState plus render::drawDialogue: one
// panel/cursor/pagination/clipping vocabulary, proved once
// (dialogue_view.cpp) and reused six times. This screen is the seventh
// reuse rather than an eighth hand-rolled layout: CreationFlow::view()
// builds the identical struct session.hpp's own dialogueView() builds, and
// a caller draws it with the identical drawDialogue() call. Every row this
// screen can ever show -- DEVIN/GABRI's twelve fixed skills and four
// attributes, or CUSTOM's nineteen designable skills and four spendable
// attributes, plus NAME and BEGIN either way -- pages and clips through the
// SAME machinery a twelve-topic conversation already does, with no new
// pagination or clipping logic to get wrong a second time.
//
// NO SDL, NO SESSION, NO WORLD. The SDL loop that drives this from a
// keyboard lives in src/client/main.cpp, the one file in this build allowed
// to know what an SDL_Scancode is.
//
// NOTHING HERE IS SIMULATION STATE. The Chargen this file holds is a
// calculator mid-edit -- chargen.hpp's own header says a Chargen is never
// hashed and never part of the ledger until apply() writes through it -- and
// everything else here (which row the cursor is on, whether text entry is
// open) is the identical kind of courtesy-to-the-screen state
// render::EasedToggle's own header already describes for exactly the same
// reason.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "granadad/render/anim.hpp"
#include "granadad/render/creation_page.hpp"
#include "granadad/render/dialogue_view.hpp"
#include "granadad/sim/appearance.hpp"
#include "granadad/sim/attributes.hpp"
#include "granadad/sim/chargen.hpp"
#include "granadad/sim/chargen_raws.hpp"
#include "granadad/sim/companions.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/social.hpp"

namespace granadad::render {

class Framebuffer;

/// One row of the restructured Origin screen. Structural framing only -- the
/// five rows and their order are docs/design/CHARGEN-DAGGERFALL-DRAFT.md
/// section 6's approved mock: the Daggerfall flow's three doors on top (TAKE
/// A CALLING / ANSWER FOR YOURSELF / WALK YOUR OWN PATH), then GABRI and
/// DEVIN as quick starts -- canon characters, never removed, their voice
/// still content/raws/companions/*.json's own, read through
/// sim::CompanionTemplate exactly as before. The mock's two group headers
/// (MAKE YOUR OWN / QUICK START) have no row of their own here because the
/// topic grid has no non-selectable row to give them; the hovered row's own
/// top-band line carries its group instead ("QUICK START -- NO-NONSENSE --
/// ..."), which is the same band the mock put the grouping in.
struct OriginTemplate {
    /// "calling", "quiz", "custom", "gabri", "devin" -- stable, never shown.
    std::string id;
    /// "TAKE A CALLING" / "GABRI" -- shown on the card.
    std::string name;
    /// The card's own one-line description, spoken in the top band when the
    /// row is hovered -- the doc mock's right-hand column for the doors,
    /// Eli's own task #80 word for the quick starts.
    std::string tag;
};

/// The fixed five, in the doc mock's own order. Never empty.
[[nodiscard]] const std::vector<OriginTemplate>& originTemplates();

/// A deterministic display order for one quiz question's three answers --
/// the doc's "shuffled per question at display time, Daggerfall's own guard
/// against pattern-marking", done WITHOUT a draw: a fixed permutation per
/// question index, so the screen position of an axis still varies question
/// to question but a captured frame is a pure function of the flow's state,
/// the contract every other pixel of this screen already keeps. Returns
/// display row -> authored answer index (the raws author A, B, C in order).
[[nodiscard]] std::array<int, 3> quizDisplayOrder(int questionIndex) noexcept;

/// Longest a typed name is allowed to run. Sixteen glyphs is generous next
/// to "CRACKSMANSHIP" (thirteen) and short enough that any surface this
/// build already prints a name on has room for it.
inline constexpr std::size_t kMaxNameLength = 16;

/// The whole point of the screen: what a caller building a game out of it
/// actually needs. Set only once, by CreationFlow::confirm(). Exactly one of
/// the two sheets below is meaningful, decided by originId:
///
///   originId == "calling"/"quiz"/ -- `chargen` is the sheet: point-bought by
///               "custom"             hand, or a taken calling designated
///                                    through the same arithmetic. Apply with
///                                    `chargen.apply(track)`.
///   originId == "devin" / "gabri" -- `companion` is the fixed sheet that
///                                    origin ships with, loaded() == true.
///                                    Apply with
///                                    `companion.applyStartingSkills(track)`.
///
/// The other field is left default (an empty Chargen with nothing
/// designated; a CompanionTemplate with loaded() == false) rather than
/// omitted, so a caller can read either one unconditionally and trust
/// loaded()/an empty picks() list to say which is real.
///
/// `appearance` is set independently of that switch: for DEVIN/GABRI it is
/// whatever their own CompanionTemplate::appearanceType() carries (possibly
/// std::nullopt -- Devin's file does not author one yet), and for CUSTOM it
/// is whichever of sim::appearanceOptions()'s eleven the player left the
/// LOOK row on. A caller wanting to draw this character anywhere in the
/// ward's own sprite vocabulary (render::ActorSheet::forType) reads this one
/// field regardless of which origin was chosen.
struct CreationResult {
    bool confirmed = false;
    std::string originId;
    std::string name;
    sim::Chargen chargen;
    sim::CompanionTemplate companion;
    std::optional<sim::WardType> appearance;
    /// What the biography (and the custom path's advantage shop, when the
    /// flow grows one) did to this character beyond the sheet: coin, heat,
    /// standings, seeds, hpMax and the dagger, in the closed vocabulary
    /// sim/chargen_raws.hpp owns. DEFAULT-CONSTRUCTED IT IS A NO-OP -- every
    /// delta zero, the dagger neutral -- so DEVIN, GABRI and a flow that
    /// never reached the biography apply through the same seam and change
    /// nothing, which is the doc's own "their history is the raws' own" rule
    /// made structural.
    sim::ChargenEffects effects;
};

/// The screens, in flow order. The three Daggerfall doors (task #92) all
/// converge on Customize -- the doc's own REVIEW screen -- with their result
/// pre-designated into the same sim::Chargen the CUSTOM path spends by hand:
///
///   TAKE A CALLING       Origin -> Calling -> Background -> Customize
///   ANSWER FOR YOURSELF  Origin -> Quiz (verdict card at the end, accept or
///                        decline back to Calling) -> Background -> Customize
///   WALK YOUR OWN PATH   Origin -> Customize (build the sheet; BEGIN routes
///                        through Background once) -> Background -> Customize
///   GABRI / DEVIN        Origin -> Customize, unchanged. No quiz and no
///                        biography: their history is the raws' own.
enum class CreationStep : std::uint8_t {
    Origin = 0,
    Calling = 1,
    Quiz = 2,
    Background = 3,
    Customize = 4,
};

/// The two screens, and the cursor, the typed name and the sim::Chargen that
/// move between them.
class CreationFlow {
public:
    /// Loads the real skill vocabulary off content/raws/skills/skills.json
    /// through sim::SkillTrack (the SAME loader Session's own conversations
    /// already read, sim/dialogue.cpp) and DEVIN's and GABRI's fixed sheets
    /// off content/raws/companions/*.json through sim::CompanionTemplate.
    /// Every one of those loaders NEVER throws on a missing or unreadable
    /// file -- their own documented contract -- so a misconfigured content
    /// directory shows a spare customize screen (NAME, the four attributes
    /// at their base, BEGIN, no skill rows and no bio) instead of crashing
    /// the flow before the game has even started.
    explicit CreationFlow(const std::filesystem::path& contentDir);

    [[nodiscard]] CreationStep step() const noexcept { return step_; }

    // --- origin select --------------------------------------------------

    [[nodiscard]] int originCursor() const noexcept { return originCursor_; }
    /// Wraps -- UP from DEVIN reaches CUSTOM. Three cards on one screen have
    /// no page to turn to, so the ring wraps rather than stopping dead the
    /// way a topic list's paged cursor is allowed to.
    void moveOriginCursor(int delta) noexcept;
    /// ENTER on the origin screen. Loads the picked template's own name into
    /// the name field -- unless the player has already typed one of their
    /// own, see loadOriginDefaultName() in the .cpp -- and moves to the
    /// picked door's own first screen (see CreationStep's flow map). A door
    /// whose content failed to load falls through to Customize and reads
    /// exactly like CUSTOM -- the same must-still-boot rule the constructor
    /// already keeps for a missing skills.json.
    void chooseOrigin() noexcept;

    // --- the Daggerfall doors: calling roster, quiz, biography ------------

    [[nodiscard]] const sim::CallingRegistry& callings() const noexcept { return callings_; }
    [[nodiscard]] const sim::ChargenQuiz& quiz() const noexcept { return quiz_; }
    [[nodiscard]] const sim::BiographyRegistry& biography() const noexcept { return biography_; }

    /// The one cursor the Calling, Quiz and Background screens share -- only
    /// one of them is ever on screen, and resetting it on every transition is
    /// what a per-screen cursor would have had to do anyway.
    [[nodiscard]] int choiceCursor() const noexcept { return choiceCursor_; }
    /// UP/DOWN on Calling, Quiz (a question's three answers, or the verdict
    /// card's two rows) and Background. Rings, the same wrap the origin
    /// cursor uses. No-op on every other step.
    void moveChoiceCursor(int delta) noexcept;
    /// ENTER on those same screens. On Calling: takes the hovered calling --
    /// resets the sheet, designates the whole calling into it through
    /// sim::Chargen's own refusing calls (CallingTemplate::designateInto),
    /// and moves on to Background (or straight to Customize when the
    /// biography was already answered, or could not be loaded). On Quiz:
    /// commits the hovered answer -- one restrained ImpactPulse, the
    /// DECISIONS.md convention -- and, after the last question, shows the
    /// verdict card, where row 0 accepts the tally's calling and row 1
    /// declines back to the roster, Daggerfall's own never-a-trap rule. On
    /// Background: commits the hovered answer; the first question also
    /// offers A PAST AT RANDOM (Daggerfall's own RANDOM option), which
    /// answers everything remaining off a seed drawn from the step count --
    /// deterministic for a capture, unrepeatable in practice for a player.
    void chooseChoice() noexcept;
    /// ESC anywhere: steps one commitment back. Mid-quiz and mid-biography
    /// it un-answers the previous question; at the front of either it leaves
    /// the screen the way the player came in; on Customize it defers to
    /// backToOrigin() unchanged.
    void back() noexcept;

    /// The taken calling's id ("netter"), empty until one is taken. Set by
    /// the roster and by an accepted verdict alike.
    [[nodiscard]] const std::string& chosenCallingId() const noexcept { return chosenCallingId_; }
    /// One authored answer index per answered quiz question, in question
    /// order -- exactly the vector sim::tallyQuiz takes.
    [[nodiscard]] const std::vector<std::int32_t>& quizAnswers() const noexcept {
        return quizAnswers_;
    }
    /// Engaged only while the verdict card is on screen.
    [[nodiscard]] const std::optional<sim::QuizTally>& quizVerdict() const noexcept {
        return verdict_;
    }
    /// Per-axis counts of the answers committed SO FAR -- what the three
    /// meters draw mid-quiz, ahead of the full tally sim::tallyQuiz renders
    /// at the end. Indexed by sim::ChargenAxis.
    [[nodiscard]] std::array<std::int32_t, sim::kChargenAxisCount> quizTallySoFar() const noexcept;
    [[nodiscard]] const std::vector<std::int32_t>& biographyAnswers() const noexcept {
        return bioAnswers_;
    }
    [[nodiscard]] bool biographyDone() const noexcept { return bioDone_; }
    /// Everything the answered biography adds up to, in the closed vocabulary
    /// sim/chargen_raws.hpp owns. Default (a no-op) until the biography is
    /// finished; carried out of the door on CreationResult::effects by
    /// confirm(). daggerPoints stays 0 in this build: the section-5 advantage
    /// shop's prices are still on the owner's desk (doc section 0 item 5),
    /// so the custom path's dagger readout shows the neutral 1.00x honestly
    /// rather than shipping an unpriced shop.
    [[nodiscard]] const sim::ChargenEffects& effects() const noexcept { return effects_; }
    /// 1 the instant an answer lands, easing to 0 over ~8 steps -- the one
    /// restrained ImpactPulse the answer-commit gets (DECISIONS.md rule 3).
    [[nodiscard]] float commitPulse() const noexcept { return commitPulse_.value(); }
    /// The screen's own centre furniture, unwrapped: the hovered calling's
    /// (or the verdict's) full sheet preview on Calling/verdict, the hovered
    /// answer's full text on Quiz/Background -- the words the topic grid's
    /// eighteen-glyph column can only ever clip, drawn whole in the one
    /// region this screen has no world or face to keep clear for.
    [[nodiscard]] std::vector<std::string> centreLines() const;

    // --- customize --------------------------------------------------------

    [[nodiscard]] const OriginTemplate& chosenOrigin() const noexcept;
    /// DEVIN's or GABRI's loaded sheet when chosenOrigin() is one of them,
    /// nullptr for CUSTOM (whose sheet is chargen() below instead). Never
    /// returns a pointer to an unloaded() template that claims to be real --
    /// see the .cpp -- a missing content file reads exactly like CUSTOM
    /// rather than like a companion with nothing in it.
    [[nodiscard]] const sim::CompanionTemplate* chosenCompanion() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] bool editingName() const noexcept { return editingName_; }

    /// Only meaningful while editingName(). Accepts a letter, a space, a
    /// hyphen or an apostrophe, refuses everything else (including a
    /// leading space, which canConfirm() would otherwise wave through as a
    /// non-empty but nameless name) and refuses past kMaxNameLength. Stored
    /// upper-case, matching every other string this build ever draws (the
    /// font draws lower-case as upper-case regardless, but this keeps the
    /// stored value visually and logically the same thing).
    void typeNameChar(char c) noexcept;
    void backspaceName() noexcept;

    // --- THE ON-SCREEN KEYBOARD --------------------------------------------
    //
    // A PAD CANNOT TYPE, AND THAT IS WHY THIS EXISTS. Until it did, a
    // controller-only player reached the sheet, pressed A on NAME, and had
    // nothing left to press: canConfirm() is gated on a non-empty name and
    // there was no pad input in the build that could put a glyph in it. That is
    // the difference between "controller supported" and "controller supported
    // except you cannot start the game", and it is the one gap that stopped a
    // pad-only verifier reaching a single world surface.
    //
    // IT IS THE SAME COMPOSITION AS EVERY OTHER STEP. Not a new widget: a
    // CreationPage whose master list happens to hold thirty single-glyph rows
    // (six columns by five, see kOskColumns) and whose detail pane names the
    // glyph under the cursor and holds the name so far. Selection is the same
    // inverted fill; the commit verb sits at the foot restating its cost; the
    // mouse gets hover and click for free out of creationPageHitTest, because
    // the hit-test is the inverse of the one composition both of them read.
    //
    // SOMEONE TYPING NEVER SEES IT. It opens only when a PAD asks -- main.cpp
    // routes a pad confirm on the NAME row here and a keyboard confirm nowhere
    // near it -- and typeNameChar(), which is the only way a real keystroke
    // reaches the name, closes it on the way past. The keyboard path is what it
    // always was.
    [[nodiscard]] bool oskOpen() const noexcept { return osk_; }
    /// No-op unless a name is actually being edited: the keyboard is the name
    /// field's, not a mode of its own.
    void openOsk() noexcept;
    void closeOsk() noexcept { osk_ = false; }

    /// The grid in READING order -- across, then down. A..Z, then the three
    /// punctuation glyphs typeNameChar() actually accepts, then the rub-out.
    /// `_` prints for a space and `<` for a backspace, because every cell is
    /// one glyph wide and that is what keeps the grid a grid.
    ///
    /// The cursor walks THIS order and so does the drawing: pageForOsk() builds
    /// a CreationListShape::Keys grid, which reads across by construction, so
    /// there is no transpose between the two any more.
    ///
    /// TEN BY THREE, NOT SIX BY FIVE. Thirty cells divide either way; ten
    /// across is the shape of a keyboard's own rows and it is what makes the
    /// block read as one. It also puts the rub-out at the bottom right, which
    /// is where a hand already looks for it.
    static constexpr int kOskColumns = 10;
    static constexpr int kOskRows = 3;
    [[nodiscard]] static const std::vector<std::string>& oskCells();
    [[nodiscard]] int oskCursor() const noexcept { return oskCursor_; }
    void setOskCursor(int index) noexcept;
    /// Wraps on both axes, so no direction is ever a dead press.
    void moveOskCursor(int dx, int dy) noexcept;
    /// Takes the glyph under the cursor, or rubs one out. Does NOT close the
    /// keyboard -- finishing is the commit verb at the foot, which is where the
    /// reference puts a commit and where this page says it is.
    void commitOsk() noexcept;

    /// Row 0 is NAME. Row 1 is LOOK, off sim/appearance.hpp's eleven
    /// options -- present for CUSTOM always, present for DEVIN/GABRI only
    /// when their own CompanionTemplate::appearanceType() actually authors
    /// one (absence costs nothing here, the same rule the skill rows
    /// already hold to below). What follows depends on chosenOrigin():
    ///
    ///   DEVIN / GABRI  one row per skill their fixed sheet actually sets
    ///                  (CompanionTemplate::startingSkills(), typically
    ///                  twelve -- absence costs nothing here the same way it
    ///                  does on the HUD, so a skill their sheet never
    ///                  touches gets no row rather than a row reading zero),
    ///                  then one row per attribute at its DERIVED value.
    ///   CUSTOM         one row per eligible skill content/raws/skills/
    ///                  skills.json defines except THE FLAME (chargen.hpp's
    ///                  own header says why that one never reaches a
    ///                  sheet), then one row per attribute at
    ///                  kAttributeBase plus whatever bonus is spent.
    ///
    /// then BEGIN. Built fresh every time it is asked for, so it can never
    /// drift from what LEFT/RIGHT is actually adjusting -- or, for DEVIN and
    /// GABRI, from what it is refusing to.
    [[nodiscard]] int customizeCursor() const noexcept { return customizeCursor_; }
    void moveCustomizeCursor(int delta) noexcept;
    /// ENTER. On NAME: opens or closes text entry. On BEGIN: confirms -- see
    /// confirm(). No-op on a skill or attribute row -- LEFT/RIGHT
    /// (adjustCustomizeRow) is what spends those on CUSTOM, the same
    /// division the options page already draws between its sliders and its
    /// bindings; DEVIN and GABRI have nothing for either key to do.
    void chooseCustomizeRow() noexcept;
    /// LEFT (negative) / RIGHT (positive) on whichever row the cursor is on.
    /// A NO-OP OUTRIGHT UNLESS chosenOrigin() IS CUSTOM -- DEVIN's and
    /// GABRI's sheets are fixed, hand-authored numbers (companions.hpp's own
    /// header: "NOT point-bought"), not a rival copy this screen could drift
    /// from theirs by letting a player nudge it; their own LOOK row is
    /// exactly as fixed, for the identical reason. On CUSTOM: no-op on NAME
    /// and BEGIN; on LOOK, cycles sim::appearanceOptions() (wraps, the same
    /// ring the origin cursor itself uses -- eleven cards on one row have no
    /// page to turn to either); on a skill row, steps that skill's
    /// designation through NONE -> PRIMARY -> MAJOR -> MINOR -> NONE,
    /// refused (row unchanged) the instant the target tier is already full
    /// -- sim::Chargen::designate's own contract, and the status line's
    /// slot counts (see view()) are the player's evidence of why nothing
    /// moved; on an attribute row, spends or returns one point off the pool
    /// via sim::Chargen::spendAttributePoints, refused past its own cap.
    void adjustCustomizeRow(int delta) noexcept;

    /// Which of sim::appearanceOptions()'s eleven the CUSTOM path's LOOK row
    /// is currently on. Meaningless (and untouched by adjustCustomizeRow)
    /// for DEVIN/GABRI, whose LOOK row reads their own fixed
    /// CompanionTemplate::appearanceType() instead -- exposed so a test can
    /// assert the cursor moved without re-deriving it from a rendered label.
    [[nodiscard]] int appearanceIndex() const noexcept { return appearanceIndex_; }

    /// ESC on the customize screen. While editing a name this only closes
    /// text entry, keeping whatever was typed. Otherwise it steps back to
    /// origin select; the name and the whole Chargen sheet both survive the
    /// round trip.
    void backToOrigin() noexcept;

    /// True once name() is non-empty -- the one thing BEGIN refuses to let
    /// through blank, because every line the game speaks from here on has
    /// whatever was typed here welded into it. Deliberately NOT gated on
    /// sim::Chargen::skillsComplete() for CUSTOM: apply() is documented safe
    /// with any subset of the sheet designated, and a screen that refused to
    /// start a game because a bonus point was left unspent would be a wall
    /// this first pass has not been asked to build. DEVIN and GABRI have
    /// nothing to complete in the first place -- their sheet is already
    /// whole the moment it loads.
    [[nodiscard]] bool canConfirm() const noexcept { return !name_.empty(); }
    /// ENTER on BEGIN. Does nothing and returns false when !canConfirm().
    /// Otherwise fills exactly one of CreationResult's two sheets -- see the
    /// struct's own header on which.
    bool confirm() noexcept;
    [[nodiscard]] bool done() const noexcept { return result_.confirmed; }
    [[nodiscard]] const CreationResult& result() const noexcept { return result_; }

    /// Read-only access to the real mechanics this screen hosts, so a test
    /// (or a future caller) can assert against sim::Chargen directly rather
    /// than re-deriving its state from rendered strings. chargen() is CUSTOM's
    /// sheet-in-progress; it exists and stays empty for DEVIN and GABRI too
    /// (nothing refuses constructing it), which is harmless since
    /// adjustCustomizeRow() never reaches it on those two origins.
    [[nodiscard]] const sim::Chargen& chargen() const noexcept { return chargen_; }
    [[nodiscard]] const sim::SkillTrack& skills() const noexcept { return skills_; }

    /// WHERE THE CURSOR IS PUT DIRECTLY, which is what a MOUSE needs and a
    /// keyboard never did. move*Cursor() are deltas and ring round; a pointer
    /// does not arrive as a delta -- it arrives as "this row" -- so these take
    /// the row and clamp it. Hover and click both go through them, which is
    /// what makes hovering MIRROR the cursor rather than run a second,
    /// parallel highlight of its own that the keyboard cannot see.
    ///
    /// Out-of-range is clamped, never wrapped: a pointer that slid off the end
    /// of a list has not asked to be teleported to the other end of it.
    void setOriginCursor(int row) noexcept;
    void setChoiceCursor(int row) noexcept;
    void setCustomizeCursor(int row) noexcept;

    /// The step the flow is currently on, as the composed terminal panel it is
    /// drawn as: breadcrumb, tab row, master list, detail pane, commit verb and
    /// global nav. See render/creation_page.hpp on why this is a MODEL and not
    /// a widget -- chiefly, that the pointer hit-test and the drawing then come
    /// from the same composition and cannot point at different rows.
    [[nodiscard]] CreationPage page() const;

    /// What the biography has cost SO FAR -- the answered questions only,
    /// accumulated through the same sim::accumulateEffects every finished
    /// biography goes through. Zero before the first answer. This is what the
    /// biography screen's persistent readout shows: the owner spent coin, heat
    /// and standing without ever being told, and a running total is the
    /// cheapest possible answer to that.
    [[nodiscard]] sim::ChargenEffects effectsSoFar() const;

    /// Builds the exact struct session.hpp's own dialogueView() builds for
    /// every other page this build has -- render::drawDialogue draws it with
    /// no further translation. Origin select's three cards are its topics,
    /// with the hovered origin's own epithet (or a neutral placeholder for
    /// CUSTOM, or when the raws could not be read) spoken in the top band;
    /// customize's rows -- see customizeCursor()'s own doc for what they are
    /// on each origin -- page the same way a twelve-topic conversation
    /// already pages. See the file header on why this is reuse and not a
    /// new widget.
    [[nodiscard]] DialogueViewState view() const;

    /// A slow, steps-based breathing phase for the cursor highlight -- the
    /// identical contract DialogueViewState::phase already makes, and for
    /// the identical reason: a captured frame has to be a pure function of
    /// how many times this was called, never of wall-clock time. Also decays
    /// the answer-commit pulse, ImpactPulse's own once-per-step contract.
    void advance() noexcept {
        ++stepCount_;
        commitPulse_.advance();
    }
    [[nodiscard]] float phase() const noexcept {
        return static_cast<float>(stepCount_) / 60.0F;
    }

private:
    /// One row of the customize screen's topic list, and what it means to
    /// step it with LEFT/RIGHT.
    struct CustomizeRow {
        enum class Kind : std::uint8_t { Name, Appearance, Skill, Attribute, Begin };
        Kind kind = Kind::Name;
        /// Valid when kind == Skill.
        std::string skillId;
        /// Valid when kind == Attribute.
        sim::AttributeId attribute = sim::AttributeId::Might;
        /// False for a DEVIN/GABRI skill, attribute or LOOK row --
        /// adjustCustomizeRow refuses outright rather than reaching
        /// sim::Chargen or appearanceIndex_ for a row that was never either
        /// one's to move. Always true for NAME and BEGIN, which have their
        /// own kind-based handling regardless of this flag.
        bool editable = true;
    };
    [[nodiscard]] std::vector<CustomizeRow> customizeRowModel() const;
    [[nodiscard]] std::string labelFor(const CustomizeRow& row) const;
    /// The top band's own line whenever a name is not being typed. On
    /// CUSTOM: "PRIMARY 2/3  MAJOR 1/3  MINOR 4/6  POINTS 9 LEFT", so the
    /// slot counts and the pool are always on screen and never only implied
    /// by the rows. On DEVIN/GABRI: says plainly that the sheet below is
    /// fixed, so a player who tries LEFT/RIGHT and sees nothing move has
    /// already been told why before they tried.
    [[nodiscard]] std::string statusLine() const;
    /// Re-suggests the highlighted template's own name into the name field,
    /// but ONLY while the player has not yet typed or backspaced one of
    /// their own -- see nameIsDefault_. Flipping between DEVIN and GABRI to
    /// compare them should not cost a name that was already customised, but
    /// arriving here for the first time -- or from a door's blank field --
    /// should hand over the template's own name rather than leave the field
    /// exactly as blank as it was. The three Daggerfall doors name PATHS,
    /// not characters, so they clear the field instead.
    void loadOriginDefaultName() noexcept;
    /// How many rows the shared choice cursor can stand on for the current
    /// step -- see choiceCursor().
    [[nodiscard]] int choiceRowCount() const noexcept;
    /// Takes `calling`: fresh sheet, designateInto through Chargen's own
    /// refusing calls, then Background (or Customize -- see chooseChoice()).
    void applyCalling(const sim::CallingTemplate& calling) noexcept;
    /// The last biography answer just landed: accumulate the effects (pure,
    /// sim::accumulateBiography) and converge on Customize.
    void finishBiography() noexcept;
    /// The full sheet preview centreLines() builds for one calling.
    [[nodiscard]] std::vector<std::string> sheetPreview(
        const sim::CallingTemplate& calling) const;

    // --- the composed page, step by step ---------------------------------
    //
    // page() is the switch; everything below is one piece of one step, kept
    // separate so a case can assert what a step SAYS without a framebuffer.
    /// The review/customize step, which is the one every door converges on.
    [[nodiscard]] CreationPage pageForSheet() const;
    /// The sheet's NAME row, opened out into its own grid. See oskCells().
    [[nodiscard]] CreationPage pageForOsk() const;
    /// One sheet row as LABEL and VALUE rather than labelFor()'s one jammed
    /// string -- this screen has a common value column and labelFor() predates
    /// it. labelFor() is untouched: DialogueViewState still wants the old shape.
    [[nodiscard]] CreationPageRow pageRowFor(const CustomizeRow& row) const;
    /// An effect target's HUMAN name -- "THE HARBOUR WATCH", never
    /// "watch_docks". Empty for the kinds that take no target.
    [[nodiscard]] std::string pageEffectName(const sim::ChargenEffect& effect) const;
    /// A biography answer's whole cost, as bullets: flavour is already above
    /// them, the named effect takes the accent, the number takes green. An
    /// answer that costs nothing SAYS SO rather than drawing an empty pane.
    [[nodiscard]] std::vector<PanelLine> pageEffectLines(
        const std::vector<sim::ChargenEffect>& effects, const Rgb& accent) const;
    /// The three axis meters, each in its own colour, filled to the counts
    /// passed in -- which the quiz screen sets to the tally AS IF the hovered
    /// answer had been given, so a player sees the meter it would move.
    [[nodiscard]] std::vector<PanelBar> pageAxisBars(
        const std::array<std::int32_t, sim::kChargenAxisCount>& counts) const;
    /// One calling's attribute spend as bars, so a trade can be compared to the
    /// one above it by shape before a number is read.
    [[nodiscard]] std::vector<PanelBar> pageCallingBars(
        const sim::CallingTemplate& calling) const;
    /// The sheet the flow is actually holding, as bars.
    [[nodiscard]] std::vector<PanelBar> pageSheetBars() const;
    /// PRIMARY / MAJOR / MINOR as three named-effect bullets.
    [[nodiscard]] std::vector<PanelLine> pageTierLines(const sim::CallingTemplate& calling,
                                                       const Rgb& accent) const;
    /// "THE MUDLARK" -- the axis identity raws own name for it.
    [[nodiscard]] std::string pageAxisName(sim::ChargenAxis axis) const;
    /// "HAND 4  MUDLARK 3  DISCIPLE 3", for the header readout.
    [[nodiscard]] std::string pageTallyReadout(
        const std::array<std::int32_t, sim::kChargenAxisCount>& counts) const;
    /// What the past has cost so far, for the header readout.
    [[nodiscard]] std::string pagePastReadout() const;
    /// The same, spelled out as bullets on the review pane.
    [[nodiscard]] std::vector<PanelLine> pagePastLines() const;
    /// The slot counts and the point pool, for the header readout.
    [[nodiscard]] std::string pageSheetReadout() const;
    /// Which tiers still have room, restated under the commit verb so a
    /// refusal is never a mystery.
    [[nodiscard]] std::string pageSlotCost() const;

    sim::SkillTrack skills_;
    sim::Chargen chargen_;
    /// The Daggerfall flow's content, loaded once at construction through
    /// the sim's own refusing loaders (chargen_raws.hpp). loaded() false on
    /// any of them closes its door -- see chooseOrigin().
    sim::CallingRegistry callings_;
    sim::ChargenQuiz quiz_;
    /// KEPT, not discarded after validating the biography. The biography's
    /// effects name factions and notables by ID, and a detail pane that told a
    /// player they had lost standing with "watch_docks" would be showing them
    /// the machine's name for something -- the exact thing test_copy.cpp exists
    /// to stop. These are how an id becomes "THE HARBOUR WATCH".
    sim::FactionRegistry factions_;
    sim::NotableRegistry notables_;
    /// DECLARED AFTER the two registries above ON PURPOSE: members initialise
    /// in declaration order, and the biography loader validates every effect
    /// target against them as it reads.
    sim::BiographyRegistry biography_;
    /// DEVIN's and GABRI's fixed sheets, loaded once at construction --
    /// there are only ever two of them, so there is nothing to gain from
    /// loading on demand. loaded() is false for either when the raws could
    /// not be read; chosenCompanion() never hands back a pointer to one in
    /// that state, see its own .cpp.
    sim::CompanionTemplate devinTemplate_;
    sim::CompanionTemplate gabriTemplate_;
    CreationStep step_ = CreationStep::Origin;
    int originCursor_ = 0;
    int customizeCursor_ = 0;
    /// Index into sim::appearanceOptions() the CUSTOM path's LOOK row is on.
    /// Meaningless for DEVIN/GABRI -- see appearanceIndex()'s own doc.
    int appearanceIndex_ = 0;
    std::string name_;
    bool nameIsDefault_ = true;
    bool editingName_ = false;
    bool osk_ = false;
    int oskCursor_ = 0;
    /// The shared Calling/Quiz/Background cursor -- see choiceCursor().
    int choiceCursor_ = 0;
    std::string chosenCallingId_;
    std::vector<std::int32_t> quizAnswers_;
    std::optional<sim::QuizTally> verdict_;
    std::vector<std::int32_t> bioAnswers_;
    bool bioDone_ = false;
    sim::ChargenEffects effects_;
    /// The answer-commit pulse -- ~8 steps of decay, the shipped default,
    /// DECISIONS.md rule 3's own numbers.
    ImpactPulse commitPulse_;
    CreationResult result_;
    std::int64_t stepCount_ = 0;
};

/// Clears the frame and draws whichever of the two screens the flow is
/// currently on. A thin wrapper -- target.clear() plus
/// render::drawDialogue(target, flow.view()) -- kept as one call so a caller
/// in src/client/main.cpp does not need to know DialogueViewState exists.
void drawCreation(Framebuffer& target, const CreationFlow& flow);

/// What a pointer at (px, py) in FRAMEBUFFER pixels is over. Built off
/// flow.page() and render::creationPageHitTest, so it can only ever agree with
/// what was drawn.
[[nodiscard]] CreationHit creationHitTest(const CreationFlow& flow, int frameWidth,
                                          int frameHeight, int px, int py);

}  // namespace granadad::render
