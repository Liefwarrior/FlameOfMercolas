// THE GRIMOIRE PAGE AND THE QUICK BAR IT LOADS -- the Spells build's own
// round-trip, driven the way test_pause.cpp drives its page: straight at
// Session, because Session owns toggleGrimoire/grimoireRows/adjustGrimoireSlot/
// chooseGrimoireRow/selectQuickSlot and none of it touches SDL.
//
// WHAT VERIFICATION GAP #77 SAID, AND WHAT CLOSES IT. The quick bar's own
// header carried "the BINDING is real ... the CONTENTS are not built" from #85
// until this build; S13's verify then flagged that Tavern::equipSpellAt had
// zero callers, so the player could never switch what C casts. The cases here
// pin the whole closed loop: a crafting bound to a slot off the page, the slot
// selected off the number row, the hand re-pointed through the ONE equip door,
// and the CAST row (spellLine) agreeing with all of it.
//
// The keyboard-to-Session wiring in main.cpp (the QuickWheel tap that opens
// the page, route_menu_key's grimoire branch) is NOT covered here, for the
// same reason test_pause.cpp gives: closing that hole needs a real SDL
// harness.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/spellforge.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

namespace {

[[nodiscard]] render::SessionConfig fresh() {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 8 * 3600;
    config.openingPage = true;
    return config;
}

/// A session with the opening casebook page already put down -- the same
/// shape test_pause.cpp's standing() has, for the same reason.
[[nodiscard]] render::Session standing() {
    render::Session session(fresh());
    MoveInput walk;
    walk.forward = 1;
    session.step(walk);
    REQUIRE_FALSE(session.casebookOpen());
    REQUIRE_FALSE(session.grimoireOpen());
    return session;
}

/// Stocks the grimoire the way the priest's teaching does, off the authored
/// shelf -- the identical shortcut test_tavern.cpp's cast cases take.
void learn(render::Session& session, std::string_view id) {
    const Spell* spell = session.tavern().spellbook().find(id);
    REQUIRE(spell != nullptr);
    REQUIRE(session.tavern().dialogue().grimoire().learn(*spell));
}

}  // namespace

TEST_CASE("the empty grimoire page speaks the cast refusal's own words") {
    render::Session session = standing();
    session.toggleGrimoire();
    REQUIRE(session.grimoireOpen());

    const render::DialogueViewState view = session.dialogueView();
    CHECK(view.open);
    CHECK(view.speaker == "GRIMOIRE");
    CHECK(view.topics.empty());
    // THE COMMON STATE, in one voice: the page and the C key name the same
    // door, or one of them is lying about where casting starts.
    CHECK(view.line == "NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES.");

    // The key that opened it closes it, and it stands down for the others.
    session.toggleGrimoire();
    CHECK_FALSE(session.grimoireOpen());
    session.toggleGrimoire();
    session.togglePause();
    CHECK_FALSE(session.grimoireOpen());
    CHECK(session.pauseOpen());
}

TEST_CASE("a stocked page lists name, difficulty and READY, and binds a slot") {
    render::Session session = standing();
    learn(session, "sting");
    learn(session, "scald");
    session.toggleGrimoire();
    REQUIRE(session.grimoireOpen());

    // Grimoire order is id order: scald first, and -- being the default
    // equip with nothing yet picked -- already READY.
    const std::vector<std::string> rows = session.grimoireRows();
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].find("SCALD") != std::string::npos);
    CHECK(rows[1].find("STING") != std::string::npos);
    CHECK(rows[0].find("READY") != std::string::npos);
    CHECK(rows[1].find("READY") == std::string::npos);
    // The cost model's own number beside each name -- information the check
    // is rolled against, never a discount.
    const Spell* scald = session.tavern().spellbook().find("scald");
    REQUIRE(scald != nullptr);
    CHECK(rows[0].find("D" + std::to_string(spellDifficulty(*scald))) != std::string::npos);

    // RIGHT walks the cursor row's binding onto slot 1 and the row says so;
    // LEFT walks it back off to NONE.
    session.adjustGrimoireSlot(1);
    REQUIRE(session.tavern().slotSpell(0) != nullptr);
    CHECK(session.tavern().slotSpell(0)->id == "scald");
    CHECK(session.grimoireRows()[0].find("SLOT 1") != std::string::npos);
    session.adjustGrimoireSlot(-1);
    CHECK(session.tavern().slotSpell(0) == nullptr);
    CHECK(session.grimoireRows()[0].find("SLOT") == std::string::npos);

    // Picking the second row equips it -- the same equipSpellAt door the
    // quick bar uses, so READY moves with it.
    session.chooseGrimoireRow(1);
    REQUIRE(session.tavern().equippedSpell() != nullptr);
    CHECK(session.tavern().equippedSpell()->id == "sting");
    CHECK(session.grimoireRows()[1].find("READY") != std::string::npos);
}

TEST_CASE("selecting a loaded quick slot equips it; an empty one says so and "
          "leaves the hand alone") {
    render::Session session = standing();
    learn(session, "sting");
    learn(session, "scald");
    REQUIRE(session.tavern().bindSpellToSlot(4, "sting"));

    // The default hand is scald (front of the grimoire); slot 5 re-points it.
    session.selectQuickSlot(4);
    REQUIRE(session.tavern().equippedSpell() != nullptr);
    CHECK(session.tavern().equippedSpell()->id == "sting");
    // The dieted toast (UI-EA-SPEC sec. 5): slot and name, no READY caption.
    CHECK(session.lastMessage().find("SLOT 5 - STING") != std::string::npos);
    // The strip is up while the number row is in use -- its own EasedToggle's
    // target, which is what a headless case can honestly assert.
    CHECK(session.quickBarWanted());
    // ONE SOURCE OF TRUTH: the CAST row reads the same equipped id.
    CHECK(session.spellLine().find("STING") != std::string::npos);

    // An empty slot says so, out loud, and changes nothing.
    session.selectQuickSlot(6);
    CHECK(session.tavern().equippedSpell()->id == "sting");
    CHECK(session.lastMessage().find("EMPTY") != std::string::npos);
}
