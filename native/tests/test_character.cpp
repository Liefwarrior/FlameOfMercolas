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
// WHAT THIS IS NOT. There is no item and no equipment-slot model in this
// build -- session.hpp's own quick-bar comment says so (#77's own
// VERIFICATION GAP) -- so this is not a paper doll and it draws no armour
// rating. It is the state the simulation actually has, laid out where a
// player can look themselves up: five derived standings and the four skills a
// verb in this build actually levels.
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
#include "granadad/render/hud.hpp"
#include "granadad/render/menu_view.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/legend.hpp"
#include "granadad/sim/social.hpp"

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

TEST_CASE("the character sheet lists all five Legend tracks, the skills the track has moved, and the four attributes") {
    render::Session session = standing();
    session.toggleCharacter();
    REQUIRE(session.characterOpen());

    const std::vector<std::string> rows = session.characterRows();
    // Five tracks, the four always-shown skills (a fresh arrival has moved
    // none), the four attributes, five faction ladders, and
    // REPUTATION/COIN/HEAT -- twenty-one rows, fixed, because the simulation
    // has exactly this much to say about a fresh player and no item system
    // to pad it with. THE HONEST SHEET (PULL PACK): the skill rows come off
    // the SkillTrack itself now -- any skill with a level above zero or uses
    // banked toward one prints, and the sixteen a verb has never touched
    // stay off, so a wall of LV 0 still cannot claim the game is watching a
    // skill it is not.
    REQUIRE(rows.size() == kLegendTracks + 4 + kAttributeCount + 5 + 3);

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

    // THE FOUR SKILLS THE SHEET ALWAYS SHOWS, each at LV 0 for a fresh
    // arrival, in the track's own id order (ascending, the raws' discipline)
    // -- and each with what the next level costs, off the track's own
    // scaledUsesForLevel: skyrunning is FAVORED (4 x 192 / 256 = 3),
    // cracksmanship, streetwise and linkcraft read their tiers the same way.
    CHECK(rows[5].rfind("CRACKSMANSHIP", 0) == 0);
    CHECK(rows[6].rfind("LINKCRAFT", 0) == 0);
    CHECK(rows[7].rfind("SKYRUNNING", 0) == 0);
    CHECK(rows[8].rfind("STREETWISE", 0) == 0);
    for (std::size_t i = 5; i < 9; ++i) {
        CHECK(rows[i].find("LV 0") != std::string::npos);
        CHECK(rows[i].find("NEXT ") != std::string::npos);
    }
    const SkillTrack& skills = session.tavern().dialogue().skills();
    CHECK(rows[7].find("NEXT " + std::to_string(skills.scaledUsesForLevel(kRoofSkill, 0))) !=
          std::string::npos);

    // THE FOUR ATTRIBUTES, with no held delta on a fresh sheet: the chargen
    // base and the effective read agree, so the row is the bare number.
    CHECK(rows[9].rfind("MGT ", 0) == 0);
    CHECK(rows[10].rfind("AGI ", 0) == 0);
    CHECK(rows[11].rfind("VIG ", 0) == 0);
    CHECK(rows[12].rfind("WIT ", 0) == 0);
    for (std::size_t i = 9; i < 13; ++i) {
        CHECK(rows[i].find("(") == std::string::npos);
    }

    // THE FIVE LADDERS, WITH THE NUMBERS ON -- the owner's ruling for this
    // build: the player's OWN sheet shows rank title, standing number and
    // next-rung cost (the word-only ruling still governs how NPCs talk). In
    // the registry's own sorted order, factions.json's authoritative note:
    // dockhands=0, merchants=1, skyrunners=2, temple=3, watch=4. A fresh
    // arrival is on no roll, so every row reads its JOIN cost -- the first
    // rung is earned exactly like every later one, and ranks.json prices
    // them at 8/10/8/10/10 standing.
    CHECK(rows[13].rfind("DOCKHANDS", 0) == 0);
    CHECK(rows[14].rfind("MERCHANTS", 0) == 0);
    CHECK(rows[15].rfind("SKYRUNNERS", 0) == 0);
    CHECK(rows[16].rfind("TEMPLE", 0) == 0);
    CHECK(rows[17].rfind("WATCH", 0) == 0);
    for (std::size_t i = 13; i < 18; ++i) {
        CHECK(rows[i].find(" 0  JOIN ") != std::string::npos);
    }
    CHECK(rows[15].find("JOIN 8") != std::string::npos);
    CHECK(rows[17].find("JOIN 10") != std::string::npos);

    // AND WHAT THE WARD AND THE PURSE SAY, always present -- see
    // characterRows' own comment on why a sheet opened on purpose prints a
    // zero rather than dropping the row the way the ambient HUD would.
    CHECK(rows[18].rfind("REPUTATION", 0) == 0);
    CHECK(rows[18].find("NOBODY IN PARTICULAR") != std::string::npos);
    CHECK(rows[19].rfind("COIN", 0) == 0);
    CHECK(rows[20].rfind("HEAT", 0) == 0);
    CHECK(rows[20].find("HEAT  0") != std::string::npos);
}

TEST_CASE("a skill the world has moved appears on the sheet, and a held tuning shows beside the attribute") {
    // THE HONEST SHEET (PULL PACK). The old sheet was a hand-typed table of
    // four while the combat build trained shieldwall on every softened blow
    // and the scalp skill on a take -- two lived skills that never printed.
    // Now the rows come off the track: one use of shieldwall banks toward
    // its first level and the row appears, NEXT counting down what is still
    // owed; a level reads LV n.
    render::Session session = standing();
    SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(kBlockSkill) != nullptr);
    const auto rowFor = [&](std::string_view name) -> std::string {
        for (const std::string& row : session.characterRows()) {
            if (row.rfind(name, 0) == 0) {
                return row;
            }
        }
        return {};
    };
    CHECK(rowFor("SHIELDWALL").empty());
    (void)skills.use(kBlockSkill);
    const std::string banked = rowFor("SHIELDWALL");
    INFO("row: ", banked);
    REQUIRE_FALSE(banked.empty());
    CHECK(banked.find("LV 0") != std::string::npos);
    CHECK(banked.find("NEXT " + std::to_string(skills.scaledUsesForLevel(kBlockSkill, 0) - 1)) !=
          std::string::npos);
    while (!skills.use(kBlockSkill)) {
    }
    CHECK(rowFor("SHIELDWALL").find("LV 1") != std::string::npos);
    // Every skill row still reads as a sentence the font can draw.
    for (const std::string& row : session.characterRows()) {
        for (const char c : row) {
            CHECK(render::isDrawableGlyph(c));
        }
    }
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
    const std::string& row = rows[15];
    INFO("row: ", row);
    CHECK(row.rfind("SKYRUNNERS TENANT", 0) == 0);
    // The standing number itself: 40 granted, minus whatever the join and the
    // mirror ledger moved -- read it back from the ledger rather than typing
    // a copy of the arithmetic here.
    CHECK(row.find(std::to_string(talk.standings().standing(roofs))) != std::string::npos);
    CHECK(row.find("NEXT 22/LV5") != std::string::npos);
}

TEST_CASE("the sheet grows an IN HAND row the moment the world arms you, and not before") {
    // EVICTOR BUILD. The reference sheet's w slot ("cane 1-6 Impact",
    // UI-REFERENCE-TERMINAL.md) lands here the day something is actually in
    // the player's hand -- and ONLY that day. This build has no item system,
    // so an empty-handed sheet printing IN HAND FISTS would claim an
    // equipment model nothing simulates; twenty-one rows stays the bare
    // truth (the honest sheet's count for a fresh arrival), twenty-two the
    // armed one.
    render::Session session = standing();
    const std::vector<std::string> bare = session.characterRows();
    REQUIRE(bare.size() == 21);
    for (const std::string& row : bare) {
        CHECK(row.rfind("IN HAND", 0) != 0);
    }

    // The same grant seam the eviction case's close beat calls, by the same
    // authored id -- see Tavern::grantPlayerWeapon.
    REQUIRE(session.tavern().grantPlayerWeapon(kEvictorWeaponId));
    const std::vector<std::string> armed = session.characterRows();
    REQUIRE(armed.size() == 22);
    CHECK(armed.back() == "IN HAND  THE EVICTOR 7-9 IMPACT");
}

TEST_CASE("twenty-one rows is three pages, and the character sheet turns like every other list here") {
    render::Session session = standing();
    session.toggleCharacter();
    REQUIRE(session.characterOpen());
    REQUIRE(session.characterRows().size() == 21);

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
    // casebook: page two, then round to the first.
    session.nextTopicPage();
    view = session.dialogueView();
    CHECK(view.page == 2);
    session.nextTopicPage();
    view = session.dialogueView();
    CHECK(view.page == 0);

    // The cursor wraps rather than stopping dead at either end.
    session.moveTopicCursor(-1);
    view = session.dialogueView();
    CHECK(view.cursor == 20);
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
