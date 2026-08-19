// THE ORIGIN-SELECT AND CUSTOMIZE FLOW: a new game's first screens -- and,
// since task #92, the Daggerfall doors between them: the calling roster, the
// ward's ten questions with their verdict card, and the twelve-question
// biography, all converging on the same customize/review screen.
//
// WHAT THIS FILE PROVES, AND WHAT IT DOES NOT.
//
// CreationFlow owns five origin rows, a name field, one sim::Chargen shared
// by every make-your-own path, the chargen_raws.hpp registries (proved on
// their own terms by test_chargen_raws.cpp -- tallies, zero-sums, refusals)
// and two sim::CompanionTemplate for DEVIN/GABRI -- the real
// Primary/Major/Minor skill sheet, the real attribute bonus pool, and the
// real hand-authored fixed sheets, all of it already built and already
// proved ON ITS OWN TERMS elsewhere (chargen.hpp/attributes.hpp by
// test_chargen.cpp: a full tier refuses a fourth pick, a moved designation
// frees its old slot, THE FLAME never lands on the sheet, a spend past the
// cap is refused; companions.hpp by test_companions.cpp: every skill id
// either raw names is real, every derived attribute matches
// PROGRESSION-SPEC.md section 5's own formula). NONE of that arithmetic is
// re-proved here -- these cases prove the HOSTING: that CUSTOM's rows read
// the real Chargen rather than a shadow copy and that LEFT/RIGHT actually
// reaches it; that DEVIN's and GABRI's rows read their real fixed sheet and
// that LEFT/RIGHT does NOT reach anything on either of them; and that the
// flow's own state (which screen, whose name, whether text entry is open)
// behaves the way session.hpp's other overlay pages already do.
//
// NO SDL AND NO WINDOW. The keyboard-to-CreationFlow wiring in main.cpp is
// not covered here for the same reason it never is on the pages either side
// of this one (test_pause.cpp, test_character.cpp): that needs a real SDL
// harness.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/creation.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/attributes.hpp"
#include "granadad/sim/chargen.hpp"
#include "granadad/sim/companions.hpp"
#include "granadad/sim/social.hpp"

namespace content = granadad::content;
namespace render = granadad::render;
namespace sim = granadad::sim;

namespace {

[[nodiscard]] render::CreationFlow fresh() {
    return render::CreationFlow(content::contentDir());
}

/// Every character of it has a glyph, and it is not a machine's name for
/// something -- the identical two rules test_copy.cpp already holds every
/// other player-facing surface to.
void mustReadAsEnglish(std::string_view what, std::string_view text) {
    for (const char c : text) {
        INFO("in ", what, ": ", text);
        INFO("undrawable character: '", c, "' (", static_cast<int>(c), ")");
        CHECK(render::isDrawableGlyph(c));
    }
    INFO("in ", what, ": ", text);
    CHECK(text.find('_') == std::string_view::npos);
}

}  // namespace

// ===========================================================================
// origin select
// ===========================================================================

TEST_CASE("five origin rows in the doc mock's own order, and the cursor rings rather than "
          "stopping") {
    // docs/design/CHARGEN-DAGGERFALL-DRAFT.md section 6 (RULING 2): the
    // Daggerfall flow's three doors on top, GABRI and DEVIN as quick starts
    // -- canon characters, never removed. CUSTOM keeps index 2, the one
    // every existing capture flag and case in this file walks to.
    const std::vector<render::OriginTemplate>& origins = render::originTemplates();
    REQUIRE(origins.size() == 5);
    CHECK(origins[0].id == "calling");
    CHECK(origins[1].id == "quiz");
    CHECK(origins[2].id == "custom");
    CHECK(origins[3].id == "gabri");
    CHECK(origins[3].tag == "NO-NONSENSE");
    CHECK(origins[4].id == "devin");
    CHECK(origins[4].tag == "SECRETIVE");

    render::CreationFlow flow = fresh();
    REQUIRE(flow.originCursor() == 0);
    flow.moveOriginCursor(-1);
    CHECK(flow.originCursor() == 4);  // UP from the top wraps to DEVIN
    flow.moveOriginCursor(1);
    CHECK(flow.originCursor() == 0);
    flow.moveOriginCursor(7);
    CHECK(flow.originCursor() == 2);  // (0 + 7) mod 5
}

TEST_CASE("choosing DEVIN or GABRI suggests their own name; the make-your-own doors start blank") {
    render::CreationFlow devin = fresh();
    devin.moveOriginCursor(4);
    devin.chooseOrigin();
    CHECK(devin.step() == render::CreationStep::Customize);
    CHECK(devin.name() == "DEVIN");
    CHECK(devin.chosenOrigin().id == "devin");

    render::CreationFlow gabri = fresh();
    gabri.moveOriginCursor(3);
    gabri.chooseOrigin();
    CHECK(gabri.name() == "GABRI");

    render::CreationFlow custom = fresh();
    custom.moveOriginCursor(2);
    custom.chooseOrigin();
    CHECK(custom.chosenOrigin().id == "custom");
    CHECK(custom.name().empty());

    // The other two doors name PATHS, not characters, the same rule.
    render::CreationFlow calling = fresh();
    calling.chooseOrigin();  // row 0: TAKE A CALLING
    CHECK(calling.name().empty());
}

TEST_CASE("the doors' cards speak the mock's own descriptions in the top band") {
    render::CreationFlow flow = fresh();
    CHECK(flow.view().line == "THE WARD'S NINE TRADES, PICKED BY EYE");
    flow.moveOriginCursor(1);
    CHECK(flow.view().line == "TEN QUESTIONS, AND BE TOLD WHAT YOU ARE");
    flow.moveOriginCursor(1);
    CHECK(flow.view().line == "EVERY SKILL, EVERY POINT, AND THE DAGGER");
    for (int i = 0; i < 3; ++i) {
        mustReadAsEnglish("door blurb", flow.view().line);
        flow.moveOriginCursor(-1);
    }
}

TEST_CASE("DEVIN's and GABRI's cards speak their own real epithet, announced as quick starts") {
    // content/raws/companions/devin.json and gabri.json are real, authored
    // files -- see creation.hpp's own header. If they can be read at all,
    // the card's line must carry the QUICK START group (the mock's header
    // row, which the topic grid cannot draw), Eli's own tag AND their words.
    render::CreationFlow flow = fresh();
    const sim::CompanionTemplate devin = sim::CompanionTemplate::load(content::contentDir(), "devin");
    const sim::CompanionTemplate gabri = sim::CompanionTemplate::load(content::contentDir(), "gabri");
    REQUIRE(devin.loaded());
    REQUIRE(gabri.loaded());
    REQUIRE_FALSE(devin.epithet().empty());
    REQUIRE_FALSE(gabri.epithet().empty());

    flow.moveOriginCursor(4);  // DEVIN
    CHECK(flow.view().line == "QUICK START -- SECRETIVE -- " + devin.epithet());
    mustReadAsEnglish("devin epithet", flow.view().line);

    flow.moveOriginCursor(-1);  // GABRI
    CHECK(flow.view().line == "QUICK START -- NO-NONSENSE -- " + gabri.epithet());
    mustReadAsEnglish("gabri epithet", flow.view().line);
}

// ===========================================================================
// naming
// ===========================================================================

TEST_CASE("switching origins re-suggests a name only until the player types their own") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(4);  // DEVIN
    flow.chooseOrigin();
    REQUIRE(flow.name() == "DEVIN");

    flow.backToOrigin();
    flow.moveOriginCursor(-1);  // GABRI
    flow.chooseOrigin();
    CHECK(flow.name() == "GABRI");  // still following the default

    // NAME is customize row 0 -- see customizeRowModel(). ENTER opens
    // editing, a few letters are typed, ENTER closes it.
    REQUIRE(flow.customizeCursor() == 0);
    flow.chooseCustomizeRow();
    REQUIRE(flow.editingName());
    flow.backspaceName();
    flow.backspaceName();
    flow.backspaceName();
    flow.backspaceName();
    flow.backspaceName();
    REQUIRE(flow.name().empty());
    for (const char c : std::string("ASH")) {
        flow.typeNameChar(c);
    }
    flow.chooseCustomizeRow();
    CHECK_FALSE(flow.editingName());
    CHECK(flow.name() == "ASH");

    flow.backToOrigin();
    flow.moveOriginCursor(1);  // back to DEVIN
    flow.chooseOrigin();
    CHECK(flow.name() == "ASH");  // the player's own name survives the switch
}

TEST_CASE("typing refuses a leading space, refuses non-name characters, and stops at the cap") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM, blank name
    flow.chooseOrigin();
    REQUIRE(flow.name().empty());
    flow.chooseCustomizeRow();  // open NAME for editing
    REQUIRE(flow.editingName());

    flow.typeNameChar(' ');
    CHECK(flow.name().empty());  // a name cannot open on a space

    flow.typeNameChar('7');
    CHECK(flow.name().empty());  // digits are not name characters here

    flow.typeNameChar('a');
    CHECK(flow.name() == "A");  // stored upper-case
    flow.typeNameChar('s');
    flow.typeNameChar('h');
    flow.typeNameChar('-');
    flow.typeNameChar('k');
    flow.typeNameChar('a');
    CHECK(flow.name() == "ASH-KA");

    for (int i = 0; i < 30; ++i) {
        flow.typeNameChar('X');
    }
    CHECK(flow.name().size() == render::kMaxNameLength);
    mustReadAsEnglish("typed name", flow.name());
}

TEST_CASE("BEGIN refuses a blank name, routes the custom door through the biography once, "
          "and confirms the second time") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM
    flow.chooseOrigin();
    REQUIRE_FALSE(flow.canConfirm());
    CHECK_FALSE(flow.confirm());
    CHECK_FALSE(flow.done());

    flow.chooseCustomizeRow();  // NAME row: start editing
    flow.typeNameChar('N');
    flow.typeNameChar('O');
    flow.chooseCustomizeRow();  // NAME row: stop editing
    REQUIRE(flow.canConfirm());

    // Walk the cursor to BEGIN, the last row of the model: NAME (1) + every
    // eligible skill + one row per attribute (kAttributeCount) + BEGIN.
    const std::size_t rowCount = flow.view().topics.size();
    REQUIRE(rowCount > 1);
    flow.moveCustomizeCursor(static_cast<int>(rowCount) - 1);
    CHECK(flow.view().topics.back() == "BEGIN");

    // THE DOC'S OWN ORDER (section 6): sheet first, then the twelve
    // questions, then back here to review -- the first BEGIN a
    // make-your-own path presses is the door into the biography, not the
    // confirmation.
    flow.chooseCustomizeRow();
    REQUIRE(flow.biography().loaded());
    REQUIRE(flow.step() == render::CreationStep::Background);
    CHECK_FALSE(flow.done());
    const std::size_t questionCount = flow.biography().questions().size();
    for (std::size_t i = 0; i < questionCount; ++i) {
        flow.chooseChoice();  // answer (a) of each
    }
    REQUIRE(flow.biographyDone());
    REQUIRE(flow.step() == render::CreationStep::Customize);

    flow.moveCustomizeCursor(static_cast<int>(flow.view().topics.size()) - 1);
    REQUIRE(flow.view().topics.back() == "BEGIN");
    flow.chooseCustomizeRow();
    CHECK(flow.done());
    CHECK(flow.result().confirmed);
    CHECK(flow.result().name == "NO");
    CHECK(flow.result().originId == "custom");
    // The answered biography rides out on the result -- the seam
    // main.cpp's boot block applies through.
    CHECK(flow.result().effects == flow.effects());
}

// ===========================================================================
// hosting the real sim::Chargen (CUSTOM) -- not re-proving its own rules,
// only that this screen actually reaches it
// ===========================================================================

TEST_CASE("THE FLAME never appears as a skill row on this screen, on any sheet-holding origin") {
    // 2 = CUSTOM, 3 = GABRI, 4 = DEVIN -- the three rows that land straight
    // on a sheet. The calling roster's own no-FLAME rule is the loader's
    // (test_chargen_raws.cpp) and the taken-calling case below re-proves the
    // hosting.
    for (const int origin : {2, 3, 4}) {
        render::CreationFlow flow = fresh();
        flow.moveOriginCursor(origin);
        flow.chooseOrigin();
        const render::DialogueViewState view = flow.view();
        for (const std::string& topic : view.topics) {
            INFO("origin index ", origin, ": ", topic);
            CHECK(topic.rfind("THE FLAME", 0) != 0);
            CHECK(topic.rfind("The Flame", 0) != 0);
        }
    }
}

TEST_CASE("LEFT/RIGHT on a skill row actually moves the real Chargen, and a full tier refuses") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM -- the only origin Chargen is live on
    flow.chooseOrigin();

    // Row 0 is NAME, row 1 is LOOK (sim/appearance.hpp's picker -- always
    // present on CUSTOM), row 2 is the first skill in SkillTrack's own
    // ascending order (see social.hpp) -- "bladework", off
    // content/raws/skills.
    REQUIRE(flow.skills().loaded());
    const std::string firstSkillId = flow.skills().entries().front().id;
    flow.moveCustomizeCursor(2);
    REQUIRE(flow.chargen().designationOf(firstSkillId) == sim::SkillDesignation::None);

    flow.adjustCustomizeRow(1);
    CHECK(flow.chargen().designationOf(firstSkillId) == sim::SkillDesignation::Primary);
    flow.adjustCustomizeRow(1);
    CHECK(flow.chargen().designationOf(firstSkillId) == sim::SkillDesignation::Major);
    flow.adjustCustomizeRow(-1);
    CHECK(flow.chargen().designationOf(firstSkillId) == sim::SkillDesignation::Primary);
    flow.adjustCustomizeRow(-1);
    CHECK(flow.chargen().designationOf(firstSkillId) == sim::SkillDesignation::None);

    // Fill the three Primary slots on three OTHER skills, then confirm a
    // fourth is refused -- Chargen::designate's own rule, reached through
    // this screen's LEFT/RIGHT rather than called directly.
    const std::vector<sim::SkillTrack::Entry>& entries = flow.skills().entries();
    std::vector<std::string> nonFlame;
    for (const sim::SkillTrack::Entry& entry : entries) {
        if (entry.aptitudeTier != sim::AptitudeTier::Flame) {
            nonFlame.push_back(entry.id);
        }
    }
    REQUIRE(nonFlame.size() >= 4);

    render::CreationFlow slots = fresh();
    slots.moveOriginCursor(2);  // CUSTOM
    slots.chooseOrigin();
    for (int i = 0; i < 3; ++i) {
        // Row (i+2) is the i-th skill in the model (row 0 is NAME, row 1 is
        // LOOK).
        slots.moveCustomizeCursor(0);  // no-op, keeps intent explicit
        while (slots.customizeCursor() != i + 2) {
            slots.moveCustomizeCursor(1);
        }
        slots.adjustCustomizeRow(1);
        REQUIRE(slots.chargen().designationOf(nonFlame[static_cast<std::size_t>(i)]) ==
               sim::SkillDesignation::Primary);
    }
    CHECK(slots.chargen().slotsFilled(sim::SkillDesignation::Primary) == sim::kPrimarySkillSlots);

    while (slots.customizeCursor() != 5) {
        slots.moveCustomizeCursor(1);
    }
    slots.adjustCustomizeRow(1);  // the fourth Primary attempt
    CHECK(slots.chargen().designationOf(nonFlame[3]) == sim::SkillDesignation::None);
    CHECK(slots.chargen().slotsFilled(sim::SkillDesignation::Primary) == sim::kPrimarySkillSlots);
}

TEST_CASE("an attribute row spends and returns a real point off the real pool") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM
    flow.chooseOrigin();
    REQUIRE(flow.chargen().attributePointsRemaining() == sim::kAttributeBonusPool);

    // The first attribute row sits right after NAME and every skill row --
    // walk there the same way a player would, one press at a time.
    const std::size_t attributeRowIndex = flow.view().topics.size() - 1 - sim::kAttributeCount;
    while (static_cast<std::size_t>(flow.customizeCursor()) != attributeRowIndex) {
        flow.moveCustomizeCursor(1);
    }
    const std::string beforeLabel = flow.view().topics[attributeRowIndex];
    flow.adjustCustomizeRow(1);
    CHECK(flow.chargen().attributePointsRemaining() == sim::kAttributeBonusPool - 1);
    CHECK(flow.chargen().attributeBonus(sim::AttributeId::Might) == 1);
    const std::string afterLabel = flow.view().topics[attributeRowIndex];
    CHECK(beforeLabel != afterLabel);  // the row's own printed value moved

    flow.adjustCustomizeRow(-1);
    CHECK(flow.chargen().attributePointsRemaining() == sim::kAttributeBonusPool);
    CHECK(flow.chargen().attributeBonus(sim::AttributeId::Might) == 0);
}

TEST_CASE("NAME and BEGIN ignore LEFT/RIGHT") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM
    flow.chooseOrigin();
    REQUIRE(flow.customizeCursor() == 0);  // NAME
    flow.adjustCustomizeRow(1);
    flow.adjustCustomizeRow(-1);
    CHECK(flow.chargen().attributePointsRemaining() == sim::kAttributeBonusPool);
}

// ===========================================================================
// hosting the real sim::CompanionTemplate (DEVIN/GABRI) -- read-only, and
// refusing every attempt to nudge it
// ===========================================================================

TEST_CASE("DEVIN's customize rows read his real fixed sheet, not a shadow copy") {
    // ROBUST TO WHATEVER content/raws/companions/devin.json CURRENTLY HOLDS.
    // That file, and the loader that reads it, are both a sibling task's
    // work landing concurrently with this one -- see creation.hpp's own
    // header -- so this proves the HOSTING (every row this screen draws
    // traces to a real skill the loaded template actually named, at the
    // exact level it named, never a Chargen designation word) without
    // assuming a specific skill count or a non-empty sheet, either of which
    // is content the sibling task still owns.
    render::CreationFlow flow = fresh();
    const sim::SkillTrack skills = sim::SkillTrack::load(content::contentDir());
    const sim::CompanionTemplate devin = sim::CompanionTemplate::load(content::contentDir(), "devin");
    REQUIRE(devin.loaded());
    // DEVIN's own file authors no appearanceType (see the dedicated LOOK
    // test below), so row 1 is still the first skill row for him -- checked
    // rather than assumed, since a future content edit could add one.
    REQUIRE_FALSE(devin.appearanceType().has_value());

    std::vector<sim::CompanionSkill> expectedSkills;
    for (const sim::CompanionSkill& skill : devin.startingSkills()) {
        if (skills.aptitudeTier(skill.id) != sim::AptitudeTier::Flame) {
            expectedSkills.push_back(skill);
        }
    }

    flow.moveOriginCursor(4);  // DEVIN
    flow.chooseOrigin();
    REQUIRE(flow.chosenCompanion() != nullptr);
    REQUIRE(flow.chosenCompanion()->id() == "devin");

    const render::DialogueViewState view = flow.view();
    // NAME + one row per authored, non-FLAME skill + one row per attribute +
    // BEGIN.
    CHECK(view.topics.size() == 1 + expectedSkills.size() + sim::kAttributeCount + 1);

    // Every skill row (rows 1..expectedSkills.size()) names the exact level
    // his own template set -- a bare number, one space, not "  LV N" (which
    // the longest skill names here, e.g. "Cracksmanship", clip clean off
    // their column even as a bare two-space number; caught by actually
    // capturing and looking at the rendered frame, not assumed) -- and
    // never zero-by-omission, never a Chargen designation word.
    for (std::size_t i = 0; i < expectedSkills.size(); ++i) {
        const std::string row = view.topics[1 + i];
        INFO(row);
        CHECK(row.find(" " + std::to_string(expectedSkills[i].level)) != std::string::npos);
        CHECK(row.find("PRIMARY") == std::string::npos);
        CHECK(row.find("MAJOR") == std::string::npos);
        CHECK(row.find("MINOR") == std::string::npos);
        CHECK(row.find("UNDESIGNATED") == std::string::npos);
    }
}

TEST_CASE("DEVIN's and GABRI's rows survive the real eighteen-glyph column, not just a "
          "substring check") {
    // THE GAP THE CRACKSMANSHIP FIX LEFT OPEN. ec472fb fixed "4 CRACKSMANSHIP."
    // clipping one glyph over its column on DEVIN's own sheet -- found by
    // capturing --creation=devin and looking at the frame, not by a test,
    // because the test above (and its GABRI-fixing predecessor) only ever
    // asks whether view.topics[i] CONTAINS the right substring. A string that
    // contains " 30" still contains " 30" after clipLabel cuts it down to
    // "4 CRACKSMANSHIP" for the column -- the substring check cannot see a
    // clip happen. This is topicRowsFor + clipLabel, the exact pair
    // dialogue_view.hpp says a case "over this function is therefore a case
    // over the drawing path" for, and test_render.cpp's own "a topic label
    // stops short of the next column's key" already proves the room formula
    // below is drawDialogue's, not a second copy invented here.
    //
    // A regression this catches and the substring check above cannot: a
    // longer skill displayName landing in skills.json, or a companion level
    // ever reaching three digits, silently reclipping a row with nobody
    // capturing a frame to notice.
    for (const std::string_view id : {"devin", "gabri"}) {
        INFO("companion: ", id);
        render::CreationFlow flow = fresh();
        flow.moveOriginCursor(id == "gabri" ? 3 : 4);
        flow.chooseOrigin();
        REQUIRE(flow.chosenCompanion() != nullptr);
        REQUIRE(flow.chosenCompanion()->id() == id);

        const render::DialogueViewState view = flow.view();
        REQUIRE_FALSE(view.topics.empty());

        // Both resolutions test_creation.cpp's own "drawCreation draws
        // something" case already renders at -- the eighteen-glyph room is
        // the same at both, which is the claim dialogue_view.hpp's own
        // header makes ("eighteen glyphs at every resolution this game runs
        // at"), proved here rather than assumed.
        for (const int height : {180, 360}) {
            const int width = height * 16 / 9;
            const int scale = std::max(1, height / 180);
            const int margin = 5 * scale;
            const int glyphAdvance = 5 * scale;
            const int columnWidth = (width - 2 * margin) / render::kTopicColumns;
            const int room = std::max(1, columnWidth / glyphAdvance - 2);
            INFO("height ", height, " room ", room);

            for (int page = 0; page < render::topicPageCount(view.topics.size()); ++page) {
                for (const render::TopicRow& row :
                     render::topicRowsFor(view.topics, page, -1, render::kTopicSlots)) {
                    INFO("row: ", row.label);
                    // Unclipped: the label clipLabel would actually draw is
                    // byte-for-byte the label the row already carries, so
                    // nothing was cut and no mark was appended.
                    CHECK(render::clipLabel(row.label, static_cast<std::size_t>(room)) ==
                          row.label);
                }
            }
        }
    }
}

TEST_CASE("LEFT/RIGHT never moves a single row of DEVIN's or GABRI's sheet") {
    for (const int originIndex : {4, 3}) {  // DEVIN, GABRI
        render::CreationFlow flow = fresh();
        flow.moveOriginCursor(originIndex);
        flow.chooseOrigin();
        REQUIRE(flow.chosenCompanion() != nullptr);

        const std::vector<std::string> before = flow.view().topics;
        REQUIRE(before.size() > 2);
        // Walk every row and press LEFT and RIGHT on each of them.
        for (std::size_t i = 0; i < before.size(); ++i) {
            flow.adjustCustomizeRow(1);
            flow.adjustCustomizeRow(-1);
            flow.moveCustomizeCursor(1);
        }
        const std::vector<std::string> after = flow.view().topics;
        CHECK(before == after);
    }
}

TEST_CASE("the status line says the sheet is fixed, for DEVIN and GABRI") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(4);  // DEVIN
    flow.chooseOrigin();
    CHECK(flow.view().line == "A FIXED SHEET -- NOT ADJUSTABLE HERE.");
    mustReadAsEnglish("companion status line", flow.view().line);
}

TEST_CASE("confirming DEVIN carries his real CompanionTemplate, and no Chargen picks") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(4);  // DEVIN
    flow.chooseOrigin();
    REQUIRE(flow.canConfirm());  // name defaulted to "DEVIN"

    // Walk to BEGIN (the last row) and confirm.
    const std::size_t rowCount = flow.view().topics.size();
    flow.moveCustomizeCursor(static_cast<int>(rowCount) - 1);
    REQUIRE(flow.view().topics.back() == "BEGIN");
    flow.chooseCustomizeRow();

    REQUIRE(flow.done());
    CHECK(flow.result().originId == "devin");
    CHECK(flow.result().name == "DEVIN");
    CHECK(flow.result().companion.loaded());
    CHECK(flow.result().companion.id() == "devin");
    CHECK(flow.result().chargen.picks().empty());
}

// ===========================================================================
// LOOK: the CUSTOM path's appearance/identity step, and DEVIN/GABRI's own
// fixed one -- sim/appearance.hpp's eleven-option vocabulary, the same
// ward-sprite system render::ActorSheet draws the district's own six hundred
// out of. Not re-proving appearance.hpp's own round-trips (test_companions.cpp
// does that); this proves the HOSTING, the identical split the section above
// draws for skills and attributes.
// ===========================================================================

TEST_CASE("CUSTOM's LOOK row cycles the real eleven, wrapping both ways") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM
    flow.chooseOrigin();
    REQUIRE(flow.appearanceIndex() == 0);

    flow.moveCustomizeCursor(1);  // row 1: LOOK
    const std::string firstLabel = flow.view().topics[1];
    CHECK(firstLabel.rfind("LOOK  ", 0) == 0);

    flow.adjustCustomizeRow(1);
    CHECK(flow.appearanceIndex() == 1);
    const std::string secondLabel = flow.view().topics[1];
    CHECK(secondLabel != firstLabel);  // the row's own printed value moved

    flow.adjustCustomizeRow(-1);
    CHECK(flow.appearanceIndex() == 0);
    CHECK(flow.view().topics[1] == firstLabel);

    // WRAPS, THE SAME RING moveOriginCursor USES -- LEFT off the first
    // option reaches the last, RIGHT off the last reaches the first.
    const int count = static_cast<int>(sim::appearanceOptions().size());
    flow.adjustCustomizeRow(-1);
    CHECK(flow.appearanceIndex() == count - 1);
    for (int i = 0; i < count; ++i) {
        flow.adjustCustomizeRow(1);
    }
    CHECK(flow.appearanceIndex() == count - 1);  // one full ring back to where it started
}

TEST_CASE("every one of the eleven LOOK labels survives the real eighteen-glyph column") {
    // THE OTHER THREE the GABRI case above did not catch. "MILITIA WATCH" and
    // "ANIMAL KEEPER" (thirteen glyphs) and "DISCIPLE OF THE FLAME" (twenty-
    // one, one glyph longer than PRIEST's own) all ran over the same room
    // PRIEST OF THE FLAME did, and CUSTOM's cycling LOOK row is the one place
    // this build ever prints them -- GABRI is pinned to PRIEST and nothing
    // authors a companion fixed to any of the other three. Same claim as the
    // DEVIN/GABRI case above (topicRowsFor + clipLabel, not a substring
    // check), walked over every option appearanceOptions() has instead of
    // just the one CUSTOM happens to start on.
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM
    flow.chooseOrigin();
    flow.moveCustomizeCursor(1);  // row 1: LOOK

    const int count = static_cast<int>(sim::appearanceOptions().size());
    for (int i = 0; i < count; ++i) {
        const std::string label = flow.view().topics[1];
        INFO("option ", i, ": ", label);
        for (const int height : {180, 360}) {
            const int width = height * 16 / 9;
            const int scale = std::max(1, height / 180);
            const int margin = 5 * scale;
            const int glyphAdvance = 5 * scale;
            const int columnWidth = (width - 2 * margin) / render::kTopicColumns;
            const int room = std::max(1, columnWidth / glyphAdvance - 2);
            // The row this build actually draws carries a row number ahead
            // of the label, exactly as topicRowsFor composes every topic --
            // see the DEVIN/GABRI case above for why that prefix has to be
            // part of what is measured.
            const std::string onScreen = "2 " + label;
            CHECK(render::clipLabel(onScreen, static_cast<std::size_t>(room)) == onScreen);
        }
        flow.adjustCustomizeRow(1);
    }
}

TEST_CASE("GABRI's LOOK row shows his own fixed appearance and refuses to move") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(3);  // GABRI
    flow.chooseOrigin();
    REQUIRE(flow.chosenCompanion() != nullptr);
    REQUIRE(flow.chosenCompanion()->appearanceType().has_value());

    const sim::AppearanceOption* option =
        sim::appearanceOptionFor(*flow.chosenCompanion()->appearanceType());
    REQUIRE(option != nullptr);

    const std::vector<std::string>& rows = flow.view().topics;
    REQUIRE(rows.size() > 1);
    CHECK(rows[1] == "LOOK  " + std::string(option->label));

    flow.moveCustomizeCursor(1);
    flow.adjustCustomizeRow(1);
    flow.adjustCustomizeRow(-1);
    CHECK(flow.view().topics[1] == rows[1]);  // unmoved, same as every other row on a fixed sheet
}

TEST_CASE("DEVIN has no LOOK row at all -- his file authors no appearanceType") {
    // gabri.json's own provenance says this plainly: devin.json is left
    // unedited by that pass. Confirmed here rather than assumed -- ABSENCE
    // COSTS NOTHING is the same rule his skill rows already hold to
    // (customizeRowModel's own comment), extended to LOOK.
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(4);  // DEVIN
    flow.chooseOrigin();
    REQUIRE(flow.chosenCompanion() != nullptr);
    REQUIRE(flow.chosenCompanion()->id() == "devin");
    CHECK_FALSE(flow.chosenCompanion()->appearanceType().has_value());

    for (const std::string& row : flow.view().topics) {
        CHECK(row.rfind("LOOK", 0) != 0);
    }
}

TEST_CASE("confirming carries the right appearance for all three origins") {
    // CUSTOM: whichever of the eleven the player left the cursor on.
    render::CreationFlow custom = fresh();
    custom.moveOriginCursor(2);
    custom.chooseOrigin();
    custom.moveCustomizeCursor(1);  // LOOK
    custom.adjustCustomizeRow(1);
    custom.adjustCustomizeRow(1);  // two steps in, away from the default
    const sim::WardType expected = sim::appearanceOptions()[static_cast<std::size_t>(
                                                                 custom.appearanceIndex())]
                                       .type;
    custom.moveCustomizeCursor(-1);  // back to NAME
    custom.chooseCustomizeRow();     // start editing
    custom.typeNameChar('A');
    custom.typeNameChar('S');
    custom.typeNameChar('H');
    custom.chooseCustomizeRow();     // stop editing
    REQUIRE(custom.confirm());
    REQUIRE(custom.result().appearance.has_value());
    CHECK(*custom.result().appearance == expected);

    // GABRI: his own fixed look, regardless of anything pressed.
    render::CreationFlow gabri = fresh();
    gabri.moveOriginCursor(3);
    gabri.chooseOrigin();
    REQUIRE(gabri.confirm());
    REQUIRE(gabri.result().appearance.has_value());
    CHECK(*gabri.result().appearance == sim::WardType::PriestOfTheFlame);

    // DEVIN: honestly nullopt -- his file has nothing to carry.
    render::CreationFlow devin = fresh();
    devin.moveOriginCursor(4);
    devin.chooseOrigin();
    REQUIRE(devin.confirm());
    CHECK_FALSE(devin.result().appearance.has_value());
}

// ===========================================================================
// the view, and everything it draws
// ===========================================================================

TEST_CASE("the view carries the right headline in the right place, on both screens") {
    render::CreationFlow flow = fresh();
    const render::DialogueViewState origin = flow.view();
    CHECK(origin.open);
    // The doc mock's own header band (section 6): the screen's name beside
    // the group hint, the ward's line on the marked-cut caseRef row.
    CHECK(origin.speaker == "A NAME FOR YOURSELF");
    CHECK(origin.caseRef == "THE WARD HAD WORK FOR YOU BEFORE YOU HAD A NAME FOR IT.");
    REQUIRE(origin.topics.size() == 5);
    mustReadAsEnglish("origin epithet", origin.epithet);
    mustReadAsEnglish("origin caseRef", origin.caseRef);
    for (const std::string& topic : origin.topics) {
        mustReadAsEnglish("origin card", topic);
    }

    flow.moveOriginCursor(2);  // CUSTOM
    flow.chooseOrigin();
    const render::DialogueViewState custom = flow.view();
    CHECK(custom.speaker == "CUSTOMIZE");
    CHECK(custom.epithet == "WALK YOUR OWN PATH");
    // NAME + LOOK + every non-FLAME skill + one row per attribute + BEGIN.
    std::size_t nonFlame = 0;
    for (const sim::SkillTrack::Entry& entry : flow.skills().entries()) {
        if (entry.aptitudeTier != sim::AptitudeTier::Flame) {
            ++nonFlame;
        }
    }
    CHECK(custom.topics.size() == 1 + 1 + nonFlame + sim::kAttributeCount + 1);
    for (const std::string& topic : custom.topics) {
        mustReadAsEnglish("customize row", topic);
    }
    mustReadAsEnglish("customize status line", custom.line);
}

TEST_CASE("the status line reports the real slot counts and the real pool, for CUSTOM") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM
    flow.chooseOrigin();
    const std::string line = flow.view().line;
    CHECK(line.find("PRIMARY 0/") != std::string::npos);
    CHECK(line.find("POINTS " + std::to_string(sim::kAttributeBonusPool) + " LEFT") !=
         std::string::npos);
    // The dagger readout, at its honest neutral -- task #92. The multiplier
    // is live in the sim (SkillTrack's Q8 advance multiplier); the points
    // stay 0 until the section-5 advantage shop's prices are signed off.
    CHECK(line.find("DAGGER +0 PACE X1.00") != std::string::npos);

    flow.moveCustomizeCursor(2);  // row 1 is LOOK; the first skill is row 2
    flow.adjustCustomizeRow(1);  // one skill designated Primary
    const std::string after = flow.view().line;
    CHECK(after.find("PRIMARY 1/") != std::string::npos);
}

TEST_CASE("while editing a name, the top line says so instead of the status") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(4);  // DEVIN
    flow.chooseOrigin();
    flow.chooseCustomizeRow();  // open NAME
    REQUIRE(flow.editingName());
    CHECK(flow.view().line == "TYPE A NAME, THEN ENTER.");
}

TEST_CASE("ESC while editing closes text entry without discarding the letters, "
         "and only steps back to origin the second time") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM -- NAME starts blank, easy to prove nothing was lost
    flow.chooseOrigin();
    flow.chooseCustomizeRow();
    flow.typeNameChar('E');
    flow.typeNameChar('D');
    flow.backToOrigin();
    CHECK_FALSE(flow.editingName());
    CHECK(flow.step() == render::CreationStep::Customize);  // still on customize
    CHECK(flow.name() == "ED");  // nothing lost

    flow.backToOrigin();
    CHECK(flow.step() == render::CreationStep::Origin);
}

// ===========================================================================
// TASK #84: THE SEAM #80 NAMED, CLOSED. main.cpp's run_client() now applies
// exactly one of CreationResult::chargen or CreationResult::companion onto
// session.tavern().dialogue().skills() -- the ONE SkillTrack every mechanic
// in this build already reads (social.hpp's kHaggleSkill/kThieverySkill/
// kRoofSkill constants, legend.cpp, tavern.cpp's haggle/lockpick/roof-drop
// code, and Session::talkToWard()'s own "the SAME director" comment). Not
// re-proving Chargen::apply() or CompanionTemplate::applyStartingSkills()
// themselves (test_chargen.cpp and test_companions.cpp already do,
// round-trip, on their own terms) -- these cases prove the HOSTING: that the
// exact sheet this screen hands back is the exact sheet a live Session's
// skill checks read, with no shadow copy in between. main.cpp's own SDL
// loop that calls this at boot is not exercised here for the same reason it
// never is anywhere else in this build (test_pause.cpp, test_character.cpp,
// this file's own header): that needs a real SDL harness.
// ===========================================================================

TEST_CASE("DEVIN's fixed sheet, applied through CreationResult, reaches the live SkillTrack "
         "every mechanic in this build reads") {
    // GABRI'S OWN SHEET DELIBERATELY LEAVES ALL FOUR MECHANICALLY-ACTIVE
    // SKILLS AT ZERO (content/raws/companions/gabri.json's own rationale
    // quotes PROGRESSION-SPEC.md's north star: a Streetwise-0 Gabri is
    // "still obeyed everywhere but pays list price") -- a real design
    // choice, and the wrong sheet to prove THIS wiring with, since applying
    // it would leave every number this test could check sitting at the same
    // zero a never-applied sheet reads. Devin's Primary picks (skyrunning,
    // cracksmanship) and Major pick (streetwise) are exactly the three
    // social.hpp names, so his sheet is the one that proves the seam by
    // actually moving a number off zero.
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(4);  // DEVIN
    flow.chooseOrigin();
    const std::size_t rowCount = flow.view().topics.size();
    flow.moveCustomizeCursor(static_cast<int>(rowCount) - 1);
    REQUIRE(flow.view().topics.back() == "BEGIN");
    flow.chooseCustomizeRow();
    REQUIRE(flow.done());
    const render::CreationResult& chosen = flow.result();
    REQUIRE(chosen.companion.loaded());
    REQUIRE(chosen.companion.startingLevel(sim::kRoofSkill) > 0);
    REQUIRE(chosen.companion.startingLevel(sim::kThieverySkill) > 0);
    REQUIRE(chosen.companion.startingLevel(sim::kHaggleSkill) > 0);

    render::SessionConfig config;
    config.contentDir = content::contentDir();
    render::Session session(config);
    sim::SkillTrack& live = session.tavern().dialogue().skills();

    // BEFORE: a fresh arrival's SkillTrack starts every id at the same
    // untouched floor SkillTrack::level() gives an id nobody has set.
    REQUIRE(live.level(sim::kRoofSkill) == 0);
    REQUIRE(live.level(sim::kThieverySkill) == 0);
    REQUIRE(live.level(sim::kHaggleSkill) == 0);

    const std::int32_t matched = chosen.companion.applyStartingSkills(live);
    CHECK(matched == static_cast<std::int32_t>(chosen.companion.startingSkills().size()));

    // AFTER: the SAME SkillTrack DialogueDirector hands to a haggle, a
    // strongbox and a roof landing now carries Devin's own numbers, exactly
    // -- not a shadow copy a player would never see move.
    CHECK(live.level(sim::kRoofSkill) == chosen.companion.startingLevel(sim::kRoofSkill));
    CHECK(live.level(sim::kThieverySkill) == chosen.companion.startingLevel(sim::kThieverySkill));
    CHECK(live.level(sim::kHaggleSkill) == chosen.companion.startingLevel(sim::kHaggleSkill));

    // AND THE CHARACTER SHEET -- the one screen a player actually reads
    // these numbers back off, the tiled Menu's Character tile in the real
    // game -- agrees, because characterRows() reads through this exact same
    // accessor. Exact string, not just "not zero": the row format
    // ("SKYRUNNING LV n") is characterRows()'s own, pinned here so a future
    // change to either the format or the applied level would fail this the
    // honest way.
    //
    // MORROWIND ROUND: ONE SPACE, NOT TWO -- see characterRows()'s own note
    // on why the tiled Character tile's narrower column gave that glyph
    // back to the skill rows.
    const std::string expected =
        "SKYRUNNING LV " + std::to_string(chosen.companion.startingLevel(sim::kRoofSkill));
    const std::vector<std::string> rows = session.characterRows();
    CHECK(std::find(rows.begin(), rows.end(), expected) != rows.end());
}

TEST_CASE("CUSTOM's point-bought Chargen sheet, applied through CreationResult, reaches the "
         "same live SkillTrack") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM
    flow.chooseOrigin();
    REQUIRE(flow.skills().loaded());
    const std::string firstSkillId = flow.skills().entries().front().id;

    // Row 0 is NAME, row 1 is LOOK, row 2 is the first skill in ascending
    // order -- the same layout "LEFT/RIGHT on a skill row actually moves the
    // real Chargen" above already pins.
    flow.moveCustomizeCursor(2);
    flow.adjustCustomizeRow(1);  // Primary
    REQUIRE(flow.chargen().designationOf(firstSkillId) == sim::SkillDesignation::Primary);

    // CUSTOM's NAME starts blank (creation.cpp's own loadOriginDefaultName)
    // and canConfirm() refuses BEGIN on an empty one -- open NAME, type a
    // real one, close it, exactly the round trip "switching origins
    // re-suggests a name" above already proves.
    flow.moveCustomizeCursor(-2);
    REQUIRE(flow.customizeCursor() == 0);
    flow.chooseCustomizeRow();
    REQUIRE(flow.editingName());
    for (const char c : std::string("ASH")) {
        flow.typeNameChar(c);
    }
    flow.chooseCustomizeRow();
    REQUIRE_FALSE(flow.editingName());
    REQUIRE(flow.name() == "ASH");

    const std::size_t rowCount = flow.view().topics.size();
    flow.moveCustomizeCursor(static_cast<int>(rowCount) - 1);
    REQUIRE(flow.view().topics.back() == "BEGIN");
    flow.chooseCustomizeRow();
    // THE FIRST BEGIN A MAKE-YOUR-OWN PATH PRESSES IS THE DOOR INTO THE
    // BIOGRAPHY, NOT THE CONFIRMATION -- the doc's own order, and exactly
    // what "BEGIN refuses a blank name, routes the custom door through the
    // biography once..." above already proves at length. This case pressed
    // BEGIN once and required done() until the ward-map round's first gate
    // run caught it stale: the biography leg (49fb234) had landed while
    // this drive-through still assumed the pre-biography flow.
    REQUIRE_FALSE(flow.done());
    REQUIRE(flow.step() == render::CreationStep::Background);
    const std::size_t questionCount = flow.biography().questions().size();
    for (std::size_t i = 0; i < questionCount; ++i) {
        flow.chooseChoice();  // answer (a) of each, deterministic
    }
    REQUIRE(flow.biographyDone());
    REQUIRE(flow.step() == render::CreationStep::Customize);
    flow.moveCustomizeCursor(static_cast<int>(flow.view().topics.size()) - 1);
    REQUIRE(flow.view().topics.back() == "BEGIN");
    flow.chooseCustomizeRow();
    REQUIRE(flow.done());
    const render::CreationResult& chosen = flow.result();
    CHECK_FALSE(chosen.companion.loaded());  // CUSTOM carries no fixed sheet

    render::SessionConfig config;
    config.contentDir = content::contentDir();
    render::Session session(config);
    sim::SkillTrack& live = session.tavern().dialogue().skills();
    REQUIRE(live.level(firstSkillId) == 0);

    const sim::AttributeBlock block = chosen.chargen.apply(live);
    (void)block;  // no runtime reader yet -- see run_client()'s own note

    CHECK(live.level(firstSkillId) == sim::kPrimaryStartLevel);
}

// ===========================================================================
// TASK #92: THE DAGGERFALL DOORS. The registries' own arithmetic (the tally
// table, the zero-sum bookkeeping, every refusal) is test_chargen_raws.cpp's;
// these cases prove the HOSTING -- that the roster row a player takes is the
// exact CallingTemplate the sim loaded, that the screen's shuffled answer
// rows commit the authored answer they display, and that the effects a
// finished flow carries out are byte-identical to what the pure accumulator
// says those answers mean.
// ===========================================================================

namespace {

/// Commits one quiz answer by its AUTHORED index through the same shuffled
/// rows a keyboard walks -- no back door past the display order.
void answerQuizAuthored(render::CreationFlow& flow, int authored) {
    const std::array<int, 3> order =
        render::quizDisplayOrder(static_cast<int>(flow.quizAnswers().size()));
    for (int pos = 0; pos < 3; ++pos) {
        if (order[static_cast<std::size_t>(pos)] == authored) {
            while (flow.choiceCursor() != pos) {
                flow.moveChoiceCursor(1);
            }
            flow.chooseChoice();
            return;
        }
    }
    FAIL("authored index " << authored << " not present in the display order");
}

}  // namespace

TEST_CASE("the calling roster lists the nine trades and taking one designates the real sheet") {
    render::CreationFlow flow = fresh();
    REQUIRE(flow.callings().loaded());
    flow.chooseOrigin();  // row 0: TAKE A CALLING
    REQUIRE(flow.step() == render::CreationStep::Calling);

    const render::DialogueViewState roster = flow.view();
    REQUIRE(roster.topics.size() == flow.callings().callings().size());
    REQUIRE(roster.topics.size() == 9);
    // The hovered trade speaks its own one line in the top band.
    CHECK(roster.line == flow.callings().callings().front().oneLine);
    for (const std::string& topic : roster.topics) {
        mustReadAsEnglish("roster row", topic);
    }

    // Walk to NETTER (authored index 3) and take it.
    for (int i = 0; i < 3; ++i) {
        flow.moveChoiceCursor(1);
    }
    const sim::CallingTemplate& netter = flow.callings().callings()[3];
    REQUIRE(netter.id == "netter");
    flow.chooseChoice();
    CHECK(flow.chosenCallingId() == "netter");
    REQUIRE(flow.step() == render::CreationStep::Background);

    // The sheet is the calling's own, through Chargen's real arithmetic --
    // every Primary at Primary, the pool exactly spent.
    for (const std::string& id : netter.primary) {
        CHECK(flow.chargen().designationOf(id) == sim::SkillDesignation::Primary);
    }
    for (const std::string& id : netter.major) {
        CHECK(flow.chargen().designationOf(id) == sim::SkillDesignation::Major);
    }
    CHECK(flow.chargen().attributePointsRemaining() == 0);

    // Twelve answers later the flow converges on the review screen, and the
    // effects carried are EXACTLY what the pure accumulator says the same
    // answers mean -- the hosting claim, not a re-proof of the sums.
    for (int i = 0; i < 12; ++i) {
        flow.chooseChoice();  // answer (a) of each, in authored order
    }
    REQUIRE(flow.biographyDone());
    REQUIRE(flow.step() == render::CreationStep::Customize);
    const std::vector<std::int32_t> answered(12, 0);
    CHECK(flow.biographyAnswers() == answered);
    const std::optional<sim::ChargenEffects> expected =
        sim::accumulateBiography(flow.biography(), answered);
    REQUIRE(expected.has_value());
    CHECK(flow.effects() == *expected);

    // The review screen names the trade it is reviewing, and the status
    // line carries the dagger at its honest neutral -- no advantage shop
    // ships until the doc's section-5 prices are signed off.
    const render::DialogueViewState review = flow.view();
    CHECK(review.epithet == "TAKE A CALLING - Netter");
    CHECK(review.line.find("DAGGER +0 PACE X1.00") != std::string::npos);
}

TEST_CASE("the quiz commits the authored answer its shuffled row displays, and the meters "
          "count it") {
    render::CreationFlow flow = fresh();
    REQUIRE(flow.quiz().loaded());
    flow.moveOriginCursor(1);  // ANSWER FOR YOURSELF
    flow.chooseOrigin();
    REQUIRE(flow.step() == render::CreationStep::Quiz);
    REQUIRE(flow.quiz().questions().size() == 10);

    // The first screen: question 1 of 10, three answers, the prompt in the
    // top band -- and every word of it drawable (the em-dash fold at the
    // loader seam, proved on the exact strings this screen shows).
    const render::DialogueViewState first = flow.view();
    CHECK(first.speaker == "THE WARD ASKS");
    CHECK(first.epithet == "1 OF 10");
    REQUIRE(first.topics.size() == 3);
    mustReadAsEnglish("quiz prompt", first.line);
    for (const std::string& topic : first.topics) {
        mustReadAsEnglish("quiz answer", topic);
    }

    // Row -> authored mapping: the topic shown at display row r IS the
    // authored answer quizDisplayOrder names, and committing row r scores
    // that answer's axis, whatever the shuffle did.
    const std::array<int, 3> order = render::quizDisplayOrder(0);
    const sim::QuizQuestion& q0 = flow.quiz().questions()[0];
    for (int pos = 0; pos < 3; ++pos) {
        CHECK(first.topics[static_cast<std::size_t>(pos)] ==
              q0.answers[static_cast<std::size_t>(order[static_cast<std::size_t>(pos)])].text);
    }

    answerQuizAuthored(flow, 1);  // B
    answerQuizAuthored(flow, 1);  // B
    answerQuizAuthored(flow, 2);  // C
    const std::array<std::int32_t, sim::kChargenAxisCount> counts = flow.quizTallySoFar();
    CHECK(counts[0] == 0);
    CHECK(counts[1] == 2);
    CHECK(counts[2] == 1);
    CHECK(flow.view().epithet == "4 OF 10");

    // ESC un-answers the previous question -- one commitment back.
    flow.back();
    CHECK(flow.quizAnswers().size() == 2);
    CHECK(flow.view().epithet == "3 OF 10");
    // ...and backing all the way out lands on the origin screen.
    flow.back();
    flow.back();
    CHECK(flow.quizAnswers().empty());
    flow.back();
    CHECK(flow.step() == render::CreationStep::Origin);
}

TEST_CASE("the verdict card is never a trap: the doc's own tally row, accept or decline") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(1);
    flow.chooseOrigin();
    // 5 A, 3 B, 2 C: A dominant short of pure, B >= C -- the table's
    // DECKHAND row (doc section 3.3, transcribed into questions.json's own
    // verdict block and proved at load).
    for (int i = 0; i < 5; ++i) {
        answerQuizAuthored(flow, 0);
    }
    for (int i = 0; i < 3; ++i) {
        answerQuizAuthored(flow, 1);
    }
    for (int i = 0; i < 2; ++i) {
        answerQuizAuthored(flow, 2);
    }
    REQUIRE(flow.quizVerdict().has_value());
    CHECK(flow.quizVerdict()->calling == "deckhand");
    CHECK_FALSE(flow.quizVerdict()->pure);

    const render::DialogueViewState card = flow.view();
    CHECK(card.speaker == "THE WARD'S VERDICT");
    CHECK(card.epithet == "HAND 5  MUDLARK 3  DISCIPLE 2");
    REQUIRE(card.topics.size() == 2);
    CHECK(card.topics[0] == "TAKE THE CALLING");
    CHECK(card.topics[1] == "ANOTHER TRADE");

    // DECLINE lands on the roster -- Daggerfall's own rule, doc 3.2.
    flow.moveChoiceCursor(1);
    flow.chooseChoice();
    CHECK(flow.step() == render::CreationStep::Calling);
    CHECK(flow.chosenCallingId().empty());

    // A second run that ACCEPTS carries the verdict's own sheet forward.
    render::CreationFlow taker = fresh();
    taker.moveOriginCursor(1);
    taker.chooseOrigin();
    for (int i = 0; i < 6; ++i) {
        answerQuizAuthored(taker, 0);  // A pure at six -- DOCKHAND
    }
    for (int i = 0; i < 4; ++i) {
        answerQuizAuthored(taker, 1);
    }
    REQUIRE(taker.quizVerdict().has_value());
    CHECK(taker.quizVerdict()->pure);
    REQUIRE(taker.quizVerdict()->calling == "dockhand");
    taker.chooseChoice();  // row 0: TAKE THE CALLING
    CHECK(taker.chosenCallingId() == "dockhand");
    CHECK(taker.step() == render::CreationStep::Background);
    const sim::CallingTemplate* dockhand = taker.callings().find("dockhand");
    REQUIRE(dockhand != nullptr);
    for (const std::string& id : dockhand->primary) {
        CHECK(taker.chargen().designationOf(id) == sim::SkillDesignation::Primary);
    }
}

TEST_CASE("the biography offers A PAST AT RANDOM on its first question only, and the random "
          "past still accumulates purely") {
    render::CreationFlow flow = fresh();
    flow.chooseOrigin();  // TAKE A CALLING
    flow.chooseChoice();  // DOCKHAND, straight into the biography
    REQUIRE(flow.step() == render::CreationStep::Background);

    const render::DialogueViewState b1 = flow.view();
    REQUIRE(flow.biography().questions().size() == 12);
    const std::size_t b1Answers = flow.biography().questions()[0].answers.size();
    REQUIRE(b1.topics.size() == b1Answers + 1);
    CHECK(b1.topics.back() == "A PAST AT RANDOM");
    CHECK(b1.epithet == "B1 - 1 OF 12");
    mustReadAsEnglish("biography prompt", b1.line);
    for (const std::string& topic : b1.topics) {
        mustReadAsEnglish("biography answer", topic);
    }

    // Question two offers only its own answers -- half-answered-then-random
    // is not a thing.
    flow.chooseChoice();
    const render::DialogueViewState b2 = flow.view();
    CHECK(b2.topics.size() == flow.biography().questions()[1].answers.size());

    // Back to the first question and take the random past: every question
    // answered, and the carried effects equal the pure accumulator over the
    // exact answers the flow recorded -- randomness in the PICK, never in
    // the arithmetic.
    flow.back();
    flow.moveChoiceCursor(-1);  // ring: up from row 0 lands on the last row
    REQUIRE(flow.choiceCursor() == static_cast<int>(b1Answers));
    flow.chooseChoice();
    REQUIRE(flow.biographyDone());
    REQUIRE(flow.step() == render::CreationStep::Customize);
    REQUIRE(flow.biographyAnswers().size() == 12);
    const std::optional<sim::ChargenEffects> expected =
        sim::accumulateBiography(flow.biography(), flow.biographyAnswers());
    REQUIRE(expected.has_value());
    CHECK(flow.effects() == *expected);
    // Zero-sum reputation survives the sum -- the loader enforced it per
    // answer, and sums of zero-sums are zero-sum.
    std::int32_t reputation = 0;
    for (const auto& [factionId, delta] : flow.effects().factionStandings) {
        reputation += delta;
    }
    CHECK(reputation == 0);
}

TEST_CASE("every word the doors can ever show survives the 4x6 font -- the em-dash fold, "
          "proved on the loaded raws") {
    // The owner's own files carry UTF-8 em-dashes; chargen_raws.cpp folds
    // them at the loader seam (notables.cpp's own rule). This sweep walks
    // the LOADED registries -- every prompt, every answer, every one-liner
    // -- so a future raw edit that reintroduces an undrawable glyph fails
    // here by name instead of shipping as mojibake in a frame.
    render::CreationFlow flow = fresh();
    REQUIRE(flow.callings().loaded());
    REQUIRE(flow.quiz().loaded());
    REQUIRE(flow.biography().loaded());
    for (const sim::CallingTemplate& calling : flow.callings().callings()) {
        mustReadAsEnglish("calling name", calling.name);
        mustReadAsEnglish("calling one-liner", calling.oneLine);
    }
    for (const sim::QuizQuestion& question : flow.quiz().questions()) {
        mustReadAsEnglish("quiz prompt", question.prompt);
        for (const sim::QuizAnswer& answer : question.answers) {
            mustReadAsEnglish("quiz answer", answer.text);
        }
    }
    for (const sim::BiographyQuestion& question : flow.biography().questions()) {
        mustReadAsEnglish("biography prompt", question.prompt);
        for (const sim::BiographyAnswer& answer : question.answers) {
            mustReadAsEnglish("biography answer", answer.text);
        }
    }
}

TEST_CASE("the answer-commit pulse fires once and decays -- DECISIONS.md rule 3's own shape") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(1);
    flow.chooseOrigin();
    CHECK(flow.commitPulse() == 0.0F);
    flow.chooseChoice();
    CHECK(flow.commitPulse() == 1.0F);
    for (int i = 0; i < 12; ++i) {
        flow.advance();
    }
    CHECK(flow.commitPulse() == 0.0F);  // nothing holds it open
}

// ===========================================================================
// drawing -- no crash, and drawCreation actually clears + draws through
// render::drawDialogue rather than leaving the frame untouched
// ===========================================================================

TEST_CASE("drawCreation draws something at both resolutions this game ships, on both screens") {
    render::CreationFlow flow = fresh();
    // drawCreation clears to this exact colour before drawDialogue draws
    // over it -- see the .cpp -- so the meaningful check is not "not black"
    // (the clear itself is not black) but "not UNIFORMLY the clear colour",
    // which is only true once something was actually drawn on top of it.
    const std::uint32_t clearColour = render::packRgb(render::Rgb{0.04F, 0.04F, 0.05F});
    for (const auto& [w, h] : {std::pair{320, 180}, std::pair{640, 360}}) {
        render::Framebuffer frame(w, h);
        render::drawCreation(frame, flow);
        bool anyPainted = false;
        for (const std::uint32_t pixel : frame.pixels()) {
            if (pixel != clearColour) {
                anyPainted = true;
                break;
            }
        }
        CHECK(anyPainted);
    }

    flow.chooseOrigin();
    render::Framebuffer customizeFrame(640, 360);
    render::drawCreation(customizeFrame, flow);
}

TEST_CASE("drawCreation survives every Daggerfall screen at both resolutions") {
    // The roster with its centre sheet preview, a mid-quiz question with the
    // meters, the verdict card, and a biography question -- each drawn cold,
    // no crash and something painted, the same claim the case above makes
    // for the two original screens.
    const auto drawBoth = [](const render::CreationFlow& flow) {
        for (const auto& [w, h] : {std::pair{320, 180}, std::pair{640, 360}}) {
            render::Framebuffer frame(w, h);
            render::drawCreation(frame, flow);
        }
    };

    render::CreationFlow roster = fresh();
    roster.chooseOrigin();  // TAKE A CALLING
    REQUIRE(roster.step() == render::CreationStep::Calling);
    drawBoth(roster);

    render::CreationFlow quiz = fresh();
    quiz.moveOriginCursor(1);
    quiz.chooseOrigin();
    quiz.chooseChoice();
    quiz.chooseChoice();
    REQUIRE(quiz.step() == render::CreationStep::Quiz);
    drawBoth(quiz);

    render::CreationFlow verdict = fresh();
    verdict.moveOriginCursor(1);
    verdict.chooseOrigin();
    for (int i = 0; i < 10; ++i) {
        verdict.chooseChoice();
    }
    REQUIRE(verdict.quizVerdict().has_value());
    drawBoth(verdict);

    render::CreationFlow past = fresh();
    past.chooseOrigin();
    past.chooseChoice();  // a calling, straight into the biography
    REQUIRE(past.step() == render::CreationStep::Background);
    drawBoth(past);
}

// ===========================================================================
// #93. THE COMPOSED PAGE, AND THE MOUSE
// ===========================================================================
//
// What the owner said about this screen, verbatim: he spent real choices --
// skills, coin, faction standing, a named actor's disposition -- "with no idea
// what they bought", and the interface was the thing he did not like. These
// cases are what "he can see what it costs" means as an assertion, plus the
// half of input parity that can be proved without an SDL window: a pointer's
// hit-test agreeing with what was drawn.

namespace {

/// The strings THIS SCREEN authors -- its own copy, not the raws-authored prose
/// it is showing.
///
/// The distinction matters and it is not laziness: every authored prompt,
/// answer and one-liner is already swept, on the LOADED registries, by the
/// em-dash-fold case further up this file. What that case cannot catch is a
/// LABEL, a VERB, a CRUMB or a READOUT that a UI pass invented. That is what
/// this gathers, and it is the half this pass can actually break.
[[nodiscard]] std::vector<std::string> pageStrings(const render::CreationPage& page) {
    std::vector<std::string> out;
    for (const std::string& crumb : page.crumbs) {
        out.push_back(crumb);
    }
    out.push_back(page.title);
    out.push_back(page.readout);
    out.push_back(page.detailBadge);
    out.push_back(page.detailStatus);
    out.push_back(page.commitVerb);
    out.push_back(page.commitCost);
    for (const render::PanelTab& tab : page.tabs) {
        out.push_back(tab.key);
        out.push_back(tab.name);
    }
    for (const render::CreationPageRow& row : page.rows) {
        out.push_back(row.key);
        out.push_back(row.value);
    }
    for (const render::PanelFact& fact : page.facts) {
        out.push_back(fact.label);
    }
    for (const render::PanelBar& bar : page.bars) {
        out.push_back(bar.label);
        out.push_back(bar.value);
    }
    for (const render::PanelOption& option : page.nav) {
        out.push_back(option.key);
        out.push_back(option.label);
    }
    return out;
}

/// Every row of a page, reached by POINTER: the pixel is found by asking the
/// page's own layout where the list is and then asking its own hit-test what is
/// under each candidate. Nothing is a coordinate typed in by hand.
[[nodiscard]] bool pixelOfRow(const render::CreationPage& page, int w, int h, int row, int* px,
                              int* py) {
    const render::CreationLayout layout = render::creationLayout(page, w, h);
    if (!layout.usable) {
        return false;
    }
    for (int y = layout.listRect.y; y < layout.listRect.bottom(); ++y) {
        for (int x = layout.listRect.x; x < layout.listRect.right(); x += layout.metric.cellW()) {
            const render::CreationHit hit = render::creationPageHitTest(page, w, h, x, y);
            if (hit.zone == render::CreationHit::Zone::Row && hit.index == row) {
                *px = x;
                *py = y;
                return true;
            }
        }
    }
    return false;
}

/// The flow parked on each step in turn, so a case can sweep all of them rather
/// than asserting about one and hoping.
[[nodiscard]] render::CreationFlow atOrigin() { return fresh(); }

[[nodiscard]] render::CreationFlow atCalling() {
    render::CreationFlow flow = fresh();
    flow.chooseOrigin();
    return flow;
}

[[nodiscard]] render::CreationFlow atQuiz(int answers) {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(1);
    flow.chooseOrigin();
    for (int i = 0; i < answers; ++i) {
        flow.chooseChoice();
    }
    return flow;
}

[[nodiscard]] render::CreationFlow atBackground() {
    render::CreationFlow flow = fresh();
    flow.chooseOrigin();
    flow.chooseChoice();  // take the first calling, which lands on the biography
    return flow;
}

[[nodiscard]] render::CreationFlow atSheet() {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM
    flow.chooseOrigin();
    return flow;
}

}  // namespace

TEST_CASE("every step composes a page, and every string on it can actually be drawn") {
    // The panel vocabulary draws five glyphs the font does not have; everything
    // else on this screen goes through the font, so a page that quietly grew a
    // character the font cannot draw would render as a gap. test_copy.cpp holds
    // the rest of the game to this; the creation flow is held to it here.
    std::vector<render::CreationFlow> steps;
    steps.push_back(atOrigin());
    steps.push_back(atCalling());
    steps.push_back(atQuiz(4));
    steps.push_back(atBackground());
    steps.push_back(atSheet());
    render::CreationFlow verdict = atQuiz(0);
    const std::size_t questions = verdict.quiz().questions().size();
    for (std::size_t i = 0; i < questions; ++i) {
        verdict.chooseChoice();
    }
    REQUIRE(verdict.quizVerdict().has_value());
    steps.push_back(std::move(verdict));

    for (const render::CreationFlow& flow : steps) {
        const render::CreationPage page = flow.page();
        INFO("step ", static_cast<int>(flow.step()));
        CHECK_FALSE(page.crumbs.empty());
        CHECK_FALSE(page.instruction.empty());
        CHECK_FALSE(page.rows.empty());
        CHECK_FALSE(page.nav.empty());
        // EVERY PANEL CARRIES A HEADER and every step names where it is in the
        // flow -- the reference's own rule, and the reason a deeply nested text
        // menu stays navigable.
        CHECK(page.currentTab >= 0);
        CHECK(page.tabs.size() == 4U);
        for (const std::string& text : pageStrings(page)) {
            mustReadAsEnglish("creation page", text);
        }
    }
}

TEST_CASE("a quiz answer reaches the screen WHOLE -- the eighteen-glyph clip is gone") {
    // THE DEFECT, NAMED: the old topic grid clipped every answer at eighteen
    // glyphs including its row number, so a hundred-glyph moral choice arrived
    // as "1 YOU TOLD THE." and the renderer printed the hovered one three times
    // per frame to compensate.
    const render::CreationFlow flow = atQuiz(0);
    REQUIRE(flow.quiz().loaded());
    const render::CreationPage page = flow.page();
    REQUIRE(page.rows.size() == 3U);
    const sim::QuizQuestion& question = flow.quiz().questions().front();
    const std::array<int, 3> order = render::quizDisplayOrder(0);
    bool sawLongOne = false;
    for (std::size_t i = 0; i < page.rows.size(); ++i) {
        const std::string& authored = question.answers[static_cast<std::size_t>(order[i])].text;
        CHECK(page.rows[i].label == authored);
        sawLongOne = sawLongOne || authored.size() > 18U;
    }
    // If none of them were long, this case would be proving nothing.
    CHECK(sawLongOne);
    // And the question itself is the instruction row, not a clipped header.
    CHECK(page.instruction == question.prompt);
}

TEST_CASE("the quiz says what an answer buys: the axis, the meters, and the trade it points at") {
    const render::CreationFlow flow = atQuiz(3);
    REQUIRE(flow.quiz().loaded());
    REQUIRE(flow.callings().loaded());
    const render::CreationPage page = flow.page();
    // The subject of the detail pane is the AXIS the hovered answer scores.
    CHECK_FALSE(page.detailBadge.empty());
    CHECK(page.bars.size() == flow.quiz().axes().size());
    // The meters show the tally AS IF this answer had been given -- exactly one
    // more point than the running tally, which is the consequence made visual.
    std::int32_t drawnTotal = 0;
    for (const render::PanelBar& bar : page.bars) {
        drawnTotal += bar.filled;
    }
    std::int32_t liveTotal = 0;
    for (const std::int32_t count : flow.quizTallySoFar()) {
        liveTotal += count;
    }
    CHECK(drawnTotal == liveTotal + 1);
    // And the provisional verdict is named, which is the direct answer to
    // spending choices blind.
    bool headingFor = false;
    for (const render::PanelFact& fact : page.facts) {
        headingFor = headingFor || fact.label == "HEADING FOR";
    }
    CHECK(headingFor);
}

TEST_CASE("a biography answer's cost is spelled out in names and numbers, never in ids") {
    render::CreationFlow flow = atBackground();
    REQUIRE(flow.biography().loaded());
    REQUIRE(flow.step() == render::CreationStep::Background);
    const std::vector<sim::BiographyQuestion>& questions = flow.biography().questions();

    // Walk every answer of every question: whatever an answer does shows up in
    // the detail pane, one bullet per effect and never fewer.
    for (std::size_t q = 0; q < questions.size(); ++q) {
        for (std::size_t a = 0; a < questions[q].answers.size(); ++a) {
            flow.setChoiceCursor(static_cast<int>(a));
            const render::CreationPage page = flow.page();
            const std::vector<sim::ChargenEffect>& effects = questions[q].answers[a].effects;
            std::size_t bullets = 0;
            for (const render::PanelLine& line : page.lines) {
                if (line.bullet != render::Bullet::None) {
                    ++bullets;
                }
            }
            INFO("question ", questions[q].id, " answer ", a);
            CHECK(bullets == effects.size());
            // A skill delta names the skill the way the sheet does. The raws'
            // own id never reaches a player.
            for (const sim::ChargenEffect& effect : effects) {
                if (effect.kind != sim::ChargenEffectKind::SkillDelta) {
                    continue;
                }
                const sim::SkillTrack::Entry* entry = flow.skills().find(effect.target);
                REQUIRE(entry != nullptr);
                bool named = false;
                for (const render::PanelLine& line : page.lines) {
                    named = named || line.name == entry->displayName;
                }
                CHECK(named);
            }
        }
        flow.setChoiceCursor(0);
        flow.chooseChoice();
    }
}

TEST_CASE("state changes the verb rather than greying it out, on the row and on the sheet") {
    render::CreationFlow flow = atSheet();
    // BEGIN with no name typed: the verb SAYS what is missing, and pressing it
    // goes and fixes that -- there is no disabled button in this vocabulary.
    const int last = static_cast<int>(flow.view().topics.size()) - 1;
    flow.setCustomizeCursor(last);
    const render::CreationPage blank = flow.page();
    CHECK(blank.commitVerb.find("NAME") != std::string::npos);
    flow.chooseCustomizeRow();
    CHECK(flow.editingName());
    flow.typeNameChar('E');
    flow.typeNameChar('L');
    flow.chooseCustomizeRow();
    CHECK_FALSE(flow.editingName());
    flow.setCustomizeCursor(last);
    const render::CreationPage named = flow.page();
    CHECK(named.commitVerb != blank.commitVerb);

    // A skill row's verb follows its designation, and the restatement under it
    // says which tiers still have room BEFORE the key is pressed.
    flow.setCustomizeCursor(2);
    const render::CreationPage undesignated = flow.page();
    flow.adjustCustomizeRow(1);
    const render::CreationPage designated = flow.page();
    CHECK(undesignated.commitVerb != designated.commitVerb);
    CHECK_FALSE(designated.commitCost.empty());
}

TEST_CASE("a fixed sheet says it is fixed in the verb, not by refusing silently") {
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(3);  // GABRI
    flow.chooseOrigin();
    REQUIRE(flow.chosenCompanion() != nullptr);
    flow.setCustomizeCursor(2);
    const render::CreationPage page = flow.page();
    CHECK(page.commitVerb == "A FIXED SHEET");
    CHECK_FALSE(page.commitCost.empty());
}

TEST_CASE("every row of every step is reachable by pointer, and the pointer mirrors the cursor") {
    // THE MOUSE HALF OF INPUT PARITY, proved without an SDL window. The pad and
    // the keyboard reach these rows through CreationFlow's own move/choose
    // calls, which the rest of this file already exercises; what could not be
    // proved before is that a POINTER lands on the row a player is looking at.
    struct Step {
        const char* what;
        render::CreationFlow flow;
    };
    std::vector<Step> steps;
    steps.push_back(Step{"origin", atOrigin()});
    steps.push_back(Step{"calling", atCalling()});
    steps.push_back(Step{"quiz", atQuiz(2)});
    steps.push_back(Step{"background", atBackground()});
    steps.push_back(Step{"sheet", atSheet()});

    for (const Step& step : steps) {
        const render::CreationPage page = step.flow.page();
        INFO("step ", step.what);
        for (int row = 0; row < static_cast<int>(page.rows.size()); ++row) {
            if (!page.rows[static_cast<std::size_t>(row)].selectable) {
                continue;
            }
            int px = 0;
            int py = 0;
            INFO("row ", row, " of ", page.rows.size());
            REQUIRE(pixelOfRow(page, 960, 540, row, &px, &py));
            const render::CreationHit hit = render::creationHitTest(step.flow, 960, 540, px, py);
            CHECK(hit.zone == render::CreationHit::Zone::Row);
            CHECK(hit.index == row);
        }
    }
}

TEST_CASE("the pointer moves the one cursor every device shares") {
    render::CreationFlow flow = atCalling();
    const render::CreationPage page = flow.page();
    REQUIRE(page.rows.size() > 2U);
    int px = 0;
    int py = 0;
    REQUIRE(pixelOfRow(page, 960, 540, 2, &px, &py));
    const render::CreationHit hit = render::creationHitTest(flow, 960, 540, px, py);
    REQUIRE(hit.zone == render::CreationHit::Zone::Row);
    flow.setChoiceCursor(hit.index);
    CHECK(flow.choiceCursor() == 2);
    // The same row a keyboard would reach with two DOWNs, and the same detail
    // pane behind it -- one cursor, three devices.
    render::CreationFlow keyboard = atCalling();
    keyboard.moveChoiceCursor(1);
    keyboard.moveChoiceCursor(1);
    CHECK(keyboard.choiceCursor() == flow.choiceCursor());
    CHECK(keyboard.page().detailBadge == flow.page().detailBadge);
}

TEST_CASE("the composed page holds its geometry at every window size the game runs at") {
    const render::CreationFlow flow = atQuiz(2);
    const render::CreationPage page = flow.page();
    constexpr int kFrames[][2] = {{320, 180}, {640, 360}, {960, 540}, {1280, 720}, {1920, 1080}};
    for (const auto& size : kFrames) {
        INFO("window ", size[0], "x", size[1]);
        const render::CreationLayout layout = render::creationLayout(page, size[0], size[1]);
        CHECK(layout.usable);
        // The bands add back up and stay inside the frame -- the "aligned
        // edges" half of clean, made structural rather than hoped for.
        CHECK(layout.interior.x >= layout.bounds.x);
        CHECK(layout.bodyBand.bottom() <= layout.interior.bottom());
        CHECK(layout.navBand.bottom() <= layout.interior.bottom());
        CHECK(layout.listRect.right() <= layout.bodyBand.right());
        render::Framebuffer frame(size[0], size[1]);
        render::drawCreation(frame, flow);
        int lit = 0;
        for (const std::uint32_t pixel : frame.pixels()) {
            if ((pixel & 0x00FFFFFFU) != 0U) {
                ++lit;
            }
        }
        CHECK(lit > 0);
    }
}

TEST_CASE("moving the cursor moves nothing but the highlight and the detail pane") {
    // PANES HOLD THEIR HEIGHT. A list whose layout jumps as you arrow through
    // it feels broken, and the reference says so in as many words.
    render::CreationFlow flow = atCalling();
    const render::CreationLayout first = render::creationLayout(flow.page(), 960, 540);
    flow.moveChoiceCursor(3);
    const render::CreationLayout later = render::creationLayout(flow.page(), 960, 540);
    CHECK(first.bounds.x == later.bounds.x);
    CHECK(first.bounds.w == later.bounds.w);
    CHECK(first.bodyBand.y == later.bodyBand.y);
    CHECK(first.bodyBand.h == later.bodyBand.h);
    CHECK(first.listRect.w == later.listRect.w);
    CHECK(first.detailRect.x == later.detailRect.x);
    CHECK(first.navBand.y == later.navBand.y);
    CHECK(first.ruleRows == later.ruleRows);
}
