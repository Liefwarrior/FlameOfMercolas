// THE CHARACTER SHEET: the five Legend tracks, the skills this build actually
// levels, and what the ward and the purse currently say.
//
// THE GAP THIS CLOSES. legend.hpp was built to answer "who am I in this city
// yet" on five tracks at once, and it has been derived fresh every frame since
// -- see its own header. Before this file, the only place any of it reached
// the screen was one HUD row (the track the player happens to be highest on)
// and Session::legendLine(), a summary sentence with exactly two callers and
// both of them tests. The other four tracks, and every rung of all five, were
// computed and thrown away every single frame with nowhere on screen to read
// them.
//
// WHAT THIS IS NOT. Still no paper doll and no armour rating. KIT BUILD
// (D10): there IS an item and an equipment-slot model now (sim/items.hpp),
// and the sheet carries it in words -- IN HAND always, a worn slot only when
// the raws hold an item for it, the LOAD line, and every carried row with
// its weight and worth -- after the five derived standings and the four
// skills a verb in this build actually levels, which are unchanged. The
// bare sheet is twenty-four rows now: the seventeen, IN HAND FISTS, the
// four worn slots the shipped raws populate (back, head, feet, belt), LOAD,
// and the five picks every body starts with.
//
// WHAT IS AND IS NOT TESTED HERE. Session owns toggleCharacter/characterRows,
// and dialogueView()'s characterOpen_ branch, and none of it touches SDL, so
// all of it is driven directly -- the same shape test_pause.cpp and
// test_casebook.cpp already use for the pages either side of this one. The
// keyboard-to-Session wiring in main.cpp's route_menu_key (C opens it, the
// arrows walk it) is not covered here for the same reason it never is on the
// other pages: that needs a real SDL harness.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/controls.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/menu_view.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/legend.hpp"

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

/// A session with the opening casebook page already put down, nothing else
/// open -- the state every case below wants to start from.
[[nodiscard]] render::Session standing() {
    render::Session session(fresh());
    MoveInput walk;
    walk.forward = 1;
    session.step(walk);  // closes the opening page, same as test_firstrun.cpp
    REQUIRE_FALSE(session.casebookOpen());
    REQUIRE_FALSE(session.characterOpen());
    return session;
}

}  // namespace

TEST_CASE("C opens the character sheet, and C closes it") {
    render::Session session = standing();

    session.toggleCharacter();
    CHECK(session.characterOpen());

    const render::DialogueViewState view = session.dialogueView();
    CHECK(view.open);
    CHECK(view.speaker == "CHARACTER");
    CHECK_FALSE(view.epithet.empty());
    CHECK_FALSE(view.line.empty());
    CHECK_FALSE(view.topics.empty());

    session.toggleCharacter();
    CHECK_FALSE(session.characterOpen());
    CHECK_FALSE(session.dialogueView().open);
}

TEST_CASE("a man who arrived this morning reads NOBODY IN PARTICULAR, not a blank epithet") {
    // legendLine() is empty at rung zero across the board -- see
    // test_casebook.cpp's "the ward has no opinion of a man who arrived this
    // morning" -- and the HUD's own rule is that absence costs nothing there.
    // A SHEET THE PLAYER OPENED ON PURPOSE is the opposite case: showing
    // nothing where the identity line goes would read as a rendering defect,
    // not as "you have not done anything yet". kReputationUnremarkable is the
    // exact phrase the rest of this build already uses for that thought.
    render::Session session = standing();
    REQUIRE(session.legend().totalRungs() == 0);
    REQUIRE(session.legendLine().empty());

    session.toggleCharacter();
    const render::DialogueViewState view = session.dialogueView();
    CHECK(view.epithet == std::string(kReputationUnremarkable));
}

TEST_CASE("the character sheet lists all five Legend tracks and the four skills this build levels") {
    render::Session session = standing();
    session.toggleCharacter();
    REQUIRE(session.characterOpen());

    const std::vector<std::string> rows = session.characterRows();
    // Five tracks, four wired skills, five faction ladders, and
    // REPUTATION/COIN/HEAT -- seventeen rows, fixed, because the simulation
    // has exactly this much to say about a player and no item system to pad
    // it with. See toggleCharacter's own header on why the fifth track's
    // neighbours are not sixteen more skill rows: the raws carry them,
    // nothing in this build levels them yet, and a wall of LV 0 would claim
    // the game is watching a skill it is not.
    // KIT BUILD: +7 -- IN HAND, four worn slots, LOAD, the starting picks.
    REQUIRE(rows.size() == kLegendTracks + 4 + 5 + 3 + 7);

    // THE FIVE TRACKS, IN legend.hpp's OWN ORDER -- Wire, Roofs, Flame, Trade,
    // Law -- each a short name and a rung title, "THE " dropped off the front
    // of the name (legendTrackName() still returns the full form everywhere
    // else; this is a display choice made once, here).
    CHECK(rows[0].rfind("WIRE", 0) == 0);
    CHECK(rows[1].rfind("ROOFS", 0) == 0);
    CHECK(rows[2].rfind("FLAME", 0) == 0);
    CHECK(rows[3].rfind("TRADE", 0) == 0);
    CHECK(rows[4].rfind("LAW", 0) == 0);
    // A fresh arrival is NOBODY on every track, and the row says so -- the
    // same title legend.hpp's own table gives rung zero. SHEETS BUILD: and
    // every track now prints what the next rung wants, in the score/threshold
    // notation every counted stage already uses -- legend.hpp's own header
    // promised exactly this line since S8 and no panel ever drew it. A fresh
    // arrival reads 0 over the first threshold on all five.
    const std::string firstRung = "0/" + std::to_string(kLegendThresholds[0]);
    for (std::size_t i = 0; i < kLegendTracks; ++i) {
        CHECK(rows[i].find("NOBODY") != std::string::npos);
        CHECK(rows[i].find(firstRung) != std::string::npos);
    }

    // THE FOUR SKILLS A VERB IN THIS BUILD ACTUALLY LEVELS, each at LV 0 for a
    // fresh arrival -- streetlevel, not raws-vocabulary. See sim::kRoofSkill,
    // kThieverySkill, kHaggleSkill (social.hpp) and kCraftingSkill
    // (spellforge.hpp): the only four call sites in this build that ever call
    // SkillTrack::use.
    CHECK(rows[5].rfind("SKYRUNNING", 0) == 0);
    CHECK(rows[6].rfind("CRACKSMANSHIP", 0) == 0);
    CHECK(rows[7].rfind("STREETWISE", 0) == 0);
    CHECK(rows[8].rfind("LINKCRAFT", 0) == 0);
    for (std::size_t i = 5; i < 9; ++i) {
        CHECK(rows[i].find("LV 0") != std::string::npos);
    }

    // THE FIVE LADDERS, WITH THE NUMBERS ON -- the owner's ruling for this
    // build: the player's OWN sheet shows rank title, standing number and
    // next-rung cost (the word-only ruling still governs how NPCs talk). In
    // the registry's own sorted order, factions.json's authoritative note:
    // dockhands=0, merchants=1, skyrunners=2, temple=3, watch=4. A fresh
    // arrival is on no roll, so every row reads its JOIN cost -- the first
    // rung is earned exactly like every later one, and ranks.json prices
    // them at 8/10/8/10/10 standing.
    CHECK(rows[9].rfind("DOCKHANDS", 0) == 0);
    CHECK(rows[10].rfind("MERCHANTS", 0) == 0);
    CHECK(rows[11].rfind("SKYRUNNERS", 0) == 0);
    CHECK(rows[12].rfind("TEMPLE", 0) == 0);
    CHECK(rows[13].rfind("WATCH", 0) == 0);
    for (std::size_t i = 9; i < 14; ++i) {
        CHECK(rows[i].find(" 0  JOIN ") != std::string::npos);
    }
    CHECK(rows[11].find("JOIN 8") != std::string::npos);
    CHECK(rows[13].find("JOIN 10") != std::string::npos);

    // AND WHAT THE WARD AND THE PURSE SAY, always present -- see
    // characterRows' own comment on why a sheet opened on purpose prints a
    // zero rather than dropping the row the way the ambient HUD would.
    CHECK(rows[14].rfind("REPUTATION", 0) == 0);
    CHECK(rows[14].find("NOBODY IN PARTICULAR") != std::string::npos);
    CHECK(rows[15].rfind("COIN", 0) == 0);
    CHECK(rows[16].rfind("HEAT", 0) == 0);
    CHECK(rows[16].find("HEAT  0") != std::string::npos);
}

TEST_CASE("a joined ladder's sheet row carries the rank title, the standing number and the next rung's price") {
    // SHEETS BUILD. The row must read the same rung join()/advance() will
    // actually measure -- FactionLedger::nextRung answers with checkRung's
    // own rung -- so the sheet can never promise a price the ladder does not
    // charge. Skyrunners rung 2 (ranks.json): 22 standing and SKYRUNNING 5,
    // so a fresh Tenant's row reads NEXT 22/LV5.
    render::Session session = standing();
    DialogueDirector& talk = session.tavern().dialogue();
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    REQUIRE(roofs >= 0);
    talk.standings().addStanding(roofs, 40);
    REQUIRE(talk.standings().join(roofs, talk.skills()) == LadderResult::Granted);

    session.toggleCharacter();
    const std::vector<std::string> rows = session.characterRows();
    const std::string& row = rows[11];
    INFO("row: ", row);
    CHECK(row.rfind("SKYRUNNERS TENANT", 0) == 0);
    // The standing number itself: 40 granted, minus whatever the join and the
    // mirror ledger moved -- read it back from the ledger rather than typing
    // a copy of the arithmetic here.
    CHECK(row.find(std::to_string(talk.standings().standing(roofs))) != std::string::npos);
    CHECK(row.find("NEXT 22/LV5") != std::string::npos);
}

TEST_CASE("IN HAND is always printed once the Kit exists: FISTS bare, THE EVICTOR when the world arms you") {
    // EVICTOR BUILD, re-ruled by D10 (KIT BUILD): the reference sheet's w
    // slot ("cane 1-6 Impact", UI-REFERENCE-TERMINAL.md) is ALWAYS here now,
    // because fists are a real state once an equipment model exists -- the
    // old "no furniture claiming a state we do not simulate" rule is what
    // the Kit retired. The grant puts THE EVICTOR in the Kit as a THING, so
    // the sheet gains a carried row for it too: twenty-four bare, twenty-five
    // armed, the IN HAND row itself in the same seat both times.
    render::Session session = standing();
    const std::vector<std::string> bare = session.characterRows();
    REQUIRE(bare.size() == 24);
    CHECK(bare[17] == "IN HAND  FISTS 3-5 IMPACT");
    CHECK(bare[18] == "ON THE BACK  NOTHING");
    CHECK(bare[19] == "ON THE HEAD  NOTHING");
    CHECK(bare[20] == "ON THE FEET  NOTHING");
    CHECK(bare[21] == "AT THE BELT  NOTHING");
    CHECK(bare[22].rfind("LOAD  ", 0) == 0);
    CHECK(bare[22].find(" / 240 DRAMS") != std::string::npos);
    CHECK(bare[23].rfind("5 PICKS", 0) == 0);

    // The same grant seam the eviction case's close beat calls, by the same
    // authored id -- see Tavern::grantPlayerWeapon.
    REQUIRE(session.tavern().grantPlayerWeapon(kEvictorWeaponId));
    const std::vector<std::string> armed = session.characterRows();
    REQUIRE(armed.size() == 25);
    CHECK(armed[17] == "IN HAND  THE EVICTOR 7-9 IMPACT");
    bool carried = false;
    for (const std::string& row : armed) {
        if (row.rfind("THE EVICTOR  44DR  30C  IN HAND", 0) == 0) {
            carried = true;
        }
    }
    CHECK(carried);
    // And the load moved by the cudgel's own drams.
    CHECK(armed[22].rfind("LOAD  49 / 240 DRAMS", 0) == 0);
}

TEST_CASE("twenty-four rows is three pages, and the character sheet turns like every other list here") {
    render::Session session = standing();
    session.toggleCharacter();
    REQUIRE(session.characterOpen());
    REQUIRE(session.characterRows().size() == 24);

    render::DialogueViewState view = session.dialogueView();
    CHECK(view.page == 0);
    CHECK(view.cursor == 0);

    // Walking past the ninth row turns the page, the identical contract
    // moveTopicCursor already gives the casebook and the keys page.
    for (int i = 0; i < 9; ++i) {
        session.moveTopicCursor(1);
    }
    view = session.dialogueView();
    CHECK(view.cursor == 9);
    CHECK(view.page == 1);
    CHECK(render::topicPageOf(view.cursor) == view.page);

    // 0 -- the MORE key -- turns the page directly, same as F1 and the
    // casebook.
    session.nextTopicPage();
    view = session.dialogueView();
    CHECK(view.page == 0);

    // The cursor wraps rather than stopping dead at either end.
    session.moveTopicCursor(-1);
    view = session.dialogueView();
    CHECK(view.cursor == 23);
}

// ---------------------------------------------------------------------------
// KIT BUILD: the Kit on the tile
// ---------------------------------------------------------------------------

TEST_CASE("a carried row prints its weight, its worth and its marks; a worn slot names what is on it") {
    render::Session session = standing();
    Tavern& tavern = session.tavern();
    REQUIRE(tavern.giveItem("coat"));
    REQUIRE(tavern.giveItem("cudgel"));
    REQUIRE(tavern.giveItem("rope"));
    REQUIRE(tavern.giveItem("dust", 2));
    const std::int32_t coat = tavern.items().indexOf("coat");
    const std::int32_t cudgel = tavern.items().indexOf("cudgel");
    REQUIRE(tavern.wearItem(coat).result == ServiceResult::Served);
    REQUIRE(tavern.wearItem(cudgel).result == ServiceResult::Served);
    REQUIRE(tavern.bindItemToSlot(2, cudgel));

    const std::vector<std::string> rows = session.characterRows();
    // Twenty-four bare, plus four carried rows (the picks were already one).
    REQUIRE(rows.size() == 28);
    CHECK(rows[17] == "IN HAND  CUDGEL 7-9 IMPACT");
    CHECK(rows[18] == "ON THE BACK  COAT  DR 2");
    CHECK(rows[22] == "LOAD  " + std::to_string(tavern.loadDrams()) + " / 240 DRAMS");
    // Registry order: the cudgel, the coat, the rope, the picks, the dust.
    CHECK(rows[23] == "CUDGEL  40DR  6C  IN HAND  SLOT 3");
    CHECK(rows[24] == "COAT  60DR  12C  WORN");
    CHECK(rows[25] == "ROPE  48DR  6C");
    CHECK(rows[26] == "5 PICKS  5DR  10C");
    CHECK(rows[27] == "2 DUST  6DR  32C  HOT");
    CHECK(session.loadLine() == rows[22]);
    CHECK(session.characterKitOffset() == 23);
    // The composed list behind the rows: the Kit's own rows are the only
    // ones a verb can act on.
    const std::vector<render::Session::KitRow> kit = session.kitRows();
    REQUIRE(kit.size() == 5);
    CHECK(kit[0].inKit);
    CHECK(kit[2].inKit);
    CHECK_FALSE(kit[3].inKit);  // the picks
    CHECK_FALSE(kit[4].inKit);  // the sack
}

TEST_CASE("ENTER on a carried row wears it or bares it; on a sheet row it does nothing") {
    render::Session session = standing();
    Tavern& tavern = session.tavern();
    REQUIRE(tavern.giveItem("coat"));
    REQUIRE(tavern.giveItem("rope"));
    session.toggleCharacter();
    REQUIRE(session.characterOpen());
    // Walk the cursor onto the coat: offset 23, the coat is the first
    // carried row (registry order puts it before the rope and the picks).
    const int coatRow = static_cast<int>(session.characterKitOffset());
    for (int i = 0; i < coatRow; ++i) {
        session.moveTopicCursor(1);
    }
    REQUIRE(session.highlightedKitRow().has_value());
    CHECK(session.highlightedKitRow()->item == tavern.items().indexOf("coat"));
    // The tile says what the press does, in the keyboard's own words.
    const std::string enter(render::promptConfirmKey(render::InputDevice::KeyboardMouse));
    CHECK(session.dialogueView().epithet == enter + " WEAR  LEFT RIGHT SLOT  X DROP");
    session.chooseTopic(static_cast<std::size_t>(session.topicCursor()));
    CHECK(tavern.kit().isWorn(tavern.items().indexOf("coat")));
    CHECK(session.lastMessage() == "COAT - ON.");
    CHECK(session.dialogueView().epithet == enter + " BARE  LEFT RIGHT SLOT  X DROP");
    session.chooseTopic(0);
    CHECK_FALSE(tavern.kit().isWorn(tavern.items().indexOf("coat")));
    CHECK(session.lastMessage() == "COAT - OFF.");
    // The rope: not worn, said so, and its epithet offers only the drop.
    session.moveTopicCursor(1);
    CHECK(session.highlightedKitRow()->item == tavern.items().indexOf("rope"));
    CHECK(session.dialogueView().epithet == "X DROP");
    session.chooseTopic(0);
    CHECK(session.lastMessage() == "THE ROPE IS NOT WORN.");
    // A sheet row is something to read.
    session.moveTopicCursor(-30);
    CHECK_FALSE(session.highlightedKitRow().has_value());
    const std::string before = session.lastMessage();
    session.chooseTopic(0);
    CHECK(session.lastMessage() == before);
}

TEST_CASE("LEFT and RIGHT on a carried row walk its quick slot, and X drops it at the feet") {
    render::Session session = standing();
    Tavern& tavern = session.tavern();
    REQUIRE(tavern.giveItem("cudgel"));
    session.toggleCharacter();
    const int row = static_cast<int>(session.characterKitOffset());
    for (int i = 0; i < row; ++i) {
        session.moveTopicCursor(1);
    }
    REQUIRE(session.highlightedKitRow().has_value());
    const std::int32_t cudgel = tavern.items().indexOf("cudgel");
    session.adjustKitSlot(1);
    CHECK(tavern.slotItemIndex(0) == cudgel);
    CHECK(session.lastMessage() == "CUDGEL -- SLOT 1.");
    session.adjustKitSlot(1);
    CHECK(tavern.slotItemIndex(0) == -1);
    CHECK(tavern.slotItemIndex(1) == cudgel);
    session.adjustKitSlot(-2);
    CHECK(tavern.slotItemIndex(1) == -1);
    CHECK(session.lastMessage() == "CUDGEL -- NO SLOT.");
    // The strip and the number key read the item slot: bind to 3, press 3.
    session.adjustKitSlot(3);
    CHECK(tavern.slotItemIndex(2) == cudgel);
    session.selectQuickSlot(2);
    CHECK(session.lastMessage() == "SLOT 3 - CUDGEL.");
    CHECK(tavern.playerWeapon() == Weapon::Blunt);
    CHECK(session.characterRows()[17] == "IN HAND  CUDGEL 7-9 IMPACT");
    // X: dropped at the feet, the row gone, the hand bare, the slot stale.
    session.toggleCharacter();
    const std::size_t rowsBefore = session.characterRows().size();
    session.dropHighlightedKitRow();
    CHECK(session.lastMessage() == "DROPPED - CUDGEL.");
    CHECK(session.characterRows().size() == rowsBefore - 1);
    CHECK(tavern.playerWeapon() == Weapon::Fists);
    CHECK(tavern.groundItemInReach() >= 0);
    session.selectQuickSlot(2);
    CHECK(session.lastMessage() == "SLOT 3 - CUDGEL. NOT ON YOU.");
}

TEST_CASE("a printed number moves the cursor on the character sheet and does nothing else") {
    // A CHARACTER SHEET ROW IS SOMETHING TO READ, NOT A CHOICE -- the same
    // no-op the keys page gives a number press, and for the identical reason:
    // there is nothing behind row six to choose.
    render::Session session = standing();
    session.toggleCharacter();
    REQUIRE(session.characterOpen());

    session.chooseVisibleTopic(3);
    CHECK(session.characterOpen());
    CHECK(session.dialogueView().cursor == 3);

    // And past the end of the list, nothing moves at all.
    session.chooseVisibleTopic(8);
    session.chooseVisibleTopic(3);  // back onto the grid first
    const int before = session.dialogueView().cursor;
    session.chooseTopic(static_cast<std::size_t>(before));
    CHECK(session.characterOpen());
    CHECK(session.dialogueView().cursor == before);
}

TEST_CASE("the character tile stays open alongside the map, letters and journal tiles, and is exclusive with keys, options and pause -- both ways") {
    // MORROWIND ROUND. Character is one of the tiled Menu's four SIMULTANEOUS
    // panels now (Session::casebookOpen()'s own header): opening any one of
    // the four opens all four together, so "exclusive with the casebook" is
    // no longer a claim this page can make about itself -- see
    // test_vertical_menu.cpp's own "Menu opens all four tiles at once" case
    // for that proof. What is still true, unchanged, is that the tiled Menu
    // as a WHOLE remains exclusive with Keys, Options and Pause, which is
    // this case's own remaining claim.
    render::Session session = standing();

    session.toggleCharacter();
    REQUIRE(session.characterOpen());
    CHECK(session.casebookOpen());
    CHECK(session.mapOpen());
    CHECK(session.lettersOpen());

    session.toggleKeys();
    CHECK(session.keysOpen());
    CHECK_FALSE(session.characterOpen());

    session.toggleCharacter();
    CHECK(session.characterOpen());
    CHECK_FALSE(session.keysOpen());

    session.toggleOptions();
    CHECK(session.optionsOpen());
    CHECK_FALSE(session.characterOpen());

    session.toggleCharacter();
    CHECK(session.characterOpen());
    CHECK_FALSE(session.optionsOpen());

    session.togglePause();
    CHECK(session.pauseOpen());
    CHECK_FALSE(session.characterOpen());

    session.toggleCharacter();
    CHECK(session.characterOpen());
    CHECK_FALSE(session.pauseOpen());
}

TEST_CASE("PagePrev/PageNext move focus onto the character tile without closing it") {
    // The Morrowind-round replacement for the old "toggleCasebook() steals
    // the page back" case above: switching focus to another tile no longer
    // closes this one, because there is no longer a "this one" to close --
    // all four are always open together.
    render::Session session = standing();

    session.toggleCharacter();
    REQUIRE(session.characterOpen());
    REQUIRE(session.menuFocus() == render::kMenuFocusCharacter);

    session.menuPageNext();  // Character -> Map
    CHECK(session.menuFocus() == render::kMenuFocusMap);
    CHECK(session.characterOpen());  // still open, just not focused

    session.menuPagePrev();  // back to Character
    CHECK(session.menuFocus() == render::kMenuFocusCharacter);
}

TEST_CASE("the character sheet never opens over a conversation or a pick in progress") {
    render::SessionConfig config = fresh();
    config.timeOfDay = 21 * 3600;
    config.spawnX = gull::kBartenderX;
    config.spawnY = gull::kBarY + 2;
    config.spawnBand = gull::kGroundBand;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);
    session.toggleCasebook();  // put the opening page down
    session.interact();
    REQUIRE(session.talking());

    session.toggleCharacter();
    CHECK_FALSE(session.characterOpen());
    CHECK(session.talking());
}

TEST_CASE("the tiled Menu covers the middle of the screen on purpose, unlike a live conversation") {
    // MORROWIND ROUND: THE CLAIM THIS CASE PROVES IS INVERTED FROM WHAT IT
    // USED TO BE. Character was one of #85's six single-panel pages and
    // shared drawDialogue's own centre-clear guarantee with every other one
    // of them; it is now one tile of a Morrowind-style tiled OVERVIEW
    // (menu_view.hpp), and that overview is deliberately exempt from the
    // centre-clear rule -- see that file's own header for why ("there is
    // nobody TO look at while it is up"). A live conversation (still
    // drawDialogue, still exempt from nothing) is the control: it MUST still
    // leave the centre alone, so this proves both halves of the claim at
    // once rather than only the new one.
    render::SessionConfig config = fresh();
    render::Session session(config);
    session.stepMany(MoveInput{}, 4);
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());

    render::Framebuffer plain(config.width, config.height);
    session.drawFrame(plain);

    session.toggleCharacter();
    REQUIRE(session.characterOpen());
    render::Framebuffer withMenu(config.width, config.height);
    session.drawFrame(withMenu);

    const render::CentreRect centre = render::hudCentreRect(config.width, config.height);
    bool anyDiffer = false;
    for (int y = centre.y0; y < centre.y1 && !anyDiffer; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            if (withMenu.pixels()[withMenu.index(x, y)] != plain.pixels()[plain.index(x, y)]) {
                anyDiffer = true;
                break;
            }
        }
    }
    CHECK(anyDiffer);
}

TEST_CASE("every word the character sheet can show is a sentence, not a diagnostic") {
    render::Session session = standing();
    session.toggleCharacter();
    REQUIRE(session.characterOpen());

    const auto mustRead = [](const std::string& text) {
        INFO("text: ", text);
        for (const char c : text) {
            CHECK(render::isDrawableGlyph(c));
        }
        CHECK(text.find('_') == std::string::npos);
    };

    for (const std::string& row : session.characterRows()) {
        mustRead(row);
    }
    const render::DialogueViewState view = session.dialogueView();
    mustRead(view.speaker);
    mustRead(view.epithet);
    mustRead(view.line);

    // AND EVERY RUNG A TRACK CAN NAME, not just the fresh-arrival ones --
    // legend.hpp's kTitles table is authored prose and this is the copy bar
    // applied to all twenty of its entries, the same way test_casebook.cpp's
    // "the five tracks are five different people" case walks the whole table
    // for collisions.
    for (std::int32_t track = 0; track < static_cast<std::int32_t>(kLegendTracks); ++track) {
        for (std::int32_t rung = 0; rung <= kLegendRungs; ++rung) {
            mustRead(std::string(legendTitle(static_cast<LegendTrack>(track), rung)));
        }
    }
}
