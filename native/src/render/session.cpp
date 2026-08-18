#include "granadad/render/session.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <utility>

// THE AUDIO WIRING PASS. The one render file that speaks to the audio engine,
// and it speaks only ids -- see setAudio()'s header in session.hpp, and the
// determinism note in audio_engine.hpp for why this include may never spread
// to a header or to anything under src/sim.
#include "granadad/audio/audio_engine.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render/signage_renderer.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/build_info.hpp"
#include "granadad/sim/docks.hpp"
// TIME-AND-TENURE BUILD: the compound sign footprints, for the one question
// the tier-3 correlation was ever going to answer -- whose ground is under
// the feet. See plotIndexUnderfoot().
#include "granadad/sim/docks_signs.hpp"
#include "granadad/sim/stealth.hpp"

namespace granadad::render {

namespace {

constexpr float kPi = 3.14159265358979323846F;

[[nodiscard]] SessionConfig resolved(const SessionConfig& in) {
    SessionConfig out = in;
    if (out.contentDir.empty()) {
        out.contentDir = content::contentDir();
    }
    if (out.spawnX < 0 || out.spawnY < 0 || out.spawnBand < 0) {
        out.spawnX = sim::docks::kSpawnTileX;
        out.spawnY = sim::docks::kSpawnTileY;
        out.spawnBand = sim::docks::kSpawnBand;
        if (!out.spawnYawGiven) {
            out.spawnYaw = sim::docks::kSpawnYaw;
        }
    }
    out.clockScale = std::max(1, out.clockScale);
    return out;
}

/// How tall a body of each kind stands, in tiles, and the width its drawing is
/// scaled to at that height.
///
/// A PERSON IS 1.875 TILES SOLE TO CROWN, which is kStandingHeightTilesQ8 out
/// of sim/vertical_scale.hpp -- the same header the player's own eye height
/// comes from. At roughly 0.9 m to the tile that is a 1.71 m adult and the
/// crown lands a hand above the player's 1.70-tile eye, so you look people in
/// the mouth. That is correct, and it is the cheapest proof the two scales
/// agree.
///
/// The WIDTH is not the drawing's own aspect. The sheet's figures are chunky
/// -- twelve texels of shoulder in a sixteen-texel cell -- and taken literally
/// that is a person a tile and a half across, standing in a collision square
/// 0.7 of a tile wide. So the height is the truth and the width is scaled to
/// put shoulders at about half a tile, which is a person shaped like a person.
struct FigureScale {
    float heightTiles;
    float widthTiles;
};

[[nodiscard]] FigureScale figureScaleOf(sim::WardType type) noexcept {
    switch (type) {
        case sim::WardType::MilitiaWatch: return {1.95F, 0.86F};
        case sim::WardType::Urchin: return {1.30F, 0.58F};
        case sim::WardType::Thief: return {1.80F, 0.72F};
        case sim::WardType::PriestOfTheFlame: return {1.90F, 0.84F};
        case sim::WardType::DiscipleOfTheFlame: return {1.82F, 0.80F};
        case sim::WardType::Dog: return {0.62F, 0.90F};
        case sim::WardType::Stray: return {0.46F, 0.66F};
        case sim::WardType::Cat: return {0.42F, 0.62F};
        case sim::WardType::Mouse: return {0.22F, 0.34F};
        default: return {1.875F, 0.80F};
    }
}

/// How much darker somebody with their back to you is.
///
/// THE FACING IS FINALLY WORTH SOMETHING AGAIN. The sheet is drawn front-on --
/// there is no back view, and inventing one would be this build authoring art
/// -- so the eight-point facing the simulation has been hashing since S2 is
/// spent on light instead: a figure looking your way catches what light there
/// is on its face and a figure turned away is a silhouette. Which is both true
/// and, at eight tiles in lamplight, the one thing about a person you actually
/// need to read.
constexpr float kBackShade = 0.55F;

/// WHICH DRAWN FIGURE A TAVERN ROLE WEARS.
///
/// #78 REPLACED THE ELLIPSE STACK, and it is worth recording what it replaced
/// and why. A tavern actor used to be three stacked ellipses -- legs, torso,
/// head -- with a pale patch for a face. That was the right first step and its
/// own header said so: a narrow stack of three reads as a person from across a
/// room where one blob reads as an egg. What it could not do is survive being
/// MULTIPLIED. The owner captured a conversation frame, looked at the figure
/// standing in the Gilded Gull's doorway, and called it what it was: a plain
/// egg shape with a round head, which will not read as polished with hundreds
/// of them on a street.
///
/// content/art/sprites was already in the repo -- twenty-five 16x16 figures in
/// the MERCOLAS-24 palette, front-on, with a helmet on the guard, white and red
/// on the priest, a hood on the vagrant and a hat on the merchant. Those are
/// silhouettes a player can tell apart at eight tiles in lamplight, which is
/// the whole visual target, and somebody had already drawn them.
///
/// So the taproom's fourteen are drawn out of the same sheet the ward's six
/// hundred are, and there is ONE look for a person in this game rather than two.
[[nodiscard]] sim::WardType figureForRole(sim::ActorRole role, std::int32_t id) noexcept {
    switch (role) {
        case sim::ActorRole::Bartender:
        case sim::ActorRole::Innkeeper:
            return sim::WardType::Shopkeeper;
        // A bouncer is hired for his shoulders, and the sheet's guard is the
        // only figure on it built like that.
        case sim::ActorRole::Bouncer:
            return sim::WardType::MilitiaWatch;
        case sim::ActorRole::PriestOfTheFlame:
            return sim::WardType::PriestOfTheFlame;
        // Grey on grey, keeping to the corner, hard to pick out. The sheet's
        // ragged vagrant is exactly that and the Skyrunners dress like it.
        case sim::ActorRole::SkyrunnerContact:
            return sim::WardType::Thief;
        case sim::ActorRole::Vermin:
            return sim::WardType::Mouse;
        case sim::ActorRole::Patron:
        default:
            // THE GULL IS THE CAPTAINS' TAVERN -- charts on the walls, factors
            // doing deals, the good wine (DOCKS-GAZETTEER K03) -- and it is on
            // a working quay. So the room is a mix and which half a patron is
            // in is a pure function of his id, because a room where everybody
            // wears the same coat is the defect this is fixing.
            return (id & 1) == 0 ? sim::WardType::Shopkeeper : sim::WardType::Serf;
    }
}

/// THE ROWS THE KEYS PAGE ADDS TO THE BINDINGS, and the only ones it still
/// hard-codes.
///
/// Everything a key is BOUND to is generated from ControlSettings by
/// Session::keyRows -- see the note there. These four are not bindings: they are
/// what W, S, SPACE and F while a WIRE IS IN A LOCK do, which is a mode the
/// simulation is in rather than a verb with a key of its own.
///
/// AND EVERY ROW FITS ITS COLUMN. The grid is three columns of eighteen glyphs
/// (render/dialogue_view.hpp) and the first S10 capture of this page shipped
/// "SPACE  UP: MANT." and "E  TALK TO WHOE.". A controls page that arrives
/// truncated is worse than none, because a player reads the truncation as the
/// binding.
const char* const kLockRows[] = {
    "LOCK: W S  AIM",
    "LOCK: SPACE TRY",
    "LOCK: F  FORCE",
    "LOCK: ESC  OUT",
};

/// TASK #82. "DAY 2 08:14" out of a Casebook::heardAt() value -- the
/// dateline a detective's log actually carries. NEGATIVE (never yet heard)
/// prints nothing rather than "DAY 0 00:00", which would read as a real
/// timestamp for an entry that has none.
///
/// DAY ONE, NOT DAY ZERO. sim::kSecondsPerDay divides the count exactly the
/// way the ward's own compound.cpp already does; +1 is the one translation
/// from a zero-based count to the ordinal a player reads ("the first day"),
/// applied here and nowhere inside the simulation, which never counts days
/// at all -- see Casebook::heardAt's own comment on why the raw value is
/// deliberately not day-and-hour already.
[[nodiscard]] std::string formatCaseDay(std::int64_t heardAtSeconds) {
    if (heardAtSeconds < 0) {
        return {};
    }
    const std::int64_t day = heardAtSeconds / sim::kSecondsPerDay + 1;
    const int hour = static_cast<int>((heardAtSeconds / 3600) % 24);
    const int minute = static_cast<int>((heardAtSeconds / 60) % 60);
    std::string out = "DAY " + std::to_string(day) + " ";
    if (hour < 10) {
        out += '0';
    }
    out += std::to_string(hour);
    out += ':';
    if (minute < 10) {
        out += '0';
    }
    out += std::to_string(minute);
    return out;
}

// ---------------------------------------------------------------------------
// the tiled Menu's four panels (plus the keys page and a live conversation)
// share one cursor/page navigation shape, wrapped or paged over a list whose
// length changes every call -- the SAME three or four lines were repeated
// once per list. Pulled out once, here: the per-list state still lives on
// Session (characterCursor_/mapCursor_/lettersCursor_/caseCursor_/
// topicCursor_ and their *Page_ twins), these just take it by reference so
// each call site stays one line instead of a paragraph.
// ---------------------------------------------------------------------------

/// Wraps `cursor` by `delta` into [0, count) and sets `page` to the page it
/// falls on -- moveTopicCursor()'s own shape, once per list this widget can
/// be navigating. `count` of zero parks both at zero rather than dividing by
/// it (unreachable for every list this build has today, since none of them
/// can ever be empty while its own overlay is open, but a caller has no way
/// to prove that from here).
void wrapCursorAndPage(int& cursor, int& page, int delta, int count) noexcept {
    if (count <= 0) {
        cursor = 0;
        page = 0;
        return;
    }
    cursor = ((cursor + delta) % count + count) % count;
    page = topicPageOf(cursor);
}

/// Steps `page` forward one and puts `cursor` on its first row --
/// nextTopicPage()'s own shape. A single page is a no-op: the row already
/// showing has nowhere else to turn to.
void advancePage(int& page, int& cursor, std::size_t total) noexcept {
    const int pages = topicPageCount(total);
    if (pages <= 1) {
        return;
    }
    page = (page + 1) % pages;
    cursor = std::min(static_cast<int>(total) - 1, page * kTopicPageSize);
}

/// Puts `cursor` on the row this number key names, if that row is actually on
/// the page showing -- chooseVisibleTopic()'s own shape. Out of range is a
/// no-op, the same as every list on this surface gives a number with nothing
/// under it.
void pickCursorIfVisible(int& cursor, int page, int slot, std::size_t total) noexcept {
    const int index = page * kTopicPageSize + slot;
    if (index < static_cast<int>(total)) {
        cursor = index;
    }
}

/// SPELLS BUILD: moved up from the HUD-line helpers' own anonymous namespace
/// below, unchanged -- the Grimoire page and the quick bar (earlier in this
/// file) speak the same upper-case menu furniture the HUD lines do.
[[nodiscard]] std::string upperAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return out;
}

}  // namespace

Session::Session(const SessionConfig& config)
    : config_(resolved(config)),
      world_(content::loadWorldFile(config_.contentDir / "maps" / "baked" /
                                    (config_.world + ".trojsav"))),
      tiles_(std::make_unique<sim::TileQuery>(world_)),
      atlas_(TileAtlas::load(config_.contentDir)) {
    renderer_ = std::make_unique<WorldRenderer>(*tiles_, atlas_,
                                                loadLamps(config_.contentDir, config_.world));
    body_ = std::make_unique<sim::PlayerBody>(*tiles_, config_.spawnX, config_.spawnY,
                                              config_.spawnBand, config_.spawnYaw);
    // S5. The district declares its own floor: everything under the harbour
    // surface is unbuilt dungeon, and a body that fell into it could not climb
    // back out. See PlayerBody::setLandingFloor for the shaft this closes.
    body_->setLandingFloor(sim::docks::kLandingFloor);
    timeOfDay_ = ((config_.timeOfDay % sim::kSecondsPerDay) + sim::kSecondsPerDay) %
                 sim::kSecondsPerDay;
    settings_.timeOfDay = timeOfDay_;

    engine_ = std::make_unique<sim::PhasedEngine>(config_.worldSeed, world_);
    auto tavern =
        std::make_unique<sim::Tavern>(*tiles_, timeOfDay_, config_.worldSeed, config_.contentDir);
    tavern_ = tavern.get();
    engine_->register_system(std::move(tavern));
    // S8: AND THE WARD'S ROLL, ON THE SAME ENGINE. See Session::ward() -- the
    // compounds were built in S7 and never constructed by anything with a
    // window on it. The registry is held rather than borrowed because Ward
    // takes it by reference and a temporary here would dangle the moment the
    // constructor returned.
    who_ = std::make_unique<sim::NotableRegistry>(
        sim::NotableRegistry::load(config_.contentDir));
    auto ward = std::make_unique<sim::Ward>(config_.worldSeed, config_.contentDir, *who_);
    ward_ = ward.get();
    engine_->register_system(std::move(ward));
    // #78: AND THE PEOPLE. Registered AFTER the roll and the taproom, so
    // registration order inside the Actors phase is a stated fact rather than
    // an accident of which line was typed first, and so the bodies decide
    // against a ward whose day has already turned.
    auto people = std::make_unique<sim::WardPopulation>(*tiles_, timeOfDay_, config_.worldSeed,
                                                        config_.contentDir);
    people_ = people.get();
    engine_->register_system(std::move(people));
    actorSheet_ = ActorSheet::load(config_.contentDir);
    // THE WARD MAP's boot-time facts: the plan's per-material tones derived
    // from the atlas the first-person pass already draws with, and the
    // authored extent with the VOID border ring cropped away. Once each --
    // see map_view.hpp.
    mapPalette_ = MapPalette::fromAtlas(atlas_);
    mapBounds_ = mapContentBounds(*tiles_);
    // The one wire between the two: a rival who rises far enough petitions the
    // Flame for a vacant charge, and the roll is where that becomes true.
    tavern_->attachRoll(ward_);
    // RADIANT BUILD: and the wire the other way -- the room learns the
    // district's own people, so the radiant board posts off the live roster
    // (TASK #81's generator, constructed by a windowed client for the first
    // time). Attached before boot, exactly as the roll is, so the first
    // day's errands are up before anybody has taken a step.
    tavern_->attachPeople(people_);
    engine_->boot();
    // S10: AND THE CASE. The bloodletter trail is the reason to be in the
    // district at all -- see sim/casebook.hpp. The raws are a member because
    // Casebook borrows them for its whole life, and a missing casebook.json
    // leaves an empty trail rather than refusing to boot, which is the contract
    // every raws loader in this build honours.
    caseRaws_ = sim::CasebookRaws::load(config_.contentDir);
    // TASK #82. `caseNowSeconds()` needs timeOfDay_ set, which the block above
    // already did, so begin() is stamped with the game's real starting hour
    // rather than the 0-default every pre-#82 caller still gets for free.
    casebook_.begin(caseRaws_, caseNowSeconds());
    // TASK #82. THE LETTERS. A second, independent raws loader -- see
    // sim/letters.hpp's own header on why a second file rather than a second
    // schema bolted onto casebook.json. No `begin()` here: a Letter carries
    // no state of its own, so there is nothing to bind besides the raws
    // themselves, which unlockedLetters() reads straight off casebook_.
    letterRaws_ = sim::LetterRaws::load(config_.contentDir);
    // THE FIRST RUN OPENS ON THE HOOK.
    //
    // Every sprint before this one dropped the player onto the Tarwalk facing a
    // door with no idea who they were or what they were for. So a fresh session
    // starts with the notes up: the case, the body that started it, and the one
    // lead the ward has given you. Anything the player does closes it -- see
    // Session::step and the four verbs -- so it costs a keypress at most and
    // never gets in the way twice.
    // #77. The field of view the session was ASKED for becomes the field of
    // view the options slider starts on, so --fov and the in-game slider are the
    // same number rather than two that disagree.
    controls_.fovDegrees = config_.fovDegrees;
    controls_.sanitise();
    if (config_.openingPage && caseRaws_.loaded()) {
        casebookOpen_ = true;
        // #85. UPDATED FOR THE CONSOLIDATED SCHEME: Keys and Options are pages
        // inside Menu now rather than their own F1/F2, and Examine is folded
        // into Interact. VERIFICATION GAP (S3, still open): this is still a
        // hand-written string rather than one built from actionLabel()/
        // keyName(), so a player who rebinds Menu off Tab still gets a stale
        // prompt -- the same gap the keys page itself closed by generating
        // its rows from controls_ instead of a static array. Not closed here;
        // stated rather than left for somebody to find by rebinding Tab and
        // reading this line.
        // NOT '[' / ']' -- hud.cpp's 4x6 font has no glyph for either (see its
        // own header on why: it carries only the characters the authored
        // barks actually use). '<'/'>' are in the table and are the same
        // glyphs the keys page prints for PagePrev/PageNext (kActions'
        // "PAGE <"/"PAGE >" labels), so this reads consistently with them.
        message_ = "TAB YOUR NOTES  < > MORE PAGES  E USE";
        messageSteps_ = 60 * 12;
    }
    // TASK #83. SNAPPED, NOT EASED. Nobody pressed a key to reach whichever of
    // these is true on frame one -- SessionConfig chose it -- so there is
    // nothing to animate from and the panel/alert draw at full strength
    // immediately, exactly as they always have. See EasedToggle::snapTo.
    panelAnim_.snapTo(conversingNow());
    alertAnim_.snapTo(!message_.empty());
    syncTavernToBody();
    // HARDENING PASS. THE SAME SNAP, GENERALIZED. syncPanelAnim() below sets
    // the target and caches the text for every other row this pass eases too
    // (see its own header) -- calling it here is a no-op for panelAnim_/
    // alertAnim_, which are already sitting exactly on the target just
    // snapped above (EasedToggle::setTarget is idempotent), but the eight
    // new toggles have never been touched and would otherwise arm their own
    // opening bump on a session that starts with, say, a rung already held
    // or the player already standing inside the Gull. Snapped to whatever
    // target it just computed, for the identical reason panelAnim_/
    // alertAnim_ are snapped instead of merely targeted, above.
    syncPanelAnim();
    interactAnim_.snapTo(interactAnim_.target());
    lockAnim_.snapTo(lockAnim_.target());
    caseAnim_.snapTo(caseAnim_.target());
    roomAnim_.snapTo(roomAnim_.target());
    rivalAnim_.snapTo(rivalAnim_.target());
    guildAnim_.snapTo(guildAnim_.target());
    objectiveAnim_.snapTo(objectiveAnim_.target());
    stealthAnim_.snapTo(stealthAnim_.target());
    // PLANNING SPRINT (item #2, the sweep). THE SAME SNAP, FOR THE THREE ROWS
    // THE SWEEP FOUND. See standingAnim_'s own header.
    standingAnim_.snapTo(standingAnim_.target());
    heatAnim_.snapTo(heatAnim_.target());
    stashAnim_.snapTo(stashAnim_.target());
    // HELD-EFFECTS BUILD. The same snap, one per slot -- a session cannot
    // boot with a hold live today, but the rule is "snap to whatever
    // syncPanelAnim() just chose", not "assume empty".
    for (EasedToggle& anim : effectAnims_) {
        anim.snapTo(anim.target());
    }
    // FATIGUE BUILD. The same snap: syncPanelAnim() just targeted the bar's
    // real frame-one state (up unless the session booted mid-conversation),
    // and there is nothing to fade in from.
    fatigueAnim_.snapTo(fatigueAnim_.target());
    // THE WARD MAP. The same snap: a session never boots with the map up
    // today, but the rule is "snap to whatever syncPanelAnim() just chose",
    // not "assume closed".
    districtMapAnim_.snapTo(districtMapAnim_.target());
    // INNOVATION SPRINT ITEM #2. SNAPPED, FOR THE IDENTICAL REASON THE ROWS
    // ABOVE ARE. NOT a hardcoded "journal starts focused" -- syncPanelAnim()
    // just computed the real answer off casebookOpen_/menuFocus_ (both true
    // only when config.openingPage put the Menu up focused on Journal from
    // the start; false the whole ordinary way a session boots, which
    // --smoke's captures always are, since openingPage is a run_client-only
    // default). Snapping to whatever setTarget() just chose is what makes
    // this correct in both cases without a special case for either.
    characterFocusAnim_.snapTo(characterFocusAnim_.target());
    mapFocusAnim_.snapTo(mapFocusAnim_.target());
    lettersFocusAnim_.snapTo(lettersFocusAnim_.target());
    journalFocusAnim_.snapTo(journalFocusAnim_.target());
    // INNOVATION SPRINT ITEM #3. THE STARTING HIT POINTS, NOT A HARD-CODED
    // 100 -- see lastPlayerHp_'s own header on why a session that boots the
    // player already hurt must not read its own first frame as a fresh hit.
    lastPlayerHp_ = tavern_->playerHp();
    // AUDIO WIRING. The starting purse, for the identical reason: a session
    // that boots with coin in the pocket has not just been paid.
    lastCoinForAudio_ = tavern_->playerCoin();
}

void Session::setAudio(audio::AudioEngine* engine) {
    audio_ = engine;
    if (audio_ == nullptr) {
        return;
    }
    // Baselines re-read at attach, so nothing that happened while no engine
    // was listening is replayed as if it just happened.
    lastCoinForAudio_ = tavern_->playerCoin();
    audioPanelWasMenu_ = casebookOpen_;
    // And the bed starts the moment there are ears: the same
    // playerInside()-keyed choice step() re-asserts every step (re-asserting
    // the current bed is a documented no-op), so a body standing still on the
    // quay hears the harbour without having to move first.
    audio_->startBed(tavern_->playerInside() ? audio::BedId::Interior
                                             : audio::BedId::Harbour);
    audio_->setTimeOfDay(timeOfDay_);
}

void Session::syncTavernToBody() {
    tavern_->setPlayer(body_->x(), body_->y(), body_->band());
}

// ---------------------------------------------------------------------------
// S5: the roof verbs
// ---------------------------------------------------------------------------

void Session::settleLanding(const sim::RoofResult& move) {
    // THE CHARGE IS THE ROOM'S. Everything a landing moves -- the craft, the
    // hit points, the roof-run, the counted verb -- is simulation state, and it
    // moved out of this file in S6 so the simulation suite can drive it and a
    // mutation to any clause of it can go red. See Tavern::settleLanding.
    const sim::Tavern::LandingResult charged = tavern_->settleLanding(
        move, body_->takeFallBands(), body_->band(), body_->tileX(), body_->tileY());
    if (charged.hurt > 0) {
        // IN METRES, because the fall curve is in metres now and a player who is
        // told "50 HURT" learns nothing they can act on. "5M - 50 HURT" tells
        // them the roof they are standing on is the last one they can come off.
        roofMove_ += " - " + std::to_string(charged.fellMm / 1000) + "M";
        if (charged.softLanding) {
            roofMove_ += " INTO WATER";
        }
        roofMove_ += " - " + std::to_string(charged.hurt) + " HURT";
    }
}

sim::RoofResult Session::tryClimb() {
    // FATIGUE BUILD: an empty pool gates the climb verbs before the body is
    // ever asked -- the body knows geometry, not stamina, which is why
    // RoofMove::Winded is the one refusal PlayerBody itself never returns
    // (see the enum's own note). A refused climb costs nothing, exactly as a
    // NoLedge costs nothing.
    if (tavern_->playerWinded()) {
        return sim::RoofResult{sim::RoofMove::Winded, 0, 0};
    }
    sim::RoofResult move = body_->mantle();
    bool leapt = false;
    if (!move.ok()) {
        const sim::DialogueDirector& talk = tavern_->dialogue();
        const std::int32_t roofs = talk.factions().indexOf("skyrunners");
        move = body_->leap(sim::leapReachTiles(talk.skills().level(sim::kRoofSkill),
                                               talk.standings().unlocked(roofs, "roof")));
        leapt = move.ok();
    }
    if (!move.ok()) {
        // NOTHING SAID. The caller decides what a failed climb means: climb()
        // itself reports the refusal, vertical() moves on to tryDropDown().
        return move;
    }
    if (leapt) {
        // FATIGUE BUILD: the leap's wind is spent when the jump is armed --
        // the moment the body commits -- AGI- and skyrunning-scaled through
        // the one charge path (Tavern::chargePlayerLeap).
        tavern_->chargePlayerLeap();
        // A LEAP IS WATCHED, NOT TELEPORTED, and this is the S5 review's second
        // finding closed. S5 shipped a `while (body_->airborne()) step()` right
        // here, inside the keypress: the arc ran to its end before the frame
        // that showed the jump was ever drawn, so every leap in real play was
        // instant -- and it burned twenty-four movement steps of tavern clock
        // inside one frame while it did it. player.hpp:227 says a leap is
        // "something the player watches happen rather than a teleport with a
        // sound effect" and test_roofrun.cpp asserts it of PlayerBody; the
        // client then threw the arc away.
        //
        // So the press ARMS the leap and nothing more. The ordinary step pump
        // -- the client's, a capture script's, a test's -- flies it, and the
        // landing is settled in step() at the moment the feet touch, which is
        // also the only moment takeFallBands() has anything to report.
        // A leap down onto a roof one tile away is a real move -- PlayerBody's
        // second pass allows it -- and it used to announce itself as "OVER 1
        // TILES".
        roofMove_ =
            "OVER " + std::to_string(move.tiles) + (move.tiles == 1 ? " TILE." : " TILES.");
        say(roofMove_);
        pendingLanding_ = move;
        awaitingLanding_ = true;
        syncTavernToBody();
        return move;
    }
    roofMove_ = "UP ONTO THE LEDGE.";
    // FATIGUE BUILD: the haul's wind, spent on a climb that happened -- the
    // same bill the automatic path pays in step().
    tavern_->chargePlayerMantle();
    // Same order as dropDown, and for the same reason: a mantle onto a LOWER
    // ledge is a fall too, and the line has to be said after it is charged.
    settleLanding(move);
    say(roofMove_);
    syncTavernToBody();
    return move;
}

void Session::climb() {
    dismissOverlays();
    if (talking()) {
        return;
    }
    const sim::RoofResult move = tryClimb();
    if (!move.ok()) {
        roofMove_ = std::string(sim::roofRefusal(move.move));
        say(roofMove_);
    }
}

sim::RoofResult Session::tryDropDown() {
    const sim::RoofResult move = body_->dropOff();
    if (!move.ok()) {
        return move;
    }
    roofMove_ =
        "DOWN " + std::to_string(move.bands) + (move.bands == 1 ? " LEVEL." : " LEVELS.");
    // SETTLE FIRST, THEN SAY IT. #77 found this by driving the real window and
    // stepping off the Gull's lead: the alert read "DOWN 2 LEVELS." and the
    // health bar quietly halved with nothing on screen connecting the two.
    // settleLanding appends the height and the injury to roofMove_, so saying
    // the line before charging the fall throws away the only part of it a player
    // can act on. What they see now is "DOWN 2 LEVELS. - 5M - 50 HURT".
    settleLanding(move);
    say(roofMove_);
    syncTavernToBody();
    return move;
}

void Session::dropDown() {
    dismissOverlays();
    if (talking()) {
        return;
    }
    const sim::RoofResult move = tryDropDown();
    if (!move.ok()) {
        roofMove_ = std::string(sim::roofRefusal(move.move));
        say(roofMove_);
    }
}

void Session::vertical() {
    dismissOverlays();
    if (talking() || picking()) {
        return;
    }
    // #85. ONE BUTTON, RESOLVED BY WHAT IS DIRECTLY AHEAD OR BELOW: climb
    // (mantle, or the leap it falls back to) first, a drop if there is a
    // ledge to step off, an ordinary standing jump when neither is there.
    // Each of the first two already refuses cleanly on ground that is
    // neither -- RoofMove::NoLedge/NoGap for a climb, "the tile ahead is
    // walkable" for a drop -- so trying them first costs nothing on flat
    // ground, which is where this actually resolves to a jump.
    if (tryClimb().ok()) {
        return;
    }
    if (tryDropDown().ok()) {
        return;
    }
    if (!body_->jump()) {
        return;
    }
    // FATIGUE BUILD: the reference prices every jump (5 points, AGI-scaled)
    // and so does this one -- charged only on a hop that actually left the
    // ground, never on the buffered request jump() banks instead.
    tavern_->chargePlayerJump();
    // NOTHING IS SAID -- see jump()'s own note: a jump that announced itself
    // on the alert row every time would be the noisiest thing in the game.
    tavern_->setPlayerMotion(true, true);
}

bool Session::stealNearestThing() {
    sim::Tavern::StealResult took = tavern_->crackStrongbox();
    // S9. THE WIRE GOES IN FIRST. A locked box used to open to this key; it now
    // refuses with Refused, so the key puts the wire in instead and the player
    // works the lock. Pressing it again once the lock has given empties the
    // box, which is what this key always did.
    //
    // Refused is ALSO the answer for a room the player rented -- "THAT ONE IS
    // YOURS" -- and beginPick refuses that room for the same reason, so a
    // player standing at their own bed-foot is told so once rather than being
    // handed a wire they cannot use.
    if (took.result == sim::ServiceResult::Refused && !tavern_->picking().open()) {
        const sim::Tavern::PickResult started = tavern_->beginPick();
        say(started.result == sim::ServiceResult::Served ? started.line : took.line);
        return true;
    }
    if (took.result == sim::ServiceResult::TooFar) {
        // Nothing to open here. The other things hands can be put on are a bale
        // in the snug and, since S6, a rat on the floor -- which is the ward's
        // own source of the one contraband the ward pays a bounty ON.
        took = tavern_->handleBale();
    }
    if (took.result == sim::ServiceResult::TooFar) {
        took = tavern_->takeScalp();
    }
    if (took.result == sim::ServiceResult::TooFar) {
        // And the last thing a pair of hands can do standing next to somebody:
        // buy the wire that opens everything above. Refused for anyone who is
        // not one of the roofs, which is what the guild's first rung buys.
        took = tavern_->buyPicks();
    }
    say(took.line);
    return took.result != sim::ServiceResult::TooFar;
}

void Session::steal() {
    dismissOverlays();
    if (talking()) {
        return;
    }
    syncTavernToBody();
    stealNearestThing();
}

// ---------------------------------------------------------------------------
// S9: crouching, lifting, and the wire
// ---------------------------------------------------------------------------

void Session::toggleCrouch() {
    dismissOverlays();
    if (talking()) {
        return;
    }
    tavern_->toggleStance();
    say(tavern_->stance() == sim::Stance::Crouched ? "CROUCHED" : "UPRIGHT");
}

sim::Stance Session::stance() const noexcept { return tavern_->stance(); }

bool Session::hidden() const noexcept { return tavern_->hidden(); }

std::string Session::roomLine() const {
    // HARDENING PASS. Pulled out of drawFrame()'s own inline block so
    // syncPanelAnim() can call the identical logic instead of a second copy
    // -- see that method's own header. Bottom-right, and only when there is
    // a room to describe.
    if (!tavern_->playerInside()) {
        return {};
    }
    std::ostringstream line;
    line << "THE GULL  " << tavern_->presentCount() << " IN  ";
    if (!tavern_->isOpen()) {
        line << "SHUT";
    } else if (tavern_->noise() >= 60) {
        line << "LOUD";
    } else if (tavern_->noise() >= 25) {
        line << "BUSY";
    } else {
        line << "QUIET";
    }
    return line.str();
}

std::string Session::stealthLine() const {
    const sim::Notice worst = tavern_->worstNotice();
    const std::int32_t light = tavern_->lightOnPlayer();
    const std::int32_t noise = tavern_->playerNoise();
    std::string line = worst.seen ? "SEEN" : "HIDDEN";
    if (tavern_->stance() == sim::Stance::Crouched) {
        line += " CROUCH";
    }
    // The two numbers a player can actually do something about, in the words
    // they would use. Deliberately short: this is an EDGE line, not a sheet.
    line += light >= 50 ? "  LIT " : "  DARK ";
    line += std::to_string(light);
    if (noise >= sim::kNoiseRunning) {
        line += "  LOUD";
    } else if (noise > 0) {
        line += "  HEARD";
    } else {
        line += "  QUIET";
    }
    return line;
}

std::string Session::lockLine() const {
    const sim::Lockpicking& wire = tavern_->picking();
    if (!wire.open()) {
        return {};
    }
    // THE WHOLE MINIGAME, IN ONE ROW OF 4x6 GLYPHS. A pin that has dropped is
    // a star and one still up is a dash; the depth track is nine dots with the
    // pick standing on one of them. Nothing here needs a panel, and a panel is
    // what the Java build's first-person view died of.
    std::string line = "LOCK  PINS ";
    for (std::int32_t i = 0; i < wire.lock().pins; ++i) {
        line += i < wire.pinsSet() ? '*' : '-';
    }
    line += "  DEPTH ";
    for (std::int32_t d = 0; d < sim::kPinDepths; ++d) {
        // '+' and not '#': the 4x6 font in hud.cpp has fifty-two glyphs and a
        // hash is not one of them, so the pick's own position drew as a hole in
        // the track. Caught by looking at the frame, which is the point of
        // looking at the frame.
        line += d == wire.depth() ? '+' : '.';
    }
    line += "  STRAIN " + std::to_string(wire.strain()) + "/" +
            std::to_string(wire.strainLimit());
    line += "  PICKS " + std::to_string(tavern_->picks());
    return line;
}

void Session::lift() {
    dismissOverlays();
    if (talking()) {
        return;
    }
    syncTavernToBody();
    say(tavern_->liftFrom().line);
}

// ---------------------------------------------------------------------------
// S10: the investigation
// ---------------------------------------------------------------------------

sim::Legend Session::legend() const {
    const sim::DialogueDirector& talk = tavern_->dialogue();
    return sim::legendOf(talk.crimes(), talk.skills(), talk.standings(), talk.contracts(),
                         casebook_);
}

std::int64_t Session::caseNowSeconds() const noexcept {
    // TASK #82. THE STARTING HOUR PLUS EVERYTHING SIMULATED SINCE. timeOfDay_
    // wraps at kSecondsPerDay (see the constructor and skipToHour), so this is
    // NOT itself a day-and-hour pair -- it is a strictly increasing count a
    // caller turns into one, exactly the way elapsedSeconds() already is one
    // ingredient of `DAY N` today. Session never wraps this back down: two
    // moments a day apart have to compare as a day apart, which a wrapped
    // value could not do.
    return static_cast<std::int64_t>(config_.timeOfDay) + elapsedSeconds_;
}

void Session::examine() {
    if (talking() || picking()) {
        return;
    }
    if (casebookOpen_) {
        // Q closes the notes as well as opening a lead, so a player who has the
        // book up and presses the look key gets the world back rather than
        // nothing at all.
        toggleCasebook();
        return;
    }
    // THE FLAME'S EYE IS THE ONE BOON THAT REACHES THE TRAIL. legend.hpp names
    // this call site as the only one; a Wielder the ward defers to reads a
    // scene from the doorway instead of standing over it.
    const std::int32_t reach = sim::kLookRangeTiles + legend().lookRangeBonus();
    // The casebook takes a range in tiles and looks for itself, so the bonus is
    // applied by widening the search here rather than by handing the simulation
    // a number the surface computed -- which is why look() is asked twice at
    // most and never asked to trust a caller's arithmetic.
    const std::int32_t tileX = body_->tileX();
    const std::int32_t tileY = body_->tileY();
    const std::int32_t band = body_->band();
    const std::int64_t now = caseNowSeconds();
    sim::LookResult saw = casebook_.look(tileX, tileY, band, now);
    if (!saw.found && saw.lead < 0 && reach > sim::kLookRangeTiles) {
        // Nothing within the base reach. Walk outward one ring at a time up to
        // the bonus, standing the look at each offset -- integer, bounded, and
        // it cannot see anything a body one tile further along could not.
        for (std::int32_t ring = 1; ring <= reach - sim::kLookRangeTiles && saw.lead < 0;
             ++ring) {
            const std::int32_t offsets[4][2] = {
                {ring, 0}, {-ring, 0}, {0, ring}, {0, -ring}};
            for (const auto& offset : offsets) {
                saw = casebook_.look(tileX + offset[0], tileY + offset[1], band, now);
                if (saw.lead >= 0) {
                    break;
                }
            }
        }
    }
    // THE CLUE IS THE MESSAGE. It is what the player walked here for, so it
    // gets the row whole; how many leads it opened is on the CASE row, which is
    // permanent and where a count belongs. A DEAD END SAYS SO OUT LOUD, though
    // -- walking across the district to learn that the sea is the wrong
    // question is work, and a game that let that read the same as a blank tile
    // would be a game telling you not to look.
    const bool cold = saw.found && saw.opened == 0;
    say(cold ? saw.line + "  (COLD)" : saw.line);
}

/// PUTS THE NOTES AND THE KEY LIST DOWN. Called by every verb that acts on the
/// world, so a player who presses a game key with a menu up gets the game and
/// not a silent refusal -- and so the opening page can never be in the way of
/// the second thing a new player does.
void Session::dismissOverlays() noexcept {
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    waitOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    awaitingKey_ = false;
    firstRun_ = false;
    pauseOpen_ = false;
    quitArmed_ = false;
    menuFocus_ = kMenuFocusJournal;
    // TASK #83. Ten call sites deep (climb, dropDown, steal, toggleCrouch,
    // lift, setCrouched, jump, interact, punch, restHere) and every one of
    // them can be the thing that closes a panel a player left open. One call
    // here catches all ten rather than repeating it at each.
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// #77: the controls, and the page that changes them
// ---------------------------------------------------------------------------

void Session::setControls(const ControlSettings& settings) {
    controls_ = settings;
    controls_.sanitise();
    // The camera reads the field of view every frame off controls_, so a
    // settings file with an FOV in it is applied by the act of loading it and
    // there is no second copy to forget to update.
}

void Session::setFov(int degrees) {
    controls_.fovDegrees = degrees < kMinFov ? kMinFov : (degrees > kMaxFov ? kMaxFov : degrees);
}

std::vector<std::string> Session::keyRows() const {
    // GENERATED FROM THE LIVE BINDINGS, and that is the whole point of the
    // change. This page used to be a static array of strings sitting a hundred
    // lines away from a switch statement in the client, and its own comment
    // said it lived here so it "does not drift from them the moment somebody
    // rebinds one without looking down". It could not help drifting: nothing
    // connected the two. Now a rebinding shows up here by construction, because
    // this IS the binding table read out loud.
    std::vector<std::string> rows;
    rows.reserve(kActionCount + 8);
    rows.emplace_back("MOUSE  LOOK");
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        // THE QUICK BAR IS ONE ROW, NOT TEN. Ten near-identical rows would push
        // everything a player is actually looking for onto page four. The
        // OPTIONS page still lists all ten, because that is where you go to
        // change one.
        if (action >= Action::QuickSlot2 && action <= Action::QuickSlot0) {
            continue;
        }
        std::string row;
        if (action == Action::QuickSlot1) {
            row = std::string(keyName(controls_.primary[i])) + "-" +
                  std::string(keyName(controls_.primary[static_cast<std::size_t>(
                      Action::QuickSlot0)])) +
                  "  QUICK BAR";
        } else {
            row = std::string(keyName(controls_.primary[i])) + "  " +
                  std::string(actionLabel(action));
        }
        rows.push_back(row);
    }
    rows.emplace_back("WALK AT A LEDGE");
    rows.emplace_back("  TO CLIMB IT");
    for (const char* row : kLockRows) {
        rows.emplace_back(row);
    }
    return rows;
}

std::vector<std::string> Session::optionRows() const {
    std::vector<std::string> rows;
    rows.reserve(kSliderRows + kActionCount);
    // THE FOUR SLIDERS, in the order somebody looking for them expects.
    rows.push_back("SENSITIVITY  " + std::to_string(controls_.mouse.sensitivity));
    rows.push_back(std::string("INVERT Y  ") + (controls_.mouse.invertY ? "ON" : "OFF"));
    rows.push_back("VIEW ANGLE  " + std::to_string(controls_.fovDegrees));
    rows.push_back("PAD DEADZONE " + std::to_string(controls_.pad.deadzonePercent));
    static_assert(Session::kSliderRows == 4, "the slider rows and adjustOption must agree");
    for (std::size_t i = 0; i < kActionCount; ++i) {
        std::string row(actionLabel(static_cast<Action>(i)));
        row += "  ";
        // The row the page is waiting on says so in place of a key, so there is
        // never a moment where the game is listening and nothing on screen says
        // it is.
        const bool listening =
            awaitingKey_ && optionCursor_ == static_cast<int>(kSliderRows + i);
        row += listening ? "..." : std::string(keyName(controls_.primary[i]));
        rows.push_back(row);
    }
    return rows;
}

void Session::toggleOptions() {
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !optionsOpen_;
    optionsOpen_ = willOpen;
    if (willOpen) {
        // EVERY OTHER OVERLAY STANDS DOWN, INCLUDING THE PAUSE MENU. This used
        // to clear only the casebook and the keys page, which left a real hole:
        // open options with F2, then press F1, and keysOpen_ went true right
        // alongside it -- the keys page drew (dialogueView() checks it first)
        // while route_menu_key kept routing to the OPTIONS branch underneath,
        // because that check runs BEFORE the keys/casebook one. The page on
        // screen and the page reading your keystrokes were two different pages.
        // Reached this build via the pause menu's SETTINGS row, which is one
        // more way to get here than F2 alone used to have.
        casebookOpen_ = false;
        keysOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        menuFocus_ = kMenuFocusJournal;
    }
    awaitingKey_ = false;
    firstRun_ = false;
    optionCursor_ = 0;
    optionPage_ = 0;
    syncPanelAnim();
}

void Session::moveOptionCursor(int delta) {
    if (!optionsOpen_ || awaitingKey_) {
        return;
    }
    // AUDIO WIRING: the same one quiet tick moveTopicCursor speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    const int count = static_cast<int>(optionRows().size());
    if (count <= 0) {
        return;
    }
    optionCursor_ = ((optionCursor_ + delta) % count + count) % count;
    optionPage_ = optionCursor_ / kTopicPageSize;
}

void Session::adjustOption(int delta) {
    if (!optionsOpen_ || awaitingKey_ || delta == 0) {
        return;
    }
    switch (optionCursor_) {
        case 0:
            controls_.mouse.sensitivity += delta * kSensitivityStep;
            break;
        case 1:
            controls_.mouse.invertY = !controls_.mouse.invertY;
            break;
        case 2:
            controls_.fovDegrees += delta * kFovStep;
            break;
        case 3:
            controls_.pad.deadzonePercent += delta;
            break;
        default:
            // A BINDING ROW IS NOT A SLIDER. Nudging left on one is a player
            // looking for a value that is not there, and doing nothing is the
            // honest answer -- silently rebinding it would be worse.
            return;
    }
    controls_.sanitise();
}

void Session::chooseOption() {
    if (!optionsOpen_ || awaitingKey_) {
        return;
    }
    // AUDIO WIRING: the same accept every other list speaks -- a slider nudge
    // and a rebind-arm are both ENTER doing something.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    if (optionCursor_ < kSliderRows) {
        // ENTER ALWAYS DOES SOMETHING. On a slider it is a nudge up, so a player
        // who has learnt ENTER on the conversation surface is not met with a
        // key that silently refuses on half the rows of this one.
        adjustOption(1);
        return;
    }
    awaitingKey_ = true;
}

void Session::bindAwaited(Key key) {
    if (!optionsOpen_ || !awaitingKey_) {
        return;
    }
    awaitingKey_ = false;
    if (key == Key::None) {
        return;
    }
    const int index = optionCursor_ - kSliderRows;
    if (index < 0 || index >= static_cast<int>(kActionCount)) {
        return;
    }
    // STEALS, VISIBLY. See ControlSettings::bind: two verbs sharing a key is a
    // game where one of them stops working and the player cannot find out
    // which. The row it was taken from shows "--" on this very page.
    controls_.bind(static_cast<Action>(index), key);
}

// ---------------------------------------------------------------------------
// the pause menu
// ---------------------------------------------------------------------------

void Session::togglePause() {
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !pauseOpen_;
    // EVERY OTHER OVERLAY STANDS DOWN, same as toggleOptions -- opening the
    // pause menu over an already-open casebook or keys page would be the same
    // split-brain bug that fix closes there: a page on screen that is not the
    // page reading the keyboard.
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    waitOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    awaitingKey_ = false;
    menuFocus_ = kMenuFocusJournal;
    pauseOpen_ = willOpen;
    pauseCursor_ = 0;
    quitArmed_ = false;
    firstRun_ = false;
    syncPanelAnim();
}

std::vector<std::string> Session::pauseRows() const {
    // MORROWIND ROUND: CONTROLS IS NEW. The tiled Menu's four tiles have no
    // room for a long, read-top-to-bottom reference list (session.hpp's own
    // note), so Keys followed Options' own precedent -- SETTINGS has always
    // opened Menu's rebinding screen through the identical optionsOpen_ state
    // -- and took a row here too, rather than being left with no door into
    // the tiled Menu at all.
    // TIME-AND-TENURE BUILD: WAIT is new, second, and a door rather than a
    // page swap -- it opens the hour-select list (see openWait). Second
    // because the two rows a player reaches for mid-game (resume, pass the
    // hours) belong above the two they visit once (controls, settings), and
    // QUIT stays last where a quit belongs.
    return {
        "RESUME",
        "WAIT",
        "CONTROLS",
        "SETTINGS",
        quitArmed_ ? "QUIT -- SURE? ENTER" : "QUIT GRANADAD",
    };
}

void Session::movePauseCursor(int delta) {
    if (!pauseOpen_) {
        return;
    }
    // MOVING THE CURSOR DISARMS QUIT. A player who backed off the row rather
    // than pressing it again plainly changed their mind, and leaving the arm
    // set for whichever row they land on next would fire QUIT off a key that
    // was never pressed twice.
    quitArmed_ = false;
    // AUDIO WIRING: the same one quiet tick every other list speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    const int count = static_cast<int>(pauseRows().size());
    if (count <= 0) {
        return;
    }
    pauseCursor_ = ((pauseCursor_ + delta) % count + count) % count;
}

void Session::choosePause() {
    if (!pauseOpen_) {
        return;
    }
    // AUDIO WIRING: the same accept every other list speaks. CONTROLS/
    // SETTINGS' page swaps add nothing further (conversingNow() stays true
    // across a swap, so panelAnim_ never flips); RESUME's close is spoken by
    // the panel pair in syncPanelAnim(), as every close is.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    switch (pauseCursor_) {
        case 0:
            // RESUME. The same thing ESC does from here, spelled out as a row
            // for the player who would rather click or press ENTER than learn
            // that ESC backs out of everything in this game.
            pauseOpen_ = false;
            pauseCursor_ = 0;
            quitArmed_ = false;
            syncPanelAnim();
            return;
        case 1:
            // WAIT (TIME-AND-TENURE BUILD). openWait() opens the hour-select
            // page and puts this menu down in the same call, the identical
            // shape the two rows below have. Wait mode, never sleep: the
            // healing door is the bed's Interact press and only that -- the
            // owner's ruling, enforced by which door you walked through.
            openWait(false);
            return;
        case 2:
            // CONTROLS. toggleKeys() opens it and puts this page down in the
            // same call, the identical shape SETTINGS below already has.
            // Relocated here from #85's own Menu cycle -- see toggleKeys()'s
            // own header on why the page itself is unchanged and only the
            // door moved.
            toggleKeys();
            return;
        case 3:
            // SETTINGS. toggleOptions() opens it and puts this page down in the
            // same call -- see its own comment on why every overlay does that,
            // AND syncs the panel anim itself -- no need to repeat it here.
            // This is the controls round's rebinding screen, reachable from the
            // menu a player actually pauses on rather than only from F2.
            toggleOptions();
            return;
        case 4:
            // QUIT. ITS OWN NUMBERED CASE, and now `default` truly is the
            // loud-failure arm the CONTROLS insertion promised: a future
            // seventh pause row that nobody wires here does NOTHING rather
            // than quietly arming QUIT.
            break;
        default:
            return;
    }
    // QUIT. Armed on the first press and confirmed on the second, so leaning on
    // ENTER once cannot close the window -- see quitArmed().
    if (!quitArmed_) {
        quitArmed_ = true;
        return;
    }
    quitArmed_ = false;
    quitRequested_ = true;
}

void Session::setCrouched(bool crouched) {
    dismissOverlays();
    if (talking()) {
        return;
    }
    if ((tavern_->stance() == sim::Stance::Crouched) == crouched) {
        return;
    }
    tavern_->toggleStance();
    say(crouched ? "CROUCHED" : "UPRIGHT");
}

void Session::selectQuickSlot(int slot) {
    if (slot < 0 || slot > 9) {
        return;
    }
    quickSlot_ = slot;
    showQuickBar();
    // SPELLS BUILD: a LOADED slot equips its crafting -- through
    // Tavern::equipSlot, which routes through the same equipSpellAt the
    // Grimoire page uses, so the CAST row, the strip's highlight and the next
    // press of C all read the one equipped id. An empty slot still says so
    // out loud: the binding is real, and a key that silently does nothing is
    // a key the player reads as broken.
    if (tavern_->equipSlot(slot)) {
        // AUDIO WIRING: the same accept every list speaks -- the press
        // changed what the hand holds.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiConfirm);
        }
        say("SLOT " + std::to_string(slot + 1) + " -- READY: " +
            upperAscii(tavern_->slotSpell(slot)->displayName) + ".");
        return;
    }
    // AUDIO WIRING: the quiet tick a cursor move gets -- nothing changed.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    say("SLOT " + std::to_string(slot + 1) + " -- NOTHING IN IT. THE GRIMOIRE BINDS.");
}

void Session::showQuickBar() {
    // A couple of seconds past the last touch -- the strip is up exactly
    // while the wheel or the number row is being used. The ease itself is
    // quickBarAnim_'s business, driven in syncPanelAnim()/step() like every
    // other row.
    quickBarShowSteps_ = kQuickBarShowSteps;
}

void Session::jump() {
    dismissOverlays();
    if (talking() || picking()) {
        return;
    }
    if (!body_->jump()) {
        return;
    }
    // NOTHING IS SAID. A jump that announced itself on the alert row every time
    // would be the noisiest thing in the game, and the player can see it.
    tavern_->setPlayerMotion(true, true);
}

void Session::toggleKeys() {
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !keysOpen_;
    keysOpen_ = willOpen;
    if (willOpen) {
        // See toggleOptions' comment: an overlay that opens without putting the
        // others down is how a page draws on screen while a different one is
        // still the one reading the keyboard.
        casebookOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        menuFocus_ = kMenuFocusJournal;
    }
    firstRun_ = false;
    caseCursor_ = 0;
    casePage_ = 0;
    caseEntry_ = -1;
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// SPELLS BUILD: the Grimoire page, and the quick bar it loads
// ---------------------------------------------------------------------------

void Session::toggleGrimoire() {
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !grimoireOpen_;
    grimoireOpen_ = willOpen;
    if (willOpen) {
        // See toggleOptions' comment: an overlay that opens without putting the
        // others down is how a page draws on screen while a different one is
        // still the one reading the keyboard.
        casebookOpen_ = false;
        keysOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        menuFocus_ = kMenuFocusJournal;
    }
    firstRun_ = false;
    grimoireCursor_ = 0;
    grimoirePage_ = 0;
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// the ward map (core action #13)
// ---------------------------------------------------------------------------

void Session::toggleDistrictMap() {
    // Inert while a conversation or the wire owns the keyboard, exactly like
    // toggleGrimoire -- a page opening under a topic list is the split-brain
    // bug toggleOptions' own comment describes.
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !districtMapOpen_;
    districtMapOpen_ = willOpen;
    if (willOpen) {
        // Every other overlay stands down -- toggleOptions' rule, same reason.
        casebookOpen_ = false;
        keysOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        menuFocus_ = kMenuFocusJournal;
    }
    firstRun_ = false;
    syncPanelAnim();
}

std::vector<std::string> Session::grimoireRows() const {
    // ONE ROW PER KNOWN CRAFTING, grimoire order -- the same order
    // equipSpellAt counts in, so the number printed beside a row IS the index
    // the equip spends. Name, the cost model's own difficulty (the number the
    // linkcraft check is rolled against -- information, never a buff), FORGED
    // on a composition of your own (forgedSpellId's "forged." prefix is the
    // honest tag; craftedCount() only counts), the slot it is bound to, and
    // READY on the one the hand is holding.
    std::vector<std::string> rows;
    const sim::Grimoire& book = tavern_->dialogue().grimoire();
    const sim::Spell* held = tavern_->equippedSpell();
    rows.reserve(book.spells().size());
    for (const sim::Spell& spell : book.spells()) {
        std::string row = upperAscii(spell.displayName);
        row += "  D" + std::to_string(sim::spellDifficulty(spell));
        if (spell.id.rfind("forged.", 0) == 0) {
            row += "  FORGED";
        }
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::Spell* bound = tavern_->slotSpell(slot);
            if (bound != nullptr && bound->id == spell.id) {
                row += "  SLOT " + std::to_string(slot + 1);
                break;
            }
        }
        if (held != nullptr && held->id == spell.id) {
            row += "  READY";
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void Session::moveGrimoireCursor(int delta) {
    if (!grimoireOpen_) {
        return;
    }
    // AUDIO WIRING: the same one quiet tick every other list speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    wrapCursorAndPage(grimoireCursor_, grimoirePage_, delta,
                      static_cast<int>(tavern_->dialogue().grimoire().spells().size()));
}

void Session::nextGrimoirePage() {
    if (!grimoireOpen_) {
        return;
    }
    advancePage(grimoirePage_, grimoireCursor_,
                tavern_->dialogue().grimoire().spells().size());
}

void Session::chooseGrimoireRow(int slot) {
    if (!grimoireOpen_ || slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    const int index = grimoirePage_ * kTopicPageSize + slot;
    const std::vector<sim::Spell>& spells = tavern_->dialogue().grimoire().spells();
    if (index >= static_cast<int>(spells.size())) {
        return;
    }
    grimoireCursor_ = index;
    // THE SAME LOCK THE QUICK BAR TURNS. equipSpellAt is the one door into
    // the hand; the CAST row, the strip's highlight and the next press of C
    // all read the one id it re-points.
    if (tavern_->equipSpellAt(index)) {
        // AUDIO WIRING: the same accept every list speaks.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiConfirm);
        }
        say("READY: " + upperAscii(spells[static_cast<std::size_t>(index)].displayName) + ".");
    }
}

void Session::adjustGrimoireSlot(int delta) {
    if (!grimoireOpen_ || delta == 0) {
        return;
    }
    const std::vector<sim::Spell>& spells = tavern_->dialogue().grimoire().spells();
    if (grimoireCursor_ < 0 || grimoireCursor_ >= static_cast<int>(spells.size())) {
        return;
    }
    const sim::Spell& spell = spells[static_cast<std::size_t>(grimoireCursor_)];
    // Which slot holds this crafting now, or -1 -- the cycle walks
    // NONE, 1..10 and wraps, so LEFT from NONE is slot 10 and RIGHT off
    // slot 10 is NONE again. Moving it clears the slot it leaves.
    std::int32_t current = -1;
    for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
        const sim::Spell* bound = tavern_->slotSpell(slot);
        if (bound != nullptr && bound->id == spell.id) {
            current = slot;
            break;
        }
    }
    const std::int32_t positions = sim::Tavern::kQuickSlotCount + 1;
    const std::int32_t next =
        ((current + 1 + delta) % positions + positions) % positions - 1;
    if (current >= 0) {
        tavern_->clearSlot(current);
    }
    if (next >= 0) {
        tavern_->bindSpellToSlot(next, spell.id);
    }
    // AUDIO WIRING: the same one quiet tick a slider nudge gets -- the
    // restraint note stands, no new sound design.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    say(next >= 0 ? upperAscii(spell.displayName) + " -- SLOT " + std::to_string(next + 1) + "."
                  : upperAscii(spell.displayName) + " -- NO SLOT.");
}

// ---------------------------------------------------------------------------
// TIME-AND-TENURE BUILD: the Wait page
// ---------------------------------------------------------------------------

namespace {

/// One row per hour ahead, up to a half-day. Enough to reach any named hour
/// from anywhere on the clock without a second page of arithmetic.
constexpr int kWaitHours = 12;

/// "HH:00", two digits, the clock the compass row already speaks.
[[nodiscard]] std::string hourLabel(int hour) {
    const int wrapped = ((hour % 24) + 24) % 24;
    return (wrapped < 10 ? "0" : "") + std::to_string(wrapped) + ":00";
}

/// The name an hour is known by out loud, or "".
[[nodiscard]] const char* hourName(int hour) noexcept {
    switch (((hour % 24) + 24) % 24) {
        case 0: return "MIDNIGHT";
        case 6: return "DAWN";
        case 12: return "NOON";
        case 18: return "DUSK";
        default: return "";
    }
}

}  // namespace

void Session::openWait(bool sleepMode) {
    if (talking() || picking()) {
        return;
    }
    // Every other overlay stands down -- toggleOptions' rule, same reason.
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    pauseOpen_ = false;
    quitArmed_ = false;
    awaitingKey_ = false;
    menuFocus_ = kMenuFocusJournal;
    firstRun_ = false;
    waitOpen_ = true;
    waitSleep_ = sleepMode;
    waitCursor_ = 0;
    waitPage_ = 0;
    syncPanelAnim();
}

std::string Session::waitRefusal() const {
    // THE WHOLE OF "ANYWHERE SAFE", in check order -- see the header block in
    // session.hpp. Each clause reads state something else already owns and
    // hashes; nothing here is new simulation. Deliberately NOT consulted:
    // heat, warrants, darkness, altitude. Waiting out a warrant is an
    // intended tactic (the owner's ruling), and a roof at midnight is only as
    // dangerous as whoever is on it -- which the HOSTILE clause already asks.
    if (tavern_->playerFloored()) {
        return "NOT FROM THE FLOOR.";
    }
    if (tavern_->playerInBrawl()) {
        return "NOT WHILE FISTS ARE UP.";
    }
    // A HOSTILE body close enough to cross the room while your eyes are off
    // it. Three times a conversation's reach -- six tiles -- and Chebyshev
    // like every reach in this build. Band deliberately ignored: a hostile
    // one floor up a stair you cannot see is the conservative read.
    constexpr std::int32_t kHostileReachQ8 = 3 * sim::kReachQ8;
    for (const sim::Actor& actor : tavern_->actors()) {
        if (!actor.present()) {
            continue;
        }
        if (tavern_->dialogue().ledger().attitudeOf(actor.id()) != sim::Attitude::Hostile) {
            continue;
        }
        if (actor.distanceTo(body_->x(), body_->y()) <= kHostileReachQ8) {
            return "NOT WITH AN ENEMY THIS CLOSE.";
        }
    }
    if (body_->airborne()) {
        return "NOT IN MID-AIR.";
    }
    if (tiles_->fluidDepth(body_->tileX(), body_->tileY(), body_->band()) > 0) {
        return "NOT STANDING IN WATER.";
    }
    return {};
}

std::vector<std::string> Session::waitRows() const {
    // Twelve rows off the LIVE clock, so the hour printed is the hour a pick
    // lands on -- skipToHour truncates to the top of the hour, and a list
    // that said "1 HOUR" while delivering forty minutes would be lying.
    std::vector<std::string> rows;
    rows.reserve(kWaitHours);
    const int nowHour = timeOfDay_ / 3600;
    for (int ahead = 1; ahead <= kWaitHours; ++ahead) {
        const int target = (nowHour + ahead) % 24;
        std::string row = std::to_string(ahead) + (ahead == 1 ? " HOUR" : " HOURS") +
                          "  TO " + hourLabel(target);
        if (hourName(target)[0] != '\0') {
            row += "  ";
            row += hourName(target);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void Session::moveWaitCursor(int delta) {
    if (!waitOpen_) {
        return;
    }
    // AUDIO WIRING: the same one quiet tick every other list speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    wrapCursorAndPage(waitCursor_, waitPage_, delta, kWaitHours);
}

void Session::nextWaitPage() {
    if (!waitOpen_) {
        return;
    }
    advancePage(waitPage_, waitCursor_, static_cast<std::size_t>(kWaitHours));
}

void Session::chooseWaitRow(int slot) {
    if (!waitOpen_ || slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    const int index = waitPage_ * kTopicPageSize + slot;
    if (index >= kWaitHours) {
        return;
    }
    waitCursor_ = index;
    const int hours = index + 1;
    const int target = ((timeOfDay_ / 3600) + hours) % 24;
    // AUDIO WIRING: the same accept every list speaks, refusals included --
    // the press is what is acknowledged, exactly chooseTopic's rule.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    if (waitSleep_) {
        // THE BED'S OWN GATE, not waitRefusal(): sleep answers where sleep
        // has always answered. Refused out loud in restRefusal's words, the
        // page staying up -- the same door the R verb names.
        const sim::ServiceResult slept = tavern_->sleepUntil(target);
        if (slept != sim::ServiceResult::Served) {
            say(std::string(sim::restRefusal(slept)));
            return;
        }
        syncClockAfterSkip();
    } else {
        // RE-CHECKED ON THE PRESS, not only at the page's opening: a brawler
        // can close the distance while the list is up, and the page and the
        // key must name the same door.
        const std::string refusal = waitRefusal();
        if (!refusal.empty()) {
            say(refusal);
            return;
        }
        // skipToHour is Tavern::skipTo plus the calendar sync -- heat cools
        // on the elapsed seconds in there, which is what makes waiting out a
        // warrant a tactic rather than an exploit.
        skipToHour(target);
    }
    waitOpen_ = false;
    waitCursor_ = 0;
    waitPage_ = 0;
    syncPanelAnim();
    say((waitSleep_ ? std::string("SLEPT UNTIL ") : std::string("WAITED UNTIL ")) +
        hourLabel(target) + ".");
}

void Session::toggleMenuFocused(int focus) {
    if (talking() || picking()) {
        return;
    }
    const int wrapped = ((focus % kMenuFocusCount) + kMenuFocusCount) % kMenuFocusCount;
    if (casebookOpen_) {
        if (menuFocus_ == wrapped) {
            // THE KEY THAT OPENED IT CLOSES IT -- the same rule every one of
            // #85's six pages used to have on its own, now scoped to "the tile
            // you are already looking at" rather than to a page that no
            // longer exists on its own.
            casebookOpen_ = false;
            caseCursor_ = 0;
            casePage_ = 0;
            caseEntry_ = -1;
            characterCursor_ = 0;
            characterPage_ = 0;
            mapCursor_ = 0;
            mapPage_ = 0;
            lettersCursor_ = 0;
            lettersPage_ = 0;
            lettersEntry_ = -1;
            lettersBodyPage_ = 0;
            menuFocus_ = kMenuFocusJournal;
        } else {
            // ALREADY OPEN, DIFFERENT TILE ASKED FOR: MOVE FOCUS, DO NOT
            // RESET. Each tile keeps its own cursor/page/picked-entry --
            // switching focus to read the Letters tile and back should not
            // have thrown away where the Journal tile's cursor was.
            menuFocus_ = wrapped;
        }
    } else {
        // OPENING FRESH. Every other overlay stands down, same as
        // toggleOptions()/toggleKeys()/togglePause() already do on their own.
        keysOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        casebookOpen_ = true;
        menuFocus_ = wrapped;
        caseCursor_ = 0;
        casePage_ = 0;
        caseEntry_ = -1;
        characterCursor_ = 0;
        characterPage_ = 0;
        mapCursor_ = 0;
        mapPage_ = 0;
        lettersCursor_ = 0;
        lettersPage_ = 0;
        lettersEntry_ = -1;
        lettersBodyPage_ = 0;
    }
    firstRun_ = false;
    syncPanelAnim();
}

void Session::toggleCasebook() { toggleMenuFocused(kMenuFocusJournal); }

void Session::toggleCharacter() { toggleMenuFocused(kMenuFocusCharacter); }

void Session::toggleMap() { toggleMenuFocused(kMenuFocusMap); }

void Session::toggleLetters() { toggleMenuFocused(kMenuFocusLetters); }

// ---------------------------------------------------------------------------
// Morrowind round: one Menu, four tiles (Keys and Options moved to Pause)
// ---------------------------------------------------------------------------

bool Session::menuOpen() const noexcept {
    // districtMapOpen_ joined this list with core action #13: the ward map
    // is a full-screen page, so the movement keys stand down under it the
    // way they do under every other page (main.cpp's `listening`).
    return casebookOpen_ || keysOpen_ || grimoireOpen_ || optionsOpen_ || districtMapOpen_;
}

void Session::toggleMenu() {
    // THE KEY THAT OPENED IT CLOSES IT -- toggleMenuFocused() already carries
    // this rule for "the tiled Menu, focused on the tile it is already
    // focused on", which is exactly what re-pressing the single Menu action
    // (never any of the four toggle*() entry points a real key no longer
    // reaches) always asks for.
    toggleMenuFocused(menuFocus_);
}

void Session::menuPageNext() {
    // MORROWIND ROUND: STEPS FOCUS, NOT PAGES. With all four tiles on screen
    // at once there is no "next page" left -- see session.hpp's own note.
    // Keys and Options have no tiles of their own to step between, so this
    // is a no-op while either of them (rather than the tiled Menu) is what is
    // open; NOT OPEN AT ALL is the same no-op it always was, so a bumper
    // press with nothing open still does not open anything.
    if (!casebookOpen_) {
        return;
    }
    // AUDIO WIRING: the plan's "page cycle in the tiled Menu -> BookFlip".
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::BookFlip);
    }
    menuFocus_ = ((menuFocus_ + 1) % kMenuFocusCount + kMenuFocusCount) % kMenuFocusCount;
}

void Session::menuPagePrev() {
    if (!casebookOpen_) {
        return;
    }
    // AUDIO WIRING: the same page-turn, backward.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::BookFlip);
    }
    menuFocus_ =
        ((menuFocus_ - 1) % kMenuFocusCount + kMenuFocusCount) % kMenuFocusCount;
}

std::vector<std::int32_t> Session::unlockedLetters() const {
    std::vector<std::int32_t> out;
    // A LETTER'S GATE IS THE SAME GATE ITS LEAD ALREADY HAS -- Cold or
    // Followed, never merely Open. An Open lead is a name in the book with
    // nobody standing over it yet; the sentence that says a letter exists at
    // all ("he wrote three unanswered letters...") is inside the lead's OWN
    // `detail` text, which the player has not read until the state has moved
    // past Open. Showing the letter earlier would hand over a document
    // before the game has told the player it exists.
    for (std::size_t i = 0; i < letterRaws_.letters().size(); ++i) {
        const sim::Letter& letter = letterRaws_.letters()[i];
        const std::int32_t lead = caseRaws_.indexOf(letter.lead);
        if (lead < 0) {
            continue;
        }
        const sim::LeadState state = casebook_.state(lead);
        if (state == sim::LeadState::Cold || state == sim::LeadState::Followed) {
            out.push_back(static_cast<std::int32_t>(i));
        }
    }
    return out;
}

bool Session::picking() const noexcept { return tavern_->picking().open(); }

const sim::Lockpicking& Session::lockpicking() const noexcept { return tavern_->picking(); }

int Session::picks() const noexcept { return tavern_->picks(); }

void Session::movePick(int delta) { tavern_->movePick(delta); }

void Session::probeLock() {
    const sim::Tavern::PickResult felt = tavern_->probeLock();
    say(felt.line);
}

void Session::forceLock() {
    syncTavernToBody();
    say(tavern_->forceLock().line);
}

void Session::stopPicking() {
    if (!tavern_->picking().open()) {
        return;
    }
    tavern_->abandonPick();
    say("WIRE OUT. IT RELOCKS.");
}

void Session::settleDefeat() {
    // THE PLAYER RESPAWNS AS THEMSELVES AND THE WORLD KEEPS THE CONSEQUENCES.
    // Eli ruled 2026-07-31 that the persistent-ward variant -- the city
    // surviving your death while you come back as somebody else -- is not being
    // built from the start. So this is the whole of the respawn: the room gives
    // the hit points back and moves the clock on, and the body wakes up on the
    // quay apron, which is the same two tiles clear of the threshold a man put
    // out of the door ends up on.
    //
    // WHAT IT DOES NOT DO IS UNDO ANYTHING. The rung, the founded house, the
    // toll on every price and the name on the roll all survive this call. That
    // is the design.
    const sim::Rise& rise = tavern_->lastDefeat();
    // AUDIO WIRING: the plan's "knockdown -> ThudHeavy" -- the one call every
    // path to the floor funnels through.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::ThudHeavy);
    }
    tavern_->reviveAfterDefeat();
    body_->placeAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
    awaitingLanding_ = false;
    syncTavernToBody();
    timeOfDay_ = tavern_->timeOfDay();
    settings_.timeOfDay = timeOfDay_;
    // What he said standing over you, out of the owner's tables -- and the
    // short report of what it made him when there is nothing authored.
    if (!rise.taunt.empty()) {
        say(rise.who + ": " + rise.taunt);
    } else if (!rise.line.empty()) {
        say(rise.line);
    }
    syncWardToCalendar();
}

void Session::step(const sim::MoveInput& input) {
    // The room moves first, then the shove it asked for is applied to the body
    // that owns its own collision, then the player's own input. That order is
    // deliberate: a bouncer's shove and a player's step in the same movement
    // step both go through PlayerBody, so neither can push the other through a
    // wall.
    // FIRST STEP CLOSES THE OPENING PAGE. The session boots with the casebook
    // up so a new player is told what the case is before they are told
    // anything else; the moment they walk, they have read it and it gets out
    // of the way. It never reopens itself.
    if (firstRun_ && (input.forward != 0 || input.strafe != 0)) {
        firstRun_ = false;
        casebookOpen_ = false;
        caseCursor_ = 0;
        casePage_ = 0;
        caseEntry_ = -1;
    }
    syncTavernToBody();
    // HELD-EFFECTS BUILD. AGI's gait reader, RE-READ EVERY STEP off the
    // effective sheet: a held Steady the Hand moves the legs the same step it
    // moves the sheet, and moves them back the second it lapses -- continuous
    // state driving continuous motion, the same seam applyPlayerAttributes
    // crossed at boot. Exactly the shipped scale whenever no hold is live, so
    // every pre-existing capture walks bit for bit.
    body_->setSpeedScaleQ8(sim::agilitySpeedScaleQ8(
        tavern_->effectiveAttributes().value(sim::AttributeId::Agility)));
    tavern_->stepMovement();
    const std::int32_t shoveX = tavern_->takePlayerShoveX();
    const std::int32_t shoveY = tavern_->takePlayerShoveY();
    if (shoveX != 0 || shoveY != 0) {
        body_->push(shoveX, shoveY);
    }
    // S9. THE STANCE IS THE ROOM'S AND THE BODY OBEYS IT. Crouching is
    // simulation state -- it decides who sees a crime -- so the tavern owns it
    // and the body is TOLD, rather than the client keeping a second copy of it
    // that the hasher cannot see. The room is told back how the body is moving,
    // which is how footfalls reach the notice rule.
    sim::MoveInput moved = input;
    moved.crouch = tavern_->stance() == sim::Stance::Crouched;
    const bool walking = moved.forward != 0 || moved.strafe != 0;
    // FATIGUE BUILD: THE SPRINT GATE, before the body ever hears the key. An
    // empty pool downgrades the sprint to the jog -- the input is edited, not
    // the body interrupted, so the legs brake through their own ordinary ramp
    // -- and the refusal is said ONCE per stretch of windedness (see
    // windedSprintSaid_'s own header on the restraint). The automatic climb
    // is gated by the same fact for the same reason the explicit verb is in
    // tryClimb(): no wind, no haul -- the wall just stops you, silently,
    // because the auto path fires sixty times a second against a held key.
    if (tavern_->playerWinded()) {
        if (moved.sprint && !moved.crouch && walking && !windedSprintSaid_) {
            say("TOO WINDED TO SPRINT.");
            windedSprintSaid_ = true;
        }
        moved.sprint = false;
        moved.autoTraverse = false;
    } else {
        windedSprintSaid_ = false;
    }
    const bool sprinting = moved.sprint && !moved.crouch;
    tavern_->setPlayerMotion(walking, sprinting);
    // The pool's own step, with the SAME flags the stealth model was just
    // told: the sprinting step pays its drain, every other one regenerates
    // (halved while the legs are working). See Tavern::stepPlayerFatigue.
    tavern_->stepPlayerFatigue(walking, sprinting);
    // FIRST-PERSON COMBAT (S13). THE EFFECTIVE GUARD, PUSHED EVERY STEP the
    // identical way the motion above is: the client only reports the key's
    // physical edge (setBlocking -> blockHeld_), and what the ROOM is told is
    // derived here so a page opening mid-hold lowers the guard the same step
    // it takes the keyboard, and a page closing under a still-held key raises
    // it again -- no edge event exists for either of those moments.
    tavern_->setPlayerBlocking(blockHeld_ && !talking() && !picking());
    body_->step(moved);
    syncTavernToBody();

    // AUDIO WIRING: FOOTSTEPS, exactly per audio_engine.hpp's plan -- the
    // material of the tile UNDER the feet, whether the body is at a run, and
    // the wading layer at the feet themselves. Called every step while the
    // input says "moving" (the engine rate-limits to a walk/run cadence
    // internally, so this is the documented legal wiring); airborne feet
    // touch nothing and say nothing.
    if (audio_ != nullptr && walking && !body_->airborne()) {
        const std::int32_t px = body_->tileX();
        const std::int32_t py = body_->tileY();
        const std::int32_t pz = body_->band();
        (void)audio_->footstep(tiles_->material(px, py, pz - 1),
                               moved.sprint && !moved.crouch,
                               tiles_->fluidDepth(px, py, pz));
    }

    // #77. A CLIMB NOBODY PRESSED A KEY FOR IS STILL A CLIMB, and the room
    // charges it exactly as it charges one that was asked for: the craft, the
    // Skyrunners' regard, the roof-run the ward would have minded, and the fall
    // if the far side turned out to be lower. Routing the automatic path through
    // a different settlement than the explicit one is how the two quietly stop
    // agreeing about what a roof-run is.
    const sim::RoofResult climbed = body_->takeAutoMove();
    if (climbed.ok()) {
        // FATIGUE BUILD: a climb nobody pressed a key for still costs wind,
        // through the identical charge the explicit verb pays in tryClimb()
        // -- routing the two through different bills is how they would
        // quietly stop agreeing about what a climb costs.
        tavern_->chargePlayerMantle();
        roofMove_ = "UP AND OVER.";
        settleLanding(climbed);
        say(roofMove_);
        syncTavernToBody();
    }

    // THE WATCH TOOK YOU AND HAS LET YOU GO. The room owns the sentence, the
    // seizure and the clock; the BODY is this file's, so the walk to the
    // impound and the morning at its gate happen here -- which is to say they
    // do not happen at all, and that is stated rather than implied.
    if (tavern_->takeArrestRelease()) {
        body_->placeAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
        awaitingLanding_ = false;
        syncTavernToBody();
        say(tavern_->lastArrest().line);
    }

    // AND SOMEBODY PUT YOU ON THE FLOOR. Same shape, same reason: the room owns
    // the beating, the rise and the clock, and the body is this file's.
    if (tavern_->takeDefeatRelease()) {
        settleDefeat();
    }

    // THE ARC CAME DOWN. A leap armed by climb() is settled HERE, on the step
    // the feet touch, because that is the only step on which the body has a
    // fall to report -- takeFallBands() is written by PlayerBody::land and read
    // exactly once. Charging it at the keypress, as S5 did, meant charging it
    // before the fall existed.
    if (awaitingLanding_ && !body_->airborne()) {
        awaitingLanding_ = false;
        settleLanding(pendingLanding_);
        syncTavernToBody();
    }

    // TASK #83. THE STRING OUTLIVES ITS OWN COUNTDOWN BY A FEW STEPS, ON
    // PURPOSE. message_ used to clear the instant messageSteps_ reached zero,
    // which is exactly the pop this pass exists to close: an alpha cannot
    // fade a string that is already gone. So the countdown alone decides
    // whether the alert is WANTED (see syncPanelAnim, just below) and the
    // string itself is only cleared once alertAnim_ has actually finished
    // easing down to nothing -- which is a handful of steps later, not the
    // same step.
    if (messageSteps_ > 0) {
        --messageSteps_;
    }

    // THE SPEECH REVEAL CLOCK. A courtesy for whoever is watching the screen
    // live, not a fact about the world -- see lastSpokenLine_'s own note. It
    // ticks only while a real conversation is open: the casebook, the keys
    // page and the options page all borrow this same widget to print
    // reference material nobody said out loud, and typing THAT out would be
    // decoration doing the opposite of its job. The clock resets the instant
    // the line changes, so a fresh reply always arrives fresh rather than
    // picking up wherever the last one left off.
    if (tavern_->dialogue().isOpen()) {
        const std::string& spoken = tavern_->dialogue().lastLine();
        if (spoken != lastSpokenLine_) {
            lastSpokenLine_ = spoken;
            speechAgeSteps_ = 0;
        } else if (speechAgeSteps_ < kSpeechRevealSteps) {
            ++speechAgeSteps_;
        }
    } else if (!lastSpokenLine_.empty()) {
        lastSpokenLine_.clear();
    }

    // TASK #83. THE PANEL AND THE ALERT EASE ONCE A STEP, IN STEP. Re-reading
    // the target every step (rather than only from the toggle methods that
    // set it eagerly) is what catches a conversation the SIMULATION closed --
    // a bouncer walking somebody out, say -- and not only one a keypress did.
    syncPanelAnim();
    panelAnim_.advance();
    alertAnim_.advance();
    // HARDENING PASS. THE SAME PER-STEP ADVANCE, ONE PER ROW.
    interactAnim_.advance();
    lockAnim_.advance();
    caseAnim_.advance();
    roomAnim_.advance();
    rivalAnim_.advance();
    guildAnim_.advance();
    objectiveAnim_.advance();
    stealthAnim_.advance();
    // PLANNING SPRINT (item #2, the sweep). THE SAME PER-STEP ADVANCE.
    standingAnim_.advance();
    heatAnim_.advance();
    stashAnim_.advance();
    // FIRST-PERSON COMBAT (S13). THE SAME PER-STEP ADVANCE.
    spellAnim_.advance();
    blockAnim_.advance();
    // HELD-EFFECTS BUILD. THE SAME PER-STEP ADVANCE, ONE PER SLOT.
    for (EasedToggle& anim : effectAnims_) {
        anim.advance();
    }
    // FATIGUE BUILD. THE SAME PER-STEP ADVANCE.
    fatigueAnim_.advance();
    // THE WARD MAP (core action #13). THE SAME PER-STEP ADVANCE.
    districtMapAnim_.advance();
    // SPELLS BUILD. The strip's own countdown and ease -- see showQuickBar().
    if (quickBarShowSteps_ > 0) {
        --quickBarShowSteps_;
    }
    quickBarAnim_.advance();
    // INNOVATION SPRINT ITEM #2. THE SAME PER-STEP ADVANCE, ONE PER TILE.
    characterFocusAnim_.advance();
    mapFocusAnim_.advance();
    lettersFocusAnim_.advance();
    journalFocusAnim_.advance();
    // INNOVATION SPRINT ITEM #3. A BLOW LANDING ON THE PLAYER, CAUGHT BY
    // COMPARISON RATHER THAN A NEW SIM-SIDE FLAG. tickBrawl() (tavern.cpp)
    // can drop tavern_->playerHp() during the stepMovement() call already
    // made above, from any of however many opponents are still swinging --
    // there is no single call site in THIS file that "throws the punch" the
    // way Session::punch() is for the player's own, so the render layer
    // reads the one number it already reads for the HUD's health bar
    // (drawFrame's hud.health) and watches it for a drop instead. Purely a
    // comparison of already-public state; nothing new reaches into the
    // simulation and nothing here is hashed -- see punchTakenPulse_'s own
    // header.
    const std::int32_t hpNow = tavern_->playerHp();
    // FIRST-PERSON COMBAT (S13). A GUARDED HIT IS ITS OWN, QUIETER MOMENT.
    // blowsBlocked() moving in the same step the hp dropped means the guard
    // caught what landed, so the cool blockPulse_ fires INSTEAD of the
    // blooded punchTakenPulse_ -- one wash per blow, per the restraint note
    // in drawFrame(), and the guard working reads different from the guard
    // failing. The same comparison-not-flag shape as lastPlayerHp_ itself.
    const std::int32_t blockedNow = tavern_->blowsBlocked();
    if (hpNow < lastPlayerHp_) {
        if (blockedNow > lastBlowsBlocked_) {
            blockPulse_.trigger();
            // AUDIO WIRING: the guard's clang, on the identical edge the cool
            // wash fires on -- one sound per blow, same as one wash per blow.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::SwordClash);
            }
        } else {
            punchTakenPulse_.trigger();
            // AUDIO WIRING: the taken hit, beside its blooded wash.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::ThudMedium);
            }
        }
    }
    lastPlayerHp_ = hpNow;
    lastBlowsBlocked_ = blockedNow;
    punchLandedPulse_.advance();
    punchTakenPulse_.advance();
    blockPulse_.advance();
    alertPulse_.advance();
    // AUDIO WIRING: the purse and the bed, both by re-read rather than by
    // instrumenting every call site that could move either. Any purse change
    // -- a haggle settled, a pocket picked, rent paid, a bounty collected --
    // is one CoinHandle, the same comparison-not-flag shape the hp watch
    // above uses; and the ambient bed is re-asserted off the one cheap
    // signal this build already keeps for "under a roof"
    // (tavern_->playerInside()), which the engine's own header blesses:
    // re-asserting the current bed is a no-op, so every step is legal.
    if (audio_ != nullptr) {
        const std::int32_t coinNow = tavern_->playerCoin();
        if (coinNow != lastCoinForAudio_) {
            audio_->playOneShot(audio::SoundId::CoinHandle);
        }
        lastCoinForAudio_ = coinNow;
        audio_->startBed(tavern_->playerInside() ? audio::BedId::Interior
                                                 : audio::BedId::Harbour);
    }
    // NOW the string can go. messageSteps_ reaching zero is what stopped
    // WANTING the alert on screen -- see the note above and syncPanelAnim's
    // own formula -- and alertAnim_ finishing its fade is what stopped
    // NEEDING the string to still be there to fade. Both, not either: a
    // message replaced by a fresh say() mid-fade re-arms messageSteps_ and
    // this never fires, which is correct -- the new line is what should be
    // showing, not a clear racing it.
    if (messageSteps_ == 0 && alertAnim_.settled() && !alertAnim_.target()) {
        message_.clear();
    }
    // THE SAME CLEAR, GENERALIZED. Once a row's own toggle has fully eased to
    // closed there is nothing left to fade, and clearing its cache is what
    // stops an old rung or an old room line from permanently claiming a
    // bottom-band slot it finished fading out of minutes ago -- see
    // BottomBand::take() in hud.cpp, which hands a row a slot by "is this
    // label non-empty", not by its alpha.
    const auto clearIfClosed = [](EasedToggle& anim, std::string& cache) {
        if (anim.settled() && !anim.target()) {
            cache.clear();
        }
    };
    clearIfClosed(interactAnim_, interactCache_);
    clearIfClosed(lockAnim_, lockCache_);
    clearIfClosed(caseAnim_, caseCache_);
    clearIfClosed(roomAnim_, roomCache_);
    clearIfClosed(rivalAnim_, rivalCache_);
    clearIfClosed(guildAnim_, guildCache_);
    clearIfClosed(objectiveAnim_, objectiveCache_);
    clearIfClosed(stealthAnim_, stealthCache_);
    clearIfClosed(standingAnim_, standingCache_);
    clearIfClosed(heatAnim_, heatCache_);
    clearIfClosed(stashAnim_, stashCache_);
    clearIfClosed(spellAnim_, spellCache_);
    clearIfClosed(blockAnim_, blockCache_);

    // One engine tick a simulated second. clockScale > 1 makes the world's
    // clock run faster than the body's, which is how a capture reaches a named
    // hour without simulating the whole afternoon.
    if (++stepsThisSecond_ >= sim::kStepsPerSecond) {
        stepsThisSecond_ = 0;
        for (int i = 0; i < config_.clockScale; ++i) {
            engine_->tick();
            ++elapsedSeconds_;
        }
        timeOfDay_ = tavern_->timeOfDay();
        settings_.timeOfDay = timeOfDay_;
    }
    syncWardToCalendar();
}

void Session::stepMany(const sim::MoveInput& input, int steps) {
    for (int i = 0; i < steps; ++i) {
        step(input);
    }
}

int Session::flyOutLeap() {
    int steps = 0;
    // Bounded by construction -- kLeapStepsPerTile * the longest reach any
    // teaching buys, or the haul -- but bounded HERE as well, because a loop
    // whose exit depends on simulation state is a loop that hangs a build the
    // day that state is wrong.
    //
    // #77 ADDS THE HAUL, and leaving it out cost four scripted lines their
    // beats before the gate caught it. A mantle is no longer instantaneous: the
    // band changes at once but the legs are locked for kHaulSteps while the eye
    // rises, because a 2.7 m wall is a climb and not a hop. A capture script
    // that drained only the LEAP walked away from the ledge on the very next
    // call, found the body would not move, counted four stuck steps and gave up
    // -- so `--skyrun` and the burglary both stopped on the roof.
    constexpr int kCeiling = 4 * sim::kLeapStepsPerTile * sim::kLeapReachTiles + sim::kHaulSteps;
    while ((body_->airborne() || body_->hauling()) && steps < kCeiling) {
        step(sim::MoveInput{});
        ++steps;
    }
    return steps;
}

void Session::say(std::string line) {
    // Clipped to what the bottom edge can hold at the narrowest resolution this
    // game runs at. S4 started routing a questline's journal prose through here
    // -- whole sentences out of the raws -- and the first capture of it ran off
    // the right edge mid-word, which looks like a bug because it is one.
    //
    // S10 RAISED IT FROM 56 TO 92. Fifty-six was the column count of a 320x180
    // frame, and it was the belt to the HUD's own braces -- drawHud clips the
    // alert with clipToWidth against the REAL frame width, which is the fix the
    // S6 review's cut-mid-glyph finding produced. So the guess here was
    // truncating a clue at 640x360, where the row holds far more, for no reason
    // but its own age. The HUD still clips; this only stops a runaway string
    // being carried around.
    constexpr std::size_t kAlertColumns = 92;
    if (line.size() > kAlertColumns) {
        line.resize(kAlertColumns);
        line += "..";
    }
    message_ = std::move(line);
    // Six seconds on screen. Long enough to read at a glance, short enough that
    // the bottom of the frame is usually empty.
    messageSteps_ = 6 * sim::kStepsPerSecond;
    // TASK #83. EAGER, so the very first frame drawn after whichever verb
    // called this -- most of them do not call step() first, and the caller
    // here could be a test that never does -- already shows the prompt easing
    // in rather than waiting for the next real step() to notice messageSteps_
    // went from 0 to nonzero.
    syncPanelAnim();
}

bool Session::talking() const noexcept {
    return tavern_->dialogue().isOpen();
}

bool Session::haggling() const noexcept {
    return tavern_->dialogue().isHaggling();
}

bool Session::talkToWard() {
    // #79. THE OTHER SIX HUNDRED AND FORTY-SEVEN.
    //
    // The Gull's fourteen are asked for first, above, because inside K03 the
    // taproom owns the room and its people carry a roster, a mood and a stock;
    // out here nobody does. Everything below is the SAME director, the same
    // authored tables and the same fallback chain -- only the Speaker is built
    // from a body in the street instead of from a body on a stool.
    const sim::WardActor* who = people_->nearestTo(body_->tileX(), body_->tileY(),
                                                   body_->band(), kWardTalkReachTiles);
    if (who == nullptr) {
        return false;
    }
    sim::DialogueDirector& talk = tavern_->dialogue();
    talk.setPlayerCoin(tavern_->playerCoin());
    const sim::Speaker speaker = sim::wardSpeakerFor(
        *who, people_->identity(who->id), talk.notables(), talk.factions(), talk.barks());
    if (!talk.open(speaker, tavern_->timeOfDay())) {
        return false;
    }
    wardTalkId_ = who->id;
    // They look at you while you talk to them, exactly as the taproom's do.
    people_->faceToward(wardTalkId_, body_->tileX(), body_->tileY());
    return true;
}

void Session::interact() {
    dismissOverlays();
    if (picking()) {
        // Not this key's mode to answer for -- main.cpp's route_menu_key
        // intercepts Space/Attack/Menu while the wire is in, and Interact
        // was never one of the three it reinterprets. Refusing quietly here
        // is safer now than it was before #85: this key does a great deal
        // more than it used to, and none of it belongs mid-pick.
        return;
    }
    if (talking()) {
        chooseTopic(static_cast<std::size_t>(std::max(0, topicCursor_)));
        return;
    }
    syncTavernToBody();
    const bool sneaking = stance() == sim::Stance::Crouched;

    // #85. THE WHOLE RESOLUTION RULE. See this method's own header in
    // session.hpp for the numbered order in prose; interactPrompt() below
    // walks the identical order on read-only queries.

    // 1. NOT SNEAKING + AT YOUR OWN BED = REST. Checked first: it is the one
    // exact-tile trigger nothing else here could also mean.
    //
    // TIME-AND-TENURE BUILD: the press now opens the hour-select page in
    // sleep mode instead of committing a fixed night on the spot -- the
    // owner's Rest/Wait ruling gives the bed a chosen waking hour, and a
    // single press that irrevocably burned eight hours was always one
    // mispress from a lost evening. sleepReadiness() is sleep()'s own checks
    // with the hands kept still; the R verb (restHere) keeps the old
    // one-press night for the suite and the muscle memory.
    if (!sneaking) {
        const sim::ServiceResult bed = tavern_->sleepReadiness();
        if (bed == sim::ServiceResult::Served) {
            openWait(true);
            return;
        }
        // NobodyThere (no room ever rented) or TooFar (not at the bed) --
        // the readiness has no third answer, so anything else falls through.
    }

    // 2. PERSON IN REACH: TALK upright, PICKPOCKET sneaking.
    wardTalkId_ = -1;
    if (!sneaking) {
        // TIME-AND-TENURE BUILD: the director is told what ground the feet
        // are on BEFORE the conversation opens, so a priest's topic list is
        // built knowing whether there is a roll to read here. See
        // syncGroundPlot().
        syncGroundPlot();
        if (tavern_->talkTo() || talkToWard()) {
            topicCursor_ = 0;
            haggleOffer_ = 0;
            const sim::DialogueDirector& talk = tavern_->dialogue();
            say(talk.speaker().name + ": " + talk.greeting());
            return;
        }
    } else {
        const sim::Tavern::StealResult lifted = tavern_->liftFrom();
        if (lifted.result != sim::ServiceResult::TooFar) {
            say(lifted.line);
            return;
        }
    }

    // 3. ITEM/FIXTURE: the box (or its lock), the bale, the rat, the wire
    // that buys more picks -- the same chain steal() always tried. Stance
    // changes what the NOTICE rule does with this (stealth.hpp), never which
    // function answers, so "facing a lock picks it, sneaking or not" is
    // already true with no branch here.
    if (stealNearestThing()) {
        return;
    }

    // 4. NOTHING RESOLVED: the investigation look, which never refuses.
    // examine() re-checks talking()/picking()/casebookOpen_ on its own, all
    // of which dismissOverlays() above already settled, so this is safe to
    // call unconditionally.
    examine();
}

std::string Session::interactPrompt() const {
    // #85. THE LABEL Interact IS ABOUT TO RESOLVE TO -- see this method's own
    // header in session.hpp for the verification gap (the bale, the rat and
    // buyPicks() are not previewed) and for why the order below has to track
    // interact()'s own order exactly.
    if (talking() || picking() || pauseOpen() || menuOpen() || waitOpen()) {
        // The topic list / the tiled Menu's own rows already show what
        // Interact (or ENTER) does on this row -- menuOpen() covers the
        // tiled Menu, Keys AND Options, not only Options, which an earlier
        // pass of this check missed (caught by test_interact_context.cpp:
        // toggleMenu() opens the casebook first, and the label kept
        // computing a real prompt behind it). A second, floating label would
        // say the same thing twice in two different places on the same
        // frame. waitOpen() joined the list with the Wait page, whose rows
        // print their own numbers the same way.
        return {};
    }
    const bool sneaking = stance() == sim::Stance::Crouched;

    // 1. REST. Tile-exact rather than sleep()'s own Q8 Chebyshev distance --
    // close enough for a label, and body_ has no reason to expose Q8 here
    // when the tile the body is standing on already answers it.
    if (!sneaking && tavern_->rentedRoom() >= 0 && body_->band() == sim::gull::kUpperBand) {
        const sim::gull::GuestRoom& room = sim::gull::kRooms[tavern_->rentedRoom()];
        if (body_->tileX() == room.standX && body_->tileY() == room.standY) {
            return "REST";
        }
    }

    // 2. PERSON. The identical reach each verb would actually use --
    // sim::kReachQ8 is talkTo()'s own 2*kSubOne, sim::kLiftReachQ8 is
    // liftFrom()'s. Nearest is a read-only query on both Tavern and
    // WardPopulation; neither talks to anybody by being asked.
    const std::int32_t reach = sneaking ? sim::kLiftReachQ8 : sim::kReachQ8;
    const bool personHere =
        tavern_->nearestTo(body_->x(), body_->y(), reach) != nullptr ||
        (!sneaking && people_->nearestTo(body_->tileX(), body_->tileY(), body_->band(),
                                        kWardTalkReachTiles) != nullptr);
    if (personHere) {
        // Pickpocketing a WARD actor is not implemented (lift() only ever
        // reached the Tavern's own roster) -- matched here rather than
        // previewing a verb the button cannot actually perform.
        return sneaking ? "PICKPOCKET" : "TALK";
    }

    // 3. THE BOX -- the one item this can preview exactly, because its
    // geometry (gull::roomAtStand) and its state (rentedRoom/crackedBoxes/
    // openedLocks) are all public, read-only, and the same ones
    // crackStrongbox() itself reads.
    if (body_->band() == sim::gull::kUpperBand) {
        const std::int32_t room = sim::gull::roomAtStand(body_->tileX(), body_->tileY());
        if (room >= 0) {
            const std::int32_t bit = 1 << room;
            if (room == tavern_->rentedRoom()) {
                // Resolves to a refusal ("THAT ONE IS YOURS"), but the box is
                // still what the press is about, so the button still names
                // an action rather than falling back to LOOK.
                return "TAKE";
            }
            if ((tavern_->crackedBoxes() & bit) == 0) {
                if ((tavern_->openedLocks() & bit) == 0) {
                    return "PICK LOCK";
                }
                return sneaking ? "TAKE QUIETLY" : "TAKE";
            }
        }
    }

    return "LOOK";
}

void Session::moveTopicCursor(int delta) {
    // AUDIO WIRING: one quiet tick per cursor move, on every list this router
    // serves -- the restrained half of the plan's "focus move -> UiTick".
    if (audio_ != nullptr && delta != 0 && (talking() || casebookOpen_ || keysOpen_)) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    if (keysOpen_) {
        wrapCursorAndPage(caseCursor_, casePage_, delta, static_cast<int>(keyRows().size()));
        return;
    }
    // MORROWIND ROUND: ROUTED BY WHICH TILE HAS FOCUS, not by which of four
    // now-simultaneous bools happens to be true -- see menuFocus()'s own
    // header. Each tile keeps its OWN cursor/page (and, for Journal and
    // Letters, its own picked entry), so reading one tile never disturbs
    // where the cursor was left on another.
    if (casebookOpen_) {
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                // THE SAME KEYS, THE SAME PAGING -- see toggleCharacter.
                wrapCursorAndPage(characterCursor_, characterPage_, delta,
                                  static_cast<int>(characterRows().size()));
                return;
            case kMenuFocusMap:
                // THE SAME KEYS, THE SAME PAGING -- see toggleMap.
                wrapCursorAndPage(mapCursor_, mapPage_, delta,
                                  static_cast<int>(mapRows().size()));
                return;
            case kMenuFocusLetters:
                if (lettersEntry_ >= 0) {
                    // AN OPEN LETTER HAS NO CURSOR TO MOVE -- lettersBodyPage_
                    // is doing a different job here (which page of the BODY is
                    // showing; see nextTopicPage's own kMenuFocusLetters
                    // branch).
                    return;
                }
                // TASK #82. THE SAME KEYS, THE SAME PAGING -- see toggleLetters.
                wrapCursorAndPage(lettersCursor_, lettersPage_, delta,
                                  static_cast<int>(unlockedLetters().size()));
                return;
            case kMenuFocusJournal:
            default:
                // THE SAME KEYS, THE SAME PAGING. The Journal tile is a
                // conversation with your own notes -- see
                // Session::toggleCasebook on why it borrows the dialogue
                // surface's own vocabulary rather than being a sheet of its
                // own. The count is leads PLUS the work rows under them
                // (journalWorkRows), so the cursor can reach a contract or a
                // log line to read it -- picking one stays a no-op.
                wrapCursorAndPage(caseCursor_, casePage_, delta,
                                  static_cast<int>(casebook_.known().size() +
                                                   journalWorkRows().size()));
                return;
        }
    }
    if (!talking()) {
        return;
    }
    // Wraps, so holding one direction walks the whole list. The page FOLLOWS
    // the cursor: walking off the bottom of a page turns it, so the arrow
    // keys reach every topic and the numbers on screen are always the numbers
    // that pick the ones you can see.
    wrapCursorAndPage(topicCursor_, topicPage_, delta,
                       static_cast<int>(tavern_->dialogue().topics().size()));
}

void Session::nextTopicPage() {
    if (optionsOpen_) {
        const int pages = topicPageCount(optionRows().size());
        optionPage_ = (optionPage_ + 1) % pages;
        optionCursor_ = std::min(static_cast<int>(optionRows().size()) - 1,
                                 optionPage_ * kTopicPageSize);
        return;
    }
    if (keysOpen_) {
        const std::size_t rows = keyRows().size();
        const int pages = topicPageCount(rows);
        casePage_ = (casePage_ + 1) % pages;
        caseCursor_ = std::min(static_cast<int>(rows) - 1, casePage_ * kTopicPageSize);
        return;
    }
    // MORROWIND ROUND: ROUTED BY FOCUS -- see moveTopicCursor()'s own note.
    if (casebookOpen_) {
        // AUDIO WIRING: turning a page WITHIN a tile is the same paper the
        // tile cycle is -- one BookFlip, whichever tile pages.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::BookFlip);
        }
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                advancePage(characterPage_, characterCursor_, characterRows().size());
                return;
            case kMenuFocusMap:
                advancePage(mapPage_, mapCursor_, mapRows().size());
                return;
            case kMenuFocusLetters:
                if (lettersEntry_ >= 0) {
                    // PAGING THROUGH THE OPEN LETTER'S OWN BODY, not the
                    // title list -- lettersBodyPage_ is a field of its own
                    // (menu_view.cpp works out the true page count off the
                    // actual wrapped rows at the panel's real width, and
                    // CLAMPS it into range every frame, so incrementing past
                    // the end here is never observable).
                    ++lettersBodyPage_;
                    return;
                }
                advancePage(lettersPage_, lettersCursor_, unlockedLetters().size());
                return;
            case kMenuFocusJournal:
            default:
                advancePage(casePage_, caseCursor_,
                            casebook_.known().size() + journalWorkRows().size());
                return;
        }
    }
    if (!talking()) {
        return;
    }
    // The cursor comes with the page, onto its first topic, so E never picks
    // something that is not on screen.
    advancePage(topicPage_, topicCursor_, tavern_->dialogue().topics().size());
}

void Session::chooseVisibleTopic(int slot) {
    if (slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    if (pauseOpen_) {
        // A MENU ROW IS AN ACTION, NOT A VALUE. Options' own numbered rows only
        // move the cursor -- ENTER is what rebinds or nudges a slider, and a
        // stray tenth-of-a-second double press should not fire a rebind. This
        // page has no sliders, only three rows the numbers already printed
        // beside on screen (topicRowsFor prints them for every list this
        // surface draws), so the honest behaviour for "press the number you
        // read" is the one talking() gets below: the press both moves the
        // cursor and acts, immediately.
        if (slot < static_cast<int>(pauseRows().size())) {
            pauseCursor_ = slot;
            choosePause();
        }
        return;
    }
    if (optionsOpen_) {
        pickCursorIfVisible(optionCursor_, optionPage_, slot, optionRows().size());
        return;
    }
    if (keysOpen_) {
        // A key row is a reference, not a choice. The cursor moves and nothing
        // else happens, which is the honest behaviour for a list you read.
        pickCursorIfVisible(caseCursor_, casePage_, slot, keyRows().size());
        return;
    }
    // MORROWIND ROUND: ROUTED BY FOCUS -- see moveTopicCursor()'s own note.
    if (casebookOpen_) {
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                // A ROW ON THIS TILE IS SOMETHING TO READ, not a choice --
                // the same honest no-op the keys page gives a number press.
                pickCursorIfVisible(characterCursor_, characterPage_, slot,
                                    characterRows().size());
                return;
            case kMenuFocusMap:
                // A ROW ON THIS TILE IS SOMETHING TO READ, not a choice -- the
                // identical no-op the character tile and the keys page give a
                // number press.
                pickCursorIfVisible(mapCursor_, mapPage_, slot, mapRows().size());
                return;
            case kMenuFocusLetters: {
                if (lettersEntry_ >= 0) {
                    // THE TITLE LIST IS NOT ON SCREEN WHILE A LETTER IS OPEN
                    // -- see DialogueViewState::letter's own note -- so a
                    // number press here has nothing on this tile to have
                    // picked. ESC (closeConversation) is what steps back to
                    // the list.
                    return;
                }
                // TASK #82. A ROW HERE IS A CHOICE, THE SAME WAY A LEAD IS ON
                // THE JOURNAL TILE -- picking a title opens the letter under
                // it. Still pure UI: nothing in the simulation moves when a
                // letter is opened, exactly as reading a casebook entry does
                // not.
                const int index = lettersPage_ * kTopicPageSize + slot;
                if (index >= static_cast<int>(unlockedLetters().size())) {
                    return;
                }
                lettersCursor_ = index;
                lettersEntry_ = index;
                lettersBodyPage_ = 0;
                return;
            }
            case kMenuFocusJournal:
            default: {
                const int index = casePage_ * kTopicPageSize + slot;
                const int leads = static_cast<int>(casebook_.known().size());
                if (index >= leads + static_cast<int>(journalWorkRows().size())) {
                    return;
                }
                caseCursor_ = index;
                if (index < leads) {
                    // Only a LEAD opens as an entry; a work row under the
                    // trail is something to read, so the number press moves
                    // the cursor onto it and nothing else -- the identical
                    // no-op the character tile gives.
                    caseEntry_ = index;
                }
                return;
            }
        }
    }
    if (!talking()) {
        return;
    }
    const int index = topicPage_ * kTopicPageSize + slot;
    if (index >= static_cast<int>(tavern_->dialogue().topics().size())) {
        return;
    }
    topicCursor_ = index;
    chooseTopic(static_cast<std::size_t>(index));
}

void Session::chooseTopic(std::size_t index) {
    // MORROWIND ROUND: ROUTED BY FOCUS -- see moveTopicCursor()'s own note.
    if (casebookOpen_ && menuFocus_ == kMenuFocusJournal) {
        // Reading an entry of your own notes. Nothing in the simulation moves;
        // this is the one place in the game where picking a row is pure UI, and
        // it is pure UI because the trail's state changed when you LOOKED, not
        // when you read your own handwriting back.
        if (index < casebook_.known().size()) {
            caseCursor_ = static_cast<int>(index);
            caseEntry_ = static_cast<int>(index);
            // AUDIO WIRING: opening an entry of your own notes is a page, not
            // a menu -- BookFlip, not UiConfirm.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::BookFlip);
            }
        } else if (index < casebook_.known().size() + journalWorkRows().size()) {
            // A work row under the trail -- a live contract, a finished
            // stage's log line -- is something to read, not a choice: the
            // cursor moves onto it, no entry opens and no page speaks. The
            // identical no-op the character tile gives a pick.
            caseCursor_ = static_cast<int>(index);
        }
        return;
    }
    if (casebookOpen_ && menuFocus_ == kMenuFocusLetters) {
        // TASK #82. Opening a letter is exactly as pure-UI as opening a
        // casebook entry -- see that branch's own note. A no-op while a
        // letter is already open, for the identical reason
        // chooseVisibleTopic's own kMenuFocusLetters branch gives.
        if (lettersEntry_ < 0 && index < unlockedLetters().size()) {
            lettersCursor_ = static_cast<int>(index);
            lettersEntry_ = static_cast<int>(index);
            lettersBodyPage_ = 0;
            // AUDIO WIRING: unfolding a letter is paper too.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::BookFlip);
            }
        }
        return;
    }
    if (!talking()) {
        return;
    }
    // AUDIO WIRING: the accept, spoken once per picked topic -- the plan's
    // "accept -> UiConfirm". Before the settlement on purpose: the press is
    // what is being acknowledged, and any coin the settlement moves speaks
    // for itself a step later (see the purse watch in step()).
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    // #79. WHICH ROOM OWNS THE CONSEQUENCE. The director decides what was SAID
    // and what the world now owes; whoever owns the body applies it. Inside the
    // Gull that is the Tavern, which has a bar to take a drink off and a
    // bouncer to send over. On the street it is the ward, which has neither --
    // so the ward has its own, much shorter, settlement.
    sim::Reply reply =
        wardTalkId_ >= 0 ? chooseWardTopic(index) : tavern_->chooseTopic(index);
    // TIME-AND-TENURE BUILD. The two ground topics are intents, exactly like
    // Buy: the director declared them and whoever owns the roll -- this
    // session, which borrowed the ward -- resolves them and fills in the
    // line. Settled HERE, after either settlement path above, because a
    // priest can be a street body (the ward path) or the Gull's own (the
    // tavern path) and the roll answers the same on both.
    if (reply.ok && reply.kind == sim::TopicKind::ReadRoll) {
        reply.line = groundRollLine();
        // Voiced into the panel too -- during a conversation the alert row
        // stands down (see drawFrame's hud.alert gate), so lastLine() is the
        // one surface the answer can actually be read on.
        tavern_->dialogue().speakResolved(reply.line);
    } else if (reply.ok && reply.kind == sim::TopicKind::Petition) {
        reply.line = settleGroundPetition();
        tavern_->dialogue().speakResolved(reply.line);
    }
    if (!reply.ok && reply.line.empty()) {
        return;
    }
    if (reply.haggling) {
        // Open the argument at what they are asking, so the first press of a
        // key is a concession rather than a guess.
        haggleOffer_ = tavern_->dialogue().haggle().asking();
    }
    if (!reply.line.empty()) {
        say(tavern_->dialogue().speaker().name + ": " + reply.line);
    }
    if (reply.forging) {
        // The bench opens where the simulation put it.
        forgeOpen_ = true;
    }
    if (talking()) {
        const int count = static_cast<int>(tavern_->dialogue().topics().size());
        if (count > 0 && topicCursor_ >= count) {
            topicCursor_ = count - 1;
        }
        topicPage_ = std::min(topicPageOf(topicCursor_), topicPageCount(
                                                             static_cast<std::size_t>(count)) -
                                                             1);
    } else {
        topicCursor_ = 0;
        topicPage_ = 0;
    }
    if (!reply.journalLine.empty()) {
        say(reply.journalLine);
    }
    // TASK #83. Covers the one path through this function that can close the
    // conversation (reply.closes, when it lands on an empty reply.line) with
    // no call to say() to have carried the sync eagerly -- see say()'s own
    // note. Every other exit already got one from a say() above; this is a
    // no-op for those.
    syncPanelAnim();
}

void Session::closeConversation() {
    if (pauseOpen_) {
        // ESC out of QUIT's second press first, and out of the menu second --
        // the same shape as awaitingKey_ below, for the same reason: a player
        // who leant on ENTER by accident has to be able to back off it without
        // the whole page vanishing under them.
        if (quitArmed_) {
            quitArmed_ = false;
            return;
        }
        pauseOpen_ = false;
        pauseCursor_ = 0;
        syncPanelAnim();
        return;
    }
    if (optionsOpen_) {
        // ESC out of a rebinding first, and out of the page second. A player
        // who opened "press a key" by accident has to be able to get out of it
        // without binding escape to something.
        if (awaitingKey_) {
            awaitingKey_ = false;
            return;
        }
        optionsOpen_ = false;
        optionCursor_ = 0;
        optionPage_ = 0;
        syncPanelAnim();
        return;
    }
    if (keysOpen_) {
        keysOpen_ = false;
        caseCursor_ = 0;
        casePage_ = 0;
        syncPanelAnim();
        return;
    }
    if (grimoireOpen_) {
        grimoireOpen_ = false;
        grimoireCursor_ = 0;
        grimoirePage_ = 0;
        syncPanelAnim();
        return;
    }
    if (waitOpen_) {
        waitOpen_ = false;
        waitCursor_ = 0;
        waitPage_ = 0;
        syncPanelAnim();
        return;
    }
    if (districtMapOpen_) {
        // ESC closes the ward map the same way it closes every page -- the
        // owner's ask names M as the toggle, and Escape is the universal
        // back-out on top of it.
        districtMapOpen_ = false;
        syncPanelAnim();
        return;
    }
    if (casebookOpen_) {
        // MORROWIND ROUND. If the focused Letters tile has a letter open,
        // ESC steps back to its own title list first -- the SAME "a page
        // reached by drilling in gives you one press back to where you
        // were" rule the options page's awaitingKey_ handling above already
        // has, now scoped to one tile instead of the whole surface.
        if (menuFocus_ == kMenuFocusLetters && lettersEntry_ >= 0) {
            lettersEntry_ = -1;
            lettersBodyPage_ = 0;
            syncPanelAnim();
            return;
        }
        // OTHERWISE ESC CLOSES THE WHOLE TILED MENU, all four tiles at once
        // -- they were never four separate pages to step back out of one at
        // a time, only one surface with four panes on it (see
        // toggleCasebook()'s own header).
        casebookOpen_ = false;
        caseCursor_ = 0;
        casePage_ = 0;
        caseEntry_ = -1;
        characterCursor_ = 0;
        characterPage_ = 0;
        mapCursor_ = 0;
        mapPage_ = 0;
        lettersCursor_ = 0;
        lettersPage_ = 0;
        lettersEntry_ = -1;
        lettersBodyPage_ = 0;
        menuFocus_ = kMenuFocusJournal;
        syncPanelAnim();
        return;
    }
    tavern_->endConversation();
    wardTalkId_ = -1;
    topicCursor_ = 0;
    topicPage_ = 0;
    haggleOffer_ = 0;
    forgeOpen_ = false;
    syncPanelAnim();
}

sim::Reply Session::chooseWardTopic(std::size_t index) {
    sim::DialogueDirector& talk = tavern_->dialogue();
    talk.setPlayerCoin(tavern_->playerCoin());
    sim::Reply reply = talk.choose(index);
    if (!reply.ok) {
        if (reply.closes || !talk.isOpen()) {
            wardTalkId_ = -1;
        }
        return reply;
    }

    // A hand in a purse takes coin off a REAL body, and the coin the ward loses
    // is the coin the player gains. Nothing is minted: the ledger's identity is
    // about creation, and a robbery creates nothing.
    if (reply.kind == sim::TopicKind::PickPocket && !reply.offence && reply.coinDelta > 0) {
        reply.coinDelta = people_->takeCoinFrom(wardTalkId_, reply.coinDelta);
        if (reply.coinDelta <= 0) {
            reply.line = "THEIR PURSE IS EMPTY.";
        }
    }
    if (reply.coinDelta != 0) {
        tavern_->setPlayerCoin(std::max(0, tavern_->playerCoin() + reply.coinDelta));
        talk.setPlayerCoin(tavern_->playerCoin());
    }
    // WHETHER ANYBODY SAW IT is a question about the street, and the street has
    // no walls, no bouncer and no door policy. So it is asked the only way it
    // honestly can be out here: were there other living people standing close
    // enough on the same band. The Watch's heat, the roofs' opinion and the
    // guilds' all move through the one call site every criminal act in this
    // build goes through, whether it happened in a taproom or on the Tarwalk.
    if (reply.criminal) {
        const bool watched = people_->witnessesAround(wardTalkId_, kWardWitnessTiles) > 0;
        talk.noteCrime(reply.crime, watched || reply.offence);
    }
    if (reply.closes || !talk.isOpen()) {
        wardTalkId_ = -1;
    }
    return reply;
}

// ---------------------------------------------------------------------------
// the workbench
// ---------------------------------------------------------------------------

bool Session::forging() const noexcept {
    return tavern_->dialogue().isForging();
}

void Session::moveForgeField(int delta) {
    tavern_->dialogue().moveForgeField(delta);
}

void Session::adjustForge(int delta) {
    tavern_->dialogue().adjustForge(delta);
}

void Session::commitForge() {
    if (!forging()) {
        return;
    }
    const std::string who = tavern_->dialogue().speaker().name;
    const sim::Reply reply = tavern_->commitForge();
    if (!reply.line.empty()) {
        say(who + ": " + reply.line);
    }
    if (!reply.journalLine.empty()) {
        say(reply.journalLine);
    }
    forgeOpen_ = forging();
}

void Session::endForge() {
    if (!forging()) {
        return;
    }
    const sim::Reply reply = tavern_->endForge();
    if (!reply.line.empty()) {
        say(reply.line);
    }
    forgeOpen_ = false;
}

void Session::adjustOffer(int delta) {
    if (!haggling()) {
        return;
    }
    const int ceiling = std::max(1, tavern_->dialogue().haggle().asking());
    haggleOffer_ = std::clamp(haggleOffer_ + delta, 0, ceiling);
}

void Session::makeOffer() {
    if (!haggling()) {
        return;
    }
    const std::string who = tavern_->dialogue().speaker().name;
    const sim::Reply reply = tavern_->offerPrice(haggleOffer_);
    if (!reply.line.empty()) {
        say(who + ": " + reply.line);
    }
    haggleOffer_ = haggling() ? std::min(haggleOffer_, tavern_->dialogue().haggle().asking()) : 0;
}

void Session::takeAskingPrice() {
    if (!haggling()) {
        return;
    }
    const std::string who = tavern_->dialogue().speaker().name;
    const sim::Reply reply = tavern_->takeAskingPrice();
    if (!reply.line.empty()) {
        say(who + ": " + reply.line);
    }
    haggleOffer_ = 0;
}

// ---------------------------------------------------------------------------
// Morrowind round: the tiled Menu's four panels, each built independently of
// which (if any) currently has focus -- drawMenuTiles() needs all four every
// frame, and dialogueView() returns whichever one menuFocus_ names so a
// caller that only ever asked about "the open page" keeps seeing exactly the
// content it always did. CONTENT UNCHANGED FROM #85's SIX-PAGE MENU; only
// the per-tile cursor/page fields (characterCursor_/mapCursor_/
// lettersCursor_ instead of a shared caseCursor_) are new -- see session.hpp's
// own note on why each tile needs its own now that all four can be mid-read
// at once.
// ---------------------------------------------------------------------------

DialogueViewState Session::characterPanelView() const {
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    view.speaker = "CHARACTER";
    const std::string title = legendLine();
    view.epithet = title.empty() ? std::string(sim::kReputationUnremarkable) : title;
    view.line =
        "WHAT THE STREETS HAVE MADE OF YOU, ON FIVE TRACKS AT ONCE, AND THE HANDS THAT "
        "DID IT.";
    for (const std::string& row : characterRows()) {
        view.topics.push_back(row);
    }
    view.cursor = characterCursor_;
    view.page = characterPage_;
    return view;
}

DialogueViewState Session::mapPanelView() const {
    // #82. THE DISTRICT MAP. Same reason every other tile is drawn through
    // this shared vocabulary: one content shape, proved once.
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    view.speaker = "THE CHART";
    // WHERE YOU ARE, right where a map's own "you are here" would go.
    view.epithet = placeLabel();
    view.line = "KNOWN GROUND, OPEN LEADS AND WHO WILL TALK, RECKONED FROM WHERE YOU STAND.";
    for (const std::string& row : mapRows()) {
        view.topics.push_back(row);
    }
    view.cursor = mapCursor_;
    view.page = mapPage_;
    return view;
}

DialogueViewState Session::lettersPanelView() const {
    // TASK #82. A LETTER, DRAWN AS A DOCUMENT INSTEAD OF A LIST. Same content
    // shape every other tile uses; menu_view.cpp switches to the parchment
    // palette and pages through the body as wrapped prose instead of a row
    // list once DialogueViewState::letter is set -- see that field's own
    // header.
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    const std::vector<std::int32_t> unlocked = unlockedLetters();
    if (lettersEntry_ >= 0 && static_cast<std::size_t>(lettersEntry_) < unlocked.size()) {
        const sim::Letter& read = letterRaws_.letters()[static_cast<std::size_t>(
            unlocked[static_cast<std::size_t>(lettersEntry_)])];
        view.speaker = read.from.empty() ? std::string("A LETTER") : read.from;
        view.line = "A LETTER, READ IN FULL BELOW.";
        view.letter = true;
        // ONE ENTRY A PARAGRAPH, RAW -- see DialogueViewState::letterLines'
        // own header on why WRAPPING happens at draw time rather than
        // here: Session has no window size to wrap prose against, and a
        // machine that pre-decided where a sentence breaks at every
        // resolution is the exact "picks badly" case casebook.hpp's own
        // `brief` field warns about for a proper noun -- prose gets to
        // wrap for real.
        if (!read.to.empty()) {
            view.letterLines.push_back("TO " + read.to);
        }
        if (!read.dateline.empty()) {
            view.letterLines.push_back(read.dateline);
        }
        if (!read.salutation.empty()) {
            view.letterLines.push_back(read.salutation);
        }
        for (const std::string& paragraph : read.body) {
            view.letterLines.push_back(paragraph);
        }
        if (!read.closing.empty()) {
            view.letterLines.push_back(read.closing);
        }
        if (!read.signature.empty()) {
            view.letterLines.push_back("- " + read.signature);
        }
        view.page = lettersBodyPage_;
    } else {
        view.speaker = "THE LETTERS";
        view.epithet = "READ, NOT RECEIVED";
        view.line = unlocked.empty()
                        ? "NOBODY HAS HANDED YOU ANYTHING WORTH KEEPING YET."
                        : "A DOCUMENT SOMEBODY ELSE WROTE. PICK ONE TO READ IT WHOLE.";
        view.page = lettersPage_;
    }
    for (const std::int32_t index : unlocked) {
        const sim::Letter& letter = letterRaws_.letters()[static_cast<std::size_t>(index)];
        // MAELL'S THREE SHARE ONE NAME, so the title list numbers them
        // against every OTHER letter tied to the same lead rather than
        // showing "FATHER MAELL" three times over with no way to tell
        // which press opens which.
        std::int32_t total = 0;
        std::int32_t position = 0;
        for (std::size_t i = 0; i < letterRaws_.letters().size(); ++i) {
            if (letterRaws_.letters()[i].lead == letter.lead) {
                ++total;
                if (static_cast<std::int32_t>(i) == index) {
                    position = total;
                }
            }
        }
        std::string label = letter.from.empty() ? std::string("A LETTER") : letter.from;
        if (total > 1) {
            label += " " + std::to_string(position) + "/" + std::to_string(total);
        }
        view.topics.push_back(label);
    }
    view.cursor = lettersCursor_;
    return view;
}

std::vector<std::string> Session::journalWorkRows() const {
    // WHAT THE PLAYER OWES AND WHAT THEY HAVE DONE, under the leads: every
    // LIVE contract off the same board the HUD's contractLine reads -- all of
    // them, where that corner row only ever shows the soonest -- and then
    // every finished stage's own authored log line, in the order they were
    // earned. Both are pure derived reads (the board and the QuestJournal are
    // already hashed sim state; this hashes nothing new), and both rows are
    // SOMETHING TO READ, not a choice -- picking one is the same honest no-op
    // a character-sheet row gives a number press.
    std::vector<std::string> rows;
    const sim::DialogueDirector& talk = tavern_->dialogue();
    const sim::ContractBoard& board = talk.contracts();
    for (const sim::Contract& row : board.contracts()) {
        if (!row.live()) {
            continue;
        }
        // The same have/want fraction contractLine() prints, so the journal
        // and the HUD corner can never disagree about the one job they both
        // show. The deadline rides the row in nights left rather than a raw
        // day index, because "DUE TONIGHT" is the fact a player acts on.
        const std::int32_t have = talk.crimes().stash().count(row.good);
        std::string line = "- " + row.label + " " + std::to_string(have) + "/" +
                           std::to_string(row.units);
        const std::int32_t nights = row.dueOnDay - board.day();
        if (nights <= 0) {
            line += "  DUE TONIGHT";
        } else if (nights == 1) {
            line += "  1 NIGHT";
        } else {
            line += "  " + std::to_string(nights) + " NIGHTS";
        }
        rows.push_back(std::move(line));
    }
    // RADIANT BUILD: the ward's own errands, under the broker's jobs. Every
    // TAKEN radiant objective, with the same have/want fraction a contract
    // row gets (a deliver has no goods to count, so its row names the two
    // ends of the walk instead), and NO deadline -- an errand has none, and
    // printing a fake one would be worse than printing the truth. The same
    // read-only contract as every row here: something to read, not a choice.
    for (const sim::RadiantObjective& row : talk.radiant().objectives()) {
        if (!row.live()) {
            continue;
        }
        std::string line = "- ";
        if (row.isFetch()) {
            const std::int32_t have = talk.crimes().stash().count(row.good);
            line += row.verb + " " + std::to_string(row.units) + " " +
                    std::string(sim::contrabandLabelFor(row.good, row.units)) + " " +
                    std::to_string(have) + "/" + std::to_string(row.units) + "  FOR " +
                    upperAscii(row.giverName);
        } else {
            line += row.verb + " TO " + upperAscii(row.targetName) + "  FROM " +
                    upperAscii(row.giverName);
        }
        rows.push_back(std::move(line));
    }
    // "* " is the journal's own "followed" mark -- a finished stage is a lead
    // that paid off, and it reads with the same glyph.
    for (const std::string& entry : talk.journal().log()) {
        rows.push_back("* " + entry);
    }
    return rows;
}

DialogueViewState Session::journalPanelView() const {
    // THE JOURNAL. Not a new panel and not a sheet: one more content shape on
    // the widget family every other tile already uses.
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    view.speaker = "THE CASEBOOK";
    view.epithet = std::string(caseRaws_.title());
    // THE ATTITUDE FIELD IS SHORT BY CONSTRUCTION -- in a conversation it
    // holds "WARM" or "HOSTILE" -- and the top-right of that band is where
    // the HUD draws the clock over it. The first S10 capture put a
    // twenty-nine character dread band there and the clock landed in the
    // middle of it. The ward's nerve moved down onto the line, where it is
    // the first thing you read in your own notes, which is also better.
    view.attitude = casebook_.closed() ? "CLOSED" : "OPEN";
    const std::vector<std::int32_t> heard = casebook_.known();
    const sim::Legend book = legend();
    if (caseEntry_ >= 0 && static_cast<std::size_t>(caseEntry_) < heard.size()) {
        const std::int32_t leadIndex = heard[static_cast<std::size_t>(caseEntry_)];
        const sim::Lead& lead = caseRaws_.leads()[static_cast<std::size_t>(leadIndex)];
        const sim::LeadState what = casebook_.state(leadIndex);
        view.line = what == sim::LeadState::Open ? lead.place + ". " + lead.what + "."
                                                  : lead.found + " " + lead.detail;
        // TASK #82. THE DATELINE, AND THE CROSS-REFERENCE -- what a
        // detective's log keeps that a bare topic list does not: when
        // this went in the book, what told you to come here, and (once
        // followed) what it put in the book next. See
        // DialogueViewState::caseRef's own header on why this is a
        // separate row rather than folded into `line`.
        std::string ref = formatCaseDay(casebook_.heardAt(leadIndex));
        const std::vector<std::int32_t> from = caseRaws_.openedBy(leadIndex);
        if (from.empty()) {
            // THE ONE LEAD WITH NO OPENER. Not blank: a log that omits
            // the start of its own case reads as missing a page, not as
            // having none to show.
            ref += "  THE CASE OPENED HERE";
        } else {
            ref += "  FROM ";
            for (std::size_t i = 0; i < from.size(); ++i) {
                if (i > 0) {
                    ref += ", ";
                }
                const sim::Lead& opener = caseRaws_.leads()[static_cast<std::size_t>(from[i])];
                ref += opener.brief.empty() ? opener.place : opener.brief;
            }
        }
        if (what == sim::LeadState::Followed && !lead.opens.empty()) {
            ref += "  OPENED ";
            for (std::size_t i = 0; i < lead.opens.size(); ++i) {
                if (i > 0) {
                    ref += ", ";
                }
                const std::int32_t opened = caseRaws_.indexOf(lead.opens[i]);
                if (opened >= 0) {
                    const sim::Lead& next = caseRaws_.leads()[static_cast<std::size_t>(opened)];
                    ref += next.brief.empty() ? next.place : next.brief;
                }
            }
        } else if (what == sim::LeadState::Cold) {
            ref += "  DEAD END";
        }
        // A HINT AT THE LETTERS, when this exact lead unlocked one. The
        // player has already earned the right to read it -- see
        // unlockedLetters() -- so the casebook says where to press
        // rather than making them discover the key by accident.
        for (const std::int32_t li : unlockedLetters()) {
            if (letterRaws_.letters()[static_cast<std::size_t>(li)].lead == lead.id) {
                ref += "  L READS HIS LETTERS";
                break;
            }
        }
        view.caseRef = ref;
    } else if (casebook_.readCount() == 0) {
        // THE OPENING PAGE OF A NEW GAME: the hook, and nothing else. It is
        // the first thing a player ever reads in this game and it gets the
        // band to itself.
        view.line = std::string(caseRaws_.hook());
    } else {
        // And afterwards: what the ward's nerve is doing, and what it calls
        // you for the work so far. TWO SHORT SENTENCES, because the band
        // wraps to three lines and the S10 capture that ran to four lost
        // "OF THE FLAME" off the end of its own title.
        view.line = std::string(caseRaws_.dreadLabel(casebook_.dread())) + ". THEY CALL YOU " +
                    std::string(book.title()) + ".";
    }
    for (const std::int32_t index : heard) {
        const sim::Lead& lead = caseRaws_.leads()[static_cast<std::size_t>(index)];
        std::string row;
        switch (casebook_.state(index)) {
            case sim::LeadState::Open:
                row = "? ";
                break;
            case sim::LeadState::Cold:
                row = "X ";
                break;
            case sim::LeadState::Followed:
                row = "* ";
                break;
            default:
                row = "  ";
                break;
        }
        // THE SHORT NAME, and it is authored rather than truncated here.
        // casebook.json carries a `short` for every lead and a case pins
        // that all of them fit.
        view.topics.push_back(row + (lead.brief.empty() ? lead.place : lead.brief));
    }
    // THE WORK, UNDER THE TRAIL: live contracts and the finished stages' own
    // log lines -- see journalWorkRows(). After the leads on purpose, so
    // every existing index into the list (caseEntry_, chooseTopic's own lead
    // guard) still means the lead it always meant.
    for (std::string& row : journalWorkRows()) {
        view.topics.push_back(std::move(row));
    }
    view.cursor = caseCursor_;
    view.page = casePage_;
    return view;
}

DialogueViewState Session::dialogueView() const {
    DialogueViewState view;
    // The panel's own small motion -- the picked row's highlight breathes
    // with it. The identical role body_->stepCount()/60 already plays for the
    // lamp flicker in drawFrame: a pure function of simulated steps, so a
    // scripted capture still draws the same frame every time it is asked to,
    // and it costs nothing on every page this widget is reused for.
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    if (waitOpen_) {
        // TIME-AND-TENURE BUILD. The same one list widget every page is. The
        // top band carries the ruling's own distinction -- and, in wait mode,
        // the refusal when this spot is not safe, so the page and the pick
        // name the same door before a key is ever pressed.
        view.open = true;
        view.speaker = waitSleep_ ? "SLEEP" : "WAIT";
        view.epithet = "ENTER PASSES THE HOURS  ESC BACKS OUT";
        if (waitSleep_) {
            view.line = "THE BED IS PAID FOR. SLEEP MENDS. PICK THE HOUR TO WAKE.";
        } else {
            const std::string refusal = waitRefusal();
            view.line = refusal.empty()
                            ? "TIME PASSES AND NOTHING MENDS. THE DISTRICT KEEPS ITS "
                              "HOURS, AND THE WATCH KEEPS FORGETTING."
                            : refusal;
        }
        for (const std::string& row : waitRows()) {
            view.topics.push_back(row);
        }
        view.cursor = waitCursor_;
        view.page = waitPage_;
        return view;
    }
    if (pauseOpen_) {
        view.open = true;
        view.speaker = "MENU";
        view.epithet = quitArmed_ ? "ENTER QUITS  ESC CANCELS" : "ENTER SELECTS  ESC RESUMES";
        // NOT "PAUSED", DELIBERATELY. This page does not stop PhasedEngine --
        // nothing in this build does, not the casebook, not the keys page, not
        // options, and a menu that promised a freeze the game does not deliver
        // would be exactly the class of bug the copy bar exists to catch. Said
        // once, here, where a player opening this for the first time reads it.
        view.line =
            "THE DOCKS KEEP RUNNING WHILE YOU DECIDE. NOTHING HERE IS LOST -- SETTINGS SAVE "
            "THEMSELVES.";
        for (const std::string& row : pauseRows()) {
            view.topics.push_back(row);
        }
        view.cursor = pauseCursor_;
        view.page = 0;
        return view;
    }
    if (keysOpen_) {
        view.open = true;
        view.speaker = "CONTROLS";
        // AND THE BUILD, HERE, WHERE A PLAYER GOES LOOKING FOR IT. The version
        // used to be burnt into the top-left corner of every captured frame at
        // full HUD scale, which is prime screen real estate spent on something
        // nobody needs more than once. F1 is the page you open when you want to
        // know how this works; what build it is belongs on it.
        view.epithet = "GRANADAD: THE DARKSTREETS  " + std::string(sim::build_info().version);
        // THREE LINES IS WHAT THE TOP BAND WRAPS TO, so this is written to fit
        // in two. The first version ran to four and lost its own last sentence.
        view.line =
            "THE DOCKS OF GRANADAD. THE DISTRICT KEEPS ITS OWN HOURS WHETHER YOU WATCH IT "
            "OR NOT. F1 PUTS THIS DOWN.";
        for (const std::string& row : keyRows()) {
            view.topics.push_back(row);
        }
        view.cursor = caseCursor_;
        view.page = casePage_;
        return view;
    }
    if (grimoireOpen_) {
        // SPELLS BUILD. The same one list widget every page is -- see
        // toggleGrimoire()'s own header. The rows carry the difficulty out of
        // the cost model beside every name: what the linkcraft check will be
        // rolled against, which is information, never a discount.
        view.open = true;
        view.speaker = "GRIMOIRE";
        view.epithet = "LEFT RIGHT BIND A SLOT  ENTER READIES";
        const std::vector<std::string> rows = grimoireRows();
        if (rows.empty()) {
            // THE COMMON STATE, in the cast refusal's own words: the page and
            // the C key must name the same door or one of them is lying.
            view.line = "NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES.";
        } else {
            view.line =
                "EVERY CRAFTING THE HAND KNOWS. D IS WHAT THE LINK ASKS OF YOUR "
                "LINKCRAFT; A SLOT PUTS IT ON THE NUMBER ROW.";
        }
        for (const std::string& row : rows) {
            view.topics.push_back(row);
        }
        view.cursor = grimoireCursor_;
        view.page = grimoirePage_;
        return view;
    }
    if (casebookOpen_) {
        // MORROWIND ROUND: WHICHEVER TILE CURRENTLY HAS FOCUS. dialogueView()
        // used to check four mutually-exclusive xOpen_ bools in turn; now all
        // four tiles are open together (casebookOpen_ is the one flag), so
        // the dispatch is by menuFocus_ instead -- a caller that only ever
        // asked "what is the open page showing" (every test written before
        // this round) keeps seeing exactly the content it always did,
        // because the Menu still opens focused on the Journal tile by
        // default, same as #85's Menu always opened on the casebook first.
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                return characterPanelView();
            case kMenuFocusMap:
                return mapPanelView();
            case kMenuFocusLetters:
                return lettersPanelView();
            case kMenuFocusJournal:
            default:
                return journalPanelView();
        }
    }
    if (optionsOpen_) {
        view.open = true;
        view.speaker = "OPTIONS";
        view.epithet = awaitingKey_ ? "PRESS A KEY  (ESC CANCELS)" : "LEFT RIGHT CHANGE  ENTER REBIND";
        view.line =
            "MOUSE LOOK IS RAW -- NO SMOOTHING AND NO ACCELERATION. A KEY YOU BIND IS TAKEN "
            "OFF WHATEVER HAD IT. F2 PUTS THIS DOWN.";
        for (const std::string& row : optionRows()) {
            view.topics.push_back(row);
        }
        view.cursor = optionCursor_;
        view.page = optionPage_;
        return view;
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    if (!talk.isOpen()) {
        return view;
    }
    view.open = true;
    view.speaker = talk.speaker().name;
    view.epithet = talk.speaker().epithet;
    view.attitude = std::string(sim::attitudeName(talk.attitude()));
    view.line = talk.lastLine();
    // THE TYPEWRITER. speechAgeSteps_ was reset to 0 the step this exact line
    // first became lastLine() -- see step()'s own note -- so this ramps 0..all
    // of it over kSpeechRevealSteps and then reports -1 (all of it, no budget
    // to track) for as long as the line stands, which is most of a
    // conversation's life. Nothing here changes what talk.lastLine() IS; only
    // how much of it the top band has been told to draw this frame.
    view.speechRevealChars =
        speechAgeSteps_ >= kSpeechRevealSteps
            ? -1
            : static_cast<int>((static_cast<std::int64_t>(view.line.size()) * speechAgeSteps_) /
                                kSpeechRevealSteps);
    view.cursor = topicCursor_;
    view.page = topicPage_;
    for (const sim::Topic& topic : talk.topics()) {
        view.topics.push_back(topic.label);
    }
    if (talk.isForging()) {
        const sim::ForgeBench& bench = talk.bench();
        view.forging = true;
        view.forgeCursor = bench.field;
        view.forgeDifficulty = bench.difficulty();
        view.forgeCeiling = sim::forgeCeilingFor(talk.skills().level(sim::kCraftingSkill));
        const sim::ForgeError problem = bench.error();
        if (problem != sim::ForgeError::None) {
            view.forgeProblem = std::string(sim::forgeErrorReason(problem));
        } else if (view.forgeDifficulty > view.forgeCeiling) {
            view.forgeProblem = std::string(sim::forgeErrorReason(sim::ForgeError::BeyondSkill));
        }
        for (int i = 0; i < sim::kForgeFieldCount; ++i) {
            view.forgeFields.push_back(bench.fieldLabel(i) + ": " + bench.fieldValue(i));
        }
    }
    if (talk.isHaggling()) {
        view.haggling = true;
        view.asking = talk.haggle().asking();
        view.offer = haggleOffer_;
        view.patience = talk.haggle().patience();
        view.goods = std::string(sim::goodsName(talk.haggle().terms().goods));
    }
    return view;
}

void Session::punch() {
    dismissOverlays();
    const sim::Tavern::PunchResult result = tavern_->playerPunchNearest();
    if (!result.swung) {
        // A punch is thrown at a person, not at a thing.
        say("NOBODY IN REACH.");
        return;
    }
    if (!sim::resolvesInWorld(result.fight)) {
        // The one transition this game has, and S2 does not have the screen it
        // transitions to. Saying so out loud is better than resolving a knife
        // fight with fist rules and hoping nobody notices.
        say("BLADE OUT - THIS IS NOT A BRAWL.");
        return;
    }
    if (result.blow.downed) {
        say(result.targetName + " GOES DOWN.");
    } else if (result.blow.landed) {
        say("HIT " + result.targetName + " FOR " + std::to_string(result.blow.damage) + ".");
    } else {
        say("MISSED " + result.targetName + ".");
    }
    // AUDIO WIRING: the plan's own three -- PunchMedium/PunchHeavy by blow
    // weight (a blow that put them down is the heavy one), Whoosh for the
    // swing that connects with nothing. One sound per swing, same as one
    // wash per landed hit.
    if (audio_ != nullptr) {
        if (result.blow.downed) {
            audio_->playOneShot(audio::SoundId::PunchHeavy);
        } else if (result.blow.landed) {
            audio_->playOneShot(audio::SoundId::PunchMedium);
        } else {
            audio_->playOneShot(audio::SoundId::Whoosh);
        }
    }
    // INNOVATION SPRINT ITEM #3. A LANDED PUNCH FINALLY HAS SOME WEIGHT --
    // downed is a landed blow that also put them on the floor, so it counts
    // here too. A miss stays silent: this is punctuation for a connecting
    // hit, not for the swing itself.
    if (result.blow.landed) {
        punchLandedPulse_.trigger();
    }
}

void Session::castEquipped() {
    dismissOverlays();
    if (talking() || picking()) {
        // The keyboard is a topic list's (or a lock's) right now. Inert, the
        // same way setCrouched already stands down -- a link opened from
        // inside a conversation would be a fact nobody saw being made.
        return;
    }
    // The room resolves the whole of it -- every refusal included -- and
    // always answers with a line, so the key can never silently do nothing.
    // See sim::Tavern::playerCastEquipped()'s own header.
    const sim::Tavern::CastResult result = tavern_->playerCastEquipped();
    say(result.line);
    // NO NEW PULSE, DELIBERATELY. The restraint note in drawFrame() stands:
    // two real moments got a flash and a cast is not being bolted on as a
    // third -- a landed harmful cast already reads on the target and the
    // alert row, and the punch-taken/blocked pair stays the whole of the
    // wash vocabulary.
}

void Session::setBlocking(bool held) {
    // The EDGE only. What the room is told is derived in step(), every step
    // -- see the comment there on why (a page can open or close mid-hold,
    // and no edge event exists for either moment).
    blockHeld_ = held;
}

void Session::applyPlayerAttributes(const sim::AttributeBlock& attributes) {
    // BOTH HALVES OR NEITHER -- see the declaration. The room takes the
    // sheet (and sizes the fatigue pool off it); the body takes the one
    // reader the room cannot hold for it, AGI's gait multiplier. At the
    // base-40 sheet both halves are exactly the shipped behaviour.
    tavern_->setPlayerAttributes(attributes);
    body_->setSpeedScaleQ8(
        sim::agilitySpeedScaleQ8(attributes.value(sim::AttributeId::Agility)));
}

void Session::restHere() {
    dismissOverlays();
    const sim::ServiceResult slept = tavern_->sleep();
    if (slept == sim::ServiceResult::Served) {
        settleSleep();
        return;
    }
    say(std::string(sim::restRefusal(slept)));
}

void Session::syncClockAfterSkip() {
    // The shared tail of every clock jump -- see the declaration's header.
    timeOfDay_ = tavern_->timeOfDay();
    settings_.timeOfDay = timeOfDay_;
    stepsThisSecond_ = 0;
    syncWardToCalendar();
}

void Session::settleSleep() {
    syncClockAfterSkip();
    say("SLEPT UNTIL MORNING.");
}

void Session::skipToHour(int hour) {
    const int wrapped = ((hour % 24) + 24) % 24;
    tavern_->skipTo(wrapped * 3600);
    syncClockAfterSkip();
}

void Session::syncWardToCalendar() {
    // THE TAVERN'S CALENDAR IS THE WORLD'S CALENDAR, and the ward follows it.
    //
    // S7 REVIEW FINDING #8, CLOSED. Ward::tick counts a second per engine tick
    // and turns a day at 86,400 of them; every skip in this file -- sleeping in
    // a rented bed, a night in a cell, the blackout after a beating, a scripted
    // capture reaching a named hour -- moved the TAVERN'S clock and simulated
    // none of the seconds it jumped, so the ward never heard about any of them.
    // Ten slept nights left stats().days at zero and the whole compound economy
    // had never run one day inside the windowed game.
    //
    // Tavern::dayNumber() is monotonic across midnight AND across a skip, which
    // is exactly the clock the ward wants. advanceToDay is idempotent, so this
    // may be called as often as it likes and the engine's own second counter
    // cannot double-count against it.
    if (ward_ != nullptr && tavern_ != nullptr) {
        ward_->advanceToDay(tavern_->dayNumber());
    }
    // #78: AND THE PEOPLE FOLLOW IT TOO, for the identical reason. A skip that
    // moved the taproom's clock and not the population's would photograph a
    // district still keeping the hours of whenever the session booted -- the
    // Watch on its day beat at two in the morning, and nobody in bed.
    // skipToSecond is a no-op when the clocks already agree, which they do on
    // every ordinary step, so this is safe to call as often as it likes.
    if (people_ != nullptr && tavern_ != nullptr) {
        people_->skipToSecond(timeOfDay_);
    }
}

// ---------------------------------------------------------------------------
// TIME-AND-TENURE BUILD: the ground underfoot, and the Flame's roll
// ---------------------------------------------------------------------------

std::int32_t Session::plotIndexUnderfoot() const noexcept {
    if (ward_ == nullptr || !ward_->loaded()) {
        return -1;
    }
    const std::string_view plotId = sim::docks::plotIdUnder(body_->tileX(), body_->tileY());
    if (plotId.empty()) {
        return -1;
    }
    return ward_->plotNamed(plotId);
}

void Session::syncGroundPlot() {
    sim::DialogueDirector& talk = tavern_->dialogue();
    // The ground underfoot, for the roll reading.
    const std::int32_t plot = plotIndexUnderfoot();
    if (plot < 0) {
        talk.setGroundPlot(-1, {});
    } else {
        const sim::Plot& roll = ward_->plots()[static_cast<std::size_t>(plot)];
        talk.setGroundPlot(plot,
                           ward_->raws().plots()[static_cast<std::size_t>(roll.raw)].name);
    }
    // And the roll's open prize, for the petition -- the FIRST vacant charge
    // in roll order (the roll carries at most one today; if it ever carries
    // two, the Flame hears them one at a time, front of the register first).
    // VACANT here must be petitionForCharge's own yes, or the topic offers a
    // door the verb then refuses: tenure Vacant, never already the player's.
    std::int32_t vacant = -1;
    if (ward_ != nullptr && ward_->loaded()) {
        const std::vector<sim::Plot>& plots = ward_->plots();
        for (std::size_t i = 0; i < plots.size(); ++i) {
            if (plots[i].tenure == sim::Tenure::Vacant && !plots[i].playerIsDuke) {
                vacant = static_cast<std::int32_t>(i);
                break;
            }
        }
    }
    if (vacant < 0) {
        talk.setVacantCharge(-1, {});
    } else {
        const sim::Plot& roll = ward_->plots()[static_cast<std::size_t>(vacant)];
        talk.setVacantCharge(vacant,
                             ward_->raws().plots()[static_cast<std::size_t>(roll.raw)].name);
    }
}

std::string Session::groundRollLine() const {
    const std::int32_t plot = plotIndexUnderfoot();
    if (plot < 0 || ward_ == nullptr) {
        // Only reachable by walking off the plot between the topic being
        // built and the press -- the honest answer is the register's.
        return "THE ROLL DOES NOT NAME THIS GROUND.";
    }
    const sim::Plot& roll = ward_->plots()[static_cast<std::size_t>(plot)];
    const sim::PlotRaw& raw = ward_->raws().plots()[static_cast<std::size_t>(roll.raw)];
    std::string line = raw.name + ". ";
    switch (roll.tenure) {
        case sim::Tenure::Glebe:
            line += "GLEBE -- CHURCH GROUND, NEVER LET.";
            return line;
        case sim::Tenure::Vacant:
            // THE NUMBER IS THE REGISTER'S, not a judgement of you -- the
            // word-only ruling governs how the ward talks ABOUT the player,
            // and a rent on a public roll is signage. Information, never a
            // discount: petitionForCharge charges exactly this.
            line += "THE CHARGE IS VACANT. THE FIRST QUARTER'S CHARGE-RENT IS " +
                    std::to_string(raw.chargeRent) + ".";
            return line;
        case sim::Tenure::Pledged:
        case sim::Tenure::Charged:
        default: {
            if (roll.playerIsDuke) {
                line += "YOU HOLD THE CHARGE. THE GROUND PENNY UNDER YOUR HOUSES IS " +
                        std::to_string(raw.groundPenny) + " A QUARTER.";
                return line;
            }
            // Who the roll says, in the roll's own priority: a re-let charge
            // carries its holder's name; otherwise the raws' own Duke,
            // resolved through the registry the roll was refused against.
            std::string holder = roll.heldBy;
            if (holder.empty() && !raw.denDuke.empty()) {
                const sim::Notable* duke = tavern_->dialogue().notables().find(raw.denDuke);
                holder = duke != nullptr ? duke->name : raw.denDuke;
            }
            line += holder.empty() ? "THE CHARGE IS HELD."
                                   : upperAscii(holder) + " HOLDS THE CHARGE.";
            if (roll.tenure == sim::Tenure::Pledged) {
                line += " THE PAPER ON IT IS PLEDGED.";
            }
            return line;
        }
    }
}

std::string Session::settleGroundPetition() {
    // THE ROLL'S OWN OPEN PRIZE, re-derived here rather than trusted from the
    // topic: the same first-vacant rule syncGroundPlot() feeds the topic
    // list, so the row and the verb can never name different plots.
    std::int32_t plot = -1;
    if (ward_ != nullptr && ward_->loaded()) {
        const std::vector<sim::Plot>& plots = ward_->plots();
        for (std::size_t i = 0; i < plots.size(); ++i) {
            if (plots[i].tenure == sim::Tenure::Vacant && !plots[i].playerIsDuke) {
                plot = static_cast<std::int32_t>(i);
                break;
            }
        }
    }
    if (plot < 0 || ward_ == nullptr) {
        return "NO CHARGE ON THE ROLL STANDS VACANT.";
    }
    const sim::PlotRaw& raw =
        ward_->raws().plots()[static_cast<std::size_t>(
            ward_->plots()[static_cast<std::size_t>(plot)].raw)];
    // ONE PURSE. The tavern's playerCoin is the purse of record everywhere in
    // the render layer; the ward keeps its own copy for NPC-parity verbs, so
    // it is synced in, the verb spends from it, and the result is synced back
    // out -- both fields already hashed, nothing minted, nothing new.
    ward_->setPlayerCoin(tavern_->playerCoin());
    const sim::TenureResult got = ward_->petitionForCharge(plot);
    tavern_->setPlayerCoin(ward_->playerCoin());
    tavern_->dialogue().setPlayerCoin(tavern_->playerCoin());
    // The roll changed (or did not); the topic list must say so either way.
    // A granted petition takes its own row off the list mid-conversation.
    syncGroundPlot();
    switch (got) {
        case sim::TenureResult::Done:
            return "THE FLAME RE-LETS THE CHARGE. YOU ARE DEN DUKE OF " + raw.name +
                   ". FIRST QUARTER PAID: " + std::to_string(raw.chargeRent) + ".";
        case sim::TenureResult::CannotAfford:
            return "THE FIRST QUARTER'S CHARGE-RENT IS " + std::to_string(raw.chargeRent) +
                   ". YOU DO NOT CARRY IT.";
        case sim::TenureResult::NotVacant:
            return "THE CHARGE IS HELD. THERE IS NOTHING TO PETITION FOR.";
        case sim::TenureResult::AlreadyHeld:
            return "THE ROLL ALREADY NAMES YOU.";
        case sim::TenureResult::NoCause:
            return "CHURCH GROUND. IT IS NEVER LET.";
        case sim::TenureResult::NoSuchThing:
        default:
            return "THE ROLL DOES NOT NAME THIS GROUND.";
    }
}

Camera Session::camera() const noexcept {
    // FROM THE LIVE SETTINGS, not from the config the session was built with.
    // #77 puts a field-of-view slider on the options page and the camera has to
    // read it every frame or the slider is decoration; the constructor seeds
    // controls_.fovDegrees from config_ so --fov still works.
    const float half = static_cast<float>(controls_.fovDegrees) * 0.5F * kPi / 180.0F;
    return Camera::fromBody(body_->x(), body_->y(), body_->eyeZ(), body_->yaw(), body_->pitch(),
                            std::tan(half));
}

std::string Session::placeLabel() const {
    const std::int32_t x = body_->tileX();
    const std::int32_t y = body_->tileY();
    const std::int32_t band = body_->band();
    for (std::size_t i = 0; i < sim::docks::kPlaceCount; ++i) {
        const sim::docks::Place& place = sim::docks::kPlaces[i];
        if (band == place.band && x >= place.x0 && x <= place.x1 && y >= place.y0 &&
            y <= place.y1) {
            return place.name;
        }
    }
    // No street name, because there is no street name to give. Saying which
    // band you are on is the whole of what the z-level actually knows.
    if (band == sim::docks::kBandQuayside) {
        return "THE DOCKS - QUAYSIDE";
    }
    if (band == sim::docks::kBandMidSlope) {
        return "THE DOCKS - MID SLOPE";
    }
    if (band == sim::docks::kBandUpper) {
        return "THE DOCKS - UPPER";
    }
    if (band < sim::docks::kBandQuayside) {
        return "UNDER THE PIERS";
    }
    return "THE DOCKS";
}

std::vector<Lamp> Session::tavernLights() const {
    // WHERE the flames are is the simulation's answer, derived from the baked
    // bytes (see gull::taproomTables). All this does is decide what each kind
    // of flame looks like -- brightness and colour, which are rendering, and
    // which are the only part of a light a renderer has any business owning.
    //
    // S2 hardcoded seven tile coordinates here with no derivation and no test.
    // The S2 review was right that this file had no business knowing them.
    std::vector<Lamp> lights;
    for (const sim::gull::HouseLight& light : tavern_->houseLights()) {
        Lamp lamp;
        lamp.x = light.x;
        lamp.y = light.y;
        lamp.z = light.band;
        switch (light.kind) {
            case sim::gull::LightKind::Hearth:
                lamp.name = "gull_hearth";
                lamp.luminance = 26;
                lamp.warmth = LampWarmth::Fire;
                break;
            case sim::gull::LightKind::Candle:
                lamp.name = "gull_candle";
                lamp.luminance = 17;
                lamp.warmth = LampWarmth::Fire;
                break;
            case sim::gull::LightKind::Lantern:
                // Cooler than the fire, so the room has two colours of light in
                // it and not one. The Gull is the captains' house and the
                // district's grandest room; charts on the walls are no use in
                // the dark, and a fifteen-tile interior lit by two hearth cells
                // and four candles reads as a cellar.
                lamp.name = "gull_lantern";
                lamp.luminance = 22;
                lamp.warmth = LampWarmth::Lantern;
                break;
        }
        lights.push_back(std::move(lamp));
    }
    return lights;
}

std::vector<SpriteInstance> Session::actorSprites() const {
    return actorSprites(camera());
}

std::vector<SpriteInstance> Session::actorSprites(const Camera& view) const {
    std::vector<SpriteInstance> sprites;
    const SkyState sky = skyAt(timeOfDay_);
    const std::vector<Lamp> live = tavernLights();
    for (const sim::Actor& actor : tavern_->actors()) {
        if (!actor.present()) {
            continue;
        }
        // Shaded by the light where they STAND, not lit from nowhere. Without
        // this a patron in an unlit corner glows like a lamp, which is the one
        // thing the committed-dark look cannot survive.
        const Rgb baked = renderer_->glow().at(actor.tileX(), actor.tileY(), actor.band());
        const Rgb dynamic = dynamicGlowAt(live, actor.tileX(), actor.tileY(), actor.band());
        // Clamped near 1: a figure standing in a lamp pool should be LIT, not
        // blown out into a featureless disc, which is what an unclamped
        // multiply does at close range.
        Rgb light{std::min(1.15F, sky.ambient.r + std::max(baked.r, dynamic.r)),
                  std::min(1.15F, sky.ambient.g + std::max(baked.g, dynamic.g)),
                  std::min(1.15F, sky.ambient.b + std::max(baked.b, dynamic.b))};

        // Sub-tile Q8 straight out of the simulation. See actor.hpp: the
        // position between two tiles is the sim's, not the renderer's.
        const float px = static_cast<float>(actor.x()) / 256.0F;
        const float py = static_cast<float>(actor.y()) / 256.0F;
        // Through bandSurface(), not straight off the band number. A band is
        // three tiles of height (sim/vertical_scale.hpp) and an actor whose
        // feet were placed at `band` stood two storeys below the floor they
        // were walking on.
        const float floorZ = bandSurface(actor.band());
        const bool down = actor.activity() == sim::Activity::Downed;

        // WHICH WAY THEY ARE LOOKING, and the reason it is still here.
        //
        // Actor::faceToward computes an eight-point facing and hashInto commits
        // it to world state on every tick. The S2 review found that no renderer
        // read it -- a feature that existed only as data -- and S3 spent it on a
        // pale patch stuck to the side of the head. The sheet is drawn front-on
        // and has no back view, so it is spent on LIGHT instead: somebody
        // looking your way catches what light there is and somebody turned away
        // is a silhouette. Which is true, and at eight tiles in lamplight it is
        // the one thing about a person you actually need to read.
        const float facingRad = static_cast<float>(actor.facing()) * (2.0F * kPi / 65536.0F);
        // BAM 0 is north, which is -Y, and increases clockwise (sim/angle.hpp).
        const float faceX = std::sin(facingRad);
        const float faceY = -std::cos(facingRad);
        const float toEyeX = view.x - px;
        const float toEyeY = view.y - py;
        const float span = std::sqrt(toEyeX * toEyeX + toEyeY * toEyeY);
        if (span > 0.0001F) {
            const float towards = (faceX * toEyeX + faceY * toEyeY) / span;
            if (towards <= 0.15F) {
                light = Rgb{light.r * kBackShade, light.g * kBackShade, light.b * kBackShade};
            }
        }

        const sim::WardType figure = figureForRole(actor.role(), actor.id());
        const ActorSprite& art =
            actorSheet_.forType(figure, static_cast<std::uint32_t>(actor.id()));
        const FigureScale scale = figureScaleOf(figure);
        const float inkRows = std::max(1.0F, static_cast<float>(art.lastRow - art.firstRow + 1));
        const float inkCols = std::max(1.0F, static_cast<float>(art.lastCol - art.firstCol + 1));

        SpriteInstance sprite;
        sprite.x = px;
        sprite.y = py;
        // THE FIGURE IS 1.875 TILES TALL, which is kStandingHeightTilesQ8 (480
        // Q8) out of sim/vertical_scale.hpp, the same header the player's own
        // eye height comes from. At roughly 0.9 m to the tile that is a 1.71 m
        // adult, and the crown lands a hand above the player's 1.70-tile eye --
        // so you look people in the mouth, which is correct and is the cheapest
        // proof the two scales agree.
        sprite.halfHeight = scale.heightTiles * 0.5F;
        sprite.halfWidth = scale.widthTiles * 0.5F * (inkCols / inkRows);
        if (down) {
            // Flat out on the boards: wide instead of tall, and low enough that
            // somebody standing over them reads as standing over them.
            sprite.halfHeight = 0.22F;
            sprite.halfWidth = scale.heightTiles * 0.5F;
        }
        sprite.z = floorZ + sprite.halfHeight;
        sprite.colour = light;
        sprite.glow = 0.0F;
        sprite.softness = 0.0F;
        // `person` is what the frame stats count apart from flames, and a rat
        // is not one. It is drawn, it is lit and it is in the frame -- it is
        // simply not somebody, which is the same distinction presentCount()
        // makes in the room itself.
        sprite.person = actor.role() != sim::ActorRole::Vermin;
        sprite.art = art.texels;
        sprite.artSize = ActorSprite::kPx;
        sprite.artU0 = art.firstCol;
        sprite.artU1 = art.lastCol;
        sprite.artV0 = art.firstRow;
        sprite.artV1 = art.lastRow;
        sprites.push_back(sprite);
    }
    return sprites;
}

namespace {

/// Clipped to something the bottom-left corner can hold without walking across
/// the frame. The HUD hugs its edge; a quest tracker that runs to the middle of
/// the screen is the exact failure the Java build shipped.
[[nodiscard]] std::string clip(std::string text, std::size_t columns) {
    if (text.size() > columns) {
        text.resize(columns);
    }
    return text;
}

// --- #82: the district map --------------------------------------------------

/// The job-family prefix content/raws/factions/factions.json's own
/// memberJobs speaks, for a notable's authored TYPE
/// (content/raws/names/notables.json). Four of the Forty's seven authored
/// types have no family at all here, and that is not a gap this table failed
/// to cover: a wastrel answers to nobody, which is factions.json's own
/// documented rule ("wastrel.streetlife ... deliberately unaffiliated"), and
/// the same silence is the honest answer for anything else this build has
/// not met yet.
[[nodiscard]] std::string_view notableJobFamily(std::string_view type) noexcept {
    if (type == "shopkeeper") {
        return "trade";
    }
    if (type == "militia_watch") {
        return "watch";
    }
    if (type == "priest_of_the_flame" || type == "disciple_of_the_flame") {
        return "clergy";
    }
    if (type == "serf") {
        return "serf";
    }
    if (type == "animal_keeper") {
        return "husbandry";
    }
    return {};
}

/// "NW 31T", or "HERE" standing on it -- the eight-point word and the
/// straight-line tile range from one point to another. This is the whole
/// reason the district map exists over the casebook's own list: a lead there
/// is a NAME; here it is a DIRECTION from where the player is actually
/// standing.
///
/// INTEGER THROUGHOUT, deliberately. sim::bearingTo is the identical call
/// PlayerBody's own stealth arc is judged by (sim/stealth.hpp) -- not a fresh
/// trig call invented for a menu -- and the range is Chebyshev, the tile
/// count a body that can step diagonally actually walks to close it, not a
/// ruler laid across the map.
[[nodiscard]] std::string bearingLabel(std::int32_t fromX, std::int32_t fromY, std::int32_t toX,
                                       std::int32_t toY) {
    const std::int32_t dx = toX - fromX;
    const std::int32_t dy = toY - fromY;
    if (dx == 0 && dy == 0) {
        return "HERE";
    }
    const std::int32_t ax = dx < 0 ? -dx : dx;
    const std::int32_t ay = dy < 0 ? -dy : dy;
    const std::int32_t tiles = ax > ay ? ax : ay;
    return std::string(sim::compass_point(sim::bearingTo(fromX, fromY, toX, toY))) + " " +
           std::to_string(tiles) + "T";
}

}  // namespace

std::vector<SpriteInstance> Session::wardSprites() const {
    return wardSprites(camera());
}

std::vector<SpriteInstance> Session::wardSprites(const Camera& view) const {
    std::vector<SpriteInstance> sprites;
    if (people_ == nullptr) {
        return sprites;
    }
    const SkyState sky = skyAt(timeOfDay_);
    // WHERE BETWEEN THE TWO TILES. The simulation says which tile a body left
    // and which one it is on; this says how far along it is THIS FRAME, and it
    // is the only place that opinion exists. Nothing here is written back --
    // which is the 2026-07-31 ruling read exactly as written: NPCs are
    // tile-stepped in the sim and interpolated at draw time.
    const float slide =
        static_cast<float>(stepsThisSecond_) / static_cast<float>(sim::kStepsPerSecond);
    sprites.reserve(people_->actors().size());
    for (const sim::WardActor& actor : people_->actors()) {
        // #80: not the starved, and not a mouse a cat has just taken off the
        // board. sim::WardActor::visible answers both in one place, which is
        // what stops a caught mouse being drawn standing in its own den for
        // three hours of ward time.
        if (!actor.visible()) {
            continue;
        }
        // Shaded by the light where they STAND. Without this a figure in an
        // unlit alley glows like a lamp, which is the one thing the
        // committed-dark look cannot survive.
        const Rgb baked = renderer_->glow().at(actor.x, actor.y, actor.band);
        Rgb light{std::min(1.15F, sky.ambient.r + baked.r),
                  std::min(1.15F, sky.ambient.g + baked.g),
                  std::min(1.15F, sky.ambient.b + baked.b)};

        const float px = static_cast<float>(actor.prevX) +
                         static_cast<float>(actor.x - actor.prevX) * slide + 0.5F;
        const float py = static_cast<float>(actor.prevY) +
                         static_cast<float>(actor.y - actor.prevY) * slide + 0.5F;
        const float floorZ = bandSurface(actor.band);

        // Which way they are looking, against where the eye is. The same
        // arithmetic the tavern's face patch uses: BAM 0 is north, which is -Y,
        // and it increases clockwise.
        const float facingRad = static_cast<float>(actor.facing) * (2.0F * kPi / 65536.0F);
        const float faceX = std::sin(facingRad);
        const float faceY = -std::cos(facingRad);
        const float toEyeX = view.x - px;
        const float toEyeY = view.y - py;
        const float span = std::sqrt(toEyeX * toEyeX + toEyeY * toEyeY);
        if (span > 0.0001F) {
            const float towards = (faceX * toEyeX + faceY * toEyeY) / span;
            if (towards <= 0.15F) {
                light = Rgb{light.r * kBackShade, light.g * kBackShade, light.b * kBackShade};
            }
        }

        const ActorSprite& art =
            actorSheet_.forType(actor.type, static_cast<std::uint32_t>(actor.id));
        const FigureScale scale = figureScaleOf(actor.type);
        const float inkRows = std::max(1.0F, static_cast<float>(art.lastRow - art.firstRow + 1));
        const float inkCols = std::max(1.0F, static_cast<float>(art.lastCol - art.firstCol + 1));

        SpriteInstance sprite;
        sprite.x = px;
        sprite.y = py;
        // Sized to the INK and stood on the pavement: the quad's bottom edge is
        // the floor, so a figure drawn short inside its own cell does not hover
        // two texels above the street.
        sprite.halfHeight = scale.heightTiles * 0.5F;
        sprite.halfWidth = scale.widthTiles * 0.5F * (inkCols / inkRows);
        sprite.z = floorZ + sprite.halfHeight;
        sprite.colour = light;
        sprite.glow = 0.0F;
        sprite.softness = 0.0F;
        sprite.person = sim::isPerson(actor.type);
        sprite.ward = true;
        sprite.art = art.texels;
        sprite.artSize = ActorSprite::kPx;
        sprite.artU0 = art.firstCol;
        sprite.artU1 = art.lastCol;
        sprite.artV0 = art.firstRow;
        sprite.artV1 = art.lastRow;
        sprites.push_back(sprite);
    }
    return sprites;
}

std::string Session::guildLine() const {
    // NOTHING WHILE THE WIRE IS IN. The lock row is drawn at
    // height - margin - 31*scale; this one is drawn at 24*scale above the
    // health bar, which IS height - margin - 31*scale. The same pixel row.
    // docs/frames/s10-08-the-lock.png caught the two interleaved, reading "THE
    // 1OCKUNPINS -TENADEPTH ....+....". It is the same class of defect the S7
    // review found with the alert over the topic grid and it gets the same fix:
    // a MODE owns its row. The wire is a mode, and while it is in, which ladder
    // you are highest on is not what the player is reading.
    //
    // It is suppressed HERE and not at the draw call so a case can assert it --
    // a suppression that lives inside drawFrame is only checkable in pixels.
    if (picking()) {
        return {};
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    const std::int32_t top = talk.standings().highestRankedFaction();
    if (top < 0) {
        return {};
    }
    const sim::Faction* faction = talk.factions().at(top);
    const std::string name = faction == nullptr ? std::string("GUILD") : faction->displayName;
    return clip(upperAscii(name) + " - " + upperAscii(talk.standings().rankTitle(top)), 34);
}

std::string Session::caseLine() const {
    if (!casebook_.active() || !caseRaws_.loaded()) {
        return {};
    }
    const std::vector<std::int32_t> heard = casebook_.known();
    std::string line = "CASE " + std::to_string(casebook_.readCount()) + "/" +
                       std::to_string(heard.size());
    // WHERE TO GO NEXT, ON THE SAME ROW. This is the orientation line: a player
    // who put the game down for a week and came back to a district of 692
    // people gets one line telling them where they were walking. Until this
    // sprint the corner of a new game was empty, which is exactly the "dropped
    // into a systems demo with no orientation" the demo brief names.
    const std::int32_t lead = casebook_.nextOpen();
    if (lead >= 0 && static_cast<std::size_t>(lead) < caseRaws_.leads().size()) {
        line += " > " + caseRaws_.leads()[static_cast<std::size_t>(lead)].place;
    } else if (casebook_.closed()) {
        line += " > " + std::string(caseRaws_.close());
    } else {
        const std::string_view mood = caseRaws_.dreadLabel(casebook_.dread());
        line += "  ";
        line.append(mood);
    }
    return clip(std::move(line), 40);
}

std::string Session::legendLine() const {
    const sim::Legend book = legend();
    if (book.totalRungs() <= 0) {
        return {};
    }
    return clip(std::string(book.title()) + "  " + std::to_string(book.totalRungs()) + " RUNGS",
                34);
}

std::vector<std::string> Session::characterRows() const {
    // FIVE ROWS, THEN FOUR, THEN FIVE, THEN THREE: the five Legend tracks
    // (S8's own "who am I in this city yet", derived and thrown away every
    // frame until this page existed -- see the header), the four skills a
    // verb in this build actually levels, the five faction ladders, and what
    // the ward and the purse currently say. The tracks and skills fill page
    // one exactly -- see kTopicPageSize -- and the ladders open page two, so
    // neither block is ever split by a page turn a player has to go looking
    // for.
    std::vector<std::string> rows;
    rows.reserve(sim::kLegendTracks + 4 + 5 + 3);
    const sim::Legend book = legend();
    for (std::size_t i = 0; i < sim::kLegendTracks; ++i) {
        const sim::LegendRow& row = book.rows()[i];
        std::string_view name = sim::legendTrackName(row.track);
        // EVERY TRACK NAME STARTS "THE ", legend.hpp's own table, and five
        // copies of the one word they share is four words spent out of an
        // eighteen-glyph column for nothing. Dropped on the way to the screen
        // only -- legendTrackName() still returns the full "THE WIRE"
        // everywhere else that reads it, because this is a display choice and
        // not a rename.
        if (name.substr(0, 4) == "THE ") {
            name.remove_prefix(4);
        }
        std::string line = std::string(name) + " - " + std::string(row.title);
        // WHAT THE NEXT RUNG WANTS, kept at last. legend.hpp's own header has
        // promised since S8 that "the panel prints what the next one wants in
        // the same units the player already sees" -- LegendRow::nextAt has
        // carried the number all along and no panel ever printed it. Score
        // over threshold, the notation every counted stage already uses; a
        // topped-out track shows its score alone, because a target it has
        // passed forever would read as work still owed.
        line += "  " + std::to_string(row.score);
        if (row.nextAt > 0) {
            line += "/" + std::to_string(row.nextAt);
        }
        rows.push_back(std::move(line));
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    struct ActiveSkill {
        std::string_view id;
        std::string_view label;
    };
    // THE FOUR OF TWENTY THIS BUILD ACTUALLY LEVELS. content/raws/skills has
    // sixteen more entries -- authored vocabulary for a Morrowind-style pass
    // that has not reached this build yet (see the header's own note) -- and
    // listing them all at LV 0 would tell a player they are being tracked when
    // nothing in the game is watching.
    static constexpr ActiveSkill kActiveSkills[] = {
        {sim::kRoofSkill, "SKYRUNNING"},
        {sim::kThieverySkill, "CRACKSMANSHIP"},
        {sim::kHaggleSkill, "STREETWISE"},
        {sim::kCraftingSkill, "LINKCRAFT"},
    };
    for (const ActiveSkill& entry : kActiveSkills) {
        // MORROWIND ROUND: ONE SPACE, NOT TWO. The Character tile's own
        // column is narrower than the old full-width panel's -- see
        // menu_view.cpp's own note on why a single column is still WIDER
        // per row than the old three-column grid ever gave a label, but at
        // the smallest resolution this build still tests (320x180) every
        // glyph matters. The saved column buys SKYRUNNING/ROOFS/STREETWISE/
        // LINKCRAFT their full "LV 0" back there; CRACKSMANSHIP (thirteen
        // letters, the longest of the four) still cuts to "CRACKSMANSHIP."
        // at that one resolution -- VERIFICATION GAP: clipLabel() marks the
        // cut rather than dropping it silently (the same contract every
        // other clipped row in this renderer already keeps), and the value
        // is not lost -- it reads on the Journal-tile-sized capture at
        // 640x360 and above, which is this build's own shipped default.
        rows.push_back(std::string(entry.label) + " LV " +
                       std::to_string(talk.skills().level(entry.id)));
    }
    // THE FIVE LADDERS, WITH THE NUMBERS ON. Owner ruling for this build: the
    // player's OWN sheet shows rank title, the standing number and what the
    // next rung costs -- the word-only ruling still governs how NPCs and the
    // ward-reputation lines TALK about you, and nothing there changed. Every
    // figure is read out of FactionLedger and the raws' own ladder: the rung
    // nextRung() answers with is the exact rung join()/advance() will measure
    // (checkRung's), so the sheet can never promise a price the ladder does
    // not charge. Cost notation is standing, then "/LV n" when the rung is
    // also measured in the ladder's own skill -- the same LV the four skill
    // rows above already taught. INFORMATION, NEVER A DISCOUNT: nothing here
    // moves a number, it only stops the climb being a slot machine.
    const sim::FactionLedger& standings = talk.standings();
    if (const sim::FactionRegistry* guilds = standings.registry(); guilds != nullptr) {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(standings.size()); ++i) {
            const sim::Faction* who = guilds->at(i);
            if (who == nullptr) {
                continue;
            }
            // The id, not the display name: "TEMPLE OF THE FLAME" would spend
            // the whole column on its own name, and the map tile's "who will
            // talk" list already prints these same ids -- one vocabulary.
            std::string line = upperAscii(who->id);
            if (const std::string_view title = standings.rankTitle(i); !title.empty()) {
                line += " " + upperAscii(title);
            }
            line += " " + std::to_string(standings.standing(i));
            if (const sim::FactionRank* next = standings.nextRung(i); next != nullptr) {
                // JOIN for an outsider, NEXT for a member -- the first rung is
                // earned exactly like every later one (faction.hpp's own rule),
                // so both read the same rung the ledger will actually check.
                line += standings.isMember(i) ? "  NEXT " : "  JOIN ";
                line += std::to_string(next->standing);
                if (next->skillLevel > 0) {
                    line += "/LV" + std::to_string(next->skillLevel);
                }
            } else if (standings.rank(i) > 0) {
                // On a ladder with no rung above you. Said plainly rather than
                // left blank, so "nothing after the number" always means "no
                // ladder" and never "top" -- two facts, two spellings.
                line += "  TOP";
            }
            rows.push_back(std::move(line));
        }
    }
    // ABSENCE COSTS NOTHING ON THE HUD; IT COSTS NOTHING HERE EITHER, but for
    // the opposite reason. The HUD drops a row that has nothing to say because
    // it is read every frame; a sheet a player opened ON PURPOSE to look
    // themselves up is the one place a zero is worth printing, so REPUTATION,
    // COIN and HEAT are always here, not just when they are interesting.
    rows.push_back("REPUTATION  " + std::string(talk.ledger().reputationLabel()));
    rows.push_back("COIN  " + std::to_string(tavern_->playerCoin()));
    rows.push_back("HEAT  " + std::to_string(talk.crimes().heat()));
    return rows;
}

std::vector<std::string> Session::mapRows() const {
    // THREE SECTIONS, EACH BUILT FROM WHAT THE CASEBOOK ALREADY KNOWS -- see
    // toggleMap's own header on why nothing here is new state. All three grow
    // by the identical act: hearing a lead. There is no separate "have you
    // been here" or "have you met them" flag to invent, forget to update, or
    // let drift from the trail itself.
    std::vector<std::string> rows;
    if (!casebook_.active()) {
        return rows;
    }
    const std::vector<std::int32_t> heard = casebook_.known();
    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();

    // -- known ground: the named places a heard lead has put in the book,
    // once each, in the order they were first heard of. The casebook's own
    // `place` field, authored, never derived -- these are the district's
    // named sites and the street table (docks::kPlaces) only knows the
    // streets between them.
    std::vector<std::string_view> places;
    for (const std::int32_t index : heard) {
        const std::string_view place = caseRaws_.leads()[static_cast<std::size_t>(index)].place;
        if (std::find(places.begin(), places.end(), place) == places.end()) {
            places.push_back(place);
        }
    }
    for (const std::string_view place : places) {
        // "- ", not "@ " -- the 4x6 font carries no glyph for '@' (hud.cpp's
        // own table), and a hole punched through a place name by a symbol
        // nobody chose to support is exactly the class of bug the standing
        // quality bar exists to catch. See test_map.cpp's own copy case.
        rows.push_back("- " + std::string(place));
    }

    // -- open leads: heard, not yet looked at, each with a bearing from HERE.
    // The one thing this page has that the casebook's own list does not: the
    // casebook prints WHAT you have heard, unplaced; this prints WHERE it is
    // from where you are standing right now.
    for (const std::int32_t index : heard) {
        if (casebook_.state(index) != sim::LeadState::Open) {
            continue;
        }
        const sim::Lead& lead = caseRaws_.leads()[static_cast<std::size_t>(index)];
        rows.push_back(bearingLabel(px, py, lead.site.x, lead.site.y) + "  " +
                       (lead.brief.empty() ? lead.place : lead.brief));
    }

    // -- who will talk: every named person a heard lead has pointed you at,
    // once each, who actually answers to somebody -- a wastrel answers to
    // nobody, which is factions.json's own rule (see notableJobFamily) and
    // not a gap in this list.
    const sim::NotableRegistry& notables = tavern_->dialogue().notables();
    const sim::FactionRegistry& factions = tavern_->dialogue().factions();
    std::vector<std::string_view> named;
    for (const std::int32_t index : heard) {
        const std::string_view who = caseRaws_.leads()[static_cast<std::size_t>(index)].who;
        if (who.empty() || std::find(named.begin(), named.end(), who) != named.end()) {
            continue;
        }
        named.push_back(who);
        const sim::Notable* notable = notables.find(who);
        if (notable == nullptr) {
            continue;
        }
        const sim::Faction* faction =
            factions.at(factions.factionForJobPrefix(notableJobFamily(notable->type)));
        if (faction == nullptr) {
            continue;
        }
        rows.push_back("! " + notable->name + "  " + upperAscii(faction->id));
    }
    return rows;
}

std::string Session::heatLine() const {
    const sim::CrimeLedger& crimes = tavern_->dialogue().crimes();
    const sim::Stash& sack = crimes.stash();
    if (crimes.heat() <= 0 && crimes.loot() <= 0 && !crimes.carryingBale() && sack.empty() &&
        !crimes.maimed()) {
        return {};
    }
    std::string line;
    // The two the ward has done TO you outrank everything else on the line:
    // a condemned man wants to know he is one before he wants his heat.
    if (crimes.condemned()) {
        line = "CONDEMNED  ";
    } else if (crimes.maimed()) {
        line = "MAIMED  ";
    }
    if (crimes.warrant()) {
        line += "WANTED  ";
    }
    line += "HEAT " + std::to_string(crimes.heat());
    if (crimes.loot() > 0) {
        line += "  LOOT " + std::to_string(crimes.loot());
    }
    if (crimes.carryingBale()) {
        line += "  BALE";
    }
    return clip(std::move(line), 34);
}

bool Session::conversingNow() const noexcept {
    // THE ONE FORMULA. Repeated in drawFrame() as `conversing` until this
    // pass, which is exactly the shape of drift that let the S7 review's
    // overprint findings happen: two places computing the same fact, and
    // nothing catching them when a seventh page joined the list and only one
    // of the two remembered to add it.
    return talking() || casebookOpen_ || keysOpen_ || grimoireOpen_ || waitOpen_ ||
           districtMapOpen_ || optionsOpen_ || pauseOpen_;
}

void Session::syncPanelAnim() noexcept {
    // AUDIO WIRING: PANEL OPEN AND CLOSE, on the exact edge the plan names --
    // "wherever an EasedToggle target flips is exactly where the matching
    // one-shot belongs". panelAnim_ is the ONE toggle all eight pages share,
    // so this is one hook for every open and close in the game: the tiled
    // Menu speaks as a book (its tiles are the casebook, the letters, the
    // map), everything else in the quiet Ui pair. The flip fires once per
    // actual change however many times this function is re-run per step,
    // because the target itself only changes once.
    const bool panelWanted = conversingNow();
    if (audio_ != nullptr && panelWanted != panelAnim_.target()) {
        if (panelWanted) {
            // SPELLS BUILD: the Grimoire page is paper too -- the same
            // BookOpen/BookClose pair the tiled Menu already speaks, no new
            // sound design.
            // The ward map is paper too -- a chart unrolled reads as a book
            // opening, not as a UI blip.
            audioPanelWasMenu_ = casebookOpen_ || grimoireOpen_ || districtMapOpen_;
            audio_->playOneShot(audioPanelWasMenu_ ? audio::SoundId::BookOpen
                                                   : audio::SoundId::UiOpen);
        } else {
            audio_->playOneShot(audioPanelWasMenu_ ? audio::SoundId::BookClose
                                                   : audio::SoundId::UiClose);
        }
    }
    panelAnim_.setTarget(panelWanted);
    // The identical two-line test drawFrame() makes for what the HUD's own
    // alert row is about to show -- see its own comment there. Duplicated
    // rather than shared through a common accessor because one runs on a
    // `const` path (drawFrame) and this one has to mutate alertAnim_; the
    // formula itself is three lines and has not moved since S6.
    const bool warned = !tavern_->lastWarning().empty() &&
                        tavern_->playerStanding() != sim::Standing::Welcome;
    // messageSteps_, NOT message_.empty(). The string is deliberately kept
    // alive a few steps past the countdown reaching zero so the fade-out has
    // something to fade -- see step()'s own note -- so the STRING being
    // non-empty cannot be what decides whether the alert is still wanted, or
    // the two would deadlock each other.
    alertAnim_.setTarget(warned || messageSteps_ > 0);
    // INNOVATION SPRINT ITEM #3. THE RISING EDGE ONLY -- a fresh warning
    // pulses the alert plate's own weight; the SAME warning re-read on every
    // later step (`warned` staying true) must not retrigger it, or the pulse
    // would sit at full strength for as long as the bouncer keeps talking
    // instead of reading as one beat. See alertPulse_'s own header.
    if (warned && !lastWarned_) {
        alertPulse_.trigger();
    }
    lastWarned_ = warned;

    // INNOVATION SPRINT ITEM #2. THE TILED MENU'S OWN FOUR BORDERS. Re-read
    // every call, exactly like every row below -- see this function's own
    // header on why that catches a focus change made through menuPageNext()/
    // menuPagePrev() (neither of which calls this directly) the moment
    // step() next runs it, not only one made through toggleMenuFocused().
    // Closed (casebookOpen_ false) targets every tile unfocused rather than
    // leaving whichever one last had it visibly "focused" underneath a
    // panel that is not there to show it.
    const bool menuUp = casebookOpen_;
    characterFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusCharacter);
    mapFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusMap);
    lettersFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusLetters);
    journalFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusJournal);

    // HARDENING PASS. EVERY OTHER ROW hud.hpp:178-187 NAMED AS STILL
    // SNAPPING, on the identical two-part shape the alert just used above:
    // setTarget() decides whether the row is WANTED, and the cached string
    // is what stays behind to fade once it is not -- see interactCache_'s
    // own header. `conversing` matches exactly what drawFrame() already
    // blanks every one of these rows for while a panel is open (the topic
    // list, the casebook, the keys page, options or pause own the bottom
    // band then), and roomLine()/stealthLine() gate themselves on
    // tavern_->playerInside() the same way drawFrame() used to inline.
    const bool conversing = conversingNow();
    const auto sync = [conversing](EasedToggle& anim, std::string& cache, std::string&& text) {
        const bool visible = !conversing && !text.empty();
        if (visible) {
            cache = std::move(text);
        }
        anim.setTarget(visible);
    };
    // #85's OWN "<key>  <label>" COMPOSITION, MOVED HERE FROM drawFrame()
    // RATHER THAN DUPLICATED. interactPrompt() answers the bare verb
    // ("TALK"); the key name off the same primary binding keyRows() prints
    // is prefixed here, once, so drawFrame() reading interactCache_ back
    // gets the exact row it used to build inline.
    std::string prompt;
    if (const std::string verb = interactPrompt(); !verb.empty()) {
        prompt = std::string(keyName(controls_.primary[static_cast<std::size_t>(Action::Interact)]));
        prompt += "  ";
        prompt += verb;
    }
    sync(interactAnim_, interactCache_, std::move(prompt));
    sync(lockAnim_, lockCache_, lockLine());
    sync(caseAnim_, caseCache_, caseLine());
    sync(roomAnim_, roomCache_, roomLine());
    sync(rivalAnim_, rivalCache_, rivalLine());
    sync(guildAnim_, guildCache_, guildLine());
    sync(objectiveAnim_, objectiveCache_, objectiveLine());
    sync(stealthAnim_, stealthCache_, tavern_->playerInside() ? stealthLine() : std::string());
    // PLANNING SPRINT (item #2, the sweep). THE SAME sync() SHAPE, FOR THE
    // TOP-RIGHT STACK'S THREE REMAINING ROWS -- see standingAnim_'s own
    // header on why these three, specifically, were still snapping.
    sync(standingAnim_, standingCache_, standingLine());
    sync(heatAnim_, heatCache_, heatLine());
    sync(stashAnim_, stashCache_, stashLine());
    // FIRST-PERSON COMBAT (S13). THE SAME sync() SHAPE, for the two rows the
    // Cast/Block task added -- each on its own EasedToggle per the pinned
    // convention, because a readied crafting appearing has nothing to do with
    // a guard going up.
    sync(spellAnim_, spellCache_, spellLine());
    sync(blockAnim_, blockCache_, blockLine());
    // HELD-EFFECTS BUILD. THE SAME sync() SHAPE, one per active-effect slot
    // -- each on its own EasedToggle per the pinned convention, because a
    // warmth lapsing in slot 0 has nothing to do with a tuning arriving in
    // slot 1.
    for (std::size_t slot = 0; slot < kEffectRows; ++slot) {
        sync(effectAnims_[slot], effectCaches_[slot], effectLine(slot));
    }
    // FATIGUE BUILD. The bar mirrors the health bar's one visibility rule --
    // down for the length of a conversation, up otherwise -- on its OWN
    // toggle per the pinned convention. No cache: the bar draws live numbers,
    // which do not go away while it fades.
    fatigueAnim_.setTarget(!conversing);
    // THE WARD MAP (core action #13). Its own toggle, its own target -- the
    // page's openAmount, per the settled convention (rule 1). No cache: the
    // page draws live off TileQuery, which does not go away when it closes.
    districtMapAnim_.setTarget(districtMapOpen_);
    // SPELLS BUILD. The quick bar strip: wanted while the wheel or the number
    // row was touched inside the last couple of seconds (showQuickBar()'s
    // countdown, run down in step()) and no panel owns the bottom band. The
    // names are cached on the same visible-edge rule every sync() above uses,
    // so the strip finishes its fade with its labels still on it.
    const bool barWanted = !conversing && quickBarShowSteps_ > 0;
    if (barWanted) {
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::Spell* bound = tavern_->slotSpell(slot);
            quickBarNames_[static_cast<std::size_t>(slot)] =
                bound == nullptr ? std::string() : upperAscii(bound->displayName);
        }
    }
    quickBarAnim_.setTarget(barWanted);
}

std::string Session::rivalLine() const {
    // BOTTOM-LEFT, ON THE EDGE, ONE LINE. The HUD rule is not a preference
    // (COMBAT-FEEL-REFERENCE section 3): the centre stays empty and an
    // inventory, a guild or a rivalry is one row in a corner until it has
    // earned more. This is the only thing in the game that says the man across
    // the room is the man who put you here.
    const sim::Nemesis* worst = tavern_->nemesis().worst();
    if (worst == nullptr) {
        return {};
    }
    std::string line = "RIVAL " + upperAscii(worst->who);
    if (!worst->title.empty()) {
        line += " - " + upperAscii(worst->title);
    }
    line += " x" + std::to_string(worst->wins);
    if (worst->hunts()) {
        line += " HUNTING";
    }
    // 44 columns: 220 pixels at scale 1 against a 320-wide frame with a
    // six-pixel margin, so the longest line this can produce still fits its
    // edge. A HUD line that runs off the frame is the S6 defect, and it is not
    // being reintroduced from a different corner.
    return clip(std::move(line), 44);
}

std::string Session::stashLine() const {
    // WHAT IS ON YOU, AND WHAT IT WEIGHS. The weight is the number that matters
    // -- it is what a watchman's eye is on -- so it is on the line beside the
    // count rather than buried in a sheet.
    const sim::Stash& sack = tavern_->dialogue().crimes().stash();
    if (sack.empty()) {
        return {};
    }
    std::string line;
    for (std::size_t i = 0; i < sim::kContrabandCount; ++i) {
        const sim::Contraband good = static_cast<sim::Contraband>(i);
        const std::int32_t held = sack.count(good);
        if (held <= 0) {
            continue;
        }
        if (!line.empty()) {
            line += "  ";
        }
        line += std::to_string(held) + " " + std::string(sim::contrabandLabelFor(good, held));
    }
    if (sack.illicitWeight() > 0) {
        line += "  " + std::to_string(sack.illicitWeight()) + "DR";
    }
    return clip(std::move(line), 34);
}

std::string Session::spellLine() const {
    // WHAT THE HAND IS HOLDING, one line on the top-right edge -- the same
    // deal the sack got, and for the same reason: a readied crafting is
    // exactly the kind of element that creeps into being a hotbar panel.
    // Empty with an empty grimoire (absence costs nothing), and the cooling
    // clock rides the same line rather than earning a meter.
    const sim::Spell* spell = tavern_->equippedSpell();
    if (spell == nullptr) {
        return {};
    }
    std::string line = "CAST  " + upperAscii(spell->displayName);
    const std::int64_t cooling = tavern_->castCooldownLeft();
    if (cooling > 0) {
        line += " (" + std::to_string(cooling) + "S)";
    }
    return clip(std::move(line), 34);
}

std::string Session::blockLine() const {
    // THE ROOM'S OWN FACT, not the keypress: playerBlocking() is what
    // tickBrawl actually reads, so this row can never say GUARD UP while a
    // menu has quietly lowered it -- see step()'s own derivation.
    return tavern_->playerBlocking() ? "GUARD UP" : std::string();
}

std::string Session::effectLine(std::size_t slot) const {
    // HELD-EFFECTS BUILD. The slot-th live hold, name and seconds left. The
    // name comes out of the grimoire by the id the hold itself carries --
    // the same one source of truth the CAST row reads -- and the clock is
    // the room's own holdSecondsLeft, re-read every step so the row counts
    // down continuously rather than snapping on expiry.
    const std::vector<sim::Tavern::ActiveHold>& holds = tavern_->heldEffects();
    if (slot >= holds.size() || slot >= kEffectRows) {
        return {};
    }
    const sim::Tavern::ActiveHold& hold = holds[slot];
    const sim::Spell* spell = tavern_->dialogue().grimoire().find(hold.spellId);
    std::string name =
        spell != nullptr ? upperAscii(spell->displayName) : upperAscii(hold.spellId);
    return clip(name + " " + std::to_string(tavern_->holdSecondsLeft(hold)) + "S", 34);
}

std::string Session::contractLine() const {
    // The job with the least time left on it, because that is the one a player
    // needs to be reminded about.
    const sim::ContractBoard& board = tavern_->dialogue().contracts();
    const sim::Contract* soonest = nullptr;
    for (const sim::Contract& row : board.contracts()) {
        if (!row.live()) {
            continue;
        }
        if (soonest == nullptr || row.dueOnDay < soonest->dueOnDay ||
            (row.dueOnDay == soonest->dueOnDay && row.id < soonest->id)) {
            soonest = &row;
        }
    }
    if (soonest == nullptr) {
        return {};
    }
    const std::int32_t have =
        tavern_->dialogue().crimes().stash().count(soonest->good);
    return clip(soonest->label + " " + std::to_string(have) + "/" +
                    std::to_string(soonest->units),
                34);
}

std::string Session::objectiveLine() const {
    // And the row above it, for the same reason: the lock's own row is bounded
    // by these two and a long lock line is nearly the width of the frame.
    if (picking()) {
        return {};
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    // A TAKEN JOB OUTRANKS AN AUTHORED STAGE, because a job has a deadline and
    // a questline does not. The Skyrunner line graduates into contract work,
    // so by the time a player is holding one the line is finished anyway.
    if (const std::string work = contractLine(); !work.empty()) {
        return work;
    }
    for (const sim::Questline& line : talk.quests().lines()) {
        if (!talk.journal().started(line.id) || talk.journal().done(line.id)) {
            continue;
        }
        const std::int32_t at = talk.journal().stage(line.id);
        if (at < 0 || static_cast<std::size_t>(at) >= line.stages.size()) {
            continue;
        }
        // The stage LABEL, not its objective: the label is already short, upper
        // case menu furniture, and the objective is a paragraph that belongs in
        // a journal rather than in the corner of a frame.
        return clip(line.stages[static_cast<std::size_t>(at)].label, 34);
    }
    // THE CASE'S OWN DESTINATION IS NOT PUT HERE. It has its own row lower down
    // the stack -- see caseLine() -- because this one sits at y - 32*scale,
    // which crosses the exclusion rectangle whenever it is non-empty, and the
    // orientation a new player needs must not be the thing that breaks the HUD
    // rule to deliver it.
    return {};
}

std::string Session::standingLine() const {
    // ABSENCE COSTS NOTHING, the identical rule every other row on this stack
    // already keeps: "NOBODY IN PARTICULAR" is the ward having no opinion of
    // you at all, and the row only appears the moment it HAS one -- see
    // drawFrame()'s own comment where this used to be computed inline.
    const std::string_view standing = tavern_->dialogue().ledger().reputationLabel();
    if (standing == sim::kReputationUnremarkable) {
        return {};
    }
    return std::string(standing);
}

FrameStats Session::drawFrame(Framebuffer& target) const {
    // The flicker phase is a pure function of the body's step count, so the
    // same scripted session captures the same frame every time.
    const float phase = static_cast<float>(body_->stepCount()) / 60.0F;
    std::vector<SpriteInstance> sprites = renderer_->lampSprites(phase);

    // A lamp is not a hole in the sky at noon. The flame billboards fade out as
    // the daylight comes up, so a lit district reads at dusk and disappears
    // into ordinary daylight the way it should.
    const float lampMix = 1.0F - 0.9F * skyAt(timeOfDay_).daylight;
    for (SpriteInstance& sprite : sprites) {
        sprite.glow *= lampMix;
        sprite.halfWidth *= 0.45F + 0.55F * lampMix;
        sprite.halfHeight *= 0.45F + 0.55F * lampMix;
    }

    RenderSettings settings = settings_;
    settings.dynamicLamps = tavernLights();

    // The hearth's own flame, and a candle head on each lit table.
    for (const Lamp& light : settings.dynamicLamps) {
        const bool isHearth = light.name == "gull_hearth";
        const bool isLantern = light.name == "gull_lantern";
        SpriteInstance flame;
        flame.x = static_cast<float>(light.x) + 0.5F;
        // Pulled a little out of the wall the hearth is set into, so the flame
        // sits in the mouth of it rather than inside the masonry.
        flame.y = static_cast<float>(light.y) + (isHearth ? -0.05F : 0.5F);
        // A hearth burns at the floor, a candle on a table, a lantern hangs —
        // and now that a storey is three tiles those are three genuinely
        // different heights instead of three points inside one tile. ABSOLUTE
        // tile heights above the floor of the cell, not fractions of the
        // storey: raising a ceiling does not raise a tabletop.
        //
        //   0.35  in the mouth of the fireplace, on the hearthstone
        //   0.95  a candle on a table, about 0.86 m
        //   2.30  a lantern on its hook, well over head height
        constexpr float kHearthFlameHeight = 0.35F;
        constexpr float kTableCandleHeight = 0.95F;
        constexpr float kHangingLanternHeight = 2.30F;
        flame.z = bandSurface(light.z) +
                  (isHearth ? kHearthFlameHeight
                            : (isLantern ? kHangingLanternHeight : kTableCandleHeight));
        const float flicker =
            0.85F + 0.15F * std::sin(phase * 4.3F + static_cast<float>(light.x));
        const float scale = 0.10F + 0.008F * static_cast<float>(light.luminance);
        flame.halfWidth = scale * flicker;
        flame.halfHeight = scale * flicker * 1.4F;
        flame.colour = isLantern ? Rgb{1.0F, 0.86F, 0.62F} : Rgb{1.0F, 0.58F, 0.22F};
        flame.glow = 1.0F;
        sprites.push_back(flame);
    }

    const Camera view = camera();
    const std::vector<SpriteInstance> people = actorSprites(view);
    sprites.insert(sprites.end(), people.begin(), people.end());
    // #78: AND THE REST OF THE DISTRICT. One roster, two sources: the Gilded
    // Gull's seventeen come out of the Tavern and everybody else out of the
    // population. They are appended and never merged, because the two systems
    // own their own bodies and a renderer that stitched them into one list
    // would be the third place that thinks it knows who is in the ward.
    const std::vector<SpriteInstance> ward = wardSprites(view);
    sprites.insert(sprites.end(), ward.begin(), ward.end());

    const FrameStats stats = renderer_->renderFrame(target, view, settings, sprites);

    // World content, not interface -- drawn like the sprites above rather
    // than gated behind config_.hud, and BEFORE the HUD/menu/dialogue passes
    // below so none of them paint over a legible sign. See
    // signage_renderer.hpp's own header for why occlusion needs the depth
    // buffer renderFrame just wrote and nothing drawn after it yet.
    drawSignage(target, view);

    // INNOVATION SPRINT ITEM #3. A BRAWL FINALLY HAS SOME PHYSICAL WEIGHT.
    // Two brief, low-alpha washes over the WORLD -- drawn here, before the
    // HUD, so the one readout that matters most while this fires (the health
    // bar) is never sitting on top of the wash instead of over the plain
    // world. RESTRAINED ON PURPOSE: this is a moody, text-forward
    // investigation game and not an arcade brawler, so both peak under a
    // fifth of full strength and are gone within a handful of frames -- see
    // punchLandedPulse_/punchTakenPulse_'s own header for why an
    // ImpactPulse (a one-shot event) rather than a held EasedToggle is what
    // drives this, and why there is no third or fourth flash bolted on: two
    // real moments, executed with restraint, rather than a scattershot pass
    // across every verb that could theoretically want one.
    if (config_.hud) {
        const float landed = punchLandedPulse_.value();
        if (landed > 0.0F) {
            // A CONNECTING PUNCH: pale and warm, the same register the
            // lantern flame and the plate's own bone border already draw in
            // -- an "it worked" beat, not a warning.
            target.fillRect(0, 0, target.width(), target.height(), Rgb{0.92F, 0.86F, 0.70F},
                            0.16F * landed);
        }
        const float taken = punchTakenPulse_.value();
        if (taken > 0.0F) {
            // A BLOW LANDING ON THE PLAYER: a low, blooded red, read as a
            // wince rather than an alarm -- deliberately not brighter than
            // the landed-punch flash above, which is meant to feel like the
            // better of the two moments.
            target.fillRect(0, 0, target.width(), target.height(), Rgb{0.58F, 0.10F, 0.08F},
                            0.18F * taken);
        }
        // FIRST-PERSON COMBAT (S13). A BLOW THE GUARD CAUGHT: cool steel
        // instead of blood, and QUIETER than either flash above -- the guard
        // working is the calmest of the three moments, and step() fires this
        // INSTEAD of the taken-wash for a blocked blow, never as well, so
        // this stays inside the "two real moments, executed with restraint"
        // budget rather than stacking on top of it.
        const float blocked = blockPulse_.value();
        if (blocked > 0.0F) {
            target.fillRect(0, 0, target.width(), target.height(), Rgb{0.52F, 0.60F, 0.70F},
                            0.12F * blocked);
        }
    }

    HudState hud;
    hud.health = tavern_->playerHp();
    hud.healthMax = 100;
    // FATIGUE BUILD: the wind, in points, right beside the hit points it
    // stands under on the frame. The fade is the bar's own EasedToggle
    // (fatigueAnim_), mirroring showHealth's one rule without borrowing its
    // snap -- see the member's header.
    hud.fatigue = tavern_->playerFatigue().currentPoints();
    hud.fatigueMax = tavern_->playerFatigue().maxPoints();
    hud.fatigueFade = fatigueAnim_.value();
    hud.yawBam = body_->yaw();
    const std::string label = placeLabel();
    hud.locationLabel = label;
    hud.timeOfDaySeconds = timeOfDay_;
    hud.coin = tavern_->playerCoin();
    // While a conversation is open the bottom band belongs to the topic list,
    // so the room line and the running message stand down rather than draw on
    // top of it.
    // The casebook and the key list stand the HUD down exactly the way a
    // conversation does: all three are drawn in the same two bands, and two
    // things fighting over one row is how the centre-clear rule gets broken by
    // accident.
    //
    // #77 ADDS THE OPTIONS PAGE, and it was found by opening the real window and
    // pressing F2. Every other surface was on this list; the new one was not, so
    // the compass strip, the case row, the health bar and the first-run hint all
    // drew straight over the sliders and the sliders drew straight back, and
    // both were illegible. Nothing in a `--screenshot` capture would ever have
    // shown it, because nothing scripted opens this page.
    const bool conversing = conversingNow();
    // HARDENING PASS. roomLabel READS THE CACHE syncPanelAnim() JUST FILLED,
    // NOT A FRESH roomLine() CALL. The cache is what keeps a room label on
    // screen fading out after tavern_->playerInside() has already gone
    // false -- roomLine() itself would answer empty by then, same as every
    // other row below -- and roomFade is Session's own EasedToggle for this
    // row (roomAnim_), the identical pattern hud.alertFade already set for
    // the alert. See interactCache_'s own header in session.hpp.
    hud.roomLabel = std::string_view{roomCache_};
    hud.roomFade = roomAnim_.value();
    // The ward's opinion of you sits under the purse -- unless somebody is in
    // front of you, in which case THEIR opinion is the one that matters and the
    // panel is already showing it.
    //
    // ABSENCE DOES NOT COST A ROW. "NOBODY IN PARTICULAR" is the ward having no
    // opinion of you at all, and it held twenty characters of the top right in
    // every frame this game has ever produced, saying that nothing had
    // happened. The row appears the moment the ward HAS an opinion, which is
    // the only moment it is worth the sky it stands in.
    //
    // PLANNING SPRINT (item #2, the sweep). READS ITS OWN CACHE AND FADES,
    // THE IDENTICAL SHAPE roomLabel JUST USED ABOVE, instead of the bare
    // `conversing ? empty : text` this used to be. A real sweep of this file
    // found these three rows (standing, heat, the sack) still popping on and
    // off with `conversing` at full strength -- stealthLabel, right below,
    // already got this treatment; these three did not.
    hud.standingLabel = std::string_view{standingCache_};
    hud.standingFade = standingAnim_.value();
    // What the Watch has heard, what is in your coat, and whether you are
    // carrying somebody's bale. Top right under the purse, hugging the edge --
    // the centre of the frame stays empty, which is the rule.
    hud.heatLabel = std::string_view{heatCache_};
    hud.heatFade = heatAnim_.value();
    hud.stashLabel = std::string_view{stashCache_};
    hud.stashFade = stashAnim_.value();
    // FIRST-PERSON COMBAT (S13). What the hand is holding and whether the
    // guard is up -- each reading its own cache and fading the identical way
    // every row above does.
    hud.spellLabel = std::string_view{spellCache_};
    hud.spellFade = spellAnim_.value();
    hud.blockLabel = std::string_view{blockCache_};
    hud.blockFade = blockAnim_.value();
    // HELD-EFFECTS BUILD. The live holds, each reading its own cache and
    // fading on its own toggle -- the identical shape every row above uses.
    for (std::size_t slot = 0; slot < kEffectRows; ++slot) {
        hud.effectLabels[slot] = std::string_view{effectCaches_[slot]};
        hud.effectFades[slot] = effectAnims_[slot].value();
    }
    // SPELLS BUILD. The quick bar strip: names out of the cache
    // syncPanelAnim() keeps (so the fade-out still has labels), the selected
    // cell off the same quickSlot_ the number row moves, and the equipped
    // cell derived fresh from the ONE equipped id sim::Tavern holds -- the
    // same id spellLine() just read for the CAST row, so the two can never
    // disagree about what the hand is holding.
    for (std::size_t slot = 0; slot < quickBarNames_.size(); ++slot) {
        hud.quickSlots[slot] = std::string_view{quickBarNames_[slot]};
    }
    hud.quickSelected = quickSlot_;
    if (const sim::Spell* held = tavern_->equippedSpell(); held != nullptr) {
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::Spell* bound = tavern_->slotSpell(slot);
            if (bound != nullptr && bound->id == held->id) {
                hud.quickEquipped = slot;
                break;
            }
        }
    }
    hud.quickBarFade = quickBarAnim_.value();
    // S9. Whether the room can see you, and the lock under the wire. Both on
    // edges, both empty when they have nothing to say -- the right-hand stack
    // for the first, the bottom band for the second. Both read their own
    // cache and fade the identical way roomLabel just did, above.
    hud.stealthLabel = std::string_view{stealthCache_};
    hud.stealthFade = stealthAnim_.value();
    hud.lockLabel = std::string_view{lockCache_};
    hud.lockFade = lockAnim_.value();
    // #85. THE RESOLVED INTERACT VERB. "E  TALK" changing to "E  PICKPOCKET"
    // the instant the player crouches facing somebody -- Eli's own brief,
    // verbatim: "the player must SEE what pressing it will do before they
    // press it." interactCache_ already carries the composed "<key>  <label>"
    // row -- see syncPanelAnim()'s own note on why that composition moved
    // there instead of staying here.
    hud.interactLabel = std::string_view{interactCache_};
    hud.interactFade = interactAnim_.value();
    // The rung, and what the line wants next. Bottom-left, over the health bar.
    hud.guildLabel = std::string_view{guildCache_};
    hud.guildFade = guildAnim_.value();
    hud.objectiveLabel = std::string_view{objectiveCache_};
    hud.objectiveFade = objectiveAnim_.value();
    // S10. Where the case stands and where it wants you next: ONE row,
    // bottom-left, in the one slot of that stack provably outside the exclusion
    // rectangle. What the ward CALLS you for the work is not on the HUD at all
    // -- it is on the casebook's own page, because a title is something you
    // look up and not something you need every frame.
    //
    // ONE SLOT, TWO TENANTS -- AND polish-1 GAVE THEM A SLOT EACH.
    //
    // The case row used to sit at y - 16*scale off the health bar, which IS
    // height - margin - 23*scale, the same pixel row the alert has used since
    // S6; the first S10 capture shipped a clue printed straight through "CASE
    // 1/4 > ...", and the fix was to suppress the case row for the six seconds
    // a message is up. The bottom band hands out slots now (see BottomBand in
    // hud.cpp), the alert takes one before the case is offered one, and the two
    // of them can no longer be given the same pixels by arithmetic. So the
    // suppression is gone and both are shown.
    // And who put you on the floor last, which is the one thing on the HUD that
    // is about somebody else rather than about you.
    hud.rivalLabel = std::string_view{rivalCache_};
    hud.rivalFade = rivalAnim_.value();
    hud.showCompass = !conversing;
    // A bouncer's warning outranks anything the player did to themselves: it is
    // the one line in this game they must not miss.
    const bool warned = !tavern_->lastWarning().empty() &&
                        tavern_->playerStanding() != sim::Standing::Welcome;
    if (warned) {
        hud.alert = std::string_view{tavern_->lastWarning()};
    } else if (!conversing) {
        hud.alert = std::string_view{message_};
    }
    // TASK #83. Eased in step()/syncPanelAnim(), not here -- see
    // HudState::alertFade's own note. 1 when nothing has changed since
    // construction, which is the whole of what every caller before this field
    // existed drew.
    hud.alertFade = alertAnim_.value();
    // INNOVATION SPRINT ITEM #3. See HudState::alertPulse's own header.
    hud.alertPulse = alertPulse_.value();
    hud.caseLabel = std::string_view{caseCache_};
    hud.caseFade = caseAnim_.value();
    hud.showHealth = !conversing;
    // AND THE BOTTOM BAND IS THE TOPIC LIST'S, WHOLE. The alert used to be
    // drawn over it and S7 shipped the frame that proves it -- see
    // DialogueViewState::alert. It moves into the conversation's top band while
    // one is open, which is sized from what it draws, so a warning shouted
    // across the room is still read and nothing is drawn on top of anything.
    hud.showAlert = !conversing;
    // THE WARD MAP (core action #13) -- its own full-screen surface, drawn by
    // map_view.hpp's drawDistrictMap, exempt from the centre-clear rule for
    // exactly the reason the tiled Menu is (menu_view.hpp's own header):
    // there is nobody to look at while a map is up. The close tail draws too
    // (its own EasedToggle still above zero) unless another page has already
    // taken the frame -- conversingNow() is false only when nothing else is
    // up, so a map closed INTO the tiled Menu hands over immediately.
    if (districtMapOpen_ || (districtMapAnim_.value() > 0.0F && !conversing)) {
        DistrictMapState plan;
        plan.tiles = tiles_.get();
        plan.palette = &mapPalette_;
        plan.bounds = mapBounds_;
        plan.playerX = static_cast<float>(body_->x()) / static_cast<float>(sim::kSubOne);
        plan.playerY = static_cast<float>(body_->y()) / static_cast<float>(sim::kSubOne);
        plan.band = body_->band();
        plan.yawBam = body_->yaw();
        plan.title = label;
        if (warned) {
            // The bouncer's warning outranks a map read -- the same routing
            // the tiled Menu gives its journal tile, below.
            plan.alert = tavern_->lastWarning();
        }
        plan.openAmount = districtMapAnim_.value();
        if (config_.hud) {
            drawDistrictMap(target, plan);
            drawHud(target, hud);
        }
        return stats;
    }
    // MORROWIND ROUND: THE TILED MENU IS A DIFFERENT SURFACE FROM THE SINGLE
    // CONVERSATION PANEL, drawn by a different function (menu_view.hpp's
    // drawMenuTiles rather than dialogue_view.hpp's drawDialogue) because it
    // deliberately does NOT respect the HUD's centre-clear rule the way every
    // other overlay in this build still does -- see menu_view.hpp's own
    // header on why a Morrowind-style overview is exempt and a live
    // conversation is not.
    if (casebookOpen_) {
        MenuTileState tiles;
        tiles.open = true;
        tiles.character = characterPanelView();
        tiles.map = mapPanelView();
        tiles.letters = lettersPanelView();
        tiles.journal = journalPanelView();
        if (warned) {
            // THE BOUNCER'S WARNING, ROUTED INTO THE JOURNAL TILE. The tiled
            // Menu has no single top band of its own to carry it the way a
            // conversation or one of #85's six pages did -- see
            // DialogueViewState::alert's own header -- so it lands on the
            // one tile that is always on screen regardless of focus and
            // already reads as "your own notes", the same place a warning
            // interrupted a casebook read before this round.
            tiles.journal.alert = tavern_->lastWarning();
        }
        tiles.focus = menuFocus_;
        tiles.phase = phase;
        // TASK #83's OWN EASE, REUSED. See the identical note on the
        // single-panel path below.
        tiles.openAmount = panelAnim_.value();
        // INNOVATION SPRINT ITEM #2. See MenuTileState::characterFocus's own
        // header -- each tile's border eases toward or away from focus off
        // its own EasedToggle rather than snapping the instant menuFocus_
        // changes.
        tiles.characterFocus = characterFocusAnim_.value();
        tiles.mapFocus = mapFocusAnim_.value();
        tiles.lettersFocus = lettersFocusAnim_.value();
        tiles.journalFocus = journalFocusAnim_.value();
        if (config_.hud) {
            drawMenuTiles(target, tiles);
            drawHud(target, hud);
        }
        return stats;
    }
    DialogueViewState panel = dialogueView();
    if (conversing && warned) {
        panel.alert = tavern_->lastWarning();
    }
    // TASK #83. THE PANEL EASES OPEN AND CLOSED INSTEAD OF POPPING.
    // panelAnim_.value() is what drawDialogue actually fades against -- see
    // DialogueViewState::openAmount. `panel.open` is ALSO forced true for as
    // long as any of that fade is still on screen, because dialogueView()
    // reports the panel closed the instant the LAST overlay flag goes false,
    // and a close animation needs a few more frames of "yes, still drawing"
    // after that to have anything left to fade. Once panelAnim_ settles at 0
    // this is exactly `panel.open` again, which is the pre-existing behaviour
    // for every caller and every test that never heard of this pass.
    //
    // MORROWIND ROUND: THIS ALSO COVERS THE TILED MENU'S OWN CLOSING TAIL.
    // The moment casebookOpen_ goes false this branch is what runs, and
    // dialogueView() falls through to an empty, closed state -- so a Menu
    // that was just closed fades out as an (empty) single panel for its last
    // few frames rather than as the four tiles it was a moment before. That
    // is the SAME pre-existing behaviour this build already had for closing
    // Keys, Options or a conversation (dialogueView() has never reconstructed
    // "what was open a frame ago" for a close tail), not a new gap the
    // Morrowind round introduced.
    panel.openAmount = panelAnim_.value();
    panel.open = panel.open || panelAnim_.value() > 0.0F;
    // The panel FIRST, the HUD over it: a bouncer's warning has to survive
    // being told mid-conversation, and it is the one line that outranks a menu.
    if (config_.hud) {
        drawDialogue(target, panel);
        drawHud(target, hud);
    }
    return stats;
}

namespace {

/// Steers the body one movement step toward a Q8 point, faced and collided
/// against exactly the geometry a player walks into. True once it has arrived.
[[nodiscard]] bool stepToward(Session& session, std::int32_t goalX, std::int32_t goalY) {
    const std::int32_t dx = goalX - session.body().x();
    const std::int32_t dy = goalY - session.body().y();
    // A QUARTER of a tile of slop, not a half. Half a tile leaves the eye
    // pressed against the next cell's face, and a capture framed from there is
    // a photograph of a wall -- which is what the first version of this made.
    //
    // AN EIGHTH, still: this walker sets MoveInput::snapVelocity, so it has no
    // momentum to glide past its own goal on and the S5 framing argument above
    // stands unchanged.
    const std::int32_t tolerance = sim::kSubOne / 8;
    const bool closeX = dx > -tolerance && dx < tolerance;
    const bool closeY = dy > -tolerance && dy < tolerance;
    if (closeX && closeY) {
        return true;
    }
    const sim::Angle eastWest = dx > 0 ? sim::kFacingEast : sim::kFacingWest;
    const sim::Angle northSouth = dy > 0 ? sim::kFacingSouth : sim::kFacingNorth;
    // The axis with more ground left to cover goes first, then the other, then
    // either perpendicular -- which is enough to get round one table.
    const bool xFirst = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
    const sim::Angle tries[4] = {
        xFirst ? eastWest : northSouth,
        xFirst ? northSouth : eastWest,
        xFirst ? sim::kFacingNorth : sim::kFacingEast,
        xFirst ? sim::kFacingSouth : sim::kFacingWest,
    };
    for (const sim::Angle facing : tries) {
        if ((facing == eastWest && closeX) || (facing == northSouth && closeY)) {
            continue;
        }
        session.body().setYaw(facing);
        const std::int32_t beforeX = session.body().x();
        const std::int32_t beforeY = session.body().y();
        sim::MoveInput input;
        input.forward = 1;
        // #77. THE CAPTURE SCRIPT MUST NOT CLIMB. This walker STEERS BY WALKING
        // INTO WALLS -- it tries each compass direction in turn and reads "did
        // the body move" as "was that way open" -- so with contextual traversal
        // on it would haul itself up the first warehouse it probed and
        // photograph the wrong district from the wrong height. A player walking
        // at a ledge means "get me up there"; this loop means "is there a wall".
        input.autoTraverse = false;
        // AND IT HAS NO LEGS EITHER. Same reason and the same sentence: this
        // loop reads "did the body move" as "is that way open", and momentum
        // from the last direction would answer for the next one. See
        // MoveInput::snapVelocity.
        input.snapVelocity = true;
        session.step(input);
        if (session.body().x() != beforeX || session.body().y() != beforeY) {
            return false;
        }
    }
    return false;
}

/// Greedy, and only the last few Q8 units of a walk. Gives up rather than
/// grinding.
void walkStraightTo(Session& session, std::int32_t tileX, std::int32_t tileY) {
    const std::int32_t goalX = sim::q8_tile_centre(tileX);
    const std::int32_t goalY = sim::q8_tile_centre(tileY);
    std::int32_t stuckFor = 0;
    for (int guard = 0; guard < 600; ++guard) {
        const std::int32_t beforeX = session.body().x();
        const std::int32_t beforeY = session.body().y();
        if (stepToward(session, goalX, goalY)) {
            return;
        }
        if (session.body().x() == beforeX && session.body().y() == beforeY) {
            if (++stuckFor > 4) {
                return;
            }
        } else {
            stuckFor = 0;
        }
    }
}

/// Presses the up-key and then WATCHES the jump. A mantle resolves under the
/// hand; a leap arms an arc and the ordinary step pump flies it, so a scripted
/// capture spends the same twenty-four movement steps in the air a player does
/// rather than arriving instantly. See Session::climb on why the press stopped
/// draining the arc itself.
void climbAndLand(Session& session) {
    session.climb();
    (void)session.flyOutLeap();
}

/// Walks the body to a tile with REAL movement steps, along a route THE ROOM'S
/// OWN PATHFINDER produced.
///
/// S5 REPLACED WHAT WAS HERE, and the S4 review is why. The old version was a
/// greedy step-toward-the-goal walk, and its own comment said it was
/// "deliberately NOT a pathfinder" because "the room already has one
/// (RegionPath) and it belongs to the actors". Right instinct, wrong
/// conclusion: it did not avoid reimplementing RegionPath, it reimplemented a
/// worse one -- and it could not get from the authored spawn on the Tarwalk
/// through the Gull's door to Father Maell. So `--flame` worked only from
/// `--spawn=150,74,19`, already inside the room; run as the README documented
/// it, it walked into a wall, photographed a conversation with the wrong
/// person, and exited 0.
///
/// It USES the room's pathfinder now. RegionPath is a breadth-first search over
/// standable tiles inside a box, and gull::kRegion is the building plus the
/// street in front of it -- every tile a capture of this house needs. The body
/// still WALKS: each waypoint is steered to with ordinary movement steps
/// through ordinary collision, so a captured frame is still a picture of a body
/// that got there on its feet.
/// The box the capture harness routes inside: the Gull, both its floors, and
/// enough of the Tarwalk to contain the AUTHORED SPAWN.
///
/// gull::kRegion stops at kStreetY - 2, which is y=61, and the S2 spawn was at
/// y=60. One tile short. A router whose box does not contain the body's own
/// cell refuses outright, so every walk that began at the spawn fell through to
/// the greedy fallback -- which is the S4 behaviour this was supposed to
/// replace, and it is why `--skyrun` reached every patron in the room and never
/// once reached the man in the snug.
///
/// #79 MOVED THE SPAWN AGAIN, so the bound is a min() rather than a subtraction
/// off it. `kSpawnTileY - 2` happened to be right while the spawn was north of
/// the street; it is arithmetic that only works in one direction, and a spawn
/// SOUTH of kStreetY would have quietly cropped the street back out of the box.
/// Both ends are named, so the box contains the frontage and the body wherever
/// the opening shot is aimed next.
constexpr sim::TileBox kCaptureRegion{
    sim::gull::kFootprintX0 - 2,
    std::min(sim::gull::kStreetY - 2, sim::docks::kSpawnTileY - 1),
    sim::gull::kGroundBand,
    sim::gull::kFootprintX1 + 2, sim::gull::kFootprintY1 + 1, sim::gull::kUpperBand};
static_assert(kCaptureRegion.x0 <= sim::docks::kSpawnTileX &&
                  sim::docks::kSpawnTileX <= kCaptureRegion.x1 &&
                  kCaptureRegion.y0 <= sim::docks::kSpawnTileY &&
                  sim::docks::kSpawnTileY <= kCaptureRegion.y1,
              "the capture router's box must contain the spawn, or every scripted "
              "line falls through to the greedy fallback -- see the note above");

void walkToTile(Session& session, std::int32_t tileX, std::int32_t tileY) {
    sim::RegionPath router(session.tiles(), kCaptureRegion);
    std::vector<sim::PathStep> route;
    const sim::PathStep from{session.body().tileX(), session.body().tileY(),
                             session.body().band()};
    const sim::PathStep to{tileX, tileY, session.body().band()};
    if (router.find(from, to, route)) {
        for (const sim::PathStep& waypoint : route) {
            const std::int32_t wx = sim::q8_tile_centre(waypoint.x);
            const std::int32_t wy = sim::q8_tile_centre(waypoint.y);
            bool arrived = false;
            for (int guard = 0; guard < 120 && !arrived; ++guard) {
                arrived = stepToward(session, wx, wy);
            }
            if (!arrived) {
                break;
            }
        }
    }
    // Whatever the route left, and the whole walk when the router refused --
    // which is what happens for a goal outside the box, the roof being the
    // obvious one.
    walkStraightTo(session, tileX, tileY);
}

/// The index of the first topic of this kind, or -1.
[[nodiscard]] int topicOfKind(const Session& session, sim::TopicKind kind) {
    const std::vector<sim::Topic>& topics = session.tavern().dialogue().topics();
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == kind) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// The present actor of that name, or nullptr.
[[nodiscard]] const sim::Actor* actorNamed(const Session& session, std::string_view name) {
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.present() && actor.name() == name) {
            return &actor;
        }
    }
    return nullptr;
}

/// Walks to a named person and opens a conversation with them.
///
/// Onto their OWN tile, and that is not laziness. "Who answers" is the nearest
/// body within two tiles with ties broken on the lower id, and the Gull's cast
/// stand shoulder to shoulder: from the tile south of Captain Wake, Edda
/// Pierpont is exactly as near and has the lower id, so a capture aiming at the
/// captain would quietly photograph her instead. Distance zero has no tie to
/// break. The frame is composed afterwards, by stepping back off them.
[[nodiscard]] bool speakTo(Session& session, std::string_view name) {
    const sim::Actor* who = actorNamed(session, name);
    if (who == nullptr) {
        return false;
    }
    walkToTile(session, who->tileX(), who->tileY());
    session.closeConversation();
    session.interact();
    return session.talking() && session.tavern().dialogue().speaker().name == name;
}

/// Steps back off somebody and turns to look at them, so the captured frame has
/// a person in it rather than the inside of their coat.
void standBackFrom(Session& session, std::string_view name) {
    const sim::Actor* who = actorNamed(session, name);
    if (who == nullptr) {
        return;
    }
    // A spot the body can stand in AND see them from. Asked of the same two
    // functions the simulation asks -- standable() and lineOfSight() -- because
    // a capture that framed itself inside a table would be a picture of the
    // inside of a table, and the first version of this was.
    const std::int32_t band = who->band();
    const std::int32_t offsets[6][2] = {{0, -3}, {0, 3}, {-3, 0}, {3, 0}, {0, -2}, {2, 0}};
    for (const auto& offset : offsets) {
        const std::int32_t x = who->tileX() + offset[0];
        const std::int32_t y = who->tileY() + offset[1];
        if (!session.tiles().standable(x, y, band) ||
            !session.tiles().lineOfSight(x, y, who->tileX(), who->tileY(), band)) {
            continue;
        }
        walkToTile(session, x, y);
        if (session.body().tileX() != x || session.body().tileY() != y) {
            continue;
        }
        const std::int32_t dx = who->tileX() - x;
        const std::int32_t dy = who->tileY() - y;
        if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
            session.body().setYaw(dx > 0 ? sim::kFacingEast : sim::kFacingWest);
        } else {
            session.body().setYaw(dy > 0 ? sim::kFacingSouth : sim::kFacingNorth);
        }
        return;
    }
}

/// Picks the first topic of a kind, if it is on the list.
bool pick(Session& session, sim::TopicKind kind) {
    const int at = topicOfKind(session, kind);
    if (at < 0) {
        return false;
    }
    session.chooseTopic(static_cast<std::size_t>(at));
    return true;
}

/// THE SCRIPTED PLAYTHROUGH the sprint is judged on, driven through exactly the
/// calls a keypress makes: walk to Father Maell, take the oath, stand three
/// people a drink, turn the night pot in, ask Captain Wake about the water,
/// bring it back, be taught a crafting, and compose one.
///
/// It reports how many stages actually landed rather than asserting anything --
/// the assertions live in the test suite, where a red is a red. This is the
/// path that produces a PICTURE of it.
[[nodiscard]] int runFlameLine(Session& session, const std::string& ending) {
    const sim::DialogueDirector& talk = session.tavern().dialogue();
    const std::string questId = "flame-disciple";

    // 1. the oath, at Maell's evening table
    if (speakTo(session, "Father Maell")) {
        pick(session, sim::TopicKind::Join);
    }
    // 2. the night pot: three drinks out of the player's own purse, then turned
    //    in to the priest.
    for (int i = 0; i < 3; ++i) {
        if (session.talking()) {
            pick(session, sim::TopicKind::BuyDrinkFor);
        }
    }
    if (session.talking()) {
        pick(session, sim::TopicKind::QuestBeat);
    }
    // 3. the captain, who was out past the fishbone the night of it.
    if (speakTo(session, "Captain Ivo Wake")) {
        pick(session, sim::TopicKind::QuestBeat);
    }
    // 4. back to the priest with it, 5. be taught, 6. compose.
    if (speakTo(session, "Father Maell")) {
        pick(session, sim::TopicKind::QuestBeat);
        pick(session, sim::TopicKind::Learn);
        // 7. Keep sitting with him. Every crafting off the shallow shelf is
        //    worth two uses of linkcraft, and the Mission's third rung is
        //    measured in exactly that: the workshop opens to somebody who has
        //    learned everything the public edition can teach. Bounded, because
        //    the topic stays on the list after there is nothing left to hand
        //    over and answers with the authored teaching.beyond line.
        for (int i = 0; i < 16; ++i) {
            if (!pick(session, sim::TopicKind::Learn)) {
                break;
            }
        }
        for (int i = 0; i < 4; ++i) {
            if (!pick(session, sim::TopicKind::Advance)) {
                break;
            }
        }
        if (pick(session, sim::TopicKind::Forge)) {
            // The smallest legal composition a novice can hold: one point of
            // vitality across a bridged link, which is the bench's own opening
            // shape. Committed with the same call the ENTER key makes.
            session.commitForge();
        }
    }
    // Compose the shot: back off the priest, looking at him, with whatever he
    // last said still on the panel.
    if (ending == "bench") {
        // The workshop an Acolyte's rung opened, standing open. Nothing is
        // committed: this is the bench mid-composition, which is the thing
        // worth photographing.
        pick(session, sim::TopicKind::Forge);
    } else if (ending == "away") {
        session.closeConversation();
    }
    standBackFrom(session, "Father Maell");
    return talk.journal().stagesDone(questId);
}

/// HELD-EFFECTS BUILD. Equips one crafting BY ID -- through the same
/// grimoire-order door the number row uses -- and presses Cast until its
/// link opens, waiting out fizzle and success cooldowns through real steps.
/// Returns true when the crafting's hold is genuinely live on the player at
/// the end, which is the thing the capture exists to photograph.
[[nodiscard]] bool runHeldCast(Session& session, const std::string& spellId) {
    // The index is looked up fresh: ids sort ascending in the grimoire, so an
    // index is only good the moment it is asked for.
    const std::vector<sim::Spell>& spells = session.tavern().dialogue().grimoire().spells();
    std::int32_t index = -1;
    for (std::size_t i = 0; i < spells.size(); ++i) {
        if (spells[i].id == spellId) {
            index = static_cast<std::int32_t>(i);
            break;
        }
    }
    if (index < 0 || !session.tavern().equipSpellAt(index)) {
        return false;
    }
    const auto held = [&]() {
        for (const sim::Tavern::ActiveHold& hold : session.tavern().heldEffects()) {
            if (hold.spellId == spellId) {
                return true;
            }
        }
        return false;
    };
    for (int attempt = 0; attempt < 12 && !held(); ++attempt) {
        // Wait out whatever the last press left cooling -- a prior crafting's
        // recovery, or this one's own fizzle -- through real steps, bounded so
        // a pathological clock cannot hang the capture.
        for (int waited = 0; session.tavern().castCooldownLeft() > 0 && waited < 600;
             ++waited) {
            session.stepMany(sim::MoveInput{}, 60);
        }
        session.castEquipped();
    }
    return held();
}

/// UP ONTO THE LEAD. In at the door, up the stair, across the guest floor to
/// the north wall, and over it -- the burglar's own route, and the only one
/// there is: the Gull is two storeys, the street cannot climb two storeys, and
/// a body gets onto the roof of the ward's grandest house by renting a bed
/// under it. Every move here is a Session call a keypress makes.
///
/// Returns how many of the four beats landed, so the caller can fail rather
/// than photograph a body still standing in the taproom.
[[nodiscard]] int runRoofLine(Session& session, const std::string& ending) {
    int landed = 0;

    // 1. through the door and to the foot of the stair.
    walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    if (session.body().tileX() == sim::gull::kStairX &&
        session.body().tileY() == sim::gull::kStairY) {
        ++landed;
    }

    // 2. up it, with the up-key -- see PlayerBody::mantle on why a stair in
    //    this build needs a verb and why walking north off it does nothing.
    climbAndLand(session);
    if (session.body().band() == sim::gull::kUpperBand) {
        ++landed;
    }

    // 3. to the north wall of the guest floor, and over it.
    walkToTile(session, 150, sim::gull::kFootprintY0 + 1);
    session.body().setYaw(sim::kFacingNorth);
    climbAndLand(session);
    if (session.body().band() == sim::gull::kRoofBand) {
        ++landed;
    }

    // 4. out onto the lead and turn to look back down the Tarwalk. South-west,
    //    because that is where the district is: the quay, the frontage opposite
    //    and the whole run of the street under the eye.
    walkToTile(session, sim::gull::kFootprintX0 + 2, sim::gull::kFootprintY0 + 2);
    if (session.body().band() == sim::gull::kRoofBand) {
        ++landed;
    }

    if (ending == "leap") {
        // West, over the two tiles of air between this house and the next.
        walkToTile(session, sim::gull::kFootprintX0, 70);
        session.body().setYaw(sim::kFacingWest);
        climbAndLand(session);
        session.body().setPitch(sim::angle_from_degrees(-10));
    } else if (ending == "street") {
        walkToTile(session, sim::gull::kFootprintX0, 70);
        session.body().setYaw(sim::kFacingWest);
        session.dropDown();
    } else {
        // North-west off the corner of the lead: the Gull's own roof in the
        // foreground, the next house's across two tiles of alley, and the
        // Tarwalk, the piers and the harbour under the fog beyond it. This is
        // the view the sprint is FOR -- the ward seen from where a Trojian has
        // no business being.
        session.body().setYaw(sim::angle_from_degrees(315));
        session.body().setPitch(sim::angle_from_degrees(-8));
    }
    return landed;
}

/// Stands in front of somebody and turns in whatever beat is owed.
void reportTo(Session& session, std::string_view who) {
    if (speakTo(session, who)) {
        pick(session, sim::TopicKind::QuestBeat);
        session.closeConversation();
    }
}

/// In at the door, up the stair, across the guest floor, and over the north
/// wall onto the lead.
void upOntoTheLead(Session& session) {
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    climbAndLand(session);
    walkToTile(session, 150, sim::gull::kFootprintY0 + 1);
    session.body().setYaw(sim::kFacingNorth);
    climbAndLand(session);
}

/// Off the Gull's west edge into the alley -- two storeys, which a Tenant of
/// the roofs lands without hurting himself -- and back in at the door.
void downFromTheLead(Session& session) {
    walkToTile(session, sim::gull::kFootprintX0, 70);
    session.body().setYaw(sim::kFacingWest);
    session.dropDown();
    walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
}

/// Back down the flight, for when the body is on the guest floor rather than
/// the roof.
void comeDownstairs(Session& session) {
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    session.dropDown();
}

/// THE SKYRUNNER LINE, played the way a player plays it and nothing reaching
/// into the simulation sideways. Sign on with Finch in the snug, take two
/// purses, crack a box above the stair, get on the roof, cross the alley, sell
/// what was taken, lean on somebody, and run a bale out past the Watch.
/// THE BOUNTY, played the way a player plays it: take it off the Watch, get the
/// Flame's mark before the priest goes home, wait for the room to quiet, and
/// bring back what the ward asked for.
///
/// Six beats, and every one of them is a Session call a keypress makes -- the
/// walk is real movement through real collision, the punches are the punch key
/// and the skinning is the same G that opens a strongbox.
[[nodiscard]] int runContractLine(Session& session, const std::string& ending) {
    sim::DialogueDirector& talk = session.tavern().dialogue();
    int landed = 0;
    std::int32_t job = -1;
    std::int32_t wanted = 0;

    // 1. ACCEPT. The ward's bounty is public work: no rung and no oath.
    if (speakTo(session, "Watchman Cull")) {
        const std::vector<sim::Topic>& topics = talk.topics();
        for (std::size_t i = 0; i < topics.size(); ++i) {
            if (topics[i].kind != sim::TopicKind::TakeContract || topics[i].payload < 0) {
                continue;
            }
            const sim::Contract* row = talk.contracts().find(topics[i].payload);
            if (row == nullptr || row->good != sim::Contraband::Scalp) {
                continue;
            }
            job = row->id;
            wanted = row->units;
            session.chooseTopic(i);
            break;
        }
        session.closeConversation();
    }
    if (job >= 0 && talk.contracts().find(job) != nullptr && talk.contracts().find(job)->live()) {
        ++landed;
    }

    // 2. THE MARK. A scalp is redeemed under the Flame's own sanction, and the
    //    priest keeps an evening hour that ends at half past nine.
    if (speakTo(session, "Father Maell")) {
        (void)pick(session, sim::TopicKind::Sanction);
        session.closeConversation();
    }
    if (job >= 0 && talk.contracts().find(job) != nullptr &&
        talk.contracts().find(job)->sanctioned) {
        ++landed;
    }

    // SHEETS BUILD: `held` STOPS HERE, WITH THE JOB STILL IN HAND. The full
    // line ends with the contract PAID, which is exactly the one state the
    // Journal tile's live-contract rows have nothing to show for -- so a
    // capture of those rows needs the line to stop while the work is still
    // owed. Two beats (taken, sanctioned), both already counted above;
    // runSmoke() asks for two instead of six when this ending is picked, so
    // a held run that landed both does not read as a line that fell short.
    if (ending == "held") {
        session.closeConversation();
        return landed;
    }

    // 3. THE HOUR. Eleven at night: the late crowd has thinned and the skirting
    //    is busy.
    session.skipToHour(23);
    if (session.tavern().verminPresent() > 0) {
        ++landed;
    }

    // 4. THE WORK. A fist, and then a knife.
    std::int32_t taken = 0;
    for (int attempt = 0; attempt < 40 && taken < wanted; ++attempt) {
        std::int32_t ratId = -1;
        std::int32_t ratX = 0;
        std::int32_t ratY = 0;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.role() == sim::ActorRole::Vermin && actor.present() &&
                actor.activity() != sim::Activity::Downed) {
                ratId = actor.id();
                ratX = actor.tileX();
                ratY = actor.tileY();
                break;
            }
        }
        if (ratId < 0) {
            break;
        }
        walkToTile(session, ratX, ratY);
        for (int swing = 0; swing < 12; ++swing) {
            const sim::Actor* still = session.tavern().actorById(ratId);
            if (still == nullptr || still->activity() == sim::Activity::Downed) {
                break;
            }
            session.punch();
            session.stepMany(sim::MoveInput{}, 2);
        }
        const sim::Actor* down = session.tavern().actorById(ratId);
        if (down == nullptr || down->activity() != sim::Activity::Downed) {
            // It moved off before the fist landed. Try the next one.
            continue;
        }
        walkToTile(session, down->tileX(), down->tileY());
        session.steal();
        const std::int32_t held = talk.crimes().stash().count(sim::Contraband::Scalp);
        if (held > taken) {
            taken = held;
        }
    }
    if (wanted > 0 && taken >= wanted) {
        ++landed;
    }

    // 5. GET PAID, over the same table it was taken across.
    if (speakTo(session, "Watchman Cull")) {
        (void)pick(session, sim::TopicKind::TurnIn);
    }
    if (job >= 0 && talk.contracts().find(job) != nullptr &&
        talk.contracts().find(job)->state == sim::ContractState::Paid) {
        ++landed;
    }
    // 6. And the ward paid for it.
    if (talk.contracts().coinEarned() > 0) {
        ++landed;
    }

    if (ending == "away") {
        session.closeConversation();
    }
    standBackFrom(session, "Watchman Cull");
    return landed;
}

/// How many beats runContractLine tries to land.
constexpr std::int32_t kContractBeats = 6;
/// And how many the `held` ending stops after -- taken and sanctioned, the
/// job still live. See the ending's own note inside runContractLine.
constexpr std::int32_t kContractHeldBeats = 2;

/// S8. THE NEMESIS ARC, PLAYED: pick a fight with a named labourer, lose it,
/// wake up on the quay, walk back in the next evening and lose it twice more.
///
/// EVERY BEAT IS A SESSION CALL A KEYPRESS MAKES. The walk is real movement
/// through real collision, the fight is the punch key and then the room
/// resolving a brawl a second at a time, and the respawn is the same
/// settleDefeat() the client's own step loop reaches. Nothing here reaches into
/// the simulation sideways -- Tavern::concedeTo exists and is deliberately NOT
/// used, because a scripted proof that skipped the fight would be a proof about
/// a function rather than about the game.
[[nodiscard]] int runNemesisLine(Session& session, const std::string& ending) {
    // TARN WRENHALE, "Two-Loads": a docker on the evening shift, fists, no
    // rung, nobody's rival. Eli's own example is "killed by a laborer in a fist
    // fight", and this is the labourer.
    constexpr std::string_view kMark = "Tarn Wrenhale";
    int landed = 0;

    const sim::Actor* mark = actorNamed(session, kMark);
    if (mark == nullptr) {
        return landed;
    }
    const std::int32_t id = mark->id();

    // 1. HE IS NOBODY. The proof is worth nothing without the before.
    if (session.tavern().nemesis().of(id) == nullptr && mark->weapon() == sim::Weapon::Fists) {
        ++landed;
    }

    // A round of the real thing: walk up to him, swing, and let the room
    // resolve it a second at a time. The loop waits for BOTH his win and the
    // player being back on their feet -- Session::step() finds the release flag
    // itself, so a loop that stopped at the win would walk into the next round
    // with the player still on the boards.
    const auto pickAFight = [&session, id]() {
        const sim::Actor* him = session.tavern().actorById(id);
        if (him == nullptr || !him->present()) {
            return false;
        }
        const sim::Nemesis* before = session.tavern().nemesis().of(id);
        const std::int32_t had = before == nullptr ? 0 : before->wins;
        walkToTile(session, him->tileX(), him->tileY());
        session.closeConversation();
        session.punch();
        for (int second = 0; second < 300; ++second) {
            session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
            const sim::Nemesis* now = session.tavern().nemesis().of(id);
            if (now != nullptr && now->wins > had && !session.tavern().playerFloored()) {
                return true;
            }
        }
        return false;
    };

    // 2. A FIST FIGHT IN A TAPROOM, LOST. Nothing about this beat is staged:
    //    the walk is real movement through real collision, the punch is the
    //    punch key, and the beating is the ordinary brawl the door policy has
    //    been resolving since S2.
    if (pickAFight()) {
        ++landed;
    }
    session.skipToHour(20);

    // 3. AND THE REMATCH IS NOT THE WORLD'S BUSINESS ANY MORE.
    //
    //    THIS IS THE RULE WORKING, NOT A FAILURE, and it is the most
    //    interesting thing the arc found. A nemesis MEANS IT from his first win
    //    on (nemesisIntent), and brawl.hpp's third clause says beating a
    //    BLOODIED man while meaning him Harm is not a bar fight whatever is in
    //    your hands. So the rematch opens as a brawl, and the moment he has the
    //    player under a quarter of their health it escalates and the room stops
    //    resolving it -- exactly as it has since S2, out loud.
    session.tavern().clearEscalation();
    (void)pickAFight();
    if (session.tavern().escalated() && !session.tavern().playerFloored()) {
        ++landed;
    }
    session.skipToHour(20);

    // 4/5. WHICH MEANS THE REST OF THE ARC BELONGS TO THE COMBAT SCREEN.
    //
    //      VERIFICATION GAP (S8): docs/design/COMBAT-SCREEN-SPEC.md's dedicated
    //      first-person screen does not exist, so the two defeats that finish
    //      the rise are taken through Tavern::concedeTo -- which is the seam
    //      that screen will call when it has one, and which goes through
    //      exactly the same applyDefeat every in-world beating does. It is
    //      named here rather than hidden: beats 2 and 3 above are the game;
    //      these two are the game's own admission that it is one screen short.
    for (int more = 0; more < 2; ++more) {
        const sim::Actor* him = session.tavern().actorById(id);
        if (him == nullptr) {
            break;
        }
        walkToTile(session, him->tileX(), him->tileY());
        session.tavern().concedeTo(id);
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        session.skipToHour(20);
        const sim::Nemesis* now = session.tavern().nemesis().of(id);
        if (now != nullptr && now->wins == more + 2) {
            ++landed;
        }
    }

    // 6. A house with members in it, and 7. ground on the ward's own roll --
    //    and none of it came off when the player got up.
    const sim::Nemesis* risen = session.tavern().nemesis().of(id);
    if (risen != nullptr && risen->foundedAHouse() && !risen->members.empty()) {
        ++landed;
    }
    if (risen != nullptr && risen->holdsGround()) {
        ++landed;
    }

    if (ending == "talk") {
        // RETRIED, because his own post has a neighbour. speakTo opens on
        // whoever is NEAREST, and Tarn Wrenhale's stool and Wick Hempson's are
        // one tile apart -- so the first attempt can photograph the wrong man,
        // which is exactly the class of thing the S4 review caught in the
        // Priest of the Flame's capture. speakTo already answers whether the
        // person who spoke is the person asked for; this believes it.
        for (int attempt = 0; attempt < 6; ++attempt) {
            if (speakTo(session, kMark)) {
                break;
            }
            session.closeConversation();
            // He is hunting by now, so standing still closes the gap for you.
            session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        }
    } else {
        session.closeConversation();
        standBackFrom(session, kMark);
    }
    return landed;
}

/// How many beats runNemesisLine tries to land: he was nobody, a fist fight
/// lost in the world, a rematch the room refuses, two more defeats through the
/// combat screen's seam, a house, and a charge on the roll.
constexpr std::int32_t kNemesisBeats = 7;

// ---------------------------------------------------------------------------
// S9: the burglary, played
// ---------------------------------------------------------------------------

/// How many beats runBurgleLine tries to land: crouch, stand in a dark doorway
/// unseen, a hand in a coat, up the stair, wire into a guest's box, the lock
/// open, and the box emptied.
constexpr std::int32_t kBurgleBeats = 7;

/// Plays a burglary end to end, through the same Session calls a keypress
/// makes: down on the haunches, across the floor, a hand in a coat, up the
/// stair, and the wire into a guest's strongbox.
///
/// EVERY BEAT IS A KEY. Nothing here reaches into the simulation sideways --
/// toggleCrouch, lift, climb, steal, movePick and probeLock are exactly what C,
/// T, SPACE, G, W/S and SPACE do -- which is the only thing that makes a
/// captured frame evidence rather than a diagram.
/// Which beats of the last burglary landed, one bit each, in order. Printed in
/// the summary so a short run says WHICH beat it dropped rather than only how
/// many -- the S4 review's whole complaint about scripted lines that report a
/// number and nothing else.
std::int32_t gBurgleBeatMask = 0;
/// And how many people were awake, upright, on this floor and in range when
/// beat 2 was judged. Zero means the beat proved nothing -- see the note there.
std::int32_t gBurgleWatchers = 0;
/// Probes made on the SECOND box by `--burgle=lock`, so the case that proves
/// that frame is a live attempt can say so with a number instead of a picture.
std::int32_t gLockEndingProbes = 0;

/// WORKS THE LOCK UNDER THE WIRE WITH WHAT A PLAYER HAS, AND NOTHING ELSE.
///
/// The S9 review's fifth finding, verbatim: "there is no test and no scripted
/// run anywhere in which a lock is picked open without foreknowledge of its
/// pins." Every case that opened one cleanly called `pinDepth()` first and
/// drove the pick straight to the answer; every shipped `--burgle` ended
/// `jammed=4 forced=4`. That is not a minigame with a hard tuning, it is a
/// minigame with no win condition on the board.
///
/// So: this is THE STRATEGY, written as a player would play it. It may look at
/// exactly four things, all of them on screen in the HUD's own lock row --
/// where the pick is being held, how many pins have dropped, how much strain is
/// on the wire, and what the last probe felt like. It never calls pinDepth, it
/// never touches the Lock, and it never reads the seed. Give it a hand with no
/// feel and it sweeps the track, which is all an apprentice can do; give it a
/// hand at kFeelLevel and it bisects, which is what the feel is FOR.
///
/// `stopAtPins` lets a capture halt mid-attempt so the shutter catches the
/// surface with pins down and the wire still in. Negative works it to the end.
/// Returns the number of probes made.
int workTheWire(Session& session, std::int32_t stopAtPins = -1,
                std::int32_t maxProbes = -1) {
    // The window the pin is known to be inside, in depth notches. Reset every
    // time a pin drops or a pick snaps, because both mean the wire is now on a
    // pin this strategy knows nothing about.
    std::int32_t low = 0;
    std::int32_t high = sim::kPinDepths - 1;
    // Which notches have already been ruled out for the pin under the wire. A
    // player's memory of the last few seconds, and nothing more.
    std::uint32_t tried = 0;
    const auto forget = [&]() {
        low = 0;
        high = sim::kPinDepths - 1;
        tried = 0;
    };
    std::int32_t pinsSeen = session.picking() ? session.lockpicking().pinsSet() : 0;
    int probes = 0;
    // A generous guard. A bisect finishes in four probes a pin; a blind sweep
    // takes nine and breaks wire doing it, and the roll runs out long before
    // this does.
    for (int guard = 0; guard < 400 && session.picking(); ++guard) {
        const sim::Lockpicking& wire = session.lockpicking();
        if (stopAtPins >= 0 && wire.pinsSet() >= stopAtPins) {
            break;
        }
        if (maxProbes >= 0 && probes >= maxProbes) {
            break;
        }
        if (wire.pinsSet() != pinsSeen) {
            pinsSeen = wire.pinsSet();
            forget();
        }
        if (low > high) {
            forget();
        }
        // The midpoint of what is left, or -- when the midpoint has already
        // been tried, which is the no-feel case -- the nearest notch to it that
        // has not been. THE SWEEP HAS TO REACH EVERY DEPTH: a version of this
        // that only ever walked the window upward looped over four of the nine
        // notches forever and could not open a lock at all.
        std::int32_t aim = low + (high - low) / 2;
        if ((tried & (1U << aim)) != 0U) {
            aim = -1;
            for (std::int32_t spread = 1; spread < sim::kPinDepths && aim < 0; ++spread) {
                const std::int32_t mid = low + (high - low) / 2;
                const std::int32_t down = mid - spread;
                const std::int32_t up = mid + spread;
                if (down >= low && (tried & (1U << down)) == 0U) {
                    aim = down;
                } else if (up <= high && (tried & (1U << up)) == 0U) {
                    aim = up;
                }
            }
            if (aim < 0) {
                // Every notch in the window is spent. Widen to the whole track,
                // and if that is spent too the pin moved under us -- forget it
                // all and start again.
                forget();
                aim = 0;
                while (aim < sim::kPinDepths && (tried & (1U << aim)) != 0U) {
                    ++aim;
                }
                if (aim >= sim::kPinDepths) {
                    aim = 0;
                }
            }
        }
        session.movePick(aim - wire.depth());
        session.probeLock();
        ++probes;
        tried |= 1U << aim;
        switch (wire.lastFeel()) {
            case sim::Feel::TooShallow:
                // The pin is DEEPER than where the pick was held.
                low = aim + 1;
                break;
            case sim::Feel::TooDeep:
                high = aim - 1;
                break;
            case sim::Feel::NoFeel:
                // No information at all. The notch is crossed off and nothing
                // else is learned. This is the apprentice's whole game, and it
                // is why an apprentice forces boxes.
                break;
            case sim::Feel::Set:
            case sim::Feel::Broke:
            case sim::Feel::Jammed:
            case sim::Feel::Open:
            default:
                forget();
                break;
        }
    }
    return probes;
}

// ---------------------------------------------------------------------------
// S10: --trail, the investigation walked
// ---------------------------------------------------------------------------

/// THE WHOLE DISTRICT, ONE BAND. kCaptureRegion is the Gilded Gull and the
/// pavement outside it, which is the right box for a scripted burglary and far
/// too small for a walk to the Mission. The trail crosses the ward, so it gets
/// a box that is the ward -- the world's own dimensions, one band deep.
///
/// It is still a BOX and still the same exact breadth-first search that
/// region_path.hpp has run since S2: nothing here is a new pathfinder.
///
/// Walks the body across the DISTRICT to a tile, on the band it is already on.
/// True when it got there -- and a false is a real answer, not a warning: a
/// lead nobody can walk to is a lead nobody can read.
[[nodiscard]] bool walkAcrossDistrict(Session& session, std::int32_t tileX,
                                      std::int32_t tileY) {
    sim::TileBox box;
    box.x0 = 0;
    box.y0 = 0;
    box.x1 = session.tiles().sizeX() - 1;
    box.y1 = session.tiles().sizeY() - 1;
    box.z0 = session.body().band();
    box.z1 = session.body().band();
    sim::RegionPath router(session.tiles(), box);
    std::vector<sim::PathStep> route;
    const sim::PathStep from{session.body().tileX(), session.body().tileY(),
                             session.body().band()};
    const sim::PathStep to{tileX, tileY, session.body().band()};
    if (!router.find(from, to, route)) {
        return false;
    }
    for (const sim::PathStep& waypoint : route) {
        const std::int32_t wx = sim::q8_tile_centre(waypoint.x);
        const std::int32_t wy = sim::q8_tile_centre(waypoint.y);
        bool arrived = false;
        // A generous guard: a tile is eight movement steps at a walk and a
        // corner costs a few more. Sixty is slack, not a licence to wander.
        for (int guard = 0; guard < 60 && !arrived; ++guard) {
            arrived = stepToward(session, wx, wy);
        }
        if (!arrived) {
            return false;
        }
    }
    return true;
}

/// How many leads the walked trail READ. Not a fixed beat count: the trail is
/// authored data and the number of leads is a property of casebook.json, so a
/// hard-coded target here would be a second source of truth for the same thing.
std::int32_t gTrailRead = 0;
std::int32_t gTrailWalked = 0;
std::int32_t gTrailUnreached = 0;

/// WALKS THE BLOODLETTER TRAIL, through the same two calls a keyboard makes.
///
/// Every beat is: find the nearest lead the casebook currently holds OPEN on
/// this band, walk to it with the district's own breadth-first router, and
/// press Q. Nothing here reads a clue it has not walked to and nothing here
/// opens a lead the simulation did not open.
///
/// It stops when there is nothing open left that it can reach, which is an
/// honest end condition: the two leads on the strand plane (z10) are one band
/// down and this line does not climb, so it leaves them in the book and says so
/// in the summary rather than pretending the case is finished.
[[nodiscard]] int runTrailLine(Session& session, const std::string& ending) {
    gTrailRead = 0;
    gTrailWalked = 0;
    gTrailUnreached = 0;
    const sim::CasebookRaws* raws = session.casebook().raws();
    if (raws == nullptr) {
        return 0;
    }
    std::int32_t last = -1;
    // `start` walks nowhere: it is the FIRST FRAME of a new game, which is the
    // one state a capture could not otherwise reach -- SessionConfig's opening
    // page is set by the client and not by the smoke path, so a scripted run
    // has to ask for it. Nothing else about the session differs.
    const int rounds = ending == "start" ? 0 : 32;
    for (int guard = 0; guard < rounds; ++guard) {
        // The nearest OPEN lead on this band. Nearest, because that is what a
        // player does, and because it makes the walk short enough to watch.
        std::int32_t best = -1;
        std::int32_t bestDistance = 0;
        for (const std::int32_t index : session.casebook().known()) {
            if (session.casebook().state(index) != sim::LeadState::Open) {
                continue;
            }
            const sim::Lead& lead = raws->leads()[static_cast<std::size_t>(index)];
            if (lead.site.band != session.body().band()) {
                continue;
            }
            const std::int32_t dx = lead.site.x - session.body().tileX();
            const std::int32_t dy = lead.site.y - session.body().tileY();
            const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (best < 0 || distance < bestDistance) {
                best = index;
                bestDistance = distance;
            }
        }
        if (best < 0) {
            break;
        }
        const sim::Lead& lead = raws->leads()[static_cast<std::size_t>(best)];
        // Stand a tile short of the anchor: the anchors are counters, flagstones
        // and sagging floors, and several of them are solid cells by design.
        // kLookRangeTiles is four, so anywhere in the room will do.
        bool got = walkAcrossDistrict(session, lead.site.x, lead.site.y);
        if (!got) {
            for (const std::int32_t offset : {1, -1, 2, -2}) {
                got = walkAcrossDistrict(session, lead.site.x + offset, lead.site.y);
                if (got) {
                    break;
                }
                got = walkAcrossDistrict(session, lead.site.x, lead.site.y + offset);
                if (got) {
                    break;
                }
            }
        }
        ++gTrailWalked;
        const std::int32_t before = session.casebook().readCount();
        session.examine();
        if (session.casebook().readCount() > before) {
            ++gTrailRead;
            last = best;
        } else {
            // Could not get near enough. Take it out of the running so the loop
            // cannot spin on it, and count it -- a lead the walk could not reach
            // is the single most useful thing this run can report.
            ++gTrailUnreached;
            (void)session.casebook().look(lead.site.x, lead.site.y, lead.site.band);
            break;
        }
        if (ending == "mission" && lead.id == "mission-backroom") {
            break;
        }
        // TASK #82. `letters` needs the SECOND Mission lead read, not the
        // first -- mission-flagstones' own detail is the sentence that says
        // Maell's letters exist at all (see unlockedLetters()'s own note),
        // and mission-backroom alone leaves them still Open, not Followed.
        if (ending == "letters" && lead.id == "mission-flagstones") {
            break;
        }
        if (ending == "weighhouse" && lead.id == "weighhouse-ledger") {
            break;
        }
        if (ending == "hold" && lead.id == "drowned-hold") {
            break;
        }
    }
    // And where the shutter goes.
    if (ending == "notes" || ending == "start") {
        session.toggleCasebook();
    } else if (ending == "keys") {
        session.toggleKeys();
    } else if (ending == "letters") {
        // TASK #82. THE LETTERS THEMSELVES, not the place they were earned
        // at -- proving the panel exists is the point of this ending, and a
        // frame of the flagstones says nothing about whether a page reads.
        session.toggleLetters();
        // Picks the first one, so the capture shows an actual document
        // rather than only the title list a player sees before choosing.
        session.chooseVisibleTopic(0);
    } else if (last >= 0) {
        // STAND BACK, THEN LOOK AT IT. The anchors are counters, flagstones and
        // sagging floors, so a body that has walked onto one is standing with
        // its nose against masonry -- the first S10 capture of the Mission is a
        // photograph of a brown wall. Four tiles back is inside kLookRangeTiles,
        // so the clue is still readable from there, and it is the difference
        // between a frame of a place and a frame of a surface.
        const sim::Lead& lead = raws->leads()[static_cast<std::size_t>(last)];
        for (const std::int32_t back : {7, 6, 5, 4, 3}) {
            if (walkAcrossDistrict(session, lead.site.x, lead.site.y + back) ||
                walkAcrossDistrict(session, lead.site.x + back, lead.site.y) ||
                walkAcrossDistrict(session, lead.site.x, lead.site.y - back) ||
                walkAcrossDistrict(session, lead.site.x - back, lead.site.y)) {
                break;
            }
        }
        session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             lead.site.x, lead.site.y));
        // AND THE CLUE IS NOT RE-PRINTED FOR THE SHUTTER. Backing off puts the
        // body outside kLookRangeTiles, which is the correct answer -- seven
        // tiles is out of reach of a flagstone -- so pressing Q again from here
        // says "NOTHING HERE WORTH WRITING DOWN", which it should. The clue is
        // in the casebook, which is where a clue belongs; what this frame is of
        // is the PLACE, with the CASE row under it saying where the trail
        // stands. `--trail=notes` is the frame of the writing.
    }
    return gTrailRead;
}

[[nodiscard]] int runBurgleLine(Session& session, const std::string& ending) {
    int landed = 0;
    gBurgleWatchers = 0;
    gLockEndingProbes = 0;
    gBurgleBeatMask = 0;
    std::int32_t beat = 0;
    const auto mark = [&](bool ok) {
        if (ok) {
            gBurgleBeatMask |= 1 << beat;
            ++landed;
        }
        ++beat;
    };

    // 1. down on the haunches. Half speed and worth more than twenty levels.
    session.toggleCrouch();
    mark(session.stance() == sim::Stance::Crouched);

    // 2. IN AT THE DOOR, AND NOT MADE OUT STANDING IN IT. Crouched, at two in
    //    the morning, with the doors just barred and the lanterns out: the room
    //    is dark and nearly empty, which is the whole reason a burglar keeps
    //    these hours.
    //
    //    THE CHECK IS HERE AND NOT LATER, deliberately. A lift that goes wrong
    //    is an offence, and an offence puts a bouncer across the room at you --
    //    at which point being seen is the game working rather than the stealth
    //    failing, and a beat that could not tell those two apart would be a
    //    beat worth nothing.
    //
    //    S10: AND SOMEBODY HAS TO BE THERE TO MISS YOU. The S9 review proved
    //    this beat landed with the notice rule hard-wired to seen -- at two in
    //    the morning the doorway is empty, everyone in reach reads oblivious,
    //    and "nobody saw me" was a fact about the hour rather than about
    //    stealth. The bit now needs a body in range that is awake, upright and
    //    on this floor, so it is a claim that can fail. If the night staff have
    //    all gone to bed, the burglar walks in until one of them is in reach.
    walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
    if (session.tavern().watchersInReach() == 0) {
        // Nobody at the door. Go and stand near whoever is still up -- being
        // unseen next to a man is the beat; being unseen in an empty room is
        // not, and the run should fail rather than quietly pass if there is
        // nobody in the building at all.
        const sim::Actor* awake = nullptr;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.present() && actor.band() == session.body().band() &&
                actor.activity() != sim::Activity::Downed &&
                actor.role() != sim::ActorRole::Vermin) {
                awake = &actor;
                break;
            }
        }
        if (awake != nullptr) {
            walkToTile(session, awake->tileX() + 2, awake->tileY());
        }
    }
    gBurgleWatchers = session.tavern().watchersInReach();
    mark(session.hidden() && gBurgleWatchers > 0);

    // 3. a hand in the coat of whoever is still on a stool.
    //
    //    THE BEAT IS THE HAND, NOT THE COIN, and that is stated rather than
    //    quietly assumed. Whether a lift SUCCEEDS is CRACKSMANSHIP against the
    //    mark's own STREETWISE (Tavern::liftFrom), and a scripted burglar
    //    starts at level zero, so against the night staff of a captains' house
    //    it fails -- correctly, and it still teaches the hands. What this beat
    //    proves is that the verb is reachable from the keys and reached a body;
    //    the summary prints which way it went, so a reader is never told a lift
    //    landed when it did not.
    const std::int32_t purseBefore = session.tavern().playerCoin();
    bool handWentIn = false;
    for (const char* target : {"Kled Tarbeck", "Finch", "Gerta Saltcotte", "Master Venn"}) {
        const sim::Actor* who = nullptr;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.name() == target && actor.present() && actor.coin() > 0) {
                who = &actor;
                break;
            }
        }
        if (who == nullptr) {
            continue;
        }
        walkToTile(session, who->tileX(), who->tileY());
        const sim::Tavern::StealResult tried = session.tavern().liftFrom();
        if (tried.result != sim::ServiceResult::TooFar) {
            handWentIn = true;
        }
        if (session.tavern().playerCoin() > purseBefore) {
            break;
        }
    }
    mark(handWentIn);

    // 4. away across the room to the foot of the stair, and up it.
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    climbAndLand(session);
    mark(session.body().band() == sim::gull::kUpperBand);

    // 5. to a bed-foot that is not yours, and the wire in.
    const sim::gull::GuestRoom& box = sim::gull::kRooms[2];
    walkToTile(session, box.standX, box.standY);
    session.steal();
    mark(session.picking());

    // 6. WORK THE LOCK, the way the player in this body can. workTheWire() is
    //    a strategy and not a cheat: it reads the depth the pick is at, how
    //    many pins have dropped, and what the last probe FELT LIKE, and it
    //    reads nothing else. Below kFeelLevel that degenerates to the same
    //    blind sweep S9 shipped, because a hand with no feel has no better
    //    move; at and above it, it bisects.
    workTheWire(session);
    const std::int32_t roomBit = 1 << 2;
    if ((session.tavern().openedLocks() & roomBit) == 0) {
        // The wire is gone and the lock is ruined. A burglar with a jammed lock
        // and a job to do puts a shoulder to it, which always works and is the
        // loudest thing in the building.
        session.forceLock();
    }
    mark((session.tavern().openedLocks() & roomBit) != 0);

    // 7. and the box, emptied.
    const std::int32_t before = session.tavern().crackedBoxes();
    session.steal();
    mark(session.tavern().crackedBoxes() != before);

    if (ending == "lock") {
        // AND HE STARTS ON THE NEXT ONE, WITH THE HANDS THE FIRST ONE GAVE HIM.
        //
        // S9 SHIPPED THIS FRAME POSED AND SAID IT WAS NOT. Its comment read
        // "Nothing is faked" directly above a setPicks() that refilled the roll
        // out of nowhere and a pinDepth() lookup that drove the pick to the
        // answer -- the S9 review's fourth finding, and it was right: the CALLS
        // were the ones a keyboard reaches, the STATE was not, and the captured
        // PNG read PICKS 5 STRAIN 0/3 after a burglary that had just spent
        // every pick in the roll.
        //
        // What happens instead is the arc the retuning exists for. Beat 6 has
        // just cost this burglar most of his wire and taught his hands a great
        // deal doing it -- every probe is a use, and usesForLevel charges 18 of
        // them for CRACKSMANSHIP 3, which is kFeelLevel. So he goes back down
        // to the snug, buys wire off the Skyrunners' own contact with the same
        // G a keyboard presses, comes back up, and works the box across the
        // landing with a hand that can now hear it. Nothing here knows where a
        // pin is.
        walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
        session.dropDown();
        // #85. STANDS UP FIRST. Beat 1 crouched this body and nothing since
        // has stood it back up; interact() now resolves a person in reach to
        // PICKPOCKET while crouched rather than TALK, and speakTo() below
        // needs the latter to reach Finch's Join topic at all. A burglar
        // squaring an honest deal for more wire stands up to have the
        // conversation, which is the in-fiction reading and not only the
        // mechanical one.
        session.setCrouched(false);
        // The oath first. Nobody sells a stranger wire -- Tavern::buyPicks
        // refuses anyone off the Skyrunners' ladder in as many words -- so the
        // burglar takes their first rung off Finch through the same Join topic
        // the roof line uses, and then buys. Both are keys.
        if (speakTo(session, "Finch")) {
            pick(session, sim::TopicKind::Join);
            session.closeConversation();
        }
        const sim::Actor* contact = nullptr;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.role() == sim::ActorRole::SkyrunnerContact && actor.present()) {
                contact = &actor;
                break;
            }
        }
        if (contact != nullptr) {
            walkToTile(session, contact->tileX(), contact->tileY());
            // Two sets if he can afford them: a bisect wants four notches of
            // slack a pin and the roll is the only thing that buys patience.
            session.tavern().buyPicks();
            session.tavern().buyPicks();
        }
        walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
        climbAndLand(session);
        const sim::gull::GuestRoom& next = sim::gull::kRooms[3];
        walkToTile(session, next.standX, next.standY);
        session.steal();
        // Worked, and then STOPPED WHILE IT IS STILL BEING WORKED -- either
        // when the last pin is one away or after a handful of probes, whichever
        // comes first. That is what makes the shutter catch the minigame rather
        // than its aftermath, and it is a budget on the PLAYER'S side of the
        // wire, not a hand on the lock's side.
        gLockEndingProbes = workTheWire(session, sim::kStrongboxPins - 1, 8);
        session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             next.bedX, next.bedY));
        session.body().setPitch(sim::angle_from_degrees(-14));
    } else if (ending == "street") {
        session.dropDown();
        walkToTile(session, sim::gull::kStreetX, sim::gull::kStreetY);
    } else if (ending == "taproom") {
        walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
        session.dropDown();
        walkToTile(session, sim::gull::kBartenderX, sim::gull::kBarY + 2);
        session.body().setYaw(sim::kFacingNorth);
    } else {
        // Back down the landing, looking at the room that has just been done.
        // NOT nose-first against the bed block: standing on the tile you
        // cracked the box from fills the frame with one CLOTH face and shows
        // nothing, which is what the first shipped attempt at this frame did.
        walkToTile(session, 152, 72);
        session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             box.bedX, box.bedY));
        session.body().setPitch(sim::angle_from_degrees(-8));
    }
    return landed;
}

[[nodiscard]] int runSkyrunLine(Session& session, const std::string& ending) {
    const sim::DialogueDirector& talk = session.tavern().dialogue();
    const std::string questId = "skyrunner-tenant";

    // 1. the oath. Finch keeps the snug after ten.
    if (speakTo(session, "Finch")) {
        pick(session, sim::TopicKind::Join);
        session.closeConversation();
    }

    // 2. two purses, off whoever is nearest that is not the fence.
    for (const char* mark : {"Sella Brinewall", "Tarn Wrenhale", "Wick Hempson",
                             "Hobbin Mastwright", "Colm Tarbeck"}) {
        if (talk.journal().counter(questId) >= 2) {
            break;
        }
        if (speakTo(session, mark)) {
            pick(session, sim::TopicKind::PickPocket);
            session.closeConversation();
        }
    }
    if (speakTo(session, "Finch")) {
        pick(session, sim::TopicKind::QuestBeat);
        session.closeConversation();
    }

    // 3. a box above the stair. Up the authored stair first -- with the up-key,
    //    because walking at a stair in this build does nothing (see
    //    PlayerBody::mantle).
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    climbAndLand(session);
    walkToTile(session, sim::gull::kRooms[1].standX, sim::gull::kRooms[1].standY);
    // AND THE BOX IS ACTUALLY OPENED, WHICH THIS LINE STOPPED DOING IN S9 AND
    // NOBODY NOTICED FOR A SPRINT.
    //
    // S9 made a locked box put the WIRE in rather than opening itself -- its own
    // documented change -- and left this beat as a single steal(), which from
    // that day forward put a wire in a lock and walked away. The stage counts a
    // CRACKED box, so the run has landed 2 of 9 and exited 1 ever since; the S9
    // review re-ran --burgle, --nemesis and --ward and did not re-run this one.
    // Verified against the S9 tip before changing anything: identical output,
    // `stages=2 cracks=0`, so this is S9's regression and not S10's.
    //
    // What it does now is what a burglar does: work the wire, and put a
    // shoulder to it if the wire loses. Both are keys.
    session.steal();
    workTheWire(session);
    if (session.picking()) {
        session.stopPicking();
    }
    session.forceLock();
    session.steal();
    comeDownstairs(session);
    reportTo(session, "Finch");

    // 4. the roof. AND THE ROUND TRIP IS THE POINT: a counted stage only counts
    //    what you do WHILE IT IS THE STAGE, so the line is walked the way a
    //    player walks it -- go, do the one thing, come back and say so. A
    //    script that did all six acts and then reported six times would prove
    //    nothing about the questline at all.
    upOntoTheLead(session);
    downFromTheLead(session);
    reportTo(session, "Finch");

    // 5. the alley.
    upOntoTheLead(session);
    walkToTile(session, sim::gull::kFootprintX0, 70);
    session.body().setYaw(sim::kFacingWest);
    climbAndLand(session);
    // Back east over the same two tiles of air, and then off the Gull's own
    // west edge into the alley rather than off the far side of a house whose
    // street the router does not carry.
    session.body().setYaw(sim::kFacingEast);
    climbAndLand(session);
    downFromTheLead(session);
    reportTo(session, "Finch");

    // 6. sell it -- WHICH FIRST MEANS EARNING THE RUNG THAT MAKES HIM A FENCE.
    //    A cutpurse is not a fence, the Skyrunners' second rung is measured in
    //    SKYRUNNING, and nothing but roofs raises that. So the body goes back
    //    up and works the alley until the roofs will have it: leap west, leap
    //    east, and again, which is exactly what the guild's name means.
    upOntoTheLead(session);
    walkToTile(session, sim::gull::kFootprintX0, 70);
    for (int i = 0; i < 24 && talk.skills().level(sim::kRoofSkill) < 5; ++i) {
        session.body().setYaw(sim::kFacingWest);
        climbAndLand(session);
        session.body().setYaw(sim::kFacingEast);
        climbAndLand(session);
    }
    downFromTheLead(session);
    if (speakTo(session, "Finch")) {
        pick(session, sim::TopicKind::Advance);
        pick(session, sim::TopicKind::Fence);
        pick(session, sim::TopicKind::QuestBeat);
        session.closeConversation();
    }

    // 7. lean on somebody.
    for (const char* mark : {"Tarn Wrenhale", "Colm Tarbeck", "Sella Brinewall",
                             "Hobbin Mastwright"}) {
        if (talk.journal().counter(questId) > 0) {
            break;
        }
        if (speakTo(session, mark)) {
            pick(session, sim::TopicKind::Lean);
            session.closeConversation();
        }
    }
    reportTo(session, "Finch");

    // 8. the bale, out of the door past the Watch.
    walkToTile(session, sim::gull::kBaleX, sim::gull::kBaleY);
    session.steal();
    walkToTile(session, sim::gull::kStreetX, sim::gull::kStreetY);
    walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
    if (speakTo(session, "Finch")) {
        pick(session, sim::TopicKind::QuestBeat);
        // 9. and what a tenant is.
        pick(session, sim::TopicKind::QuestBeat);
    }

    if (ending == "away") {
        session.closeConversation();
    }
    standBackFrom(session, "Finch");
    return talk.journal().stagesDone(questId);
}

}  // namespace

// ---------------------------------------------------------------------------
// #79 -- the street
// ---------------------------------------------------------------------------

namespace {

/// The trades a `--street` run knows how to ask for. Named the way a person
/// would say them out loud, not the way the enum spells them: "watch", not
/// "MilitiaWatch", and "hand" for the six hundred rope-and-crate bodies the
/// district actually runs on.
struct StreetTrade {
    const char* word;
    sim::WardType type;
};

/// The same two tiles Session::kWardTalkReachTiles is, named again here because
/// this file's anonymous namespace cannot see a private static member. Pinned
/// to it by a case: a search that confirmed its candidate against a DIFFERENT
/// reach than the key uses would confirm nothing.
constexpr std::int32_t kStreetReachTiles = 2;

constexpr StreetTrade kStreetTrades[] = {
    {"hand", sim::WardType::Serf},        {"watch", sim::WardType::MilitiaWatch},
    {"priest", sim::WardType::PriestOfTheFlame},
    {"disciple", sim::WardType::DiscipleOfTheFlame},
    {"keeper", sim::WardType::Shopkeeper}, {"fisher", sim::WardType::Fisher},
    {"sailor", sim::WardType::Sailor},    {"carter", sim::WardType::Carter},
    {"wastrel", sim::WardType::Wastrel},  {"urchin", sim::WardType::Urchin},
    {"thief", sim::WardType::Thief},      {"drover", sim::WardType::AnimalKeeper},
    {"cat", sim::WardType::Cat},          {"dog", sim::WardType::Dog},
};

}  // namespace

StreetLineResult runStreetLine(Session& session, const std::string& who, int topic) {
    StreetLineResult out;

    sim::WardType wanted = sim::WardType::Serf;
    for (const StreetTrade& trade : kStreetTrades) {
        if (who == trade.word) {
            wanted = trade.type;
        }
    }

    // The first body of that trade you can stand beside AND BE TALKING TO.
    //
    // The second half of that is the whole of the search, and the first attempt
    // did not have it: it took the first free tile next to the first body of
    // the trade, and at a muster where fourteen spare hands share one commons
    // the key reached whichever of them the reach rule picked -- a body of the
    // right trade, chosen by nobody, and sometimes a different one. So the
    // candidate is confirmed against the SAME question Session::interact asks
    // before the body is moved: "who would answer from here". If the answer is
    // not this person, this is not the tile.
    //
    // Clear of the Gilded Gull as well, because the taproom's own roster is
    // asked first and reaches two tiles: a capture that stood outside K03's
    // door would photograph a bouncer and call it the ward.
    const sim::WardActor* target = nullptr;
    std::int32_t standX = 0;
    std::int32_t standY = 0;
    for (const sim::WardActor& actor : session.people().actors()) {
        if (!actor.visible() || actor.type != wanted) {
            continue;
        }
        if (actor.x >= sim::gull::kFootprintX0 - 4 && actor.x <= sim::gull::kFootprintX1 + 4 &&
            actor.y >= sim::gull::kFootprintY0 - 6 && actor.y <= sim::gull::kFootprintY1 + 4) {
            continue;
        }
        static constexpr std::int32_t dx[4] = {1, -1, 0, 0};
        static constexpr std::int32_t dy[4] = {0, 0, 1, -1};
        for (int n = 0; n < 4 && target == nullptr; ++n) {
            const std::int32_t sx = actor.x + dx[n];
            const std::int32_t sy = actor.y + dy[n];
            if (!session.tiles().standable(sx, sy, actor.band)) {
                continue;
            }
            const sim::WardActor* answers =
                session.people().nearestTo(sx, sy, actor.band, kStreetReachTiles);
            if (answers != nullptr && answers->id == actor.id) {
                target = &actor;
                standX = sx;
                standY = sy;
            }
        }
        if (target != nullptr) {
            break;
        }
    }
    if (target == nullptr) {
        return out;
    }
    out.found = true;
    out.actorId = target->id;

    // THE ONE PLACEMENT, and see SmokeRunConfig::street on why it is a placement
    // and not a walk. Everything after this line is the game.
    session.body().placeAt(standX, standY, target->band);
    {
        // Facing them, so the frame is a picture of a conversation. Four-point
        // and integer: a heading is simulation state, and an atan2 here would
        // put a double in the middle of one.
        const std::int32_t toX = target->x - standX;
        const std::int32_t toY = target->y - standY;
        sim::Angle look = sim::kFacingNorth;
        if (std::abs(toX) >= std::abs(toY)) {
            look = toX > 0 ? sim::kFacingEast : sim::kFacingWest;
        } else {
            look = toY > 0 ? sim::kFacingSouth : sim::kFacingNorth;
        }
        session.body().setYaw(look);
    }
    // One movement step, so the room's own idea of where the player is standing
    // catches up with the body before the key is pressed.
    session.stepMany(sim::MoveInput{}, 1);
    session.interact();
    out.opened = session.talking() && session.wardTalkingTo() == out.actorId;
    if (!out.opened) {
        return out;
    }
    if (topic > 0) {
        session.chooseTopic(static_cast<std::size_t>(topic - 1));
    }
    const sim::DialogueDirector& talk = session.tavern().dialogue();
    out.name = talk.speaker().name;
    out.barkKey = talk.greetingKey();
    out.line = talk.lastLine();
    return out;
}

PetitionLineResult runPetitionLine(Session& session, bool grantCoin) {
    PetitionLineResult out;

    // 1. SOMEBODY OF THE CLOTH, ANSWERABLE FROM COMPOUND GROUND. The roster
    // puts the Mission's priest and disciples where their day puts them, so
    // the run walks the clock an hour at a time -- the same public
    // skipToHour a WAIT pick spends -- until one of them has a standable
    // tile within talking reach that is ON a plot (the roll reading's ground
    // is the PLAYER'S feet). Any plot serves: the reading is about the
    // ground stood on, and the petition is about the roll's own vacant
    // charge wherever the conversation happens.
    const sim::WardActor* target = nullptr;
    std::string_view plotId;
    std::int32_t standX = 0;
    std::int32_t standY = 0;
    // One scan of the CURRENT hour's positions. THE STAND TILE DECIDES, not
    // the priest's own: the ground the topics read is the ground under the
    // PLAYER's feet, so a priest passing a compound gate still answers for
    // its ground when the player stands inside -- which is why the stand
    // candidates run over the conversation's whole reach (kStreetReachTiles,
    // Chebyshev, the exact reach the talk key confirms with below) and not
    // only the four adjacent tiles.
    const auto scanNow = [&]() {
        for (const sim::WardActor& actor : session.people().actors()) {
            if (!actor.visible() || (actor.type != sim::WardType::PriestOfTheFlame &&
                                     actor.type != sim::WardType::DiscipleOfTheFlame)) {
                continue;
            }
            for (std::int32_t oy = -kStreetReachTiles; oy <= kStreetReachTiles; ++oy) {
                for (std::int32_t ox = -kStreetReachTiles; ox <= kStreetReachTiles; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    const std::int32_t sx = actor.x + ox;
                    const std::int32_t sy = actor.y + oy;
                    const std::string_view ground = sim::docks::plotIdUnder(sx, sy);
                    if (ground.empty()) {
                        continue;
                    }
                    const std::int32_t plot = session.ward().plotNamed(ground);
                    if (plot < 0) {
                        continue;
                    }
                    if (!session.tiles().standable(sx, sy, actor.band)) {
                        continue;
                    }
                    const sim::WardActor* answers =
                        session.people().nearestTo(sx, sy, actor.band, kStreetReachTiles);
                    if (answers == nullptr || answers->id != actor.id) {
                        continue;
                    }
                    target = &actor;
                    plotId = ground;
                    standX = sx;
                    standY = sy;
                    return true;
                }
            }
        }
        return false;
    };
    // One pass of the clock. The placement always happens at the hour the
    // body was actually seen, so the scan can never point at where somebody
    // stood three skips ago.
    bool found = false;
    for (int hourStep = 0; hourStep < 24 && !found; ++hourStep) {
        if (hourStep > 0) {
            session.skipToHour((session.timeOfDay() / 3600 + 1) % 24);
        }
        found = scanNow();
    }
    if (!found || target == nullptr) {
        return out;
    }
    out.found = true;
    out.plotId = std::string(plotId);

    // 2. THE PURSE, WHEN ASKED FOR -- capture plumbing, said out loud in the
    // header: the VACANT plot's first quarter's charge-rent through the
    // public setter, so the frame photographs the priest's YES rather than
    // the one refusal (CannotAfford) that proves nothing about the wiring.
    // The real verb still spends it for real.
    if (grantCoin) {
        const std::vector<sim::Plot>& plots = session.ward().plots();
        for (const sim::Plot& roll : plots) {
            if (roll.tenure != sim::Tenure::Vacant || roll.playerIsDuke) {
                continue;
            }
            const sim::PlotRaw& raw =
                session.ward().raws().plots()[static_cast<std::size_t>(roll.raw)];
            session.tavern().setPlayerCoin(
                std::max(session.tavern().playerCoin(), raw.chargeRent));
            break;
        }
    }

    // 3. THE ONE PLACEMENT -- runStreetLine's own move, same reasons.
    session.body().placeAt(standX, standY, target->band);
    {
        const std::int32_t toX = target->x - standX;
        const std::int32_t toY = target->y - standY;
        sim::Angle look = sim::kFacingNorth;
        if (std::abs(toX) >= std::abs(toY)) {
            look = toX > 0 ? sim::kFacingEast : sim::kFacingWest;
        } else {
            look = toY > 0 ? sim::kFacingSouth : sim::kFacingNorth;
        }
        session.body().setYaw(look);
    }
    session.stepMany(sim::MoveInput{}, 1);

    // 4. EVERYTHING AFTER THIS IS THE GAME: the real interact, the real
    // director building a clergy topic list over real ground, the real
    // Ward::petitionForCharge behind the petition row.
    session.interact();
    out.opened = session.talking() && session.wardTalkingTo() == target->id;
    if (!out.opened) {
        return out;
    }
    out.speaker = session.tavern().dialogue().speaker().name;
    if (pick(session, sim::TopicKind::ReadRoll)) {
        out.rollLine = session.lastMessage();
    }
    if (pick(session, sim::TopicKind::Petition)) {
        out.petitionLine = session.lastMessage();
    }
    // The petition named the roll's vacant plot, which need not be the one
    // stood on -- so the claim checked is the roll's own: SOMEBODY's charge
    // now reads the player.
    for (const sim::Plot& roll : session.ward().plots()) {
        out.becameDuke = out.becameDuke || roll.playerIsDuke;
    }
    return out;
}

RadiantLineResult runRadiantLine(Session& session, bool takeIt) {
    RadiantLineResult out;

    // 1. AN OFFERED ERRAND WHOSE GIVER ANSWERS FROM WHERE THEY NOW STAND.
    // The board bound its nouns at posting time; the giver has been living
    // their day since. So the search is the petition line's own: scan the
    // CURRENT hour's positions for a stand tile the talk key would confirm,
    // and walk the clock an hour at a time until somebody's day allows it --
    // re-reading the board each hour, because a skip across midnight posts a
    // fresh one and yesterday's untaken offers go with it. Clear of the
    // Gilded Gull, runStreetLine's own exclusion for its own reason.
    const sim::WardActor* giver = nullptr;
    const sim::RadiantObjective* row = nullptr;
    std::int32_t standX = 0;
    std::int32_t standY = 0;
    const auto scanNow = [&]() {
        for (const sim::RadiantObjective& offer :
             session.tavern().dialogue().radiant().objectives()) {
            if (offer.state != sim::RadiantState::Offered) {
                continue;
            }
            const sim::WardActor* body = session.people().byId(offer.giverActorId);
            if (body == nullptr || !body->visible()) {
                continue;
            }
            if (body->x >= sim::gull::kFootprintX0 - 4 && body->x <= sim::gull::kFootprintX1 + 4 &&
                body->y >= sim::gull::kFootprintY0 - 6 && body->y <= sim::gull::kFootprintY1 + 4) {
                continue;
            }
            for (std::int32_t oy = -kStreetReachTiles; oy <= kStreetReachTiles; ++oy) {
                for (std::int32_t ox = -kStreetReachTiles; ox <= kStreetReachTiles; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    const std::int32_t sx = body->x + ox;
                    const std::int32_t sy = body->y + oy;
                    if (!session.tiles().standable(sx, sy, body->band)) {
                        continue;
                    }
                    const sim::WardActor* answers =
                        session.people().nearestTo(sx, sy, body->band, kStreetReachTiles);
                    if (answers == nullptr || answers->id != body->id) {
                        continue;
                    }
                    giver = body;
                    row = &offer;
                    standX = sx;
                    standY = sy;
                    return true;
                }
            }
        }
        return false;
    };
    bool found = false;
    for (int hourStep = 0; hourStep < 24 && !found; ++hourStep) {
        if (hourStep > 0) {
            session.skipToHour((session.timeOfDay() / 3600 + 1) % 24);
            // ONE SETTLING STEP BEFORE THE SCAN, and it is load-bearing: a
            // skip that crossed midnight leaves yesterday's board standing
            // until the next advanceSecond posts the new day's -- so a scan
            // straight off the skip could pick a row the very next step
            // sweeps, and the conversation would open on a giver whose offer
            // no longer exists. The step runs the posting first; the scan
            // reads the board the interact below will actually see.
            session.stepMany(sim::MoveInput{}, 1);
        }
        found = scanNow();
    }
    if (!found || giver == nullptr || row == nullptr) {
        return out;
    }
    out.found = true;
    out.objectiveId = row->id;
    out.giver = row->giverName;
    out.brief = row->brief;

    // 2. THE ONE PLACEMENT -- runStreetLine's own move, same reasons.
    session.body().placeAt(standX, standY, giver->band);
    {
        const std::int32_t toX = giver->x - standX;
        const std::int32_t toY = giver->y - standY;
        sim::Angle look = sim::kFacingNorth;
        if (std::abs(toX) >= std::abs(toY)) {
            look = toX > 0 ? sim::kFacingEast : sim::kFacingWest;
        } else {
            look = toY > 0 ? sim::kFacingSouth : sim::kFacingNorth;
        }
        session.body().setYaw(look);
    }
    session.stepMany(sim::MoveInput{}, 1);

    // 3. EVERYTHING AFTER THIS IS THE GAME: the real interact, the real
    // director recognising its own giver, the real board behind the press.
    session.interact();
    out.opened = session.talking() && session.wardTalkingTo() == giver->id;
    if (!out.opened) {
        return out;
    }
    const std::vector<sim::Topic>& topics = session.tavern().dialogue().topics();
    std::int32_t offerIndex = -1;
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == sim::TopicKind::TakeRadiant && topics[i].payload == out.objectiveId) {
            offerIndex = static_cast<std::int32_t>(i);
            break;
        }
    }
    out.offered = offerIndex >= 0;
    if (!out.offered || !takeIt) {
        // The "offer" end: the shutter gets the giver's own row on the list.
        return out;
    }
    session.chooseTopic(static_cast<std::size_t>(offerIndex));
    const sim::RadiantObjective* moved =
        session.tavern().dialogue().radiant().find(out.objectiveId);
    out.taken = moved != nullptr && moved->state == sim::RadiantState::Taken;
    // 4. AND THE JOURNAL SHOWS IT -- the same rows the Journal tile draws,
    // asked for the errand's own cast rather than eyeballed off a PNG: the
    // row carries the verb and names the errand's own party.
    const std::string party = row->isFetch() ? "FOR " + upperAscii(row->giverName)
                                             : "TO " + upperAscii(row->targetName);
    for (const std::string& line : session.journalWorkRows()) {
        if (line.rfind("- ", 0) == 0 && line.find(row->verb) != std::string::npos &&
            line.find(party) != std::string::npos) {
            out.journal = true;
            break;
        }
    }
    // The taken end is DONE talking -- its evidence is the board and the
    // journal, and the menu-opening flags below runSmoke's scripted lines
    // (--refocus=journal in particular) cannot open a page over a
    // conversation still holding the keys. The "offer" end above returns
    // with the list up because the list IS its photograph.
    session.closeConversation();
    return out;
}

int scriptedStartHour(const SmokeRunConfig& config) noexcept {
    // ONE IN THE MORNING, and the hour is the point.
    //
    // Finch keeps the snug from ten at night, so the line cannot start before
    // then -- but S6 put the ward's law in the same taproom between nine and
    // one, and a man who commits all six of the ward's crimes in one evening
    // with the impound keeper three tables away is a man who loses a hand for
    // it. That is the game working: the run was arrested on its way back from
    // the last delivery and lost two of its nine beats, with `sentence=maimed`
    // printed in its own summary.
    //
    // So the scripted burglar does what a burglar does and starts after the
    // Watch has gone home. Finch is still in the snug until three; Watchman
    // Cull left at one. Nothing about the line changed -- the hour did, and the
    // hour is now part of what the line teaches.
    if (config.skyrun) {
        return 1;
    }
    // Father Maell takes an evening hour in the Gull between seven and half
    // past nine. Eight is the middle of it, which is also the default.
    if (config.flame) {
        return 20;
    }
    // The bounty wants two men in one room: Watchman Cull comes in at nine and
    // Father Maell leaves at half past. Quarter past is the only quarter of an
    // hour in the day when the ward will both sell you the work and sign for
    // it.
    if (config.contract) {
        return 21;
    }
    // The nemesis arc wants a labourer on shift and a room with his own guild
    // in it: Tarn Wrenhale keeps the taproom from six in the evening until one,
    // and the crowd he is enlisted out of is there from seven. Eight.
    if (config.nemesis) {
        return 20;
    }
    // TWO IN THE MORNING, and the hour is the whole point of the line.
    //
    // The Gull shuts at two: the doors are barred, the lanterns and the table
    // candles are out (Tavern::houseLights only shows them while isOpen()), the
    // hearth is banked from three, and the crowd has gone. What is left is a
    // dark room with a handful of night staff in it -- which is the only state
    // of this building a burglary is actually possible in, and proving that is
    // the point of `--burgle`. Later than three and there is nobody left to
    // creep past at all; earlier than two and the doors are open and the room
    // is lit. Run it at eight in the evening and it lands fewer beats and says
    // so, which is the game working.
    if (config.burgle) {
        return 2;
    }
    // The roof line needs the door open and nobody in particular.
    return -1;
}

SmokeRunResult runSmoke(const SmokeRunConfig& config) {
    SmokeRunResult result;
    SessionConfig started = config.session;
    if (!started.timeOfDayGiven) {
        const int hour = scriptedStartHour(config);
        if (hour >= 0) {
            started.timeOfDay = hour * 3600;
        }
    }
    Session session(started);

    // A scripted walk, so a capture at N steps is a picture of the game moving
    // rather than a picture of the spawn. Forward, with a slow drift of the
    // head, which is enough to exercise collision and band changes. `walk` off
    // holds position and lets the ROOM move instead, which is what a capture of
    // a tavern at two different hours wants.
    sim::MoveInput input;
    input.forward = config.walk ? 1 : 0;
    for (int i = 0; i < config.steps; ++i) {
        input.turn = (config.walk && (i / 90) % 4 == 3) ? 1 : 0;
        session.step(input);
    }

    // A capture OF a conversation, not of somebody standing beside one. Driven
    // through exactly the calls a keypress makes, so the frame a sprint proves
    // itself with is a picture of the game and not of a test harness.
    if (config.talk) {
        session.interact();
        result.talking = session.talking();
        for (const int topic : config.topics) {
            if (topic >= 0) {
                session.chooseTopic(static_cast<std::size_t>(topic));
            }
        }
        if (config.offer >= 0 && session.haggling()) {
            const int delta = config.offer - session.haggleOffer();
            session.adjustOffer(delta);
        }
        if (config.again) {
            session.closeConversation();
            session.interact();
        }
        result.talking = session.talking();
    }

    if (config.pause) {
        // THE SAME THREE CALLS ESC, DOWN, DOWN AND ENTER MAKE -- see
        // Session::togglePause's own header on why this exists at all.
        session.togglePause();
        bool landed = false;
        if (config.pauseEnd == "settings") {
            // TIME-AND-TENURE BUILD: one further press than before -- WAIT
            // took the second row, so every door below it moved down one.
            session.movePauseCursor(3);  // RESUME -> WAIT -> CONTROLS -> SETTINGS
            session.choosePause();
            landed = session.optionsOpen();
        } else if (config.pauseEnd == "controls") {
            // MORROWIND ROUND: KEYS' OWN NEW PAUSE-SIDE DOOR.
            session.movePauseCursor(2);  // RESUME -> WAIT -> CONTROLS
            session.choosePause();
            landed = session.keysOpen();
        } else if (config.pauseEnd == "armed") {
            session.movePauseCursor(4);  // ... -> SETTINGS -> QUIT
            session.choosePause();       // arms it; does not fire on one press
            landed = session.pauseOpen() && session.quitArmed();
        } else {
            landed = session.pauseOpen();
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += landed ? 1 : 0;
    }

    // SHEETS BUILD: THE MENU-OPENING FLAGS (`--character`, `--map`,
    // `--refocus`) MOVED BELOW THE SCRIPTED LINES. They used to run here,
    // before them, and the combination was silently useless: every scripted
    // line closes whatever page is up on its way through, so
    // `--flame --character` photographed a CLOSED menu over a finished line,
    // and there was no way at all to photograph a tile WITH the state a line
    // had just driven into existence. Opening the page is now the last thing
    // the script does before the shutter, which is also the order a player's
    // own evening runs in.

    if (config.sprintSteps > 0) {
        // VERIFICATION ONLY. See SmokeRunConfig::sprintSteps's own header:
        // real sprint steps through the real gate, so the frame taken after
        // this shows whatever the pool honestly reads -- mid at a thousand
        // steps, empty-plus-refusal past two thousand.
        sim::MoveInput sprintInput;
        sprintInput.forward = 1;
        sprintInput.sprint = true;
        session.stepMany(sprintInput, config.sprintSteps);
    }

    if (config.punch) {
        // VERIFICATION ONLY. See SmokeRunConfig::punch's own header. The
        // SAME key F makes -- Session::punch() -- retried until it actually
        // connects, since a swing that misses leaves nothing on screen for
        // punchLandedPulse_ to draw.
        session.closeConversation();
        bool landed = false;
        for (int attempt = 0; attempt < 8 && !landed; ++attempt) {
            session.punch();
            const std::string& msg = session.lastMessage();
            landed = msg.rfind("HIT ", 0) == 0 || msg.find(" GOES DOWN.") != std::string::npos;
            if (!landed) {
                session.stepMany(sim::MoveInput{}, 1);
            }
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += landed ? 1 : 0;
    }

    if (config.block) {
        // VERIFICATION ONLY. See SmokeRunConfig::block's own header. The
        // brawl starts the way --punch starts one, the guard goes up through
        // the SAME Session::setBlocking() the right mouse button calls, and
        // the wait is until the room itself says a blow was softened --
        // blowsBlocked() moving -- because a run of whiffs leaves a GUARD UP
        // row over a fight the guard never actually worked in.
        session.closeConversation();
        session.punch();
        session.setBlocking(true);
        const std::int32_t before = session.tavern().blowsBlocked();
        // Blows land once a simulated second; a dozen seconds of held guard
        // is enough for several, and the bound keeps a pathological room from
        // hanging the capture.
        constexpr int kBlockWaitSteps = 12 * 60;
        int waited = 0;
        while (session.tavern().blowsBlocked() == before && waited < kBlockWaitSteps) {
            session.stepMany(sim::MoveInput{}, 1);
            ++waited;
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += session.tavern().blowsBlocked() > before ? 1 : 0;
    }

    if (config.street) {
        const StreetLineResult street = runStreetLine(session, config.streetWho,
                                                      config.streetTopic);
        result.talking = session.talking();
        // ONE BEAT, AND IT EITHER LANDED OR IT DID NOT. The whole claim of #79
        // is that a body in the open district answers, so the run reports it
        // the same way every other scripted line reports its beats and a
        // capture that photographed nobody cannot pass as one that did.
        result.scriptedWanted += 1;
        result.scriptedLanded += street.opened ? 1 : 0;
        result.streetSpeaker = street.name;
        result.streetKey = street.barkKey;
    }

    if (config.flame) {
        result.flameStages = runFlameLine(session, config.flameEnd);
        result.talking = session.talking();
        const sim::Questline* line = session.tavern().dialogue().quests().find("flame-disciple");
        result.scriptedWanted += line == nullptr ? 0 : static_cast<std::int32_t>(
                                                          line->stages.size());
        result.scriptedLanded += result.flameStages;
    }

    if (config.petition) {
        // TIME-AND-TENURE BUILD. See SmokeRunConfig::petition's own header.
        // Three beats: somebody of the cloth found ON a plot, the roll read,
        // the charge petitioned -- and the third only counts when the ROLL
        // says it did (playerIsDuke), not when a sentence was merely said.
        result.petitionResult = runPetitionLine(session, true);
        result.talking = session.talking();
        result.scriptedWanted += 3;
        result.scriptedLanded += (result.petitionResult.opened ? 1 : 0) +
                                 (result.petitionResult.rollLine.empty() ? 0 : 1) +
                                 (result.petitionResult.becameDuke ? 1 : 0);
    }

    if (config.radiant) {
        // RADIANT BUILD. See SmokeRunConfig::radiant. The "offer" end owes
        // one beat (the giver's own row on the visible list, conversation
        // left open for the shutter); "taken" owes three -- offered, the
        // board moved, the journal shows it.
        const bool takeIt = config.radiantEnd != "offer";
        result.radiantResult = runRadiantLine(session, takeIt);
        result.talking = session.talking();
        if (takeIt) {
            result.scriptedWanted += 3;
            result.scriptedLanded += (result.radiantResult.offered ? 1 : 0) +
                                     (result.radiantResult.taken ? 1 : 0) +
                                     (result.radiantResult.journal ? 1 : 0);
        } else {
            result.scriptedWanted += 1;
            result.scriptedLanded += result.radiantResult.offered ? 1 : 0;
        }
    }

    if (config.quickbar) {
        // VERIFICATION ONLY. See SmokeRunConfig::quickbar's own header: the
        // same public verbs a keypress calls, end to end -- page open, the
        // cursor's crafting walked onto slot 3 (three RIGHT presses from
        // NONE), page down, number pressed. The beat lands only if the slot
        // genuinely holds a crafting AND the hand genuinely equipped it,
        // which is the round-trip the whole task exists to close. BEFORE the
        // cast block below, deliberately, so `--flame --quickbar --cast` is
        // a true equip-then-cast sequence: what C spends is what the number
        // row just readied.
        session.closeConversation();
        session.toggleGrimoire();
        bool landed = false;
        if (session.grimoireOpen()) {
            session.adjustGrimoireSlot(3);
            session.closeConversation();
            session.selectQuickSlot(2);
            const sim::Spell* bound = session.tavern().slotSpell(2);
            const sim::Spell* held = session.tavern().equippedSpell();
            landed = bound != nullptr && held != nullptr && bound->id == held->id;
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += landed ? 1 : 0;
    }

    if (config.cast) {
        // VERIFICATION ONLY. See SmokeRunConfig::cast's own header: the same
        // call C makes, once. AFTER the flame block above, deliberately --
        // that line's teaching beat is what stocks the grimoire, so
        // `--flame --flameEnd=away --cast` photographs the CAST row with a
        // crafting actually in the hand; --cast alone photographs the
        // empty-grimoire refusal on the alert row, the common state.
        session.closeConversation();
        session.castEquipped();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.lastMessage().empty() ? 0 : 1;
    }

    if (!config.heldSpells.empty()) {
        // HELD-EFFECTS BUILD. See SmokeRunConfig::heldSpells's own header:
        // each id equipped and cast until its link opens, in order, so the
        // frame photographs live holds with their clocks -- and, for the
        // two-id run, the second crafting's recovery printed off a mind the
        // first is still holding. One beat per id.
        session.closeConversation();
        std::size_t start = 0;
        while (start <= config.heldSpells.size()) {
            const std::size_t comma = config.heldSpells.find(',', start);
            const std::string id =
                comma == std::string::npos
                    ? config.heldSpells.substr(start)
                    : config.heldSpells.substr(start, comma - start);
            if (!id.empty()) {
                result.scriptedWanted += 1;
                result.scriptedLanded += runHeldCast(session, id) ? 1 : 0;
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
    }

    if (config.grimoire) {
        // VERIFICATION ONLY. See SmokeRunConfig::grimoire's own header. After
        // the quickbar block, deliberately, so `--flame --quickbar --grimoire`
        // photographs the page WITH a slot column on it.
        session.closeConversation();
        session.toggleGrimoire();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.grimoireOpen() ? 1 : 0;
    }

    if (config.wait) {
        // TIME-AND-TENURE BUILD. See SmokeRunConfig::wait's own header: the
        // pause menu's own WAIT row, pressed the way a hand presses it, so
        // the capture is a picture of the DOOR working and not only of the
        // page existing.
        session.closeConversation();
        session.togglePause();
        session.movePauseCursor(1);  // RESUME -> WAIT
        session.choosePause();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.waitOpen() && !session.waitSleeping() ? 1 : 0;
    }

    if (config.roofs) {
        // Four beats: the stair, the floor above it, the wall, the lead.
        constexpr std::int32_t kRoofBeats = 4;
        const std::int32_t landed = static_cast<std::int32_t>(
            runRoofLine(session, config.roofsEnd));
        result.scriptedWanted += kRoofBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.contract) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runContractLine(session, config.contractEnd));
        result.contractBeats = landed;
        // `held` stops the line after two beats ON PURPOSE (see its note in
        // runContractLine), so two is what it owes -- asking for six would
        // print the fell-short warning over a run that did exactly what was
        // asked of it.
        result.scriptedWanted +=
            config.contractEnd == "held" ? kContractHeldBeats : kContractBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.nemesis) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runNemesisLine(session, config.nemesisEnd));
        result.nemesisBeats = landed;
        result.scriptedWanted += kNemesisBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.trail) {
        // NOT A FIXED BEAT COUNT. The trail is authored data; how many leads
        // there are is casebook.json's business, and a target typed in here
        // would be a second source of truth for it. What the run owes is that
        // every lead it WALKED TO it also read -- an unreachable lead fails the
        // run, which is exactly the failure a coordinate drifting would cause.
        result.trailRead = static_cast<std::int32_t>(runTrailLine(session, config.trailEnd));
        result.trailWalked = gTrailWalked;
        result.scriptedWanted += gTrailWalked;
        result.scriptedLanded += result.trailRead;
        result.talking = session.talking();
    }

    if (config.burgle) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runBurgleLine(session, config.burgleEnd));
        result.burgleBeats = landed;
        result.burgleBeatMask = gBurgleBeatMask;
        result.watchersInReach = gBurgleWatchers;
        result.scriptedWanted += kBurgleBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.skyrun) {
        result.skyrunStages = runSkyrunLine(session, config.skyrunEnd);
        result.talking = session.talking();
        const sim::Questline* line =
            session.tavern().dialogue().quests().find("skyrunner-tenant");
        result.scriptedWanted += line == nullptr ? 0 : static_cast<std::int32_t>(
                                                           line->stages.size());
        result.scriptedLanded += result.skyrunStages;
    }

    // SHEETS BUILD: THE MENU-OPENING FLAGS RUN HERE NOW, AFTER every scripted
    // line -- see the note where they used to sit, above `--punch` -- so a
    // capture can finally photograph a tile WITH the state a line just drove
    // into existence (`--flame --flameEnd=away --refocus=character` is a
    // sheet with rungs on it; `--contract --contractEnd=held
    // --refocus=journal` is a journal with a live job on it). `--character`/
    // `--map` alone behave exactly as they always did: nothing above runs
    // without its own flag.
    if (config.character) {
        // THE SAME CALL `C` MAKES -- see SmokeRunConfig::character's own
        // header on why this exists at all.
        session.toggleCharacter();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.characterOpen() ? 1 : 0;
    }

    if (config.map) {
        // THE TILED MENU'S CHART TILE -- see SmokeRunConfig::map's own header.
        // (M no longer reaches this: core action #13 gave M to the ward map
        // below; the Chart tile is reached through the Menu key as ever.)
        session.toggleMap();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.mapOpen() ? 1 : 0;
    }

    if (config.mapOverlay) {
        // THE WARD MAP -- THE SAME CALL `M` MAKES. See
        // SmokeRunConfig::mapOverlay's own header.
        session.toggleDistrictMap();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.districtMapOpen() ? 1 : 0;
    }

    // PLANNING SPRINT (item #1). THE GAP `--character --map` COULD NEVER
    // CLOSE, closed. See SmokeRunConfig::refocus's own header for what this
    // fixes and why: with nothing here, a caller wanting a mid-crossfade
    // frame had to fire both toggle*() calls back to back (`config.character`
    // and `config.map` above, with zero step() calls between them) and could
    // only ever photograph the SECOND tile's animation from a cold start.
    //
    // THIS RUNS BEFORE `settleSteps`' OWN GENERAL LOOP FURTHER DOWN, on
    // purpose: it spends its own settle budget getting the ORIGIN tile
    // (`character`/`map` above) genuinely open first, switches focus, spends
    // a SECOND, separate budget (`refocusSteps`) easing the switch partway
    // (or all the way -- the caller's choice), and only then falls through to
    // the ordinary settle logic below, which sees `config.refocus` non-empty
    // and stands down rather than spending a third, redundant round of steps.
    if (!config.refocus.empty()) {
        const int preSteps = config.settleSteps >= 0 ? config.settleSteps : (config.settle ? 16 : 0);
        const sim::MoveInput still{};
        for (int i = 0; i < preSteps; ++i) {
            session.step(still);
        }
        // THE SAME FOUR PUBLIC TOGGLES A KEYPRESS CALLS -- toggleCharacter()/
        // toggleMap()/toggleLetters()/toggleCasebook() -- never
        // toggleMenuFocused() directly, so this capture proves nothing a
        // player's own keyboard could not also have produced.
        bool refocused = false;
        if (config.refocus == "character") {
            session.toggleCharacter();
            refocused = true;
        } else if (config.refocus == "map") {
            session.toggleMap();
            refocused = true;
        } else if (config.refocus == "letters") {
            session.toggleLetters();
            refocused = true;
        } else if (config.refocus == "journal") {
            session.toggleCasebook();
            refocused = true;
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += refocused ? 1 : 0;
        for (int i = 0; i < config.refocusSteps; ++i) {
            session.step(still);
        }
    }

    // SHEETS BUILD. See SmokeRunConfig::tilePage's own header: N presses of
    // the same public call the 0/MORE key makes, so a tile's second page --
    // where the faction-ladder rows live -- can actually be photographed.
    // After the menu-opening flags above, which is the only order in which
    // there is a page to turn.
    for (int i = 0; i < config.tilePage; ++i) {
        session.nextTopicPage();
    }

    // S7. The cursor, last, so it survives every scripted line above it. This
    // is the flag that lets a frame be taken OF the row that does not fit --
    // see SmokeRunConfig::cursorRow on why that had to be possible.
    if (config.cursorRow > 0 && session.talking()) {
        const int want = config.cursorRow - 1;
        session.moveTopicCursor(want - session.topicCursor());
    }

    // VERIFICATION. See SmokeRunConfig::settle's own header. Zero-input
    // steps, same as `--hold`'s own loop -- nothing walks, nothing turns --
    // run comfortably past EasedToggle's default riseSteps (8) so the panel
    // this capture opened is fully open, not caught on its first tick.
    //
    // GATED ON A SCREENSHOT ACTUALLY BEING TAKEN, not on config.settle alone.
    // The field defaults true, but a scripted run that only reads back
    // scriptedWanted/scriptedLanded counters (most of this suite) never asked
    // for a picture, and the twelve-odd extra ticks of a live world this loop
    // runs are twelve-odd ticks a counter-only test has no reason to absorb.
    // "the default behavior for screenshot captures" is what was asked for,
    // not "sixteen more ticks of every scripted line in this project" --
    // those are the same sixteen ticks that were always safe for the frame
    // this exists to fix and never free for a hash or a count nobody asked to
    // move.
    // PLANNING SPRINT (item #1). SKIPPED WHEN `refocus` ALREADY SPENT ITS OWN
    // TWO BUDGETS, above -- running this too would spend a THIRD, unasked-for
    // round of settle steps past the deliberately mid-crossfade point a
    // `refocusSteps` capture asked to stop at, silently finishing the swap
    // the caller wanted photographed partway through.
    if (!config.screenshot.empty() && config.refocus.empty()) {
        // VERIFICATION ONLY. settleSteps overrides the count exactly when
        // given; otherwise this is unchanged from before that field existed
        // -- 16 or 0. See SmokeRunConfig::settleSteps's own header.
        const int steps = config.settleSteps >= 0 ? config.settleSteps : (config.settle ? 16 : 0);
        const sim::MoveInput still{};
        for (int i = 0; i < steps; ++i) {
            session.step(still);
        }
    }

    Framebuffer frame(config.session.width, config.session.height);
    result.stats = session.drawFrame(frame);
    // PLANNING SPRINT (item #1). THE NUMBERS BESIDE THE PICTURE -- see
    // SmokeRunResult::characterFocusAtCapture's own header. Read AFTER every
    // step() above has already run, so this is the exact value the frame
    // just drawn was a picture of.
    result.characterFocusAtCapture = session.characterFocusValue();
    result.mapFocusAtCapture = session.mapFocusValue();
    result.lettersFocusAtCapture = session.lettersFocusValue();
    result.journalFocusAtCapture = session.journalFocusValue();
    result.lampCount = session.lampCount();
    result.endTileX = session.body().tileX();
    result.endTileY = session.body().tileY();
    result.endBand = session.body().band();
    result.gullPresent = session.tavern().presentCount();
    {
        // How much of the ward this frame is actually looking at. `seen` comes
        // out of the renderer itself -- a body counted only when it put a pixel
        // on screen -- because a figure standing behind a warehouse is
        // submitted, sorted, projected and drawn nowhere, and counting those
        // would make "the street is busy" a claim about the roster rather than
        // about the frame. `near` is a plain radius and says how many were
        // within twelve tiles whether or not the camera was pointed at them.
        const sim::WardCensus roll = session.people().census();
        result.wardRoll = roll.alive;
        result.roofHomed = roll.roofHomed;
        result.onRoofNow = roll.onRoofNow;
        result.prey = roll.prey;
        result.preyUp = roll.preyUp;
        result.catches = session.people().catches();
        result.wardDrawn = static_cast<std::int32_t>(result.stats.wardActorsDrawn);
        const Camera view = session.camera();
        for (const sim::WardActor& actor : session.people().actors()) {
            if (!actor.visible()) {
                continue;
            }
            const float dx = static_cast<float>(actor.x) + 0.5F - view.x;
            const float dy = static_cast<float>(actor.y) + 0.5F - view.y;
            if (dx * dx + dy * dy <= 144.0F) {
                ++result.wardNear;
            }
        }
    }
    result.craftLevel = session.tavern().dialogue().skills().level(sim::kThieverySkill);
    result.pinsSet = session.picking() ? session.lockpicking().pinsSet() : 0;

    const int hour = session.timeOfDay() / 3600;
    const int minute = (session.timeOfDay() / 60) % 60;
    std::ostringstream summary;
    summary << "steps=" << config.steps << " at (" << result.endTileX << ',' << result.endTileY
            << ",z" << result.endBand << ") facing " << sim::compass_point(session.body().yaw())
            << " | " << (hour < 10 ? "0" : "") << hour << ':' << (minute < 10 ? "0" : "") << minute
            << ' ' << session.placeLabel() << " | lamps=" << result.lampCount
            // #79: `gull=`, and it used to say `actors=`. Printed one space from
            // `ward=661` it read as "one of the six hundred is on screen", which
            // it never was: it is how many of the Gilded Gull's own seventeen
            // are inside K03, a different population in a different building,
            // and it has no relationship to the frame at all. The HUD has always
            // called it "THE GULL n IN"; so does this now.
            << " gull=" << result.gullPresent
            << " art=" << (session.atlas().fromAuthoredArt() ? "custom" : "procedural")
            // #78: THE WARD'S OWN ROLL, PRINTED BESIDE THE TAPROOM'S. `ward` is
            // how many bodies the district holds, `seen` is how many of them
            // this frame actually drew a pixel of, and `near` is how many stand
            // within twelve tiles of the camera. The three differing is the
            // normal case and is exactly what the owner ruled OUT as a gate: a
            // cellar or a back lane at four in the morning is legitimately
            // empty, so this is EVIDENCE for a person to read and never a
            // threshold a build fails on.
            << " ward=" << result.wardRoll
            << " seen=" << result.wardDrawn
            << " near=" << result.wardNear
            // #80. THE TWO THINGS THIS ROUND ADDED, ON THE FRAME'S OWN LINE.
            // `roof` is beds-on-a-deck / bodies-standing-on-one-right-now, so a
            // capture at midnight and a capture at noon can be told apart by
            // reading it. `mice` is prey on the board out of prey on the roll,
            // and `ate` is how many the ward's cats and strays have caught
            // since the roster was baked -- which is the difference between a
            // dock district with a food chain and one with vermin nobody eats.
            << " roof=" << result.roofHomed << '/' << result.onRoofNow
            << " mice=" << result.preyUp << '/' << result.prey
            << " ate=" << result.catches
            << " figures=" << (session.actorSheet().fromAuthoredArt() ? "sheet" : "procedural")
            << " | world px=" << result.stats.worldPixels
            << " sky px=" << result.stats.skyPixels
            << " sprite px=" << result.stats.spritePixels
            << " actor px=" << result.stats.actorPixels << " luma="
            << result.stats.meanLuma << " colours=" << result.stats.distinctColours;
    if (config.street) {
        // WHO ANSWERED AND OUT OF WHICH TABLE, printed beside the frame. A
        // capture that says only "a conversation is open" cannot tell a
        // dockhand from a watchman, which is the entire claim being made.
        summary << " | street who=" << config.streetWho
                << " speaker=\"" << result.streetSpeaker << "\""
                << " key=" << (result.streetKey.empty() ? "-" : result.streetKey)
                << " talking=" << (result.talking ? "yes" : "no");
    }
    if (config.petition) {
        // TIME-AND-TENURE BUILD: which ground, who read the roll, and whether
        // the ROLL says the charge moved -- the claim is a changed register,
        // so the register's own answer is what gets printed.
        const PetitionLineResult& pet = result.petitionResult;
        summary << " | petition plot=" << (pet.plotId.empty() ? "-" : pet.plotId)
                << " speaker=\"" << pet.speaker << "\""
                << " opened=" << (pet.opened ? "yes" : "no")
                << " roll=\"" << pet.rollLine << "\""
                << " answer=\"" << pet.petitionLine << "\""
                << " duke=" << (pet.becameDuke ? "yes" : "no");
    }
    if (config.radiant) {
        // RADIANT BUILD: which errand, whose word it is, and whether the
        // BOARD says it moved -- the claim is a reachable generator, so the
        // board's own answer is what gets printed.
        const RadiantLineResult& job = result.radiantResult;
        summary << " | radiant id=" << job.objectiveId
                << " giver=\"" << job.giver << "\""
                << " opened=" << (job.opened ? "yes" : "no")
                << " offered=" << (job.offered ? "yes" : "no")
                << " taken=" << (job.taken ? "yes" : "no")
                << " journal=" << (job.journal ? "yes" : "no")
                << " brief=\"" << job.brief << "\"";
    }
    if (config.wait) {
        summary << " | wait open=" << (session.waitOpen() ? "yes" : "no")
                << " rows=" << session.waitRows().size()
                << " refusal=\"" << session.waitRefusal() << "\"";
    }
    if (config.trail) {
        const sim::Casebook& notes = session.casebook();
        summary << " | trail read=" << result.trailRead << '/' << result.trailWalked
                << " leads=" << notes.known().size()
                << " cold=" << notes.coldCount()
                << " unreached=" << gTrailUnreached
                << " dread=" << notes.dread()
                << " closed=" << (notes.closed() ? "yes" : "no")
                << " flame=" << session.legend().row(sim::LegendTrack::Flame).rung
                << " called=" << session.legend().title()
                << " notes=" << (session.casebookOpen() ? "open" : "shut");
    }
    if (config.burgle) {
        summary << " | burgle beats=" << result.burgleBeats << '/' << kBurgleBeats
                << " mask=" << gBurgleBeatMask
                << " lift=" << (session.tavern().dialogue().crimes().tally(sim::Crime::Lift) > 0
                                    ? "tried"
                                    : "none")
                << " light=" << session.tavern().lightOnPlayer()
                << " noise=" << session.tavern().playerNoise()
                // WHETHER THE STEALTH BEAT PROVED ANYTHING, printed beside it.
                // `watchers=` is how many awake, upright bodies were in range
                // when beat 2 was judged; a `hidden` with `watchers=0` beside it
                // is a fact about the hour and not about being unseen, and the
                // S9 review had to read the source to work that out. Now it is
                // one word away from the claim.
                << " watchers=" << gBurgleWatchers
                << " " << (session.hidden() ? "hidden" : "seen") << " "
                << " picking=" << (session.picking() ? "yes" : "no")
                << " pins=" << (session.picking() ? session.lockpicking().pinsSet() : 0)
                << " nextprobes=" << gLockEndingProbes
                << " picks=" << session.picks()
                << " locks open=" << session.tavern().openedLocks()
                << " jammed=" << session.tavern().jammedLocks()
                << " forced=" << session.tavern().forcedLocks()
                << " cracked=" << session.tavern().crackedBoxes()
                << " cracksmanship="
                << session.tavern().dialogue().skills().level(sim::kThieverySkill)
                << " skyrunning="
                << session.tavern().dialogue().skills().level(sim::kRoofSkill);
    }
    if (config.nemesis) {
        const sim::Nemesis* worst = session.tavern().nemesis().worst();
        summary << " | nemesis beats=" << result.nemesisBeats << '/' << kNemesisBeats;
        // WHO IS ON THE FRAME, named, so a capture cannot quietly photograph
        // the wrong docker. The first shipped attempt at the `talk` ending did
        // exactly that -- his stool and Wick Hempson's are one tile apart.
        if (session.talking()) {
            summary << " talking to " << session.tavern().dialogue().speaker().name;
        }
        if (worst != nullptr) {
            summary << " " << worst->who << " x" << worst->wins;
            if (!worst->title.empty()) {
                summary << " " << worst->title;
            }
            const sim::ChapterRaw* house =
                session.tavern().nemesis().chapters().at(worst->chapter);
            if (house != nullptr) {
                summary << " of " << house->displayName << " (" << worst->members.size()
                        << " members, toll "
                        << session.tavern().nemesis().tollPercent(worst->faction) << "%)";
            }
            if (worst->holdsGround()) {
                summary << " holds "
                        << session.ward()
                               .raws()
                               .plots()[static_cast<std::size_t>(
                                   session.ward().plots()[static_cast<std::size_t>(worst->plot)]
                                       .raw)]
                               .name;
            }
        }
    }
    if (config.roofs || config.skyrun) {
        const sim::DialogueDirector& talk = session.tavern().dialogue();
        const std::int32_t roofs = talk.factions().indexOf("skyrunners");
        const std::int32_t watch = talk.factions().indexOf("watch");
        summary << " | roofs stages=" << result.skyrunStages << " rank="
                << talk.standings().rank(roofs) << ' ' << talk.standings().rankTitle(roofs)
                << " standing=" << talk.standings().standing(roofs)
                << " watch=" << talk.standings().standing(watch)
                << " climbs=" << talk.crimes().tally(sim::Crime::RoofRun)
                << " lifts=" << talk.crimes().tally(sim::Crime::Lift)
                << " cracks=" << talk.crimes().tally(sim::Crime::Burgle)
                << " leans=" << talk.crimes().tally(sim::Crime::Extort)
                << " fences=" << talk.crimes().tally(sim::Crime::Fence)
                << " runs=" << talk.crimes().tally(sim::Crime::Smuggle)
                << " heat=" << talk.crimes().heat()
                << " warrant=" << (talk.crimes().warrant() ? "yes" : "no")
                << " skyrunning=" << talk.skills().level(sim::kRoofSkill)
                // S6: and whether the ward took him for any of it. A scripted
                // line that fell short because a watchman crossed the room is a
                // very different failure from one that fell short because a
                // beat is broken, and the summary has to be able to tell them
                // apart.
                << " arrests=" << talk.crimes().arrests() << " sentence="
                << sim::sentenceName(talk.crimes().lastSentence());
    }
    if (config.contract) {
        const sim::DialogueDirector& talk = session.tavern().dialogue();
        const sim::Stash& sack = talk.crimes().stash();
        summary << " | work beats=" << result.contractBeats << '/' << kContractBeats
                << " day=" << talk.contracts().day()
                << " open=" << talk.contracts().contracts().size()
                << " taken=" << talk.contracts().takenCount()
                << " paid=" << talk.contracts().paidCount()
                << " lost=" << talk.contracts().failedCount()
                << " earned=" << talk.contracts().coinEarned()
                << " scalps=" << sack.count(sim::Contraband::Scalp)
                << " load=" << sack.illicitWeight() << "dr"
                << " heat=" << talk.crimes().heat();
    }
    if (config.flame) {
        const sim::DialogueDirector& talk = session.tavern().dialogue();
        const std::int32_t temple = talk.factions().indexOf("temple");
        summary << " | flame stages=" << result.flameStages << '/'
                << (talk.quests().find("flame-disciple") == nullptr
                        ? 0
                        : static_cast<int>(talk.quests().find("flame-disciple")->stages.size()))
                << " rank=" << talk.standings().rank(temple) << ' '
                << talk.standings().rankTitle(temple)
                << " standing=" << talk.standings().standing(temple)
                << " influence=" << talk.standings().influence(temple)
                << " known=" << talk.grimoire().size() << " forged="
                << talk.grimoire().craftedCount()
                << " linkcraft=" << talk.skills().level(sim::kCraftingSkill);
    }
    // A SCRIPTED RUN THAT FELL SHORT SAYS SO, AND FAILS.
    //
    // S4's did neither: runFlameLine returned a stage count that runSmoke threw
    // away, and a run that landed ZERO of six stages photographed the wrong
    // person and exited 0. The S4 review found it. A capture tool that reports
    // success while photographing the wrong thing will mislabel a future
    // sprint's evidence, so this is a hard failure and not a warning.
    if (result.scriptFellShort()) {
        summary << " | WARNING: scripted run landed " << result.scriptedLanded << " of "
                << result.scriptedWanted << " beats";
    }
    result.summary = summary.str();

    // The corner stamp, unless somebody is standing in it: while a conversation
    // is open the top-left is the speaker's name, and two strings in the same
    // eleven characters of screen is unreadable in a capture.
    // NOT WHILE ANY PANEL IS UP. The top-left is the speaker's name whenever
    // the conversation surface is drawing -- and S10 gave that surface two more
    // users, the casebook and the key list, neither of which sets `talking`.
    // The first S10 capture shipped "GRANADAD 0.10.0" printed straight through
    // "THE CASEBOOK" because this test only knew about the third of them.
    if (config.stamp && !result.talking && !session.casebookOpen() && !session.keysOpen() &&
        !session.districtMapOpen()) {
        // DERIVED, NOT TYPED. S9's read "GRANADAD S6" -- a literal three sprints
        // out of date, burnt into the top-left of every capture including all
        // four of S9's own, and found by the review in a PNG rather than in the
        // source. It reads the project version now, which CMake sets in one
        // place and build_info() carries, so there is nothing here left to
        // forget to update.
        //
        // AND IT IS DRAWN AT 1:1, WHICH IS METADATA-SIZED. This is a capture
        // stamp and not a HUD element -- the windowed game has never drawn it,
        // as `--version` and the F1 keys page have always been where a player
        // is told what build they are on. It was drawn at the full HUD scale
        // anyway, so every screenshot this project has ever produced carried
        // "GRANADAD 0.10.0" in 222 by 21 pixels of the top-left corner, and
        // that corner is the first thing anybody looks at. Provenance is still
        // burnt into every frame; it costs a ninth of what it did.
        const sim::BuildInfo info = sim::build_info();
        std::string stamp = "GRANADAD ";
        stamp.append(info.version);
        drawText(frame, 3, 3, stamp, Rgb{0.55F, 0.53F, 0.46F}, 0.55F, 1);
    }

    result.ok = !result.scriptFellShort();
    if (!config.screenshot.empty()) {
        // The PNG is still written. A frame of a run that fell short is
        // evidence OF the shortfall, and deleting it would make the failure
        // harder to diagnose rather than easier -- but ok stays false.
        const Framebuffer output =
            config.captureScale > 1 ? upscaleNearest(frame, config.captureScale) : frame;
        result.ok = writePng(output, config.screenshot) && result.ok;
    }
    return result;
}

}  // namespace granadad::render
