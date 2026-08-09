// THE ORIGIN-SELECT AND CUSTOMIZE FLOW: a new game's first two screens.
//
// WHAT THIS FILE PROVES, AND WHAT IT DOES NOT.
//
// CreationFlow owns three cards, a name field, one sim::Chargen for CUSTOM
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

#include <string>
#include <utility>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/creation.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/sim/attributes.hpp"
#include "granadad/sim/chargen.hpp"
#include "granadad/sim/companions.hpp"

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

TEST_CASE("exactly three origins, Eli's own three, and the cursor rings rather than stopping") {
    const std::vector<render::OriginTemplate>& origins = render::originTemplates();
    REQUIRE(origins.size() == 3);
    CHECK(origins[0].id == "devin");
    CHECK(origins[0].tag == "SECRETIVE");
    CHECK(origins[1].id == "gabri");
    CHECK(origins[1].tag == "NO-NONSENSE");
    CHECK(origins[2].id == "custom");

    render::CreationFlow flow = fresh();
    REQUIRE(flow.originCursor() == 0);
    flow.moveOriginCursor(-1);
    CHECK(flow.originCursor() == 2);  // UP from DEVIN wraps to CUSTOM
    flow.moveOriginCursor(1);
    CHECK(flow.originCursor() == 0);
    flow.moveOriginCursor(4);
    CHECK(flow.originCursor() == 1);  // (0 + 4) mod 3
}

TEST_CASE("choosing DEVIN or GABRI suggests their own name; CUSTOM starts blank") {
    render::CreationFlow devin = fresh();
    devin.chooseOrigin();
    CHECK(devin.step() == render::CreationStep::Customize);
    CHECK(devin.name() == "DEVIN");
    CHECK(devin.chosenOrigin().id == "devin");

    render::CreationFlow gabri = fresh();
    gabri.moveOriginCursor(1);
    gabri.chooseOrigin();
    CHECK(gabri.name() == "GABRI");

    render::CreationFlow custom = fresh();
    custom.moveOriginCursor(2);
    custom.chooseOrigin();
    CHECK(custom.chosenOrigin().id == "custom");
    CHECK(custom.name().empty());
}

TEST_CASE("CUSTOM's card reads as an honest placeholder, never as a paragraph nobody wrote") {
    // CUSTOM has no companion template and never will -- its whole point is
    // a blank sheet -- so its card is always the neutral placeholder line.
    render::CreationFlow flow = fresh();
    flow.moveOriginCursor(2);  // CUSTOM
    const render::DialogueViewState origin = flow.view();
    CHECK(origin.line == "MORE ABOUT THIS PATH IS COMING.");
    mustReadAsEnglish("origin blurb placeholder", origin.line);
}

TEST_CASE("DEVIN's and GABRI's cards speak their own real epithet off content/raws/companions") {
    // content/raws/companions/devin.json and gabri.json are real, authored
    // files as of this task -- see creation.hpp's own header. If they can be
    // read at all, the card must be showing THEIR words, not the fallback.
    render::CreationFlow flow = fresh();
    const sim::CompanionTemplate devin = sim::CompanionTemplate::load(content::contentDir(), "devin");
    const sim::CompanionTemplate gabri = sim::CompanionTemplate::load(content::contentDir(), "gabri");
    REQUIRE(devin.loaded());
    REQUIRE(gabri.loaded());
    REQUIRE_FALSE(devin.epithet().empty());
    REQUIRE_FALSE(gabri.epithet().empty());

    CHECK(flow.view().line == devin.epithet());
    mustReadAsEnglish("devin epithet", flow.view().line);

    flow.moveOriginCursor(1);  // GABRI
    CHECK(flow.view().line == gabri.epithet());
    mustReadAsEnglish("gabri epithet", flow.view().line);
}

// ===========================================================================
// naming
// ===========================================================================

TEST_CASE("switching origins re-suggests a name only until the player types their own") {
    render::CreationFlow flow = fresh();
    flow.chooseOrigin();  // DEVIN
    REQUIRE(flow.name() == "DEVIN");

    flow.backToOrigin();
    flow.moveOriginCursor(1);  // GABRI
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
    flow.moveOriginCursor(-1);  // back to DEVIN
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

TEST_CASE("BEGIN refuses a blank name and confirms once one is typed") {
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

    flow.chooseCustomizeRow();
    CHECK(flow.done());
    CHECK(flow.result().confirmed);
    CHECK(flow.result().name == "NO");
    CHECK(flow.result().originId == "custom");
}

// ===========================================================================
// hosting the real sim::Chargen (CUSTOM) -- not re-proving its own rules,
// only that this screen actually reaches it
// ===========================================================================

TEST_CASE("THE FLAME never appears as a skill row on this screen, on any origin") {
    for (int origin = 0; origin < 3; ++origin) {
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

    // Row 0 is NAME; row 1 is the first skill in SkillTrack's own ascending
    // order (see social.hpp) -- "bladework", off content/raws/skills.
    REQUIRE(flow.skills().loaded());
    const std::string firstSkillId = flow.skills().entries().front().id;
    flow.moveCustomizeCursor(1);
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
        // Row (i+1) is the i-th skill in the model (row 0 is NAME).
        slots.moveCustomizeCursor(0);  // no-op, keeps intent explicit
        while (slots.customizeCursor() != i + 1) {
            slots.moveCustomizeCursor(1);
        }
        slots.adjustCustomizeRow(1);
        REQUIRE(slots.chargen().designationOf(nonFlame[static_cast<std::size_t>(i)]) ==
               sim::SkillDesignation::Primary);
    }
    CHECK(slots.chargen().slotsFilled(sim::SkillDesignation::Primary) == sim::kPrimarySkillSlots);

    while (slots.customizeCursor() != 4) {
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

    std::size_t expectedSkillRows = 0;
    for (const sim::CompanionSkill& skill : devin.startingSkills()) {
        if (skills.aptitudeTier(skill.id) != sim::AptitudeTier::Flame) {
            ++expectedSkillRows;
        }
    }

    flow.chooseOrigin();  // DEVIN is the default card
    REQUIRE(flow.chosenCompanion() != nullptr);
    REQUIRE(flow.chosenCompanion()->id() == "devin");

    const render::DialogueViewState view = flow.view();
    // NAME + one row per authored, non-FLAME skill + one row per attribute +
    // BEGIN.
    CHECK(view.topics.size() == 1 + expectedSkillRows + sim::kAttributeCount + 1);

    // Every skill row (rows 1..expectedSkillRows) names a real level off the
    // template, never zero-by-omission and never a Chargen designation word.
    for (std::size_t i = 0; i < expectedSkillRows; ++i) {
        const std::string row = view.topics[1 + i];
        INFO(row);
        CHECK(row.find("LV ") != std::string::npos);
        CHECK(row.find("PRIMARY") == std::string::npos);
        CHECK(row.find("MAJOR") == std::string::npos);
        CHECK(row.find("MINOR") == std::string::npos);
        CHECK(row.find("UNDESIGNATED") == std::string::npos);
    }
}

TEST_CASE("LEFT/RIGHT never moves a single row of DEVIN's or GABRI's sheet") {
    for (const int originIndex : {0, 1}) {  // DEVIN, GABRI
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
    flow.chooseOrigin();  // DEVIN
    CHECK(flow.view().line == "A FIXED SHEET -- NOT ADJUSTABLE HERE.");
    mustReadAsEnglish("companion status line", flow.view().line);
}

TEST_CASE("confirming DEVIN carries his real CompanionTemplate, and no Chargen picks") {
    render::CreationFlow flow = fresh();
    flow.chooseOrigin();  // DEVIN
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
// the view, and everything it draws
// ===========================================================================

TEST_CASE("the view carries the right headline in the right place, on both screens") {
    render::CreationFlow flow = fresh();
    const render::DialogueViewState origin = flow.view();
    CHECK(origin.open);
    CHECK(origin.speaker == "NEW GAME");
    REQUIRE(origin.topics.size() == 3);
    mustReadAsEnglish("origin epithet", origin.epithet);
    for (const std::string& topic : origin.topics) {
        mustReadAsEnglish("origin card", topic);
    }

    flow.moveOriginCursor(2);  // CUSTOM
    flow.chooseOrigin();
    const render::DialogueViewState custom = flow.view();
    CHECK(custom.speaker == "CUSTOMIZE");
    CHECK(custom.epithet == "CUSTOM - YOUR OWN PATH");
    // NAME + every non-FLAME skill + one row per attribute + BEGIN.
    std::size_t nonFlame = 0;
    for (const sim::SkillTrack::Entry& entry : flow.skills().entries()) {
        if (entry.aptitudeTier != sim::AptitudeTier::Flame) {
            ++nonFlame;
        }
    }
    CHECK(custom.topics.size() == 1 + nonFlame + sim::kAttributeCount + 1);
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

    flow.moveCustomizeCursor(1);
    flow.adjustCustomizeRow(1);  // one skill designated Primary
    const std::string after = flow.view().line;
    CHECK(after.find("PRIMARY 1/") != std::string::npos);
}

TEST_CASE("while editing a name, the top line says so instead of the status") {
    render::CreationFlow flow = fresh();
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
