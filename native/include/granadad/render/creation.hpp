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

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "granadad/render/dialogue_view.hpp"
#include "granadad/sim/appearance.hpp"
#include "granadad/sim/attributes.hpp"
#include "granadad/sim/chargen.hpp"
#include "granadad/sim/chargen_raws.hpp"
#include "granadad/sim/companions.hpp"
#include "granadad/sim/social.hpp"

namespace granadad::render {

class Framebuffer;

/// One of the three doors into a game. Structural framing only -- `tag` is
/// Eli's own one-word description of each template, task #80's own text,
/// not this file's invention. The actual voice (a bio, an epithet, a
/// self-introduction) lives in content/raws/companions/*.json for DEVIN and
/// GABRI, read through sim::CompanionTemplate -- see CreationFlow's own
/// companion accessors below, which is where that content actually reaches
/// the screen.
struct OriginTemplate {
    /// "devin", "gabri", "custom" -- stable, never shown.
    std::string id;
    /// "DEVIN" -- shown on the card.
    std::string name;
    /// "SECRETIVE" -- Eli's own one-word framing for the template, task #80.
    std::string tag;
};

/// The fixed three, in the order the screen offers them. Never empty.
[[nodiscard]] const std::vector<OriginTemplate>& originTemplates();

/// Longest a typed name is allowed to run. Sixteen glyphs is generous next
/// to "CRACKSMANSHIP" (thirteen) and short enough that any surface this
/// build already prints a name on has room for it.
inline constexpr std::size_t kMaxNameLength = 16;

/// The whole point of the screen: what a caller building a game out of it
/// actually needs. Set only once, by CreationFlow::confirm(). Exactly one of
/// the two sheets below is meaningful, decided by originId:
///
///   originId == "custom"          -- `chargen` is the point-bought sheet
///                                    the player built. Apply with
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

enum class CreationStep : std::uint8_t {
    Origin = 0,
    Customize = 1,
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
    /// own, see loadOriginDefaultName() in the .cpp -- and moves to
    /// Customize.
    void chooseOrigin() noexcept;

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
    /// how many times this was called, never of wall-clock time.
    void advance() noexcept { ++stepCount_; }
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
    /// arriving here for the first time -- or from CUSTOM's blank field --
    /// should hand over the template's own name rather than leave the field
    /// exactly as blank as it was. CUSTOM has no name of its own to offer,
    /// so it clears the field instead.
    void loadOriginDefaultName() noexcept;

    sim::SkillTrack skills_;
    sim::Chargen chargen_;
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
    CreationResult result_;
    std::int64_t stepCount_ = 0;
};

/// Clears the frame and draws whichever of the two screens the flow is
/// currently on. A thin wrapper -- target.clear() plus
/// render::drawDialogue(target, flow.view()) -- kept as one call so a caller
/// in src/client/main.cpp does not need to know DialogueViewState exists.
void drawCreation(Framebuffer& target, const CreationFlow& flow);

}  // namespace granadad::render
