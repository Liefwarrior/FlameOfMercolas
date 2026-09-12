#include "granadad/render/session.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
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
#include "granadad/sim/path_finder.hpp"
#include "granadad/sim/stealth.hpp"

namespace granadad::render {

namespace {

constexpr float kPi = 3.14159265358979323846F;

/// CASE WATCH. Holds Session::watchDepth_ raised for one recorded verb's own
/// body, so a verb a verb calls internally (interact()'s closing examine(),
/// a step-pump courier) records nothing -- the replay's own top-level call
/// will make those again. See setWatchRecorder in session.hpp.
struct WatchDepthGuard {
    int& depth;
    explicit WatchDepthGuard(int& d) noexcept : depth(d) { ++depth; }
    ~WatchDepthGuard() noexcept { --depth; }
    WatchDepthGuard(const WatchDepthGuard&) = delete;
    WatchDepthGuard& operator=(const WatchDepthGuard&) = delete;
};

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

}  // namespace

// THE FIGURE TABLES ARE PUBLIC (3D BUILD, A lane): render3d's actor instancer
// draws the same sixteen kinds of body out of the same two tables, so a
// bouncer is the Watch's build in 3D exactly as he is on the sheet.

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
FigureScale figureScaleOf(sim::WardType type) noexcept {
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

namespace {

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
}  // namespace

sim::WardType figureForRole(sim::ActorRole role, std::int32_t id) noexcept {
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

namespace {


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

/// SHIP NOTE MOVE 3. The opening hint, GENERATED from the live bindings in
/// the live device's vocabulary -- which closes the S3 verification gap the
/// constructor stated out loud (a hand-written "TAB YOUR NOTES..." that went
/// stale the moment Menu was rebound off Tab).
///
/// UI-EA (LANE HUD): LIVE VERBS ONLY, AND FEWER OF THEM. The old band sold
/// "< > MORE PAGES" on the street, where PagePrev/PageNext do nothing at all
/// (flow map violation #11 -- a dead control advertised in the first ten
/// seconds of play). The tutor tier names the three verbs a stranger can
/// actually press where they stand: the notes, the map, the hand. Keycap
/// then verb, no filler words -- "J NOTES  M MAP  E USE" on the shipped
/// keyboard, the pad's own buttons the moment a pad speaks.
[[nodiscard]] std::string openingHintLine(const ControlSettings& controls, InputDevice device) {
    return std::string(promptLabel(controls, Action::Menu, device)) + " NOTES  " +
           std::string(promptLabel(controls, Action::Map, device)) + " MAP  " +
           std::string(promptLabel(controls, Action::Interact, device)) + " USE";
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
    if (config_.spawnPitchGiven) {
        body_->setPitch(config_.spawnPitch);
    }
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
    // COURIER CASE. THE SECOND CASE AND ITS PAPER, loaded the same never-
    // throws way and bound the same way -- but with NO start lead in its
    // file, begin() leaves every lead Unheard, so the errand is invisible
    // until the courier beat calls hear(). A missing mission_sheet.json is
    // a session with one case in it, exactly as before this build.
    sheetRaws_ = sim::CasebookRaws::loadFile(sim::missionSheetRawsPath(config_.contentDir));
    sheetBook_.begin(sheetRaws_, caseNowSeconds());
    sheetLetterRaws_ =
        sim::LetterRaws::loadFile(sim::missionSheetLetterRawsPath(config_.contentDir));
    // EVICTION CASE. THE THIRD CASE AND ITS PAPER, the courier trio's exact
    // contract: never-throws loaders, begin() with no start lead so every
    // lead sits Unheard and the errand is invisible until Maell's own
    // conversation opens it (settleTakeWrit). A missing eviction.json is a
    // session with two cases in it, exactly as before this build.
    evictRaws_ = sim::CasebookRaws::loadFile(sim::evictionRawsPath(config_.contentDir));
    evictBook_.begin(evictRaws_, caseNowSeconds());
    evictLetterRaws_ =
        sim::LetterRaws::loadFile(sim::evictionLetterRawsPath(config_.contentDir));
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
        // into Interact. SHIP NOTE MOVE 3 CLOSED THE S3 VERIFICATION GAP this
        // comment used to state: the hint is GENERATED from the live bindings
        // now (openingHintLine, above), so a rebound Menu renames itself here
        // by construction -- and noteInputDevice() re-words it live when a
        // pad speaks while it is still up. On the shipped table this reads
        // "J NOTES  M MAP  E USE" -- J per the owner's own "use J for
        // journal since that's how it's done by convention".
        //
        // UI-EA (LANE HUD): FOUR SECONDS, NOT TWELVE. The hint is tutor
        // tier now -- the message-row narration hold, not a twelfth of a
        // minute of furniture. A device change while it is still on screen
        // re-words it AND re-raises it whole (noteInputDevice -- the tutor
        // tier's own "raised in full on device change"), so the four
        // seconds are four seconds in whichever hand holds the machine.
        message_ = openingHintLine(controls_, promptDevice_);
        messageSteps_ = 60 * 4;
    }
    // TASK #83. SNAPPED, NOT EASED. Nobody pressed a key to reach whichever of
    // these is true on frame one -- SessionConfig chose it -- so there is
    // nothing to animate from and the panel/alert draw at full strength
    // immediately, exactly as they always have. See EasedToggle::snapTo.
    panelAnim_.snapTo(conversingNow());
    alertAnim_.snapTo(!message_.empty());
    // DISTRICT PHASE D. SEEDED FROM WHERE THE BODY ACTUALLY IS, BEFORE
    // syncPanelAnim() BELOW EVER RUNS -- lastPlayerHp_'s own reasoning,
    // verbatim: a session that boots the player already standing in Tarwalk
    // (or, with --spawn, anywhere else that has a name) has not CROSSED into
    // it, and an empty lastPlaceName_ here would read the very first
    // syncPanelAnim() call as a crossing and open the game on a plate nobody
    // walked through a gate to earn. Nothing announces on frame one, ever.
    lastPlaceName_.assign(
        sim::docks::placeNameAt(body_->tileX(), body_->tileY(), body_->band()));
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
    // UI-EA (LANE HUD). The earned-text toggles: syncPanelAnim() above has
    // just seeded every last-seen edge (hudEdgesSeeded_) and armed nothing,
    // so all three targets are the family's frame-one truth -- closed.
    clockAnim_.snapTo(clockAnim_.target());
    purseAnim_.snapTo(purseAnim_.target());
    wheelHint_.anim.snapTo(wheelHint_.anim.target());
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
    // DISTRICT PHASE D. The same snap, and with lastPlaceName_ seeded above
    // the target syncPanelAnim() just computed is always closed -- which is
    // the point: the plate cannot arm its own opening bump on frame one.
    placePlateAnim_.snapTo(placePlateAnim_.target());
    // THE CASEBOOK PASS. The same snap, and with nothing ever having been
    // looked at on frame one the target syncPanelAnim() just chose is always
    // closed -- which is the point: the notice cannot arm its own opening bump
    // on the first frame of a session.
    casePlateAnim_.snapTo(casePlateAnim_.target());
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
    lastHandsUp_ = tavern_->playerHandsUp();
    lastClosedBooks_ = closedBookCount();
    // And the bed starts the moment there are ears: the same
    // playerInside()-keyed choice step() re-asserts every step (re-asserting
    // the current bed is a documented no-op), so a body standing still on the
    // quay hears the harbour without having to move first.
    audio_->startBed(tavern_->playerInside() ? audio::BedId::Interior
                                             : audio::BedId::Harbour);
    // THE LOT PASS: and the music's zone, on the same signal.
    audio_->music().setZone(tavern_->playerInside() ? audio::MusicZone::Interior
                                                    : audio::MusicZone::Docks);
    audio_->setTimeOfDay(timeOfDay_);
}

std::int32_t Session::closedBookCount() const noexcept {
    return (casebook_.closed() ? 1 : 0) + (sheetBook_.closed() ? 1 : 0) +
           (evictBook_.closed() ? 1 : 0);
}

void Session::syncTavernToBody() {
    tavern_->setPlayer(body_->x(), body_->y(), body_->band());
    // ACTION-COMBAT BUILD. WHICH WAY THE BODY IS FACING, pushed beside the
    // position the same step. The sightline raycast (VETO 1) casts down
    // playerYaw_, so facing is authoritative exactly the way position is -- and
    // without this every swing and touch-cast aims due-north (yaw 0), which is
    // the SIM lane's one downstream requirement of this file. A room the client
    // never syncs (a pre-combat capture, the workload) keeps its default facing.
    tavern_->setPlayerYaw(body_->yaw());
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
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Climb);
    const WatchDepthGuard watchGuard(watchDepth_);
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
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Drop);
    const WatchDepthGuard watchGuard(watchDepth_);
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
    if (refusedInCustody()) {
        return;
    }
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
    if (refusedInCustody()) {
        return;
    }
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
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Crouch);
    const WatchDepthGuard watchGuard(watchDepth_);
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
    if (refusedInCustody()) {
        return;
    }
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

int Session::leadInLookReach() const {
    // THE SAME WALK examine() MAKES, WITH THE HANDS KEPT STILL. See this
    // method's own header in session.hpp on why it cannot simply call look().
    //
    // Casebook::look picks the nearest lead by MANHATTAN distance within
    // kLookRangeTiles, at the body's own band, ties broken on the earlier
    // authored index; and examine() widens that by standing the look at four
    // axis offsets, one ring at a time, up to the Flame's own bonus. Both
    // halves are reproduced here rather than approximated, because a
    // crosshair that named a lead the key would not find is worse than one
    // that named nothing.
    if (casebook_.raws() == nullptr) {
        return -1;
    }
    const std::vector<sim::Lead>& leads = casebook_.raws()->leads();
    const std::int32_t band = body_->band();
    const auto nearestFrom = [&](std::int32_t fromX, std::int32_t fromY) {
        int best = -1;
        std::int32_t bestDistance = 0;
        for (std::size_t i = 0; i < leads.size(); ++i) {
            const sim::Lead& lead = leads[i];
            if (lead.site.band != band) {
                continue;
            }
            const std::int32_t dx = lead.site.x - fromX;
            const std::int32_t dy = lead.site.y - fromY;
            const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (distance > sim::kLookRangeTiles) {
                continue;
            }
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        return best;
    };
    const std::int32_t tileX = body_->tileX();
    const std::int32_t tileY = body_->tileY();
    if (const int here = nearestFrom(tileX, tileY); here >= 0) {
        return here;
    }
    const std::int32_t bonus = legend().lookRangeBonus();
    for (std::int32_t ring = 1; ring <= bonus; ++ring) {
        const std::int32_t offsets[4][2] = {{ring, 0}, {-ring, 0}, {0, ring}, {0, -ring}};
        for (const auto& offset : offsets) {
            if (const int found = nearestFrom(tileX + offset[0], tileY + offset[1]); found >= 0) {
                return found;
            }
        }
    }
    return -1;
}

int Session::sheetLeadInLookReach() const {
    // COURIER CASE. leadInLookReach()'s exact walk over the second book --
    // the same Manhattan pick, the same ties-to-earlier-index rule, the same
    // ring widening -- so the crosshair can name the errand's own sites with
    // the identical honesty. Kept a sibling rather than a parameter because
    // the two books are two members, and a helper taking "which book" would
    // be the only call site in the build that has to name one.
    if (sheetBook_.raws() == nullptr) {
        return -1;
    }
    const std::vector<sim::Lead>& leads = sheetBook_.raws()->leads();
    const std::int32_t band = body_->band();
    const auto nearestFrom = [&](std::int32_t fromX, std::int32_t fromY) {
        int best = -1;
        std::int32_t bestDistance = 0;
        for (std::size_t i = 0; i < leads.size(); ++i) {
            const sim::Lead& lead = leads[i];
            if (lead.site.band != band) {
                continue;
            }
            const std::int32_t dx = lead.site.x - fromX;
            const std::int32_t dy = lead.site.y - fromY;
            const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (distance > sim::kLookRangeTiles) {
                continue;
            }
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        return best;
    };
    const std::int32_t tileX = body_->tileX();
    const std::int32_t tileY = body_->tileY();
    if (const int here = nearestFrom(tileX, tileY); here >= 0) {
        return here;
    }
    const std::int32_t bonus = legend().lookRangeBonus();
    for (std::int32_t ring = 1; ring <= bonus; ++ring) {
        const std::int32_t offsets[4][2] = {{ring, 0}, {-ring, 0}, {0, ring}, {0, -ring}};
        for (const auto& offset : offsets) {
            if (const int found = nearestFrom(tileX + offset[0], tileY + offset[1]); found >= 0) {
                return found;
            }
        }
    }
    return -1;
}

void Session::examine() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Examine);
    const WatchDepthGuard watchGuard(watchDepth_);
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
    const auto lookIn = [&](sim::Casebook& book) {
        sim::LookResult got = book.look(tileX, tileY, band, now);
        if (!got.found && got.lead < 0 && reach > sim::kLookRangeTiles) {
            // Nothing within the base reach. Walk outward one ring at a time
            // up to the bonus, standing the look at each offset -- integer,
            // bounded, and it cannot see anything a body one tile further
            // along could not.
            for (std::int32_t ring = 1; ring <= reach - sim::kLookRangeTiles && got.lead < 0;
                 ++ring) {
                const std::int32_t offsets[4][2] = {
                    {ring, 0}, {-ring, 0}, {0, ring}, {0, -ring}};
                for (const auto& offset : offsets) {
                    got = book.look(tileX + offset[0], tileY + offset[1], band, now);
                    if (got.lead >= 0) {
                        break;
                    }
                }
            }
        }
        return got;
    };
    sim::LookResult saw = lookIn(casebook_);
    // COURIER CASE. THE SECOND BOOK GETS THE IDENTICAL LOOK, asked only when
    // the first found nothing at all here -- member order is the tiebreak,
    // fixed and deterministic. The two files share exactly one site (the
    // Mission's back room), and there the Bloodletter always answers first;
    // the errand's own close never rides this key anyway -- delivery is
    // stepSheetCase()'s scripted arrival, because walking in with the man IS
    // the act, and no press should be owed on top of it.
    if (saw.lead < 0) {
        saw = lookIn(sheetBook_);
    }
    // EVICTION CASE. And the third book third, the same member-order
    // tiebreak one book deeper. Its two close leads never ride this key
    // either -- both are heard and read only by the case's own scripted
    // beats (stepEvictCase, settleYieldWrit) -- and every non-close lead of
    // its sits over four tiles from every lead of the other two books, so
    // the order here can never actually decide anything a player sees.
    if (saw.lead < 0) {
        saw = lookIn(evictBook_);
    }
    // THE CLUE IS THE MESSAGE. It is what the player walked here for, so it
    // gets the row whole; how many leads it opened is on the CASE row, which is
    // permanent and where a count belongs. A DEAD END SAYS SO OUT LOUD, though
    // -- walking across the district to learn that the sea is the wrong
    // question is work, and a game that let that read the same as a blank tile
    // would be a game telling you not to look.
    const bool cold = saw.found && saw.opened == 0;
    say(cold ? saw.line + "  (COLD)" : saw.line);
    // THE CASEBOOK PASS: THE MOMENT LEADS OPEN, ANNOUNCED.
    //
    // `saw.opened` has been computed correctly since S10 and thrown away
    // everywhere but the (COLD) suffix above. This is the whole of the defect
    // the owner's playthrough found: he read Crell's ledger, THREE leads went
    // into the book, and the only thing on the frame that said so was a dim
    // grey corner row changing from CASE 4/6 to CASE 4/9.
    //
    // THE RISING EDGE AND NOTHING ELSE. `saw.opened` counts leads that were not
    // already known (Casebook::hear's own return), so a lead the trail
    // CONVERGES on -- the Drowned Hold is named by four separate leads -- opens
    // nothing new the second time and fires nothing. Looking at an
    // already-read site returns before any of this. There is no way to make
    // this notice repeat by standing still and pressing the key.
    if (saw.opened > 0) {
        // THROUGH promptLabel, NOT keyName(primary[Menu]): the plate has to
        // name the key in the hand that is holding the machine -- "J" on a
        // keyboard, "D-PAD UP" on a pad -- and the raw primary slot is
        // keyboard-only wording, the exact literal class the opening hint
        // already left behind (openingHintLine). UI-EA: the notice grammar
        // is armCasePlate's -- "3 NEW LEADS - J", the spec's own line.
        armCasePlate(std::to_string(saw.opened) +
                     (saw.opened == 1 ? " NEW LEAD" : " NEW LEADS"));
    }
}

void Session::armCasePlate(std::string news) {
    // UI-EA (LANE HUD): see the declaration. The dash and the live key are
    // the whole chrome a notice gets; the news is the ward's own words and
    // arrives untouched.
    casePlateText_ = std::move(news) + " - " +
                     std::string(promptLabel(controls_, Action::Menu, promptDevice_));
    casePlateShowSteps_ = kCasePlateShowSteps;
    syncPanelAnim();
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
// COURIER CASE: the quiet tenant
// ---------------------------------------------------------------------------
//
// The owner's second authored case, session-scripted end to end. Everything
// here is presentation-side state driving the SAME public verbs a keypress
// drives -- hear() on the second book, say() on the alert row, the identical
// plate machinery the lead-opened notice uses -- so the population baseline
// and both gate workloads (which never construct a Session) cannot see any of
// it. Determinism is the scripted line's business: --case runs the errand
// through walkAcrossDistrict and the ordinary verbs, and test_case_line runs
// it twice and requires identical books.

namespace {
/// Six seconds of world-steps between the opening page going down and Onna's
/// hail, and the same again before the follow-up line: long enough that the
/// street exists first, short enough that the sheet IS the first thing that
/// happens to a new player.
constexpr int kCourierDelaySteps = 6 * granadad::sim::kStepsPerSecond;
/// How close TAKE HIM UP reaches, in tiles -- a body you are standing over,
/// the same arm's length the punch that put him down was thrown at.
constexpr std::int32_t kTakeReachTiles = 2;
}  // namespace

void Session::courierDeliverNow() {
    recordWatchOp(WatchOpKind::CourierNow);
    const WatchDepthGuard watchGuard(watchDepth_);
    if (courierStage_ != 0 || !sheetRaws_.loaded() || !sheetBook_.active()) {
        return;
    }
    // THE SHEET INTO THE HAND. Hearing the first lead is the whole delivery:
    // the errand lands in the book, and the letter is `handed`, so the same
    // act puts the sheet itself on the Letters tile -- unlockedLetters()
    // derives it, nothing is flagged.
    const std::int32_t first = sheetRaws_.indexOf("gull-door");
    if (first < 0) {
        return;
    }
    (void)sheetBook_.hear(first, caseNowSeconds());
    say("ONNA, AT YOUR ELBOW: PAPER FOR YOU, OUT OF THE MISSION. IT COULD NOT WAIT.");
    // The lead-opened plate, in its own words -- the same field, the same
    // countdown, the same live Menu binding the new-leads notice builds from,
    // all through armCasePlate so the device-aware key and the notice grammar
    // ("A MISSION SHEET - J") are one fact in one place.
    armCasePlate("A MISSION SHEET");
    courierStage_ = 1;
    courierSteps_ = 0;
}

const sim::Actor* Session::sheetQuarry() const {
    // The quiet tenant, by ROLE rather than by name: the roster names exactly
    // one SkyrunnerContact and the roster is never reordered. Present, on his
    // feet gone from under him, and still where he fell.
    for (const sim::Actor& actor : tavern_->actors()) {
        if (actor.role() == sim::ActorRole::SkyrunnerContact && actor.present() &&
            actor.activity() == sim::Activity::Downed) {
            return &actor;
        }
    }
    return nullptr;
}

bool Session::caseTakeReady() const {
    if (!sheetCaseLive() || sheetCarry_) {
        return false;
    }
    const sim::Actor* quarry = sheetQuarry();
    if (quarry == nullptr || quarry->band() != body_->band()) {
        return false;
    }
    const std::int32_t dx = quarry->tileX() - body_->tileX();
    const std::int32_t dy = quarry->tileY() - body_->tileY();
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= kTakeReachTiles;
}

void Session::stepSheetCase() {
    if (!sheetRaws_.loaded() || !sheetBook_.active()) {
        return;
    }
    // 1. THE COURIER. Counts only while the world has the keys -- never under
    // the opening page, a menu, a conversation or a lock -- so the hail can
    // never land on a surface that would swallow it, and never lands in a
    // scripted capture at all unless the config asked for a courier.
    if (config_.courier && courierStage_ < 2 && !firstRun_ && !conversingNow() && !picking()) {
        if (courierStage_ == 0) {
            if (++courierSteps_ >= kCourierDelaySteps) {
                courierDeliverNow();
            }
        } else if (++courierSteps_ >= kCourierDelaySteps) {
            // The one follow-up, and then the paper does the teaching.
            say("THE SHEET IS IN YOUR LETTERS. MAELL DOES NOT WRITE TWICE.");
            courierStage_ = 2;
        }
    }
    // 2. THE TAKE, NAMED WHEN IT IS LIVE. Once per downing: the flag re-arms
    // when he is back on his feet, so a player who hesitated is told again
    // the next time they earn the moment, and never told twice for standing
    // still.
    if (caseTakeReady()) {
        if (!sheetTakeSaid_) {
            say("FINCH IS ON THE BOARDS. " +
                std::string(promptLabel(controls_, Action::Interact, promptDevice_)) +
                " TAKES HIM UP.");
            sheetTakeSaid_ = true;
        }
    } else {
        sheetTakeSaid_ = false;
    }
    // 3. THE DELIVERY. Walking into the back room with the man IS the act --
    // no press owed on top of it. The scripted look lands on the close lead's
    // own site, which this body is within look range of, so the book closes
    // through the one verb every lead closes through.
    if (sheetCarry_ && !sheetBook_.closed()) {
        const std::int32_t close = sheetRaws_.indexOf("bring-him-in");
        if (close >= 0) {
            const sim::Lead& lead =
                sheetRaws_.leads()[static_cast<std::size_t>(close)];
            const std::int32_t dx = lead.site.x - body_->tileX();
            const std::int32_t dy = lead.site.y - body_->tileY();
            if (lead.site.band == body_->band() &&
                (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= sim::kLookRangeTiles) {
                const std::int64_t now = caseNowSeconds();
                (void)sheetBook_.hear(close, now);
                const sim::LookResult done =
                    sheetBook_.look(lead.site.x, lead.site.y, lead.site.band, now);
                sheetCarry_ = false;
                say(done.line);
                // Device-aware through armCasePlate, the courier plate's
                // twin: "THE ERRAND IS PAID - J", the pad's own button when
                // a pad holds the machine (Ship phase).
                armCasePlate("THE ERRAND IS PAID");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// EVICTION CASE: the writ, the door, and the two ways it closes
// ---------------------------------------------------------------------------

namespace {
/// The evening the door will answer a knock -- the rota's own home hours.
/// Serfs are on the quay to six and walk home after (ward_actors.cpp Anchor
/// window 07:00-18:00; ReturnHome night term at 22:00), so from six the
/// family is arriving and by midnight abed. Knock outside this and the lane
/// answers instead of the door -- the brief's "once people are home" made a
/// gate rather than a suggestion.
constexpr int kEvictHomeFromHour = 18;
constexpr int kEvictHomeUntilHour = 24;
/// How close the door verbs reach: the family-door lead's own look range, an
/// arm's length from the wood, the same measure every lead is stood at.
constexpr std::int32_t kEvictDoorReachTiles = sim::kLookRangeTiles;
}  // namespace

void Session::syncEvictionTopics() {
    // THE STAGE, HANDED TO THE DIRECTOR the way the ground plot is: 0 offers
    // the hire, 1 offers the walk-back, 2 offers neither -- and the one line
    // of continuity the design note allows rides the courier case having
    // closed. Fed before every conversation opens (interact, below), so
    // Maell's writ row is built knowing where the errand stands. A missing
    // eviction.json (the loader's never-throws contract) reads as stage 2:
    // no case, no topic, exactly the empty-trail state a missing file leaves
    // everywhere else.
    const int stage =
        !evictRaws_.loaded() ? 2 : (evictBook_.closed() ? 2 : (evictCaseLive() ? 1 : 0));
    tavern_->dialogue().setEvictionCase(stage, sheetBook_.closed());
}

void Session::settleTakeWrit() {
    // THE HIRE. The director already read the open-hand bar and answered the
    // speech (dialogue.cpp TakeWrit); this is the CASE half of the intent,
    // the ReadRoll/Petition split one book further. Hearing the first lead
    // is the whole delivery, exactly the courier's courierDeliverNow: the
    // errand lands in the book, and the writ letter is `handed`, so the same
    // act puts it on the Letters tile (unlockedLetters derives it).
    if (evictCaseLive() || evictBook_.closed() || !evictRaws_.loaded()) {
        return;
    }
    const std::int32_t first = evictRaws_.indexOf("writ-in-hand");
    if (first < 0) {
        return;
    }
    const std::int64_t now = caseNowSeconds();
    (void)evictBook_.hear(first, now);
    // AND READ, scripted, the same contract both closes keep: the docket is
    // Maell's own briefing, given to your face with the writ in your hand --
    // there is nothing left to walk to and discover about it. Reading it is
    // what opens the Netters' gate lead (the authored chain's first link);
    // without this the examine key at the gate reads an Unheard corner of
    // the ward and the trail never starts.
    const sim::Lead& lead = evictRaws_.leads()[static_cast<std::size_t>(first)];
    (void)evictBook_.look(lead.site.x, lead.site.y, lead.site.band, now);
    syncEvictionTopics();
    armCasePlate("A WRIT OF DISTRAINT");
}

void Session::settleYieldWrit() {
    // THE DISRUPT CLOSE. The director already priced the walk-back through
    // the ledger's own WalkedOut (dialogue.cpp YieldWrit); this closes the
    // book on the stood-down lead -- read HERE, never by the examine key,
    // the same scripted-look contract the courier's delivery keeps. The
    // family got whatever warning the knock bought them, and no weapon is
    // granted: the point of the path.
    if (!evictCaseLive() || evictBook_.closed()) {
        return;
    }
    const std::int32_t close = evictRaws_.indexOf("stood-down");
    if (close < 0) {
        return;
    }
    const sim::Lead& lead = evictRaws_.leads()[static_cast<std::size_t>(close)];
    const std::int64_t now = caseNowSeconds();
    (void)evictBook_.hear(close, now);
    const sim::LookResult done = evictBook_.look(lead.site.x, lead.site.y, lead.site.band, now);
    say(done.line);
    syncEvictionTopics();
    armCasePlate("THE WRIT IS GIVEN BACK");
}

int Session::evictLeadInLookReach() const {
    // sheetLeadInLookReach()'s exact walk over the third book, so the
    // crosshair names the eviction's sites with the identical honesty. The
    // two close leads are excluded here for the same reason the examine key
    // never reads them: they belong to the scripted beats, not to a look.
    if (evictBook_.raws() == nullptr) {
        return -1;
    }
    const std::vector<sim::Lead>& leads = evictBook_.raws()->leads();
    const std::int32_t band = body_->band();
    const auto nearestFrom = [&](std::int32_t fromX, std::int32_t fromY) {
        int best = -1;
        std::int32_t bestDistance = 0;
        for (std::size_t i = 0; i < leads.size(); ++i) {
            const sim::Lead& lead = leads[i];
            if (lead.site.band != band || lead.close) {
                continue;
            }
            const std::int32_t dx = lead.site.x - fromX;
            const std::int32_t dy = lead.site.y - fromY;
            const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (distance > sim::kLookRangeTiles) {
                continue;
            }
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        return best;
    };
    return nearestFrom(body_->tileX(), body_->tileY());
}

bool Session::evictDoorReady() const {
    // THE KNOCK/SERVE VERB IS LIVE when the writ is out and unserved, the body
    // is within an arm's length of the Marrow door, and it is evening -- the
    // rota's own home hours, the brief's "once people are home". Gated on the
    // case being LIVE (hired) rather than on the door lead being heard: the
    // writ letter names the door outright, so a hired hand can serve it
    // whether or not they walked the gate lead first. Outside the hours the
    // door is there but nobody is: interact falls through to its ordinary
    // meaning and the nudge says why.
    if (!evictCaseLive() || writServed_ || !evictRaws_.loaded()) {
        return false;
    }
    const std::int32_t door = evictRaws_.indexOf("family-door");
    if (door < 0) {
        return false;
    }
    const sim::Lead& lead = evictRaws_.leads()[static_cast<std::size_t>(door)];
    if (lead.site.band != body_->band()) {
        return false;
    }
    const std::int32_t dx = lead.site.x - body_->tileX();
    const std::int32_t dy = lead.site.y - body_->tileY();
    if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > kEvictDoorReachTiles) {
        return false;
    }
    const int hour = static_cast<int>((timeOfDay_ / 3600) % 24);
    return hour >= kEvictHomeFromHour && hour < kEvictHomeUntilHour;
}

void Session::grantEvictor() {
    // THE ONE PLACE THIS BUILD ARMS THE PLAYER. The weapon lane's grant
    // seam, under the agreed id: arms The Evictor and touches nothing else
    // -- intent stays where the player left it (the weapon-only setter the
    // case lane's flag asked for is exactly what grantPlayerWeapon is).
    // Unknown ids refuse rather than disarm, so a misspelled reward fails
    // its test instead of quietly emptying the player's hand.
    (void)tavern_->grantPlayerWeapon(sim::kEvictorWeaponId);
}

void Session::stepEvictCase() {
    if (!evictRaws_.loaded() || !evictBook_.active()) {
        return;
    }
    // THE DOOR NUDGE, named when it is live and re-armed when the player
    // steps away -- caseTakeReady's own once-per-arming rule. Two verbs at
    // one press: knock first, serve second, the nudge naming whichever is
    // next.
    if (evictDoorReady()) {
        if (!evictDoorSaid_) {
            const std::string verb =
                std::string(promptLabel(controls_, Action::Interact, promptDevice_));
            say(evictKnocked_ ? "THE MARROWS ARE AT THE DOOR. " + verb + " SERVES THE WRIT."
                              : "A LIGHT UNDER THE MARROW DOOR. " + verb + " KNOCKS.");
            evictDoorSaid_ = true;
        }
    } else {
        evictDoorSaid_ = false;
        // The knock is a fact about tonight; a player who wandered off the
        // step loses the answered door and must knock again.
        if (!writServed_) {
            evictKnocked_ = false;
        }
    }
    // THE PARTICIPATE CLOSE. Walking back into the Mission with the served
    // writ IS the delivery -- no press owed, stepSheetCase's own contract --
    // so the book closes through the one scripted look, and The Evictor is
    // paid on the same step the roll is signed.
    if (writServed_ && !evictBook_.closed()) {
        const std::int32_t close = evictRaws_.indexOf("served");
        if (close < 0) {
            return;
        }
        const sim::Lead& lead = evictRaws_.leads()[static_cast<std::size_t>(close)];
        const std::int32_t dx = lead.site.x - body_->tileX();
        const std::int32_t dy = lead.site.y - body_->tileY();
        if (lead.site.band == body_->band() &&
            (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= sim::kLookRangeTiles) {
            const std::int64_t now = caseNowSeconds();
            (void)evictBook_.hear(close, now);
            const sim::LookResult done =
                evictBook_.look(lead.site.x, lead.site.y, lead.site.band, now);
            grantEvictor();
            say(done.line);
            armCasePlate("THE EVICTOR IS YOURS");
        }
    }
}

// ---------------------------------------------------------------------------
// #77: the controls, and the page that changes them
// ---------------------------------------------------------------------------

void Session::noteInputDevice(InputDevice device) {
    if (device == promptDevice_) {
        return;
    }
    // THE ONE STORED PROMPT. Every other prompt in this file is assembled at
    // draw time and re-words itself by construction; the opening hint is
    // state with a twelve-second life, so the hand-over re-words it in
    // place -- but ONLY while it is still the opening hint. A bark or an
    // alert that has since taken message_ is not this function's to touch.
    const bool hintUp = !message_.empty() && message_ == openingHintLine(controls_, promptDevice_);
    promptDevice_ = device;
    if (hintUp) {
        message_ = openingHintLine(controls_, promptDevice_);
        // UI-EA (LANE HUD): the tutor tier raises in full on a device
        // change. The hint's four seconds shrank from twelve in the word
        // diet, so a hand-over mid-read gets the whole read back in the new
        // vocabulary rather than the tail of the old one's clock.
        messageSteps_ = std::max(messageSteps_, 60 * 4);
    }
}

void Session::noteInputKey(Key key) {
    if (key == Key::None) {
        return;  // nobody spoke; Key::None must not read as the keyboard
    }
    noteInputDevice(deviceOfKey(key));
}

void Session::setControls(const ControlSettings& settings) {
    // THE OPENING HINT FOLLOWS THE TABLE IT NAMES KEYS FROM -- the same
    // still-the-hint re-wording noteInputDevice does, for the same reason:
    // the hint is the one STORED prompt, and the constructor worded it off
    // the shipped defaults before the settings file arrived here. Without
    // this, a file that rebinds Menu leaves the first thing a player reads
    // naming the old key for its twelve seconds.
    const bool hintUp = !message_.empty() && message_ == openingHintLine(controls_, promptDevice_);
    controls_ = settings;
    controls_.sanitise();
    if (hintUp) {
        message_ = openingHintLine(controls_, promptDevice_);
    }
    // The camera reads the field of view every frame off controls_, so a
    // settings file with an FOV in it is applied by the act of loading it and
    // there is no second copy to forget to update.
}

void Session::setFov(int degrees) {
    controls_.fovDegrees = degrees < kMinFov ? kMinFov : (degrees > kMaxFov ? kMaxFov : degrees);
}

namespace {

/// WHICH FAMILY A VERB BELONGS TO, and therefore what colour its row is on the
/// controls page. Movement, the things you do, the screens you open, the quick
/// bar. See render/keys_page.hpp -- one accent per family is what makes a
/// thirty-row page scannable before a word of it is read.
[[nodiscard]] int keyGroupFor(Action action) noexcept {
    switch (action) {
        case Action::Forward:
        case Action::Back:
        case Action::StrafeLeft:
        case Action::StrafeRight:
        case Action::TurnLeft:
        case Action::TurnRight:
            return kKeysGroupMove;
        case Action::Menu:
        case Action::PagePrev:
        case Action::PageNext:
        case Action::Pause:
        case Action::Map:
        case Action::Screenshot:
        // Violation #5 (FLOW): the two F-key pages are screens, and they
        // file with the screens -- two case labels, PAGES' grouped page
        // takes it from here.
        case Action::KeysPage:
        case Action::OptionsPage:
            return kKeysGroupScreen;
        case Action::QuickWheel:
        case Action::QuickNext:
        case Action::QuickPrev:
            return kKeysGroupQuick;
        default:
            break;
    }
    if (action >= Action::QuickSlot1 && action <= Action::QuickSlot0) {
        return kKeysGroupQuick;
    }
    return kKeysGroupAct;
}

/// THE ROWS THE CONTROLS PAGE ADDS TO THE BINDINGS, and the only ones it still
/// hard-codes.
///
/// Everything a key is BOUND to is generated from ControlSettings -- see the
/// note in keyPageRows(). These five are not bindings: they are what walking
/// into a ledge does, and what W, S, SPACE and F do WHILE A WIRE IS IN A LOCK,
/// which is a mode the simulation is in rather than a verb with a key of its
/// own. They carry bindable=false, and the detail pane says so with a state
/// label where the rebind verb would otherwise be.
///
/// `listRow` is the exact string the flat keyRows() list has always printed for
/// them, kept because two cases assert on it and because the one-line wording
/// genuinely differs from the two-column wording -- see KeysPageRow::listRow.
[[nodiscard]] std::vector<KeysPageRow> contextualRows() {
    std::vector<KeysPageRow> rows;
    rows.push_back(KeysPageRow{"WALK", "CLIMB A LEDGE", "",
                               "NO KEY FOR THIS -- THAT IS THE POINT. WALK AT A LOW LEDGE "
                               "AND YOU HAUL YOURSELF UP.",
                               false, "WALK AT A LEDGE", kKeysGroupNote});
    rows.push_back(KeysPageRow{"W  S", "AIM THE WIRE", "",
                               "TURNS THE WIRE IN THE LOCK. THE WARDS ARE NOT ALL AT THE "
                               "SAME DEPTH AND YOU HAVE TO FEEL FOR EACH ONE.",
                               false, "LOCK: W S  AIM", kKeysGroupNote});
    rows.push_back(KeysPageRow{"SPACE", "TRY THE PIN", "",
                               "PUSHES AT THE PIN YOU ARE AIMED AT. GET IT WRONG AND THE "
                               "WIRE COMPLAINS, LOUDLY, TO ANYBODY IN THE ROOM.",
                               false, "LOCK: SPACE TRY", kKeysGroupNote});
    rows.push_back(KeysPageRow{"F", "FORCE IT", "",
                               "STOPS BEING SUBTLE. FAST, CRUDE, AND IT COSTS YOU THE WIRE "
                               "MORE OFTEN THAN NOT.",
                               false, "LOCK: F  FORCE", kKeysGroupNote});
    rows.push_back(KeysPageRow{"ESC", "LEAVE THE LOCK", "",
                               "TAKES THE WIRE BACK OUT. THE LOCK REMEMBERS NOTHING AND "
                               "NEITHER DOES THE WATCH, IF YOU ARE QUICK.",
                               false, "LOCK: ESC  OUT", kKeysGroupNote});
    return rows;
}

}  // namespace

std::vector<KeysPageRow> Session::keyPageRows() const {
    // GENERATED FROM THE LIVE BINDINGS, and that is the whole point of the
    // change. This page used to be a static array of strings sitting a hundred
    // lines away from a switch statement in the client, and its own comment
    // said it lived here so it "does not drift from them the moment somebody
    // rebinds one without looking down". It could not help drifting: nothing
    // connected the two. Now a rebinding shows up here by construction, because
    // this IS the binding table read out loud.
    std::vector<KeysPageRow> rows;
    rows.reserve(kActionCount + 8);
    rows.push_back(KeysPageRow{"MOUSE", "LOOK", "",
                               "WHERE YOU POINT IS WHERE YOU LOOK. RAW, WITH NO SMOOTHING "
                               "AND NO ACCELERATION -- THE SENSITIVITY IS ON THE OPTIONS "
                               "PAGE.",
                               false, "MOUSE  LOOK", kKeysGroupMove});
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        // THE QUICK BAR IS ONE ROW, NOT TEN. Ten near-identical rows would push
        // everything a player is actually looking for down the page. The
        // OPTIONS page still lists all ten, because that is where you go to
        // change one.
        if (action >= Action::QuickSlot2 && action <= Action::QuickSlot0) {
            continue;
        }
        KeysPageRow row;
        row.verb = std::string(actionLabel(action));
        row.help = std::string(actionHelp(action));
        row.group = keyGroupFor(action);
        row.bindable = true;
        if (action == Action::QuickSlot1) {
            row.binding =
                std::string(keyName(controls_.primary[i])) + "-" +
                std::string(keyName(controls_.primary[static_cast<std::size_t>(
                    Action::QuickSlot0)]));
            row.verb = "QUICK BAR";
            row.help = "THE NUMBER ROW READIES A CRAFTING WITH NOTHING OPEN. THE "
                       "OPTIONS PAGE LISTS ALL TEN.";
        } else {
            row.binding = std::string(keyName(controls_.primary[i]));
            const Key second = controls_.secondary[i];
            if (second != Key::None) {
                row.alternate = std::string(keyName(second));
            }
        }
        rows.push_back(std::move(row));
    }
    for (KeysPageRow& row : contextualRows()) {
        rows.push_back(std::move(row));
    }
    return rows;
}

KeysPageState Session::keysPageState() const {
    KeysPageState state;
    state.open = keysOpen_;
    state.title = "CONTROLS";
    // AND THE BUILD, HERE, WHERE A PLAYER GOES LOOKING FOR IT -- right-aligned
    // in the tab row, which is where the reference puts the number you want on
    // screen while you read the page. It used to be burnt into the top-left
    // corner of every captured frame at full HUD scale.
    state.readout = "GRANADAD " + std::string(sim::build_info().version);
    // The intro prose is retired (UI-EA-SPEC 1.7 #36): the tab row is the
    // header, the table is the page, and the universal grammar (ESC/B backs
    // out one layer) needs no sentence. The PAGE keeps printing BOTH devices'
    // bindings side by side -- the table's two slots ARE the two devices.
    state.rows = keyPageRows();
    state.cursor = caseCursor_;
    return state;
}

std::vector<std::string> Session::keyRows() const {
    // DERIVED, NOT WRITTEN TWICE. See KeysPageRow::listRow: the flat list and
    // the drawn page were separate once, and separate lists of different
    // lengths is how a cursor ends up able to select a row nothing draws.
    std::vector<std::string> rows;
    for (const KeysPageRow& row : keyPageRows()) {
        rows.push_back(row.listRow.empty() ? row.binding + "  " + row.verb : row.listRow);
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
    if (willOpen) {
        // UI-EA-SPEC sec. 4 violation #4: REMEMBER THE OPENER, before the
        // stand-down below erases the evidence. Opened over the pause menu
        // (its SETTINGS row) means back returns there; opened over the
        // sibling Keys page (the TAB swap) inherits whatever that page owed
        // -- the pair is one tabbed surface as far as the way out is
        // concerned; opened from the street (F2) owes nothing.
        pageOpenedFromPause_ = pauseOpen_ || ((keysOpen_ || waitOpen_) && pageOpenedFromPause_);
    }
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
    // JUSTICE BUILD: PAUSE still opens over the hearing (a player can always
    // quit the game) -- and is REFUSED on the rope's plate and the end rows,
    // whose two rows are the only live input (spec 5).
    if (ropeCeremonyUp()) {
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

std::string Session::custodyWaitLine() const {
    // THE WAIT ROW'S OWN REFUSAL, by who has you: the priest at the bench,
    // the officer under the arrest beat before it. The rope's plate and the
    // end rows refuse the pause menu outright (togglePause), so no line is
    // needed there; the priest's stands for any custody the two above miss.
    if (courtOpen_) {
        return "THE PRIEST IS WAITING.";
    }
    if (takenHold_ > 0) {
        return "THE WATCH HAS YOU.";
    }
    return "THE PRIEST IS WAITING.";
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
    // JUSTICE BUILD: at the bench the WAIT row is the refusal itself --
    // state changes the label (UI-REFERENCE: never a greyed-out row), and
    // choosePause says the same line and does nothing. The row keeps its
    // place so CONTROLS, SETTINGS and QUIT keep their digits.
    return {
        "RESUME",
        inCustody() ? custodyWaitLine() : std::string("WAIT"),
        "CONTROLS",
        "SETTINGS",
        // SHIP NOTE MOVE 3: the armed row names the device's own confirm.
        quitArmed_ ? "QUIT -- SURE? " + std::string(promptConfirmKey(promptDevice_))
                   : std::string("QUIT GRANADAD"),
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
            // JUSTICE BUILD: refused out loud in custody -- at the bench, and
            // under the officer's hand before it -- the page staying up; the
            // row already reads the refusal.
            if (inCustody()) {
                say(custodyWaitLine());
                return;
            }
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
    if (refusedInCustody()) {
        return;
    }
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
        // `SLOT 3 - CLEAR THE HEAD` (UI-EA-SPEC sec. 5): the toast is the
        // slot and the name; READY was the toast announcing itself.
        // KIT BUILD: an item slot names the thing now worn the same way.
        const sim::Spell* crafting = tavern_->slotSpell(slot);
        const sim::ItemDef* thing = tavern_->slotItem(slot);
        say("SLOT " + std::to_string(slot + 1) + " - " +
            (crafting != nullptr ? upperAscii(crafting->displayName)
                                 : thing != nullptr ? thing->name : std::string("EMPTY")) +
            ".");
        return;
    }
    if (tavern_->slotItem(slot) != nullptr) {
        // KIT BUILD: the slot names a thing no longer carried -- said, not
        // swallowed, the empty slot's own rule.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiTick);
        }
        say("SLOT " + std::to_string(slot + 1) + " - " + tavern_->slotItem(slot)->name +
            ". NOT ON YOU.");
        return;
    }
    // AUDIO WIRING: the quiet tick a cursor move gets -- nothing changed.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    say("SLOT " + std::to_string(slot + 1) + " - EMPTY. THE GRIMOIRE BINDS.");
}

void Session::showQuickBar() {
    // A couple of seconds past the last touch -- the strip is up exactly
    // while the wheel or the number row is being used. The ease itself is
    // quickBarAnim_'s business, driven in syncPanelAnim()/step() like every
    // other row.
    quickBarShowSteps_ = kQuickBarShowSteps;
}

void Session::jump() {
    if (refusedInCustody()) {
        return;
    }
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
    if (willOpen) {
        // Violation #4: remember the opener -- see toggleOptions(). Opened
        // over the pause menu's CONTROLS row, back returns there; the
        // sibling-tab swap from Options inherits the debt.
        pageOpenedFromPause_ =
            pauseOpen_ || ((optionsOpen_ || waitOpen_) && pageOpenedFromPause_);
    }
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
    if (refusedInCustody()) {
        return;
    }
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !grimoireOpen_;
    grimoireOpen_ = willOpen;
    // KIT BUILD: the widget opened by the wheel's tap is the Grimoire, never
    // a search list left behind by a body walked away from.
    searchActorId_ = -1;
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
    if (refusedInCustody()) {
        return;
    }
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
        // THE MAP PASS. OPENING PUTS THE CURSOR WHERE YOU ARE STANDING, so the
        // first thing the page says is true of the ground under your feet and
        // the plan is already panned to your quarter. A body standing on no
        // named footprint at all (mid-harbour, on a roof) gets the nearest
        // place instead -- never a stale index from the last time it was open,
        // because "the map opened somewhere else" is the exact disorientation
        // this pass exists to remove.
        // A footprint first (the building you are standing in), then the
        // STREET you are standing on -- mapWayUnder walks every authored
        // segment, not the one segment mapPlaces() kept for its label, so a
        // body anywhere along the Tarwalk selects the Tarwalk. Only a stand on
        // neither (mid-harbour, a roof deck) falls back to the nearest place.
        int here = mapPlaceUnder(body_->tileX(), body_->tileY());
        if (here < 0) {
            here = mapWayUnder(body_->tileX(), body_->tileY(), 0);
        }
        districtMapSelected_ = here >= 0 ? here : nearestDistrictMapPlace();
        districtMapDetailFirst_ = 0;
    }
    firstRun_ = false;
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// THE MAP PASS: the page's cursor
// ---------------------------------------------------------------------------

int Session::nearestDistrictMapPlace() const {
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return 0;
    }
    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();
    int best = 0;
    std::int64_t bestD = 0;
    for (std::size_t i = 0; i < places.size(); ++i) {
        const std::int64_t dx = static_cast<std::int64_t>(places[i].anchorX) - px;
        const std::int64_t dy = static_cast<std::int64_t>(places[i].anchorY) - py;
        const std::int64_t d = dx * dx + dy * dy;
        if (i == 0 || d < bestD) {
            best = static_cast<int>(i);
            bestD = d;
        }
    }
    return best;
}

void Session::moveDistrictMapCursor(MapStep step) {
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return;
    }
    const int count = static_cast<int>(places.size());
    districtMapSelected_ = std::clamp(districtMapSelected_, 0, count - 1);
    if (districtMapTab_ == MapTab::Index &&
        (step == MapStep::North || step == MapStep::South)) {
        // ON A LIST, AN ARROW WALKS THE LIST. The Index is the same selection
        // the plan shows, read alphabetically, and a reader arrowing down it
        // means "the next name", not "the next place northward". Left and right
        // still step the ward, so the geographic cursor is never out of reach.
        districtMapSelected_ =
            std::clamp(districtMapSelected_ + (step == MapStep::North ? -1 : 1), 0, count - 1);
        return;
    }
    districtMapSelected_ = mapPlaceToward(districtMapSelected_, step);
}

void Session::cycleDistrictMapTab(int delta) {
    int at = static_cast<int>(districtMapTab_) + delta;
    while (at < 0) {
        at += kMapTabCount;
    }
    districtMapTab_ = static_cast<MapTab>(at % kMapTabCount);
    districtMapDetailFirst_ = 0;
}

void Session::setDistrictMapTab(int index) {
    if (index < 0 || index >= kMapTabCount) {
        return;
    }
    districtMapTab_ = static_cast<MapTab>(index);
    districtMapDetailFirst_ = 0;
}

void Session::adjustDistrictMapZoom(int delta) {
    districtMapZoom_ = std::clamp(districtMapZoom_ + delta, 0, mapZoomSteps() - 1);
}

bool Session::selectDistrictMapPlace(std::string_view name) {
    const int at = mapPlaceIndex(name);
    if (at < 0) {
        return false;
    }
    districtMapSelected_ = at;
    districtMapDetailFirst_ = 0;
    return true;
}

void Session::faceDistrictMapSelection() {
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return;
    }
    const MapPlace& place =
        places[static_cast<std::size_t>(std::clamp(districtMapSelected_, 0,
                                                   static_cast<int>(places.size()) - 1))];
    // TURNING IS NOT SIMULATION STATE THAT CAN DIVERGE -- setYaw is the same
    // call every scripted line in this file already makes to point the body at
    // somebody before it talks to them, and yaw is integer BAM.
    // THE SAME POINT THE PANE PRINTED A BEARING TO -- mapAimPoint, once, so
    // the body cannot turn one way while the page says another.
    std::int32_t aimX = 0;
    std::int32_t aimY = 0;
    mapAimPoint(place, body_->tileX(), body_->tileY(), aimX, aimY);
    body_->setYaw(sim::bearingTo(body_->tileX(), body_->tileY(), aimX, aimY));
    districtMapOpen_ = false;
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// FAST TRAVEL (TRAVEL lane) -- the ward map's second commit verb
// ---------------------------------------------------------------------------

namespace {

/// The nearest standable cell to (x, y) on `band`, searched in a fixed
/// widening square so the answer is a pure function of the request -- the
/// demo's own arrival rule (demo.cpp's nearestStandable), restated here
/// because a travel lands a body the same way a Cut beat lands one: on real
/// ground, never inside geometry. False when nothing within eight tiles will
/// hold a body, which is a refusal and not a fudge.
[[nodiscard]] bool travelStandable(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                                   std::int32_t band, std::int32_t* outX, std::int32_t* outY) {
    if (tiles.standable(x, y, band)) {
        *outX = x;
        *outY = y;
        return true;
    }
    for (std::int32_t ring = 1; ring <= 8; ++ring) {
        for (std::int32_t dy = -ring; dy <= ring; ++dy) {
            for (std::int32_t dx = -ring; dx <= ring; ++dx) {
                if (dx != ring && dx != -ring && dy != ring && dy != -ring) {
                    continue;
                }
                if (tiles.standable(x + dx, y + dy, band)) {
                    *outX = x + dx;
                    *outY = y + dy;
                    return true;
                }
            }
        }
    }
    return false;
}

/// "02:14" -- the clock as the arrival line speaks it. The wait page says
/// whole hours because it deals in them; a walk lands mid-hour and says so.
[[nodiscard]] std::string travelClockText(int secondOfDay) {
    const int hour = ((secondOfDay / 3600) % 24 + 24) % 24;
    const int minute = (secondOfDay / 60) % 60;
    std::string text = (hour < 10 ? "0" : "") + std::to_string(hour) + ":";
    text += (minute < 10 ? "0" : "") + std::to_string(minute);
    return text;
}

}  // namespace

std::string Session::travelRefusal() const {
    // THE CARRY CLAUSE FIRST, because it is the one refusal that is
    // load-bearing rather than circumstantial: stepSheetCase() completes the
    // delivery the moment the body is within look range of the back room, so
    // a travel permitted while carrying would teleport-finish the courier
    // case's whole final act. The nervous walk across the night district IS
    // that case's payoff, and the line says so in the ward's voice.
    if (sheetCarry_) {
        return "NOT WITH THE FINCH. WALK HIM.";
    }
    // THE WATCH, ACTIVELY NOTICING: Closing is a watchman crossing the room
    // about you, and you do not stroll off mid-witness. Mere heat or a
    // warrant deliberately does NOT refuse -- the owner ruled waiting one out
    // a tactic, and waitRefusal() ignores them for the same reason.
    if (tavern_->watchStance() == sim::Tavern::WatchStance::Closing) {
        return "NOT WITH THE WATCH CLOSING.";
    }
    // AND THE WAIT PAGE'S OWN DEFINITION OF "ANYWHERE SAFE", VERBATIM -- a
    // travel is a wait plus a relocation, so every door that refuses the one
    // refuses the other, in the same words.
    return waitRefusal();
}

Session::TravelPlan Session::districtMapTravelPlan() const {
    TravelPlan plan;
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return plan;
    }
    const MapPlace& place =
        places[static_cast<std::size_t>(std::clamp(districtMapSelected_, 0,
                                                   static_cast<int>(places.size()) - 1))];
    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();
    if (place.contains(px, py)) {
        plan.standingIn = true;
        return plan;
    }
    // THE STATE REFUSALS FIRST -- they are cheap, they are the ones a player
    // needs told about, and a refused plan owes no route.
    plan.refusal = travelRefusal();
    if (!plan.refusal.empty()) {
        return plan;
    }
    // THE ARRIVAL: the same aim point the pane's bearing and FACE IT already
    // use (the door you knock on), snapped to standable ground on the place's
    // own band -- the demo Cut's own landing rule.
    std::int32_t aimX = 0;
    std::int32_t aimY = 0;
    mapAimPoint(place, px, py, aimX, aimY);
    if (!travelStandable(*tiles_, aimX, aimY, place.band, &plan.toX, &plan.toY)) {
        plan.refusal = "NO GROUND TO STAND ON.";
        return plan;
    }
    plan.toBand = place.band;
    // THE WALK: the district's own PathFinder, salt 0 (no jitter,
    // deterministic), Gait::Walk (the pace being charged). kSearchMaxSpan was
    // sized to admit a route from one end of the district to the other and
    // the worst measured district leg expands under 7,000 of the 10,000-node
    // budget -- and a search that still fails is an honest refusal, not a
    // fallback to the crow.
    sim::PathFinder router(*tiles_);
    std::vector<sim::PathStep> route;
    const sim::PathStep from{px, py, body_->band()};
    const sim::PathStep to{plan.toX, plan.toY, plan.toBand};
    if (!router.find(from, to, 0, route)) {
        plan.refusal = "NO WAY THERE ON FOOT.";
        return plan;
    }
    plan.routeSteps = static_cast<std::int32_t>(route.size());
    plan.units = travelRouteUnits(from, route);
    plan.seconds = travelWalkSeconds(plan.units);
    plan.minutes = travelClockMinutes(plan.seconds);
    plan.available = true;
    return plan;
}

void Session::travelDistrictMapSelection() {
    // THE VERB LIVES ON THE PAGE. Inert with the map down, exactly as the
    // other map commits are -- a key that teleported with no page up would be
    // a debug verb wearing a binding.
    if (!districtMapOpen_) {
        return;
    }
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return;
    }
    // RE-CHECKED ON THE PRESS, not only at draw -- chooseWaitRow's own rule: a
    // watchman can start closing while the map is up, and the page and the key
    // must name the same door.
    const TravelPlan plan = districtMapTravelPlan();
    if (plan.standingIn) {
        return;
    }
    // AUDIO: the press is what is acknowledged, refusals included --
    // chooseWaitRow's rule again.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    if (!plan.available) {
        // REFUSED OUT LOUD, THE PAGE STAYING UP. One string, the same one the
        // pane's verb row is printing.
        if (!plan.refusal.empty()) {
            say(plan.refusal);
        }
        return;
    }
    const MapPlace& place =
        places[static_cast<std::size_t>(std::clamp(districtMapSelected_, 0,
                                                   static_cast<int>(places.size()) - 1))];

    // 1. THE PAGE GOES DOWN FIRST. syncPanelAnim() suppresses the threshold
    // plate under any open panel and assigns lastPlaceName_ even while
    // suppressed, so a relocate-then-close would eat the plate silently.
    districtMapOpen_ = false;

    // 2. THE WAIT MACHINERY, PLUS A RELOCATION. The clock advances by exactly
    // the minutes the verb restated -- heat cools, the cellar restocks, the
    // ward's calendar and all six hundred bodies settle to the new hour, all
    // of it the same spent code every WAIT pick runs -- and the body makes
    // the same honest jump sleeping in a rented bed already makes.
    skipSeconds(plan.minutes * 60);
    placeBodyAt(plan.toX, plan.toY, plan.toBand);

    // 3. FACING THE DOOR. You arrive looking at the threshold you came for --
    // the place's own anchor -- not wherever the walk left your eyes.
    const std::int32_t doorX = static_cast<std::int32_t>(place.anchorX);
    const std::int32_t doorY = static_cast<std::int32_t>(place.anchorY);
    if (doorX != plan.toX || doorY != plan.toY) {
        body_->setYaw(sim::bearingTo(plan.toX, plan.toY, doorX, doorY));
    }

    // 4. THE THRESHOLD PLATE, ARMED EXPLICITLY. A walked crossing arms it on
    // the rising edge of the ground's own name; a travel is a crossing whose
    // walk was elided, so it is armed by hand -- with the ground's name where
    // the landing has one, the authored name where the ring pushed the body
    // just outside its own footprint. If the next step's ground disagrees,
    // syncPanelAnim()'s newest-crossing rule corrects it, exactly as it
    // corrects a walked seam.
    const std::string_view ground =
        sim::docks::placeNameAt(plan.toX, plan.toY, plan.toBand);
    lastPlaceName_.assign(!ground.empty() ? std::string(ground) : upperAscii(place.name));
    placePlateName_ = lastPlaceName_;
    placePlateShowSteps_ = kPlacePlateShowSteps;

    // 5. THE SEAM. Snapped fully black on the commit frame -- the origin is
    // never seen again after the press -- easing up on the destination with
    // the plate and the arrival line already on it. The owner called the raw
    // cut "teleporting"; this is the difference. The dressing is
    // dressInstantCut() -- the same cloth every scripted cut wears now --
    // and the view snap rode in with placeBodyAt() above.
    dressInstantCut();

    say("WALKED TO " + upperAscii(place.name) + ". " + travelClockText(timeOfDay_) + ".");
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// RELOCATED: SNAP THE VIEW -- the one seam for every instant jump
// ---------------------------------------------------------------------------
//
// See the header's own block. The three functions are deliberately small:
// the SNAP is the bug fix, the DRESSING is the owner's read of even a clean
// cut ("teleporting"), and placeBodyAt is what keeps the next relocation
// feature from having to remember either.

void Session::snapViewAfterRelocation() {
    // THE GHOST, EXACTLY: each of these rows eases out over ~8 steps when its
    // subject goes away, which is right for a subject that walked out of
    // reach and wrong for a BODY that jumped -- the old street's crosshair
    // subject, lock and room name have no business fading over the new one.
    // snapTo, not setTarget: the whole point is that nothing eases here.
    // The caches are cleared the same instant for the same reason
    // clearIfClosed() clears them -- a label must not survive the verb it
    // hung off, and after a jump there is no verb left.
    interactAnim_.snapTo(false);
    interactSubjectCache_.clear();
    interactNoteCache_.clear();
    interactKindCache_ = AimKind::Nothing;
    lockAnim_.snapTo(false);
    lockCache_.clear();
    roomAnim_.snapTo(false);
    roomCache_.clear();
    // NOT the case trail, the guild row, the standing, the heat, the stash,
    // the stance: those are facts about the PLAYER, and the player made the
    // journey. Only what was derived from the ground under the old feet.
    //
    // AND NOTHING FOR THE CAMERA, stated so the next reader does not hunt
    // for it: the camera is Camera::fromBody(body_->...) recomputed raw
    // every frame -- there is no eased camera position in this build, which
    // test_demo's "the frame after a cut" case now pins. If one is ever
    // added, THIS is where its snap belongs.
}

void Session::placeBodyAt(std::int32_t tileX, std::int32_t tileY, std::int32_t band) {
    body_->placeAt(tileX, tileY, band);
    snapViewAfterRelocation();
}

void Session::dressInstantCut() {
    travelFadeAnim_.snapTo(true);
    travelFadeAnim_.setTarget(false);
}

DistrictMapState Session::districtMapState() const {
    DistrictMapState plan;
    plan.tiles = tiles_.get();
    plan.palette = &mapPalette_;
    plan.bounds = mapBounds_;
    plan.playerX = static_cast<float>(body_->x()) / static_cast<float>(sim::kSubOne);
    plan.playerY = static_cast<float>(body_->y()) / static_cast<float>(sim::kSubOne);
    plan.band = body_->band();
    plan.yawBam = body_->yaw();
    plan.title = placeLabel();
    plan.openAmount = districtMapAnim_.value();
    plan.selected = districtMapSelected_;
    plan.zoom = districtMapZoom_;
    plan.tab = districtMapTab_;
    plan.detailFirst = districtMapDetailFirst_;
    // THE READOUT: the clock and the band, right-aligned and permanent. The
    // two facts a map reader wants on screen the whole time -- and the clock
    // prints its LIVE MINUTE (UI-EA-SPEC sec. 4 #10): the HUD says 08:01 and a
    // header that answers 08:00 is a page disagreeing with the world it maps.
    const int hour = ((timeOfDay_ / 3600) % 24 + 24) % 24;
    const int minute = ((timeOfDay_ / 60) % 60 + 60) % 60;
    plan.readout = (hour < 10 ? std::string("0") : std::string()) + std::to_string(hour) + ":" +
                   (minute < 10 ? std::string("0") : std::string()) + std::to_string(minute) +
                   "   BAND " + std::to_string(body_->band());

    // SHIP NOTE MOVE 3: the nav band's keys, in the device's own vocabulary.
    // Each pad wording is what main.cpp's map branch ACTUALLY routes: the
    // D-pad walks places, LB/RB (PagePrev/PageNext) cycle the views, RT/LT
    // (Cast/Block) ride the zoom ladder, SELECT (Action::Map's own pad half)
    // shuts the page, A commits. The movement keys are the KEYCAP MOTIFS
    // (UI-EA-SPEC sec. 5): four arrowheads for the keyboard, the d-pad cross
    // for the pad -- rendered by the panel drawers from the sentinel bytes.
    plan.navMoveKeys = std::string(kGlyphMoveKeys);
    if (promptDevice_ == InputDevice::Pad) {
        plan.navMoveKeys = std::string(kGlyphCross);
        plan.navTabKeys = std::string(promptLabel(controls_, Action::PagePrev, promptDevice_)) +
                          " " + std::string(promptLabel(controls_, Action::PageNext, promptDevice_));
        plan.navZoomKeys = std::string(promptLabel(controls_, Action::Cast, promptDevice_)) + " " +
                           std::string(promptLabel(controls_, Action::Block, promptDevice_));
    }
    plan.navCloseKey = std::string(promptLabel(controls_, Action::Map, promptDevice_));
    plan.commitKey = std::string(promptConfirmKey(promptDevice_));

    // FAST TRAVEL (TRAVEL lane): the verb's key in the device's own
    // vocabulary -- T on a keyboard (a raw map-page key, Tab/=/-'s own
    // precedent, so a literal like the nav keys' own "TAB"), the Attack half
    // on a pad (X/PadWest, the one face button unclaimed on this page: A is
    // FACE IT, so travel takes the "verb wearing a different mode's clothes"
    // slot the zoom triggers already spend). Through promptLabel so a rebind
    // of Attack re-words it. Cost and refusal come off the SAME plan the
    // commit spends, so the row and the press can never name different doors.
    plan.travelKey = promptDevice_ == InputDevice::Pad
                         ? std::string(promptLabel(controls_, Action::Attack, promptDevice_))
                         : std::string("T");
    const TravelPlan travel = districtMapTravelPlan();
    if (!travel.standingIn) {
        if (!travel.refusal.empty()) {
            plan.travelRefusal = travel.refusal;
        } else if (travel.available) {
            plan.travelCost = travelCostLabel(travel.minutes);
        }
    }

    // WHO IS IN THERE RIGHT NOW -- the People view, and the direct answer to
    // "finding the person or thing I want at that place". A const walk over the
    // already-public roster; nothing is written and nothing is hashed.
    const std::vector<MapPlace>& places = mapPlaces();
    if (!places.empty() && people_) {
        const MapPlace& place =
            places[static_cast<std::size_t>(std::clamp(districtMapSelected_, 0,
                                                       static_cast<int>(places.size()) - 1))];
        const std::vector<sim::WardActor>& roster = people_->actors();
        for (const sim::WardActor& actor : roster) {
            if (!actor.visible() || !place.contains(actor.x, actor.y)) {
                continue;
            }
            // ANY BAND, deliberately. A footprint is a claim on the GROUND and
            // everything standing on it -- the same rule plotIndexUnderfoot
            // already uses for tenure -- and a reader asking "who is in the
            // Gilded Gull" means the taproom and the rooms over it alike.
            MapPersonRow row;
            const sim::WardIdentity& who = people_->identity(actor.id);
            row.name = who.name.empty() ? std::string("SOMEBODY") : who.name;
            row.what = std::string(sim::wardTypeName(actor.type));
            row.x = actor.x;
            row.y = actor.y;
            plan.people.push_back(std::move(row));
        }
        // AND THE TAPROOM'S OWN PEOPLE, WHICH THE ROSTER DELIBERATELY DOES NOT
        // HOLD. WardPopulation spawns nobody inside the Gilded Gull -- its own
        // header says so, because the Gull already had seventeen bodies of its
        // own before the ward had any -- so a People view built off the roster
        // alone reported "NOBODY IS IN THERE" for the one address in the
        // district a player is most likely to be looking somebody up in. The
        // first capture of this page said exactly that at one in the
        // afternoon. Two systems, one question, one list.
        for (const sim::Actor& actor : tavern_->actors()) {
            if (!actor.present() || !place.contains(actor.tileX(), actor.tileY())) {
                continue;
            }
            MapPersonRow row;
            row.name = actor.name();
            row.what = std::string(sim::actorRoleName(actor.role()));
            row.x = actor.tileX();
            row.y = actor.tileY();
            plan.people.push_back(std::move(row));
        }
        // NAME ORDER, so a page turn is stable and a capture reproducible. The
        // roster's own index order is bake order, which means a body that walks
        // out of a room reshuffles every row under it.
        std::sort(plan.people.begin(), plan.people.end(),
                  [](const MapPersonRow& a, const MapPersonRow& b) {
                      if (a.name != b.name) {
                          return a.name < b.name;
                      }
                      return a.what < b.what;
                  });
    }
    return plan;
}

std::vector<std::string> Session::grimoireRows() const {
    if (searchOpen()) {
        // KIT BUILD: the corpse's kit, less what was taken -- name, weight,
        // worth -- and TAKE ALL as the last row, a row being a choice.
        std::vector<std::string> rows;
        const std::vector<sim::Tavern::CorpseRow> carried = tavern_->corpseRows(searchActorId_);
        for (const sim::Tavern::CorpseRow& row : carried) {
            const sim::ItemDef* thing = tavern_->items().at(row.item);
            if (thing == nullptr) {
                continue;
            }
            std::string line = thing->name + "  " + std::to_string(thing->drams) + "DR";
            if (thing->royals > 0) {
                line += "  " + std::to_string(thing->royals) + "C";
            }
            rows.push_back(std::move(line));
        }
        if (!carried.empty()) {
            rows.push_back("TAKE ALL");
        }
        return rows;
    }
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
                      static_cast<int>(searchOpen() ? grimoireRows().size()
                                                    : tavern_->dialogue().grimoire().spells().size()));
}

void Session::nextGrimoirePage() {
    if (!grimoireOpen_) {
        return;
    }
    advancePage(grimoirePage_, grimoireCursor_,
                searchOpen() ? grimoireRows().size()
                             : tavern_->dialogue().grimoire().spells().size());
}

void Session::chooseGrimoireRow(int slot) {
    if (!grimoireOpen_ || slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    const int index = grimoirePage_ * kTopicPageSize + slot;
    if (searchOpen()) {
        // KIT BUILD: TAKE the row, or TAKE ALL on the last one. The list is
        // re-read after every take (a pure function of the room), so the
        // cursor lands on what is left; an emptied body closes the list.
        const std::vector<sim::Tavern::CorpseRow> carried = tavern_->corpseRows(searchActorId_);
        if (carried.empty()) {
            closeConversation();
            return;
        }
        std::string line;
        bool anyTaken = false;
        if (index >= static_cast<int>(carried.size())) {
            if (index != static_cast<int>(carried.size())) {
                return;
            }
            std::int32_t taken = 0;
            for (const sim::Tavern::CorpseRow& row : carried) {
                const sim::Tavern::StealResult took =
                    tavern_->takeFromCorpse(searchActorId_, row.kitRow);
                if (took.result == sim::ServiceResult::Served) {
                    ++taken;
                    anyTaken = true;
                } else {
                    line = took.line;
                    break;
                }
            }
            if (line.empty()) {
                line = "TAKEN - ALL OF IT. " + std::to_string(taken) +
                       (taken == 1 ? " THING." : " THINGS.");
            } else if (taken > 0) {
                line = std::to_string(taken) + " TAKEN, THEN " + line;
            }
        } else {
            const sim::Tavern::StealResult took = tavern_->takeFromCorpse(
                searchActorId_, carried[static_cast<std::size_t>(index)].kitRow);
            anyTaken = took.result == sim::ServiceResult::Served;
            line = took.line;
        }
        if (audio_ != nullptr) {
            audio_->playOneShot(anyTaken ? audio::SoundId::ClothRustle : audio::SoundId::UiTick);
        }
        say(line);
        const int left = static_cast<int>(grimoireRows().size());
        if (left == 0) {
            closeConversation();
            return;
        }
        grimoireCursor_ = std::min(grimoireCursor_, left - 1);
        grimoirePage_ = topicPageOf(grimoireCursor_);
        return;
    }
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
    if (!grimoireOpen_ || delta == 0 || searchOpen()) {
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

/// One row per hour ahead, round the WHOLE clock (UI-EA-SPEC 1.7 #38:
/// 12 -> 24). Any named hour is reachable in one pick from anywhere -- the
/// same skipSeconds machinery per row, rows offered only, no sim change.
constexpr int kWaitHours = 24;

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
    if (refusedInCustody()) {
        return;
    }
    if (talking() || picking()) {
        return;
    }
    // Violation #4: remember the opener -- see toggleOptions(). The pause
    // menu's WAIT row is a door, and back walks back through it.
    pageOpenedFromPause_ = pauseOpen_;
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
        // `1 - DAWN 06:00` (UI-EA-SPEC 1.7 #38): the hours-ahead count, the
        // hour's name where it has one, and the clock it lands on. The unit
        // word went; the numbers did not.
        std::string row = std::to_string(ahead) + " - ";
        if (hourName(target)[0] != '\0') {
            row += hourName(target);
            row += ' ';
        }
        row += hourLabel(target);
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
    if (refusedInCustody()) {
        return;
    }
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

void Session::toggleCasebook() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Casebook);
    const WatchDepthGuard watchGuard(watchDepth_);
    toggleMenuFocused(kMenuFocusJournal);
}

void Session::toggleCharacter() { toggleMenuFocused(kMenuFocusCharacter); }

void Session::toggleMap() { toggleMenuFocused(kMenuFocusMap); }

void Session::toggleLetters() {
    recordWatchOp(WatchOpKind::Letters);
    const WatchDepthGuard watchGuard(watchDepth_);
    toggleMenuFocused(kMenuFocusLetters);
}

// ---------------------------------------------------------------------------
// THE CASEBOOK PASS: the book as a composed master/detail page, and the route
// out of it to where a lead actually is
// ---------------------------------------------------------------------------
//
// See render/casebook_page.hpp for the defect and the composition. Everything
// below is pure UI state plus two handoffs -- examine() (the look key's own
// call) and the ward map's own cursor -- so nothing here can move the world
// hash and nothing here is a second way to do anything.

namespace {

/// SHOUTED, because every authored string on this surface already is and a
/// notable's name is the one that is not.
[[nodiscard]] std::string shoutName(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

}  // namespace

int Session::mapPlaceForLead(std::int32_t leadIndex) const {
    // COURIER CASE. THE ACTIVE BOOK'S INDEX SPACE, because every caller
    // (the page's routable flag, commitCasebookLead, showLeadOnMap) indexes
    // the book the page is showing -- one rule, sheetCaseLive(), applied
    // everywhere a surface reads "the case".
    const sim::Casebook& book = activeCasebook();
    if (book.raws() == nullptr || leadIndex < 0 ||
        static_cast<std::size_t>(leadIndex) >= book.raws()->leads().size()) {
        return -1;
    }
    const sim::Lead& lead = book.raws()->leads()[static_cast<std::size_t>(leadIndex)];
    // BY THE AUTHORED NAME FIRST, case-folded -- because that is what the two
    // files actually agree on. casebook.json shouts its `place` ("THE
    // WEIGHHOUSE") and the .tmx sign table spells it as a proper noun ("The
    // Weighhouse"); every one of the twelve leads matches a sign that way, and
    // a case pins that so a renamed building goes red here rather than silently
    // routing a player to the wrong door.
    const std::vector<MapPlace>& places = mapPlaces();
    const std::string want = shoutName(lead.place);
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (shoutName(places[i].name) == want) {
            return static_cast<int>(i);
        }
    }
    // AND THE GEOMETRY AS THE FALLBACK: the smallest authored footprint the
    // lead's own site stands in. A name that has drifted is still a coordinate
    // that has not.
    return mapPlaceUnder(lead.site.x, lead.site.y);
}

bool Session::bodyCanLookAt(const sim::Lead& lead) const {
    if (lead.site.band != body_->band()) {
        return false;
    }
    // THE SAME REACH examine() ACTUALLY USES, boon included -- this flag changes
    // a verb that says LOOK, so it has to mean what LOOK means or the page
    // offers a key that would do nothing.
    const std::int32_t reach = sim::kLookRangeTiles + legend().lookRangeBonus();
    const std::int32_t dx = lead.site.x - body_->tileX();
    const std::int32_t dy = lead.site.y - body_->tileY();
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= reach;
}

void Session::moveCasebookCursor(int delta) {
    const int count = static_cast<int>(activeCasebook().known().size());
    if (count <= 0) {
        casePageCursor_ = 0;
        return;
    }
    // WRAPS, so holding one direction walks the whole book -- the same rule
    // every other list in this build keeps.
    casePageCursor_ = ((casePageCursor_ + delta) % count + count) % count;
}

void Session::setCasebookCursor(int index) {
    const int count = static_cast<int>(activeCasebook().known().size());
    if (count <= 0 || index < 0 || index >= count) {
        return;
    }
    casePageCursor_ = index;
}

void Session::cycleCasebookTab(int delta) {
    int at = static_cast<int>(casebookTab_) + delta;
    while (at < 0) {
        at += kCasebookTabCount;
    }
    casebookTab_ = static_cast<CasebookTab>(at % kCasebookTabCount);
}

bool Session::selectCasebookLead(std::string_view leadId) {
    const sim::Casebook& book = activeCasebook();
    if (book.raws() == nullptr) {
        return false;
    }
    const std::int32_t want = book.raws()->indexOf(leadId);
    if (want < 0) {
        return false;
    }
    const std::vector<std::int32_t> heard = book.known();
    for (std::size_t i = 0; i < heard.size(); ++i) {
        if (heard[i] == want) {
            casePageCursor_ = static_cast<int>(i);
            return true;
        }
    }
    // IN THE FILE BUT NOT IN THE BOOK is a real answer and not a typo: a lead
    // the player has not been told about is not selectable, which is the whole
    // gate the investigation runs on.
    return false;
}

bool Session::showLeadOnMap(std::int32_t leadIndex) {
    const int place = mapPlaceForLead(leadIndex);
    if (place < 0) {
        return false;
    }
    // THE HANDOFF, AND IT IS THE MAP'S OWN CURSOR. The map pass made a PLACE
    // the unit of selection, gave it a plan with the names inside the shapes, a
    // detail pane and a People view; the casebook's answer to "where is Harl's
    // Yard" is that page, arrived at from the book, rather than a second
    // navigation surface that would drift from it.
    //
    // The stand-down list is toggleDistrictMap()'s own, minus the part that
    // would undo the point: that method puts the cursor where the BODY is
    // standing, which is exactly what a player who just pressed "show me" does
    // not want.
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    waitOpen_ = false;
    optionsOpen_ = false;
    pauseOpen_ = false;
    quitArmed_ = false;
    awaitingKey_ = false;
    menuFocus_ = kMenuFocusJournal;
    districtMapOpen_ = true;
    districtMapSelected_ = place;
    // OVERVIEW, ALWAYS, whatever tab the map was left on. The question this
    // route asks is "where is it", and Overview is the view that answers it --
    // arriving on the Legend because that is where the map was last put down
    // would be the page answering somebody else's question.
    districtMapTab_ = MapTab::Overview;
    districtMapDetailFirst_ = 0;
    firstRun_ = false;
    syncPanelAnim();
    return true;
}

void Session::commitCasebookLead() {
    const sim::Casebook& book = activeCasebook();
    const std::vector<std::int32_t> heard = book.known();
    if (heard.empty() || book.raws() == nullptr) {
        return;
    }
    const int at = std::clamp(casePageCursor_, 0, static_cast<int>(heard.size()) - 1);
    const std::int32_t index = heard[static_cast<std::size_t>(at)];
    const sim::Lead& lead = book.raws()->leads()[static_cast<std::size_t>(index)];
    // STATE CHOOSES THE VERB, and the page has already printed which one this
    // is going to be -- see drawLeadDetail. Standing in reach of a lead nobody
    // has stood over yet, the useful act is to LOOK; the map would be the page
    // telling you to go where you already are.
    if (bodyCanLookAt(lead) && book.state(index) == sim::LeadState::Open) {
        // CLOSED FIRST, because examine() refuses while the book is up (it
        // treats the look key as "put the notes down") -- so the page hands
        // over rather than fighting for the same key.
        casebookOpen_ = false;
        syncPanelAnim();
        examine();
        return;
    }
    (void)showLeadOnMap(index);
}

CasebookPageState Session::casebookPageState() const {
    // COURIER CASE. THE PAGE SHOWS THE LIVE ERRAND while one runs and the
    // Bloodletter otherwise -- sheetCaseLive()'s one rule, no switcher UI
    // (flagged scope cut). Every row, cross-reference and readout below is
    // built off this pair, so the page can never mix two books' indices.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    CasebookPageState page;
    page.open = true;
    page.openAmount = panelAnim_.value();
    page.title = "THE CASEBOOK";
    page.caseTitle = std::string(raws.title());
    page.tab = casebookTab_;
    page.hook = std::string(raws.hook());
    page.closeLine = std::string(raws.close());
    page.closed = book.closed();
    page.dread = book.dread();
    page.dreadBand = std::string(raws.dreadLabel(book.dread()));
    page.read = book.readCount();
    page.cold = book.coldCount();
    page.calledYou = std::string(legend().title());
    // SHIP NOTE MOVE 3. NOT promptLabel(Menu, Pad) for the pad's close key,
    // deliberately: Menu's pad half is D-PAD UP, but while the book is open
    // the parity pass reads the D-pad as list movement, raw, ahead of the
    // binding (route_menu_key's own header) -- so the key that ACTUALLY
    // closes the book on a pad is B, the universal back. The keyboard half
    // keeps the live Menu binding, exactly as before.
    page.closeKey =
        promptDevice_ == InputDevice::Pad
            ? std::string(promptBackKey(InputDevice::Pad))
            : std::string(promptLabel(controls_, Action::Menu, InputDevice::KeyboardMouse));
    page.lookKey = std::string(promptLabel(controls_, Action::Interact, promptDevice_));
    // The page grammar's confirm, in the live hand's vocabulary -- "ENTER" /
    // "A" -- for the commit verb and the nav band's GO TO IT. The last two
    // keyboard literals on this page rode along as "ENTER" until now. (Both
    // lanes wrote this line; one field, commitKey, survives the merge.)
    page.commitKey = std::string(promptConfirmKey(promptDevice_));

    const std::vector<std::int32_t> heard = book.known();
    page.known = static_cast<std::int32_t>(heard.size());
    page.total = static_cast<std::int32_t>(raws.leads().size());
    page.rows.reserve(heard.size());

    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();
    for (const std::int32_t index : heard) {
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(index)];
        const sim::LeadState what = book.state(index);
        CasebookLeadRow row;
        row.brief = lead.brief.empty() ? lead.place : lead.brief;
        row.place = lead.place;
        row.what = lead.what;
        row.close = lead.close;
        row.state = what == sim::LeadState::Followed ? CasebookLeadState::Followed
                    : what == sim::LeadState::Cold   ? CasebookLeadState::Cold
                                                     : CasebookLeadState::Open;
        if (!lead.who.empty() && who_) {
            if (const sim::Notable* person = who_->find(lead.who); person != nullptr) {
                row.who = shoutName(person->name);
                row.whoWhat = shoutName(person->epithet);
            }
        }
        // THE CLUE IS NOT SHOWN UNTIL IT HAS BEEN STOOD OVER, and that is the
        // same rule the crosshair pass wrote down: a book that printed
        // `found`/`detail` for an Open lead would hand the player the answer
        // for having been TOLD the lead exists, which turns an investigation
        // into a reading exercise. Nothing on this page calls look().
        if (what != sim::LeadState::Open) {
            row.found = lead.found;
            row.detail = lead.detail;
            for (const std::string& id : lead.opens) {
                const std::int32_t opened = raws.indexOf(id);
                if (opened < 0) {
                    continue;
                }
                const sim::Lead& next = raws.leads()[static_cast<std::size_t>(opened)];
                row.opened.push_back(next.brief.empty() ? next.place : next.brief);
            }
        }
        row.heard = formatCaseDay(book.heardAt(index));
        for (const std::int32_t from : raws.openedBy(index)) {
            const sim::Lead& opener = raws.leads()[static_cast<std::size_t>(from)];
            if (!row.from.empty()) {
                row.from += ", ";
            }
            row.from += opener.brief.empty() ? opener.place : opener.brief;
        }
        row.here = bodyCanLookAt(lead);
        // THE BEARING IS TO THE LEAD'S OWN SITE and not to the place's door,
        // because the site is where the key works -- the Mission's flagstones
        // and the Mission's back room are two leads in one building, and one
        // bearing to the building would be the same arrow for both.
        const double dx = static_cast<double>(lead.site.x) - static_cast<double>(px);
        const double dy = static_cast<double>(lead.site.y) - static_cast<double>(py);
        const std::int32_t paces =
            static_cast<std::int32_t>(std::lround(std::sqrt(dx * dx + dy * dy)));
        // `NE 40`, the map badge's own form (UI-EA-SPEC sec. 5): the number
        // stays exact, the unit word retires -- paces are the only distance
        // this game ever states, so the unit was decoration.
        row.bearing =
            std::string(sim::compass_point(sim::bearingTo(px, py, lead.site.x, lead.site.y))) +
            " " + std::to_string(paces);
        if (lead.site.band != body_->band()) {
            // A LEAD ON ANOTHER PLANE SAYS SO. Two of the twelve are one band
            // down; a bearing and a distance with no band on them would send a
            // player walking into the seawall.
            row.bearing += "  BAND " + std::to_string(lead.site.band);
        }
        row.routable = mapPlaceForLead(index) >= 0;
        page.rows.push_back(std::move(row));
    }

    page.cursor = page.rows.empty()
                      ? 0
                      : std::clamp(casePageCursor_, 0, static_cast<int>(page.rows.size()) - 1);
    // THE READOUT: how far the case has got and what the ward's nerve is at.
    // The reference's persistent resource readout, and on this surface the
    // resource being spent is the investigation itself.
    page.readout = "READ " + std::to_string(page.read) + "/" + std::to_string(page.known) +
                   "   DREAD " + std::to_string(page.dread);
    // THE INSTRUCTION ROW SAYS WHAT THE PAGE IS FOR rather than repeating the
    // tab row, and it changes with the state of the book because the useful
    // sentence changes with it.
    if (page.rows.empty()) {
        page.instruction = "NOTHING IN THE BOOK YET.";
    } else if (page.closed) {
        page.instruction = "THE TRAIL IS WALKED OUT.";
    } else if (page.read == 0) {
        page.instruction = "PICK A LEAD. " + std::string(promptConfirmKey(promptDevice_)) +
                           " SHOWS YOU WHERE.";
    } else {
        const std::int32_t waiting = page.known - page.read > 0 ? page.known - page.read : 0;
        page.instruction = std::to_string(waiting) +
                           (waiting == 1 ? " LEAD IS STILL WAITING TO BE WALKED TO."
                                         : " LEADS ARE STILL WAITING TO BE WALKED TO.");
    }
    return page;
}

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

void Session::setMenuFocus(int focus) {
    // THE POINTER PASS (pointer lane, flagged): the mouse's own door onto the
    // focus the bumpers cycle -- setCasebookCursor's "a printed digit, or a
    // mouse click" shape, applied to tiles. ONE page-turn when the focus
    // actually moves, not one per step a cycle would have taken: a hover
    // crossing from Character to Letters is one gesture, and three stacked
    // BookFlips for it would be a sound bug a case cannot see.
    if (!casebookOpen_ || focus < 0 || focus >= kMenuFocusCount || focus == menuFocus_) {
        return;
    }
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::BookFlip);
    }
    menuFocus_ = focus;
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
    // COURIER CASE. The sheet file rides the same shelf in COMBINED indices
    // (see letterAt), gated on ITS OWN book -- and a `handed` document is
    // post the player was SENT, readable the moment its lead is heard: the
    // courier put it in their hand, and a gate that made them walk to the
    // Gull before they could read the sheet that names the Gull would be
    // the tutorial eating itself.
    const std::int32_t base = static_cast<std::int32_t>(letterRaws_.letters().size());
    for (std::size_t i = 0; i < sheetLetterRaws_.letters().size(); ++i) {
        const sim::Letter& letter = sheetLetterRaws_.letters()[i];
        const std::int32_t lead = sheetRaws_.indexOf(letter.lead);
        if (lead < 0) {
            continue;
        }
        const sim::LeadState state = sheetBook_.state(lead);
        const bool readable =
            letter.handed
                ? state != sim::LeadState::Unheard
                : (state == sim::LeadState::Cold || state == sim::LeadState::Followed);
        if (readable) {
            out.push_back(base + static_cast<std::int32_t>(i));
        }
    }
    // EVICTION CASE. The third file rides the same shelf above the sheet's,
    // gated on ITS OWN book by the identical handed/read-not-received rule:
    // the writ is `handed` (readable the moment the hire lead is heard); the
    // widow's petition and the served notice keep the Cold-or-Followed gate,
    // so they surface only once the gate and door leads are actually walked.
    const std::int32_t evictBase =
        base + static_cast<std::int32_t>(sheetLetterRaws_.letters().size());
    for (std::size_t i = 0; i < evictLetterRaws_.letters().size(); ++i) {
        const sim::Letter& letter = evictLetterRaws_.letters()[i];
        const std::int32_t lead = evictRaws_.indexOf(letter.lead);
        if (lead < 0) {
            continue;
        }
        const sim::LeadState state = evictBook_.state(lead);
        const bool readable =
            letter.handed
                ? state != sim::LeadState::Unheard
                : (state == sim::LeadState::Cold || state == sim::LeadState::Followed);
        if (readable) {
            out.push_back(evictBase + static_cast<std::int32_t>(i));
        }
    }
    return out;
}

const sim::Letter& Session::letterAt(std::int32_t combined) const {
    // COURIER + EVICTION CASE. One shelf over THREE files, in fixed member
    // order: the Bloodletter's letters, then the sheet's, then the
    // eviction's above both. Callers only ever hand back indices
    // unlockedLetters() produced, so the subscripts below hold by
    // construction.
    const std::int32_t base = static_cast<std::int32_t>(letterRaws_.letters().size());
    const std::int32_t evictBase =
        base + static_cast<std::int32_t>(sheetLetterRaws_.letters().size());
    if (combined >= evictBase) {
        return evictLetterRaws_.letters()[static_cast<std::size_t>(combined - evictBase)];
    }
    if (combined >= base) {
        return sheetLetterRaws_.letters()[static_cast<std::size_t>(combined - base)];
    }
    return letterRaws_.letters()[static_cast<std::size_t>(combined)];
}

std::int32_t Session::letterCount() const noexcept {
    return static_cast<std::int32_t>(letterRaws_.letters().size() +
                                     sheetLetterRaws_.letters().size() +
                                     evictLetterRaws_.letters().size());
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
    // ACTION-COMBAT BUILD (section 4.5). A DEFEAT UNDER LETHAL RULES (steel was
    // out -- tavern_->escalated()) is not a plain brawl KO: it earns the
    // OCCASION -- the dip, the epitaph naming killer, weapon and place, the held
    // hush of kDeathHoldSteps, then the reveal of the quay the revive below puts
    // you on. Armed HERE, from where the body fell, before reviveAfterDefeat and
    // placeBodyAt move it; composeDeathCeremony() paces the veil over the frames
    // that follow, render-only, while the sim revives underneath at once. A
    // plain brawl defeat (never escalated) keeps the bare settle it always had.
    if (tavern_->escalated()) {
        armDeathCeremony();
    }
    // AUDIO WIRING: the plan's "knockdown -> ThudHeavy" -- the one call every
    // path to the floor funnels through. THE LOT PASS: fall_land is what
    // ThudHeavy resolves to now, and the DEATH CEREMONY (an escalated
    // defeat, the occasion armed just above) gets the owner's own death vox
    // over it; a plain brawl KO keeps the bare thud.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::ThudHeavy);
        if (tavern_->escalated()) {
            audio_->playOneShot(audio::SoundId::PlayerDown);
        }
    }
    tavern_->reviveAfterDefeat();
    // Through placeBodyAt: waking on the quay apron is a relocation like any
    // other, and the room you were beaten in must not ghost over the street.
    placeBodyAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
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

namespace {
/// ACTION-COMBAT BUILD (section 4.5). The two eases that bracket the death
/// ceremony's held epitaph: the frame dips to black over kDeathDipInSteps and
/// eases back over kDeathFadeOutSteps, with the sim's kDeathHoldSteps hush
/// between. The eases are one page-ease each; the HOLD is the sim's own number.
constexpr std::int32_t kDeathDipInSteps = kPageEaseSteps;
constexpr std::int32_t kDeathFadeOutSteps = kPageEaseSteps;
}  // namespace

void Session::armDeathCeremony() {
    // From the last defeat, captured before the revive moves the body: the
    // killer's name in the terminal register, a weapon word off his own hand,
    // and the place it happened. Two lines, the veto-blessed epitaph grammar
    // (section 5): "PUT DOWN IN THE GILDED GULL. / BY CANNIC. BY STEEL."
    const sim::Rise& rise = tavern_->lastDefeat();
    const std::string killer = rise.who.empty() ? std::string("THE DARK") : upperAscii(rise.who);
    // The weapon word is honest to what put you down, read off the killer's
    // rival record. A lethal fight is usually steel, but a bloodied man beaten
    // to death by fists is lethal too (B3), so the word follows the hand.
    std::string weaponWord = "STEEL";
    if (const sim::Nemesis* rival = tavern_->nemesis().of(rise.actorId); rival != nullptr) {
        switch (rival->weapon()) {
            case sim::Weapon::Edged:
                weaponWord = "STEEL";
                break;
            case sim::Weapon::Blunt:
            case sim::Weapon::Evictor:
                weaponWord = "THE CUDGEL";
                break;
            case sim::Weapon::Improvised:
                weaponWord = "WHAT CAME TO HAND";
                break;
            case sim::Weapon::Fists:
                weaponWord = "BARE HANDS";
                break;
        }
    }
    // Inside the house it is the Gull by name (the veto's own literal); on the
    // street it is the ground placeLabel() answers for.
    const std::string place =
        tavern_->playerInside() ? std::string("THE GILDED GULL") : placeLabel();
    deathEpitaphTop_ = "PUT DOWN IN " + place + ".";
    deathEpitaphBottom_ = "BY " + killer + ". BY " + weaponWord + ".";
    deathCeremonySteps_ = kDeathDipInSteps + sim::kDeathHoldSteps + kDeathFadeOutSteps;
}

void Session::composeDeathCeremony(Framebuffer& target) const {
    if (deathCeremonySteps_ <= 0) {
        return;
    }
    constexpr std::int32_t kTotal =
        kDeathDipInSteps + sim::kDeathHoldSteps + kDeathFadeOutSteps;
    const std::int32_t remaining = deathCeremonySteps_;
    // The veil's strength across the three phases: ramp up as it comes down,
    // full through the hush, ramp back as the quay comes up under it.
    float dip = 1.0F;
    if (remaining > sim::kDeathHoldSteps + kDeathFadeOutSteps) {
        const std::int32_t elapsed = kTotal - remaining;  // 0 .. kDeathDipInSteps
        dip = static_cast<float>(elapsed) / static_cast<float>(kDeathDipInSteps);
    } else if (remaining <= kDeathFadeOutSteps) {
        dip = static_cast<float>(remaining) / static_cast<float>(kDeathFadeOutSteps);
    }
    dip = dip < 0.0F ? 0.0F : (dip > 1.0F ? 1.0F : dip);
    // The veil itself -- the same black-fill primitive dipTravelSeam draws with,
    // over the whole finished frame (HUD included).
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F}, dip);
    // The epitaph, centred, in the plate's own bone register. The black field IS
    // its backing, so it needs no plate of its own; the text alpha rides the dip
    // so the words arrive as the room goes dark and leave as it comes back.
    const int scale = hudScale(target.height());
    const int glyphH = 6 * scale;  // the 4x6 font's own height, drawText's grid
    const int lineStep = glyphH + 2 * scale;
    const int cx = target.width() / 2;
    const int topY = target.height() / 2 - lineStep;
    const Rgb bone{0.86F, 0.82F, 0.72F};
    const auto centre = [&](const std::string& line, int y) {
        if (line.empty()) {
            return;
        }
        const int w = textWidth(line, scale);
        drawText(target, cx - w / 2, y, line, bone, dip, scale);
    };
    centre(deathEpitaphTop_, topY);
    centre(deathEpitaphBottom_, topY + lineStep);
}

void Session::recordWatchOp(WatchOpKind kind, std::int32_t a, bool ok) {
    if (watchTape_ == nullptr || watchDepth_ != 0) {
        return;
    }
    WatchOp op;
    op.kind = kind;
    op.a = a;
    op.ok = ok;
    watchTape_->push_back(op);
}

void Session::watchChapter(std::int32_t index) { recordWatchOp(WatchOpKind::Chapter, index); }

void Session::watchBeatLanded(std::int32_t index, bool ok) {
    recordWatchOp(WatchOpKind::BeatLanded, index, ok);
}

void Session::step(const sim::MoveInput& input) {
    // CASE WATCH. Recorded with the yaw the body carries INTO the step,
    // because the scripted walker (stepToward) steers by writing yaw directly
    // between steps -- an input tape without the yaw would replay every walk
    // in a straight line. Replay restores the yaw, then steps.
    if (watchTape_ != nullptr && watchDepth_ == 0) {
        WatchOp op;
        op.kind = WatchOpKind::Step;
        op.move = input;
        op.a = body_->yaw();
        watchTape_->push_back(op);
    }
    const WatchDepthGuard watchGuard(watchDepth_);
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
    // KIT BUILD. THE LOAD, at the same seam: AGI's gait times the Q8 term
    // the drams on the body earn (items.hpp's loadSpeedScaleQ8 -- 256 to
    // half the budget, down to 128 at it), >> 8. Exactly the shipped scale
    // with nothing on the body, so every pre-existing capture walks bit for
    // bit; player-only, integer, the one movement seam the gap analysis
    // named. PlayerBody clamps the product to its own 64..512.
    body_->setSpeedScaleQ8(static_cast<std::int32_t>(
        (static_cast<std::int64_t>(sim::agilitySpeedScaleQ8(
             tavern_->effectiveAttributes().value(sim::AttributeId::Agility))) *
         tavern_->loadSpeedQ8()) >>
        8));
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
    // JUSTICE BUILD: THE PLAYER CANNOT WALK OUT OF CUSTODY (PLAY-MODE-SPEC's
    // own rule -- "you cannot just walk out of custody by holding a key").
    // While the hearing page, the rope's plate or the end rows own the frame
    // the movement input is dropped HERE, before the body hears it, so a
    // held key and a scripted stepMany alike move nothing; the world keeps
    // stepping underneath, as it does under every page.
    sim::MoveInput moved = inCustody() ? sim::MoveInput{} : input;
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
    // ACTION-COMBAT BUILD (section 1.3): the guard holds only while the hand is
    // FREE. A swing opens you -- the vulnerable window is the Oblivion rhythm --
    // so the derived block state gains the combat-idle clause, and the GUARD UP
    // row honestly vanishes for the ~15-step swing because the row reads this
    // very state (blockLine -> playerBlocking). The caught-blow wind cost is
    // the SIM's (kBlockCatchFatiguePoints, drained in stepBrawl), not ours.
    tavern_->setPlayerBlocking(blockHeld_ && !talking() && !picking() &&
                               tavern_->playerCombatIdle());
    // STANCE & ROOM BUILD (oblivion-roadmap.md 3.2, the cancel list). A page
    // owning the keyboard -- any page, a conversation, the wire in a lock --
    // drops a live charge AND lowers the hands, pushed every step the identical
    // way the guard above is derived: no edge event exists for a page opening
    // mid-hold, so the room is told the fact rather than the keypress. The sim
    // lowers them itself for talking, picking, sleeping, arrest and defeat;
    // this is the one clause only the client can see (its own pages).
    if (picking() || conversingNow()) {
        tavern_->cancelPlayerCharge();
        tavern_->lowerPlayerHands();
    }
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
    //
    // JUSTICE BUILD (HEARING PAGE LANE). THE SAME RELEASE, THREE MEANINGS:
    // with a hearing pending it is "to the Mission's door" (openCourt below
    // takes the body there off the pending record); after the page served a
    // sentence it is "turned loose" -- at the Mission's door or on the
    // Tarwalk, by the terms; and a paperless search is the shipped Tarwalk
    // release exactly as it always was.
    if (tavern_->takeArrestRelease()) {
        if (courtServing_) {
            closeCourt();
        } else if (tavern_->hearingPending()) {
            // TAKEN, WITH PAPER -- THE ARREST HAS ITS MOMENT ON SCREEN before
            // the bench does anything (JUSTICE-SPEC 2.2): the officer's own
            // line (the room's watch.held / maimed / condemned row) said on
            // the alert row with his hand on you and the room still around
            // you, and held; then the plate; then the page. The body stays
            // where he took it until the cut. Nothing here touches the sim:
            // the hearing is already open on the ledger.
            say(tavern_->lastArrest().line);
            takenHold_ = kTakenOfficerSteps + kTakenPlateSteps;
            takenPlate_.clear();
        } else {
            placeBodyAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
            awaitingLanding_ = false;
            syncTavernToBody();
            say(tavern_->lastArrest().line);
        }
    }
    // THE ARREST BEAT, spent once a step. At the cut the plate is written
    // off the live clock and said (it is the line the spec promised, and
    // lastMessage() carries it for the drive); when the hold runs out the
    // page opens over it and the plate goes.
    if (takenHold_ > 0) {
        --takenHold_;
        if (takenHold_ == kTakenPlateSteps) {
            takenPlate_ = "TAKEN TO THE MISSION. " + travelClockText(timeOfDay_) + ".";
            say(takenPlate_);
        }
        if (takenHold_ == 0) {
            takenPlate_.clear();
            openCourt();
        }
    }
    // TAKEN. A hearing open on the ledger with no page up and no beat
    // running -- a record reopened between the arrest and the plea -- opens
    // the bench at once: the page is DERIVED from the hashed record, never
    // the other way round.
    if (!courtOpen_ && takenHold_ <= 0 && !courtServing_ && !ropeCeremonyUp() &&
        tavern_->hearingPending() && !tavern_->executed()) {
        openCourt();
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

    // ACTION-COMBAT BUILD (section 4.5). THE DEATH CEREMONY's own hold, spent
    // once a step like every other countdown -- see composeDeathCeremony() for
    // the dip, the epitaph hold and the reveal it paces. The sim already
    // revived at the defeat (settleDefeat); this is only the veil over the top,
    // so nothing here touches sim state and the player is free underneath it.
    if (deathCeremonySteps_ > 0) {
        --deathCeremonySteps_;
    }
    // JUSTICE BUILD. The judgment's held badge, and the rope's dip and
    // plate, spent once a step the same way. When the plate's hold runs out
    // the end rows come up under it; nothing fades back.
    if (courtJudgmentHold_ > 0) {
        --courtJudgmentHold_;
    }
    if (ropeCeremonySteps_ > 0) {
        --ropeCeremonySteps_;
        if (ropeCeremonySteps_ == 0) {
            ropeRowsUp_ = true;
            ropeCursor_ = 0;
        }
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
    // ACTION-COMBAT BUILD. THE SAME PER-STEP ADVANCE for the HELD HARD row.
    chargeAnim_.advance();
    // STANCE & ROOM BUILD. THE SAME PER-STEP ADVANCE for the FISTS UP row.
    handsAnim_.advance();
    // HELD-EFFECTS BUILD. THE SAME PER-STEP ADVANCE, ONE PER SLOT.
    for (EasedToggle& anim : effectAnims_) {
        anim.advance();
    }
    // FATIGUE BUILD. THE SAME PER-STEP ADVANCE.
    fatigueAnim_.advance();
    // THE WARD MAP (core action #13). THE SAME PER-STEP ADVANCE.
    districtMapAnim_.advance();
    // FAST TRAVEL's arrival seam eases back down here, one step at a time,
    // exactly like every sibling -- see travelFadeAnim_'s own header.
    travelFadeAnim_.advance();
    // SPELLS BUILD. The strip's own countdown and ease -- see showQuickBar().
    if (quickBarShowSteps_ > 0) {
        --quickBarShowSteps_;
    }
    quickBarAnim_.advance();
    // UI-EA (LANE HUD): THE EARNED-TEXT COUNTDOWNS, spent HERE and only
    // here, once a step -- placePlateShowSteps_'s own rule, whole family:
    // syncPanelAnim() can run several times in one step and a decrement in
    // there would make a row's 2.5 seconds depend on how many keys were
    // pressed during them.
    if (clockShowSteps_ > 0) {
        --clockShowSteps_;
    }
    if (purseShowSteps_ > 0) {
        --purseShowSteps_;
    }
    if (caseShowSteps_ > 0) {
        --caseShowSteps_;
    }
    if (roomShowSteps_ > 0) {
        --roomShowSteps_;
    }
    if (guildShowSteps_ > 0) {
        --guildShowSteps_;
    }
    if (objectiveShowSteps_ > 0) {
        --objectiveShowSteps_;
    }
    clockAnim_.advance();
    purseAnim_.advance();
    wheelHint_.advance();
    // UI-EA contract (c), joined at integration: the page tutor bands.
    // Raised in full on the page's own open edge and on any wake FLOW
    // signals (a device change, a press the page did not recognize), they
    // hold kTutorHoldSteps and ease back down to bare keycaps. Advanced
    // here, once a step, the family's own rule.
    {
        const bool mapUp = districtMapOpen_;
        const bool bookUp = casebookPageOpen();
        const bool keysUp = keysOpen_;
        const bool courtUp = courtOpen_;
        if (courtUp && !courtTutorWasOpen_) {
            courtTutor_.raise();
        }
        courtTutorWasOpen_ = courtUp;
        if (mapUp && !mapTutorWasOpen_) {
            mapTutor_.raise();
        }
        if (bookUp && !casebookTutorWasOpen_) {
            casebookTutor_.raise();
        }
        if (keysUp && !keysTutorWasOpen_) {
            keysTutor_.raise();
        }
        mapTutorWasOpen_ = mapUp;
        casebookTutorWasOpen_ = bookUp;
        keysTutorWasOpen_ = keysUp;
        if (tutorWakeSerial_ != tutorWakeSeen_) {
            tutorWakeSeen_ = tutorWakeSerial_;
            if (mapUp) {
                mapTutor_.raise();
            }
            if (bookUp) {
                casebookTutor_.raise();
            }
            if (keysUp) {
                keysTutor_.raise();
            }
            if (courtUp) {
                courtTutor_.raise();
            }
        }
        courtTutor_.sync(!courtUp);
        courtTutor_.advance();
        mapTutor_.sync(!mapUp);
        casebookTutor_.sync(!bookUp);
        keysTutor_.sync(!keysUp);
        mapTutor_.advance();
        casebookTutor_.advance();
        keysTutor_.advance();
    }
    // DISTRICT PHASE D. The plate's own countdown and ease -- the strip's
    // shape directly above, for the strip's reason. The countdown runs down
    // HERE and only here, once a step: syncPanelAnim() can be called several
    // times in one step (every toggle calls it) and a decrement in there would
    // make the plate's two seconds depend on how many keys were pressed
    // during them.
    if (placePlateShowSteps_ > 0) {
        --placePlateShowSteps_;
    }
    placePlateAnim_.advance();
    // THE CASEBOOK PASS. The lead-opened notice, on the identical countdown and
    // for the identical reason: run down HERE and only here, once a step, so
    // its three seconds do not depend on how many keys were pressed during
    // them.
    if (casePlateShowSteps_ > 0) {
        --casePlateShowSteps_;
    }
    casePlateAnim_.advance();
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
            // AUDIO WIRING: the taken hit, beside its blooded wash -- and THE
            // LOT PASS lays the owner's own hurt vox over it, the four takes
            // in strict turn.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::ThudMedium);
                audio_->playOneShotRoundRobin(audio::SoundId::PlayerHurt);
            }
        }
        // THE LOT PASS: a blow TAKEN, blocked or not, is a fight the music
        // has to know about.
        if (audio_ != nullptr) {
            audio_->music().noteCombat();
        }
    }
    // 3D BUILD, V LANE. THE HANDS, STEPPED HERE AND NOWHERE ELSE: once per
    // movement step, off the room's own charge and guard state plus the
    // edges latched since the last step. A blow the guard caught keeps the
    // guard up (the block nudge already says it landed); an unguarded blow
    // is the flinch, over everything. The latches clear whether or not the
    // machine took them -- an edge is a fact about THIS step.
    {
        ViewmodelInputs hands;
        hands.chargeSteps = tavern_->playerChargeSteps();
        hands.chargeHard = tavern_->playerChargeHard();
        hands.blocking = tavern_->playerBlocking();
        hands.releasedLight = viewmodelSwingPending_ == 1;
        hands.releasedHard = viewmodelSwingPending_ == 2;
        hands.cast = viewmodelCastPending_;
        hands.hit = hpNow < lastPlayerHp_ && blockedNow <= lastBlowsBlocked_;
        // STANCE & ROOM BUILD meets the 3D build: the room's own hashed
        // fighting-mode bit is what the hands read -- raised fists while
        // it is true, the arms hanging while it is false -- the identical
        // fact the FISTS UP row shows, so the row and the hands can never
        // disagree. Read here, on the step, like every input above.
        hands.handsUp = tavern_->playerHandsUp();
        (void)viewmodel_.step(hands);
        viewmodelSwingPending_ = 0;
        viewmodelCastPending_ = false;
    }
    // KIT BUILD -- DEFENCE v1 ON THE ROW. A blow the worn kit softened says
    // so, once per blow, on the same edge shape the guard's clang rides:
    // blowsTurned() moving. The piece named is the heaviest DR on the body
    // (the coat over the hood over the boots), the number is what the blow
    // lost to it. An EVENT row, never furniture: it is said only when a
    // blow actually lands and the kit actually turned some of it.
    if (const std::int32_t turnedNow = tavern_->blowsTurned(); turnedNow > lastBlowsTurned_) {
        std::string piece = "THE KIT";
        std::int32_t best = 0;
        for (const sim::ItemSlot slot :
             {sim::ItemSlot::Body, sim::ItemSlot::Head, sim::ItemSlot::Feet, sim::ItemSlot::Trinket}) {
            const sim::ItemDef* worn = tavern_->items().at(tavern_->kit().worn(slot));
            if (worn != nullptr && worn->dr > best) {
                best = worn->dr;
                piece = worn->name;
            }
        }
        say(piece + " TURNS " + std::to_string(tavern_->lastTurned()) + ".");
    }
    lastBlowsTurned_ = tavern_->blowsTurned();
    lastPlayerHp_ = hpNow;
    lastBlowsBlocked_ = blockedNow;
    // ACTION-COMBAT BUILD (section 4.1). THE STEEL-OUT FLIP, spoken ONCE on the
    // rising edge of the fight crossing the brawl line -- whoever drew it (a
    // player's hard swing on a bloodied man, an NPC drawing, a Kill-intent
    // nemesis). This is the line and the sound that replace the dead refusal
    // "BLADE OUT - THIS IS NOT A BRAWL.": there is no screen to route to, the
    // fight keeps resolving in the world, so the flip is feedback, not a
    // transition. Edge-latched off tavern_->escalated(); clearEscalation()
    // drops the latch and lastEscalated_ follows it down, ready to fire again.
    const bool escalatedNow = tavern_->escalated();
    if (escalatedNow && !lastEscalated_) {
        say("STEEL OUT. THE ROOM STANDS BACK.");
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::SwordDraw);
            // THE LOT PASS: steel out IS the combat edge for the music.
            audio_->music().noteCombat();
        }
    }
    lastEscalated_ = escalatedNow;
    // THE LOT PASS: THE STANCE, BOTH EDGES. tavern_->playerHandsUp() is the
    // one hashed bit the room reads for "fighting mode"; it rises on the
    // first swing raised and falls on the lull (kLowerHandsSteps), on LOWER
    // HANDS, and on every cancel-list clause (a page, a conversation, a
    // pick, sleep, arrest, defeat). Rising = the Malbers draw (SwordDraw --
    // the same id the escalation edge speaks, so a fight that draws steel
    // may say it twice, once for the hands and once for the blade); falling
    // = the Malbers store (Sheathe). Latched here regardless of audio_, so an
    // engine attached mid-stance does not replay the edge.
    const bool handsUpNow = tavern_->playerHandsUp();
    if (audio_ != nullptr) {
        if (handsUpNow && !lastHandsUp_) {
            audio_->playOneShot(audio::SoundId::SwordDraw);
        } else if (!handsUpNow && lastHandsUp_) {
            audio_->playOneShot(audio::SoundId::Sheathe);
        }
    }
    lastHandsUp_ = handsUpNow;
    // THE LOT PASS: A CASE CLOSING, caught by comparison across all three
    // books -- the sting is the owner's own victory cue, spoken once.
    const std::int32_t closedBooksNow = closedBookCount();
    if (audio_ != nullptr && closedBooksNow > lastClosedBooks_) {
        audio_->playOneShot(audio::SoundId::CaseClosed);
    }
    lastClosedBooks_ = closedBooksNow;
    punchLandedPulse_.advance();
    punchTakenPulse_.advance();
    blockPulse_.advance();
    // ACTION-COMBAT BUILD. The hard-release camera dip decays here beside its
    // wash siblings; camera() reads its value() as a render-only pitch offset.
    hardSwingDipPulse_.advance();
    alertPulse_.advance();
    // UI-EA-SPEC sec. 3 rule 5 (contract b). THE SAME PER-STEP ADVANCE: the
    // commit beat decays here beside its pulse siblings; the client arms it
    // at commit routing and the page drawers read it on the inverted fill.
    commitPulse_.advance();
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
        // THE LOT PASS: the music's zone off the identical signal (re-
        // asserting is a no-op there too), and its calm clock, which counts
        // SIM steps -- brawlers standing keep it at zero; ten seconds of
        // nobody swinging and nobody standing is the way back to exploration.
        // Audio-side state only; the sim never reads any of it.
        audio_->music().setZone(tavern_->playerInside()
                                    ? audio::MusicZone::Interior
                                    : audio::MusicZone::Docks);
        audio_->music().step(tavern_->playerInBrawl());
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
    // THE CROSSHAIR PASS. The aim prompt's other three parts, cleared on the
    // identical edge and by the identical rule -- one element, one toggle, so
    // a name must not survive the verb it hung off.
    clearIfClosed(interactAnim_, interactSubjectCache_);
    clearIfClosed(interactAnim_, interactNoteCache_);
    if (interactAnim_.settled() && !interactAnim_.target()) {
        interactKindCache_ = AimKind::Nothing;
    }
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
    clearIfClosed(chargeAnim_, chargeCache_);
    clearIfClosed(handsAnim_, handsCache_);

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
    // STREET PANIC BUILD (feel/build, 9a). THE STREET REACTS TO WHAT IT CAN
    // SEE, and this is the ONE call site that tells it. The tavern's own
    // violence is read off public state by the comparison-not-flag shape the
    // hp watch above uses, tiered, and handed to the district's people at
    // the player's own tile -- the takeCoinFrom shape: the client moves a
    // WardActor through one named verb and hashes nothing of its own. Three
    // tiers, by severity (ward_actors.hpp names every constant and why):
    //   KILL   a body on the roster died this step -- a non-vermin corpse
    //          appeared. The edge, fired at once.
    //   BLOW   a blow landed on somebody under LETHAL rules this step -- the
    //          room's hp fell while a lethal fight was live or the fight had
    //          escalated. The edge, fired at once. A bar-fight punch is the
    //          house's business and frightens nobody outside.
    //   STEEL  the hands are up with steel in them, or up over a floored man
    //          in reach. Continuous, so it is re-asserted once a simulated
    //          second -- on the ward's own tick, just taken -- and the bubble
    //          follows a man walking the Tarwalk with a blade out.
    // Who actually runs is the ward's rule (same band, in range, line of
    // sight), which is why a killing inside the roofed Gull reaches the street
    // only through the door. people_ is null in a session without a district
    // (the tavern fixture), and then the street is nobody.
    if (people_ != nullptr) {
        std::int32_t roomHp = 0;
        std::int32_t corpses = 0;
        bool flooredInReach = false;
        for (const sim::Actor& actor : tavern_->actors()) {
            if (actor.role() == sim::ActorRole::Vermin) {
                continue;  // a rat is not a man, alive or dead
            }
            roomHp += actor.hp();
            if (actor.activity() == sim::Activity::Dead) {
                ++corpses;
            }
            if (actor.present() && sim::isFloored(actor.activity()) &&
                actor.distanceTo(body_->x(), body_->y()) <= sim::kMeleeReach) {
                flooredInReach = true;
            }
        }
        bool alarmed = false;
        sim::AlarmSeverity severity = sim::AlarmSeverity::Steel;
        if (lastCorpsesForAlarm_ >= 0 && corpses > lastCorpsesForAlarm_) {
            severity = sim::AlarmSeverity::Kill;
            alarmed = true;
        } else if (lastRoomHpForAlarm_ >= 0 && roomHp < lastRoomHpForAlarm_ &&
                   (tavern_->lethalFightLive() || tavern_->escalated())) {
            severity = sim::AlarmSeverity::Blow;
            alarmed = true;
        } else if (stepsThisSecond_ == 0 && tavern_->playerHandsUp() &&
                   (tavern_->playerWeapon() >= sim::kFirstLethalWeapon || flooredInReach)) {
            severity = sim::AlarmSeverity::Steel;
            alarmed = true;
        }
        if (alarmed) {
            people_->alarm(body_->tileX(), body_->tileY(), body_->band(),
                           sim::alarmRadius(severity), severity);
        }
        lastRoomHpForAlarm_ = roomHp;
        lastCorpsesForAlarm_ = corpses;
    }
    // BARKS LANE (feel/build). WHAT THE ROOM SAID THIS STEP, on the alert
    // row: the watchman's halt (or his contraband demand -- lastDemand was
    // composed by the sim since S6 and read by nobody but the tests), the man
    // stepping into the fight (brawl.join), the panic line (crowd.flee). Each
    // is a string the sim already owns; the client only notices it CHANGE --
    // the comparison-not-flag shape lastPlayerHp_ keeps, three unhashed
    // strings on this side and nothing new reaching into the simulation. The
    // halt is spoken last so it wins the row when all three land on one step.
    if (tavern_->lastJoin() != lastJoinSpoken_) {
        lastJoinSpoken_ = tavern_->lastJoin();
        say(lastJoinSpoken_);
    }
    if (tavern_->lastFlee() != lastFleeSpoken_) {
        lastFleeSpoken_ = tavern_->lastFlee();
        say(lastFleeSpoken_);
    }
    if (tavern_->lastDemand() != lastDemandSpoken_) {
        lastDemandSpoken_ = tavern_->lastDemand();
        say(lastDemandSpoken_);
    }
    syncWardToCalendar();
    // COURIER CASE. Last, deliberately: the courier's countdown, the take
    // nudge and the scripted delivery all read the step the world just
    // finished taking, and anything they say() eases in on the next frame
    // exactly as every other verb's message does.
    stepSheetCase();
    // EVICTION CASE, alongside the courier's for the identical reason: the
    // door nudge and the scripted delivery read the step just finished.
    stepEvictCase();
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
    // UI-EA (LANE HUD): FOUR SECONDS, the spec's own message-row narration
    // hold (sec. 2, EVENT tier). Six was the pre-diet number; a line the
    // player is reading is re-armed by the next line anyway, and the bottom
    // of the frame is meant to be empty more than it is full. A bouncer's
    // WARNING is not on this clock -- `warned` holds the alert up for as
    // long as the house is still saying it (syncPanelAnim's own rule).
    messageSteps_ = 4 * sim::kStepsPerSecond;
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
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Interact);
    const WatchDepthGuard watchGuard(watchDepth_);
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
    // COURIER CASE, checked ahead of TALK for the reason the rest-check runs
    // first: it is the one thing this press could mean that nothing else here
    // could also mean. A downed man is not a conversation.
    if (!sneaking && caseTakeReady()) {
        sheetCarry_ = true;
        sheetTakeSaid_ = false;
        const std::int32_t close = sheetRaws_.indexOf("bring-him-in");
        if (close >= 0) {
            // The book learns where this ends the moment the man is in hand,
            // so the objective row swings to the Mission on the same press.
            (void)sheetBook_.hear(close, caseNowSeconds());
        }
        say("YOU HAVE HIM. THE MISSION'S BACK ROOM, AND NOTHING EDGED ON THE WAY.");
        return;
    }
    // EVICTION CASE, checked ahead of TALK for caseTakeReady's own reason:
    // a served door is the one thing this press could mean that nothing else
    // here could also mean. Knock first, serve second -- two acts, one verb,
    // and the serve is never forced: a player who means to disrupt knocks
    // (or does not) and walks back to the priest to give the writ up.
    if (!sneaking && evictDoorReady()) {
        if (!evictKnocked_) {
            evictKnocked_ = true;
            evictEverKnocked_ = true;
            evictDoorSaid_ = false;
            say("YOU KNOCK. A CHAIR SCRAPES. THE MARROW DOOR OPENS ON A TIRED MAN AND THE "
                "SMELL OF THIN SOUP.");
            return;
        }
        // THE SERVE. The paper changes hands; nobody swings, which is what
        // the good hand was hired to make true. The objective swings to the
        // Mission on this press -- the book learns where the errand ends the
        // moment the writ is served.
        writServed_ = true;
        evictDoorSaid_ = false;
        const std::int32_t served = evictRaws_.indexOf("served");
        if (served >= 0) {
            (void)evictBook_.hear(served, caseNowSeconds());
        }
        say("THE WRIT INTO HIS HAND. HE READS THE ROLL'S OWN WORDS AND DOES NOT ARGUE WITH "
            "PAPER. CARRY IT BACK SIGNED.");
        return;
    }
    // 2a. THE DEAD (KIT BUILD): a corpse in reach, when no living roster
    // body is NEARER, opens its search list -- ahead of the living for the
    // person walk's own reason (a body at your feet is the one thing this
    // press could mean that the man beside it could not also mean), and
    // behind them when they are closer, so a crowded taproom still talks.
    if (searchNearestCorpse()) {
        return;
    }
    if (!sneaking) {
        // TIME-AND-TENURE BUILD: the director is told what ground the feet
        // are on BEFORE the conversation opens, so a priest's topic list is
        // built knowing whether there is a roll to read here. See
        // syncGroundPlot().
        syncGroundPlot();
        // EVICTION CASE: and the writ's stage, so Maell's own list carries
        // the hire (or the walk-back) the moment his conversation opens.
        syncEvictionTopics();
        const auto opened = [this]() {
            topicCursor_ = 0;
            haggleOffer_ = 0;
            const sim::DialogueDirector& talk = tavern_->dialogue();
            say(talk.speaker().name + ": " + talk.greeting());
        };
        if (tavern_->talkTo()) {
            opened();
            return;
        }
        // 2b. A THING ON THE GROUND (KIT BUILD) sits between the house's
        // roster and the street's people: the thing you walked to outranks
        // a passer-by (and a mouse), and a named lead outranks the thing
        // (lowerHandsResolves' own rule). After the box chain would have
        // been the tidier seat, but the street's TALK is inside this walk
        // and a rope on the Tarwalk must beat it. Upright only: crouched,
        // the press is a lift or the box chain's TAKE QUIETLY below.
        if (!leadNamedInReach() && takeNearestItem()) {
            return;
        }
        if (talkToWard()) {
            opened();
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

    // 3b. A THING ON THE GROUND (KIT BUILD), the crouched half: upright the
    // walk took it at 2b; crouched it comes after the box chain (a bed-foot
    // strongbox keeps its stand) as TAKE QUIETLY -- the notice rule, never
    // the verb, is what the stance moves. The look still outranks the take.
    if (sneaking && !leadNamedInReach() && takeNearestItem()) {
        return;
    }

    // 4. HANDS UP AND NOTHING IN REACH: LOWER HANDS (STANCE & ROOM BUILD,
    // section 3.2 lower rule 1). After the person, the fixture and a lead the
    // book has heard of -- lowerHandsResolves() is the ONE predicate the
    // prompt walk answers LOWER HANDS with, so the reticle and the key agree
    // by construction. No line: the FISTS UP row going down is the feedback.
    if (lowerHandsResolves()) {
        tavern_->lowerPlayerHands();
        return;
    }

    // 5. NOTHING RESOLVED: the investigation look, which never refuses.
    // examine() re-checks talking()/picking()/casebookOpen_ on its own, all
    // of which dismissOverlays() above already settled, so this is safe to
    // call unconditionally.
    examine();
}

bool Session::leadNamedInReach() const {
    // A LEAD THE CROSSHAIR WOULD NAME IS SOMETHING IN REACH -- the identical
    // three-book walk, with the identical "an unheard lead is not named"
    // line, that resolveInteract() names the subject with. Read-only:
    // leadInLookReach() and its two siblings are walks over Lead::site,
    // never Casebook::look().
    const auto named = [](const sim::Casebook& book, int lead) {
        return lead >= 0 && book.raws() != nullptr &&
               book.state(static_cast<std::int32_t>(lead)) != sim::LeadState::Unheard;
    };
    return named(casebook_, leadInLookReach()) || named(sheetBook_, sheetLeadInLookReach()) ||
           named(evictBook_, evictLeadInLookReach());
}

bool Session::lowerHandsResolves() const {
    if (!tavern_->playerHandsUp()) {
        return false;
    }
    // The look outranks the lower.
    return !leadNamedInReach();
}

const sim::Actor* Session::corpseToSearch() const {
    // The corpse in reach of the BODY, and only when no living roster body
    // is nearer -- ties to the dead, so standing on him is a search.
    const sim::Actor* corpse = tavern_->corpseInReachOf(body_->x(), body_->y(), body_->band());
    if (corpse == nullptr) {
        return nullptr;
    }
    const bool sneaking = stance() == sim::Stance::Crouched;
    const std::int32_t reach = sneaking ? sim::kLiftReachQ8 : sim::kReachQ8;
    const sim::Actor* living = tavern_->nearestTo(body_->x(), body_->y(), reach);
    if (living != nullptr &&
        living->distanceTo(body_->x(), body_->y()) < corpse->distanceTo(body_->x(), body_->y())) {
        return nullptr;
    }
    return corpse;
}

bool Session::groundTargetFor(InteractTarget& out) const {
    if (leadNamedInReach()) {
        return false;
    }
    const std::int32_t at = tavern_->groundItemInReachOf(body_->x(), body_->y(), body_->band());
    if (at < 0) {
        return false;
    }
    const sim::GroundItem& entry = tavern_->groundItems()[static_cast<std::size_t>(at)];
    const sim::ItemDef* thing = tavern_->items().at(entry.item);
    // A fixed thing (the strongbox) is not named here; the box block is.
    if (thing == nullptr || thing->fixed) {
        return false;
    }
    out = InteractTarget{};
    out.verb = stance() == sim::Stance::Crouched ? "TAKE QUIETLY" : "TAKE";
    out.subject = entry.count > 1 ? std::to_string(entry.count) + " " + thing->name : thing->name;
    out.note = entry.owned ? std::string("THEIRS") : std::to_string(thing->drams) + "DR";
    out.kind = entry.owned ? AimKind::Owned : AimKind::Thing;
    return true;
}

std::string Session::interactPrompt() const {
    // ONE DESCRIPTION OF THE RESOLUTION ORDER, NOT TWO. The crosshair pass
    // needed the walk to answer WHAT as well as WHICH VERB, and a second copy
    // of the order in this file is precisely the drift that lets the HUD start
    // lying about the key -- see interact()'s own header. So the walk moved
    // whole into interactTarget() and this is the half of its answer every
    // caller before the crosshair pass wanted.
    return interactTarget().verb;
}

namespace {

/// THE FURTHEST A CROSSHAIR REACHES FOR A BUILDING'S NAME, in tiles.
///
/// FIVE, and it is the same measurement kLookRangeTiles is: a shop in this
/// ward is seven by eight (DOCKS-GAZETTEER 3.1), so five tiles down the line
/// of sight is "the frontage I am standing at" and not "the warehouse across
/// the quay". It is deliberately one tile longer than the look range, because
/// a player reads a door from the pavement and looks at a lead from inside.
constexpr std::int32_t kAimPlaceReachTiles = 5;

/// Whether two labels say the same word, ignoring case.
///
/// THE WARD HAS BODIES WHOSE NAME IS THEIR TRADE. A mouse is named "MOUSE" and
/// its trade is "MOUSE", and the first capture of the crosshair over one read
/// "MOUSE  MOUSE" -- a qualifier qualifying nothing, which is the same "absence
/// costs nothing" rule the top-right stack has kept since polish-1, missed in
/// a new place. See interactTarget().
[[nodiscard]] bool saysTheSame(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char lhs = a[i] >= 'a' && a[i] <= 'z' ? static_cast<char>(a[i] - 32) : a[i];
        const char rhs = b[i] >= 'a' && b[i] <= 'z' ? static_cast<char>(b[i] - 32) : b[i];
        if (lhs != rhs) {
            return false;
        }
    }
    return true;
}

/// A Q16 offset, rounded to the nearest whole tile in both directions.
/// Truncation toward zero would make a body facing due west name a place one
/// tile east of the one it is looking at.
[[nodiscard]] std::int32_t tilesFromQ16(std::int32_t q16, std::int32_t steps) noexcept {
    const std::int64_t scaled = static_cast<std::int64_t>(q16) * steps;
    const std::int64_t half = sim::kTrigOne / 2;
    return static_cast<std::int32_t>(scaled >= 0 ? (scaled + half) / sim::kTrigOne
                                                 : (scaled - half) / sim::kTrigOne);
}

}  // namespace

Session::InteractTarget Session::interactTarget() const {
    InteractTarget out = resolveInteract();
    // ONE RULE OVER ALL EIGHT EXITS OF THE WALK -- see resolveInteract()'s own
    // header. A note that repeats its subject is a row of the HUD holding
    // twice the width to say one thing, which is the exact defect polish-1
    // deleted "NOBODY IN PARTICULAR" for.
    if (saysTheSame(out.subject, out.note)) {
        out.note.clear();
    }
    return out;
}

Session::InteractTarget Session::resolveInteract() const {
    InteractTarget out;
    // #85. THE LABEL Interact IS ABOUT TO RESOLVE TO -- see interactPrompt()'s
    // own header in session.hpp for the verification gap (the bale, the rat and
    // buyPicks() are not previewed) and for why the order below has to track
    // interact()'s own order exactly.
    if (talking() || picking() || pauseOpen() || menuOpen() || waitOpen() || inCustody()) {
        // JUSTICE BUILD: in custody -- the officer's hand on you, the page,
        // the plate -- Interact does nothing, so the crosshair promises
        // nothing: "E - TALK" over the watchman who has just taken you would
        // be a prompt for a verb that is refused.
        // The topic list / the tiled Menu's own rows already show what
        // Interact (or ENTER) does on this row -- menuOpen() covers the
        // tiled Menu, Keys AND Options, not only Options, which an earlier
        // pass of this check missed (caught by test_interact_context.cpp:
        // toggleMenu() opens the casebook first, and the label kept
        // computing a real prompt behind it). A second, floating label would
        // say the same thing twice in two different places on the same
        // frame. waitOpen() joined the list with the Wait page, whose rows
        // print their own numbers the same way.
        return out;
    }
    const bool sneaking = stance() == sim::Stance::Crouched;

    // 1. REST. Tile-exact rather than sleep()'s own Q8 Chebyshev distance --
    // close enough for a label, and body_ has no reason to expose Q8 here
    // when the tile the body is standing on already answers it.
    if (!sneaking && tavern_->rentedRoom() >= 0 && body_->band() == sim::gull::kUpperBand) {
        const sim::gull::GuestRoom& room = sim::gull::kRooms[tavern_->rentedRoom()];
        if (body_->tileX() == room.standX && body_->tileY() == room.standY) {
            out.verb = "REST";
            out.subject = "YOUR BED";
            out.note = "ROOM " + std::to_string(tavern_->rentedRoom() + 1);
            out.kind = AimKind::Thing;
            return out;
        }
    }

    // 2. PERSON. The identical reach each verb would actually use --
    // sim::kReachQ8 is talkTo()'s own 2*kSubOne, sim::kLiftReachQ8 is
    // liftFrom()'s. Nearest is a read-only query on both Tavern and
    // WardPopulation; neither talks to anybody by being asked.
    const std::int32_t reach = sneaking ? sim::kLiftReachQ8 : sim::kReachQ8;
    // COURIER CASE, mirrored EXACTLY where interact() checks it: ahead of the
    // person walk, so the crosshair names the take on the same frame the key
    // would perform it.
    if (!sneaking && caseTakeReady()) {
        const sim::Actor* quarry = sheetQuarry();
        out.verb = "TAKE HIM UP";
        out.subject = quarry == nullptr ? std::string("FINCH") : quarry->name();
        out.note = "DOWN, AND COMING WITH YOU";
        out.kind = AimKind::Person;
        return out;
    }
    // EVICTION CASE, mirrored where interact() checks it: the knock, then the
    // serve, named on the frame the key would perform it.
    if (!sneaking && evictDoorReady()) {
        out.verb = evictKnocked_ ? "SERVE THE WRIT" : "KNOCK";
        out.subject = "THE MARROW DOOR";
        out.note = evictKnocked_ ? "THEY ARE AT THE DOOR" : "A LIGHT IS ON";
        out.kind = AimKind::Place;
        return out;
    }
    // 2a. THE DEAD (KIT BUILD), mirrored where interact() checks it: the
    // corpse in reach, when no living roster body is nearer, is a SEARCH,
    // named, a body's own accent, and DEAD where a living man's trade would
    // print.
    if (const sim::Actor* corpse = corpseToSearch(); corpse != nullptr) {
        out.verb = "SEARCH";
        out.subject = corpse->name();
        out.note = "DEAD";
        out.kind = AimKind::Person;
        return out;
    }
    // NAMED IN THE ORDER interact() WOULD REACH THEM. The taproom's own roster
    // is asked first because talkTo() is, so the body the crosshair names is
    // the body the key would actually speak to -- a prompt that named the
    // wrong one of two people in a doorway would be worse than the bare verb
    // it replaced.
    if (const sim::Actor* inHouse = tavern_->nearestTo(body_->x(), body_->y(), reach);
        inHouse != nullptr) {
        // Pickpocketing a WARD actor is not implemented (lift() only ever
        // reached the Tavern's own roster) -- matched here rather than
        // previewing a verb the button cannot actually perform.
        out.verb = sneaking ? "PICKPOCKET" : "TALK";
        out.subject = inHouse->name();
        out.note = std::string(sim::actorRoleName(inHouse->role()));
        out.kind = AimKind::Person;
        return out;
    }
    // 2b. A THING ON THE GROUND (KIT BUILD), mirrored where interact()
    // reaches it upright -- between the house's roster and the street's
    // people: TAKE names the thing, the note is its weight, or THEIRS with
    // the Owned accent when taking it is theft (the reference's red hand,
    // before the press). A named lead in reach outranks it. The BODY's own
    // position, as every other query on this walk. Crouched, the same read
    // comes after the box (TAKE QUIETLY), where interact() takes it.
    if (!sneaking) {
        if (InteractTarget thing; groundTargetFor(thing)) {
            return thing;
        }
        if (const sim::WardActor* outside = people_->nearestTo(
                body_->tileX(), body_->tileY(), body_->band(), kWardTalkReachTiles);
            outside != nullptr) {
            out.verb = "TALK";
            // #79's own point, on the HUD at last: the ward HAS names. A body
            // the bake never named is "SOMEBODY", which is the ward map's own
            // wording for the same gap rather than a second invention.
            const sim::WardIdentity& who = people_->identity(outside->id);
            out.subject = who.name.empty() ? std::string("SOMEBODY") : who.name;
            out.note = std::string(sim::wardTypeName(outside->type));
            out.kind = AimKind::Person;
            return out;
        }
    }

    // 3. THE BOX -- the one item this can preview exactly, because its
    // geometry (gull::roomAtStand) and its state (rentedRoom/crackedBoxes/
    // openedLocks) are all public, read-only, and the same ones
    // crackStrongbox() itself reads.
    if (body_->band() == sim::gull::kUpperBand) {
        const std::int32_t room = sim::gull::roomAtStand(body_->tileX(), body_->tileY());
        if (room >= 0) {
            const std::int32_t bit = 1 << room;
            out.subject = "THE STRONGBOX";
            out.note = "ROOM " + std::to_string(room + 1);
            out.kind = AimKind::Thing;
            if (room == tavern_->rentedRoom()) {
                // Resolves to a refusal ("THAT ONE IS YOURS"), but the box is
                // still what the press is about, so the button still names
                // an action rather than falling back to LOOK. STATE CHANGES
                // THE NOTE, which is the reference's own rule -- and it is
                // the difference between "that key does nothing" and "that
                // one is mine".
                out.verb = "TAKE";
                out.note += "  YOURS";
                return out;
            }
            if ((tavern_->crackedBoxes() & bit) == 0) {
                if ((tavern_->openedLocks() & bit) == 0) {
                    out.verb = "PICK LOCK";
                    out.note += "  LOCKED";
                    return out;
                }
                out.verb = sneaking ? "TAKE QUIETLY" : "TAKE";
                out.note += "  OPEN";
                return out;
            }
            // Cracked already. LOOK is what the key falls through to, and the
            // note says why rather than leaving the player to press it twice.
            out.note += "  EMPTIED";
        }
    }

    // 3b. A THING ON THE GROUND (KIT BUILD), the crouched half: TAKE QUIETLY
    // after the box's own stand, exactly where interact() takes it crouched.
    if (sneaking) {
        if (InteractTarget thing; groundTargetFor(thing)) {
            return thing;
        }
    }

    // 4. THE LOOK, AND WHAT IT WOULD BE LOOKING AT.
    //
    // interactPrompt() has answered a bare "LOOK" here since #85 and that is
    // the single biggest reason the owner could not tell a failed search from
    // absent content -- his Bloodletter run stalled at the Weighhouse. The
    // verb is unchanged; what is new is that the crosshair NAMES THE LEAD when
    // one is standing under it.
    out.verb = "LOOK";
    if (out.subject.empty()) {
        if (const int lead = leadInLookReach(); lead >= 0 && casebook_.raws() != nullptr) {
            const sim::Lead& site = casebook_.raws()->leads()[static_cast<std::size_t>(lead)];
            const sim::LeadState state = casebook_.state(static_cast<std::int32_t>(lead));
            // AN UNHEARD LEAD IS NOT NAMED, and that is a design line rather
            // than an oversight. Unheard means nobody has told the player this
            // exists; a crosshair that named it would hand them the trail for
            // walking past a door and turn an investigation into a sweep. Once
            // it IS in the book -- which is what `start` and every `opens` list
            // do -- naming it is exactly what the owner's stall at the
            // Weighhouse needed.
            if (state != sim::LeadState::Unheard) {
                // `what` and not `short`: the authored line is the OBJECT, in
                // the ward's own words -- "THE HARBORMASTER'S LEDGER", "THE
                // SEWER MOUTH IN THE SEAWALL", "THE BODY, AND WHOEVER FOUND
                // IT". `short` is a casebook ROW label ("THE BODY"), authored
                // for a three-column grid, and pairing the two would print the
                // same noun twice on one row.
                out.subject = site.what.empty() ? site.place : site.what;
                // STATE CHANGES THE NOTE, which is the reference's own rule
                // and the whole point here: a player sweeping a room they have
                // already worked must not be sent round it a second time.
                // ABSENCE COSTS NOTHING (this file's own polish-1 rule) -- an
                // unread lead says nothing extra, because "not read" is what
                // the ordinary case is.
                if (state != sim::LeadState::Open) {
                    out.note = "ALREADY READ";
                }
                out.kind = AimKind::Clue;
                return out;
            }
        }
    }
    // COURIER CASE. The second book's sites get the identical naming, asked
    // only when the first named nothing -- the same member-order tiebreak
    // examine() keeps.
    if (out.subject.empty()) {
        if (const int lead = sheetLeadInLookReach(); lead >= 0 && sheetBook_.raws() != nullptr) {
            const sim::Lead& site = sheetBook_.raws()->leads()[static_cast<std::size_t>(lead)];
            const sim::LeadState state = sheetBook_.state(static_cast<std::int32_t>(lead));
            if (state != sim::LeadState::Unheard) {
                out.subject = site.what.empty() ? site.place : site.what;
                if (state != sim::LeadState::Open) {
                    out.note = "ALREADY READ";
                }
                out.kind = AimKind::Clue;
                return out;
            }
        }
    }
    // EVICTION CASE. The third book's sites, the same member-order tiebreak
    // one book deeper -- its two close leads are excluded by
    // evictLeadInLookReach itself, so a look here can only ever name an
    // investigation lead.
    if (out.subject.empty()) {
        if (const int lead = evictLeadInLookReach(); lead >= 0 && evictBook_.raws() != nullptr) {
            const sim::Lead& site = evictBook_.raws()->leads()[static_cast<std::size_t>(lead)];
            const sim::LeadState state = evictBook_.state(static_cast<std::int32_t>(lead));
            if (state != sim::LeadState::Unheard) {
                out.subject = site.what.empty() ? site.place : site.what;
                if (state != sim::LeadState::Open) {
                    out.note = "ALREADY READ";
                }
                out.kind = AimKind::Clue;
                return out;
            }
        }
    }
    // 5. LOWER HANDS (STANCE & ROOM BUILD): every lead block above returned
    // when it named one, so reaching here with the hands up is exactly the
    // press interact() lowers them on -- the same lowerHandsResolves() it
    // reads. A verb with no subject: "nothing in reach" is the whole point,
    // and a box note or a place name would be the HUD naming a thing the key
    // is not about to touch.
    if (lowerHandsResolves()) {
        out.verb = "LOWER HANDS";
        out.subject.clear();
        out.note.clear();
        out.kind = AimKind::Nothing;
        return out;
    }
    if (out.subject.empty()) {
        // THE DOOR YOU ARE ACTUALLY FACING. The ward map pass built the
        // district's own place index off the authored footprints -- see
        // mapPlaceUnder -- so "what building is this" is a lookup here rather
        // than a second walk over the sign table. WAYS ARE SKIPPED: the
        // compass ribbon already prints the street's name every frame, and a
        // crosshair repeating it would be the HUD saying one thing twice.
        const std::int32_t fx = sim::forward_x_q16(body_->yaw());
        const std::int32_t fy = sim::forward_y_q16(body_->yaw());
        for (std::int32_t step = 0; step <= kAimPlaceReachTiles; ++step) {
            const std::int32_t tx = body_->tileX() + tilesFromQ16(fx, step);
            const std::int32_t ty = body_->tileY() + tilesFromQ16(fy, step);
            const int place = mapPlaceUnder(tx, ty);
            if (place < 0) {
                continue;
            }
            const MapPlace& named = mapPlaces()[static_cast<std::size_t>(place)];
            if (named.way) {
                continue;
            }
            out.subject = named.name;
            out.kind = AimKind::Place;
            break;
        }
    }
    return out;
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
                                  static_cast<int>(activeCasebook().known().size() +
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
                            activeCasebook().known().size() + journalWorkRows().size());
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
    recordWatchOp(WatchOpKind::Topic, slot);
    const WatchDepthGuard watchGuard(watchDepth_);
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
                const int leads = static_cast<int>(activeCasebook().known().size());
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
        if (index < activeCasebook().known().size()) {
            caseCursor_ = static_cast<int>(index);
            caseEntry_ = static_cast<int>(index);
            // AUDIO WIRING: opening an entry of your own notes is a page, not
            // a menu -- BookFlip, not UiConfirm.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::BookFlip);
            }
        } else if (index < activeCasebook().known().size() + journalWorkRows().size()) {
            // A work row under the trail -- a live contract, a finished
            // stage's log line -- is something to read, not a choice: the
            // cursor moves onto it, no entry opens and no page speaks. The
            // identical no-op the character tile gives a pick.
            caseCursor_ = static_cast<int>(index);
        }
        return;
    }
    if (casebookOpen_ && menuFocus_ == kMenuFocusCharacter) {
        // KIT BUILD. A CARRIED ROW IS A CHOICE NOW: ENTER wears it (or bares
        // it) through the room's own door. A sheet row stays something to
        // read -- the no-op the keys page gives -- so the index passed in
        // is ignored for the tile's OWN cursor, which is the row on screen.
        (void)index;
        wearHighlightedKitRow();
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
    } else if (reply.ok && reply.kind == sim::TopicKind::TakeWrit) {
        // EVICTION CASE. The director answered the speech (and gated the
        // open-hand bar); the CASE half is the session's, the ReadRoll split
        // one book on. The priest's own line is already in reply.line -- the
        // book opens under it.
        settleTakeWrit();
    } else if (reply.ok && reply.kind == sim::TopicKind::YieldWrit) {
        settleYieldWrit();
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
    // UI-EA-SPEC sec. 4 violation #4: BACK RETURNS TO THE OPENER. A page
    // entered through the pause menu -- CONTROLS, SETTINGS, WAIT -- backs
    // out ONE layer, to the pause menu with its cursor on the row that
    // opened the page, instead of skipping past it to the street. A page
    // entered by its direct shortcut (F1/F2, a bed's wait) still lands on
    // the street exactly as before: pageOpenedFromPause_ was derived false
    // at its open. The panel never closes across the return -- conversingNow
    // stays true -- so the swap reads as content changing inside one frame,
    // transition rule 2, and no close/open sound pair fires.
    const auto returnToPause = [this](int row) {
        pageOpenedFromPause_ = false;
        pauseOpen_ = true;
        // The row indices are pauseRows()' own order: RESUME, WAIT,
        // CONTROLS, SETTINGS, QUIT.
        pauseCursor_ = row;
        quitArmed_ = false;
        syncPanelAnim();
    };
    if (optionsOpen_) {
        // ESC out of a rebinding first, and out of the page second. A player
        // who opened "press a key" by accident has to be able to get out of it
        // without binding escape to something.
        if (awaitingKey_) {
            awaitingKey_ = false;
            return;
        }
        const bool toPause = pageOpenedFromPause_;
        optionsOpen_ = false;
        optionCursor_ = 0;
        optionPage_ = 0;
        if (toPause) {
            returnToPause(3);  // SETTINGS
            return;
        }
        syncPanelAnim();
        return;
    }
    if (keysOpen_) {
        const bool toPause = pageOpenedFromPause_;
        keysOpen_ = false;
        caseCursor_ = 0;
        casePage_ = 0;
        if (toPause) {
            returnToPause(2);  // CONTROLS
            return;
        }
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
        const bool toPause = pageOpenedFromPause_;
        waitOpen_ = false;
        waitCursor_ = 0;
        waitPage_ = 0;
        if (toPause) {
            returnToPause(1);  // WAIT
            return;
        }
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
    // KIT BUILD: over a carried row the epithet is the verbs, in the
    // device's own words -- the Grimoire page's "LEFT RIGHT BIND A SLOT"
    // idiom -- so the tile never advertises a press it will not take. The
    // reputation line stands on every sheet row.
    if (const std::optional<KitRow> row = highlightedKitRow(); row.has_value() && row->inKit) {
        const sim::ItemDef* thing = tavern_->items().at(row->item);
        const InputDevice dev = promptDevice_;
        // The arrows as motifs on a keyboard (the tile is thirty-odd cells
        // wide at 960 and "LEFT RIGHT" pushed DROP off it), the bare cross on
        // a pad -- UI-EA-SPEC sec. 5's own substitutions.
        std::string verbs;
        if (thing != nullptr && thing->slot != sim::ItemSlot::None) {
            verbs = std::string(promptConfirmKey(dev)) +
                    (tavern_->kit().isWorn(row->item) ? " BARE  " : " WEAR  ") +
                    (dev == InputDevice::Pad ? std::string(1, kMotifDPad)
                                             : std::string{kMotifLeft, kMotifRight}) +
                    " SLOT  ";
        }
        verbs += "X DROP";
        view.epithet = verbs;
    }
    view.line = "WHAT THE STREETS HAVE MADE OF YOU, AND THE HANDS THAT DID IT.";
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
    // AND WHAT THE BLANK HALF OF THE PANEL IS WAITING FOR. All three sections
    // above are built out of casebook_.known() and nothing else -- see
    // mapRows' own header -- so the honest sentence is that the chart is a
    // reading of the book, and the book is what the player fills. True at
    // every point in a run, which is why it is not gated on the row count:
    // this panel is never finished until the case is.
    // Empty states run six words or fewer now (UI-EA-SPEC): the reading is
    // the book's, said in four.
    view.emptyLine = "WHAT THE CASEBOOK KNOWS.";
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
    // COURIER CASE. Whether anything on the shelf right now was put into the
    // player's own hand -- the epithet and the pick line below stop claiming
    // "read, not received" the moment that stops being the whole truth.
    bool anyHanded = false;
    for (const std::int32_t index : unlocked) {
        if (letterAt(index).handed) {
            anyHanded = true;
            break;
        }
    }
    if (lettersEntry_ >= 0 && static_cast<std::size_t>(lettersEntry_) < unlocked.size()) {
        const sim::Letter& read =
            letterAt(unlocked[static_cast<std::size_t>(lettersEntry_)]);
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
        // COURIER CASE. "READ, NOT RECEIVED" survives only while it is true:
        // a handed mission sheet IS received post, so the epithet and the
        // pick line change with the shelf rather than lying about it.
        view.epithet = anyHanded ? "THE WARD'S PAPER" : "READ, NOT RECEIVED";
        view.line = unlocked.empty()
                        ? "YOU HAVE READ NOBODY'S POST YET."
                        : (anyHanded
                               ? "YOUR OWN PAPER, AND OTHER PEOPLE'S. PICK ONE AND READ IT WHOLE."
                               : "OTHER PEOPLE'S PAPER. PICK ONE AND READ IT WHOLE.");
        // THE ONE PANEL IN THE BUILD THAT SHIPS WITH NOTHING IN IT AT ALL, and
        // the tile draws no `line`, so until this the first thing a stranger
        // saw here was a title over a quarter-screen of black. The gate is
        // unlockedLetters()' own -- a letter turns up when its lead has been
        // stood over, never merely heard -- so the sentence names exactly the
        // act that fills the panel, and stops claiming nobody has written to
        // you the moment somebody has.
        //
        // "READ, NOT RECEIVED" is the epithet above it, and the wording keeps
        // faith with it: none of these five documents is addressed to the
        // player, so the copy talks about paper the leads keep, never about
        // post the player was sent.
        // Six words or fewer (UI-EA-SPEC empty-state law), the same paper-
        // not-post wording, shorter.
        view.emptyLine = unlocked.empty() ? "NO PAPER YET. STAND OVER LEADS."
                                          : "STAND OVER MORE LEADS.";
        view.page = lettersPage_;
    }
    for (const std::int32_t index : unlocked) {
        const sim::Letter& letter = letterAt(index);
        // MAELL'S THREE SHARE ONE NAME, so the title list numbers them
        // against every OTHER letter tied to the same lead rather than
        // showing "FATHER MAELL" three times over with no way to tell
        // which press opens which. Combined indices (letterAt) -- lead ids
        // are per-file strings, so the count can never mix the two books.
        std::int32_t total = 0;
        std::int32_t position = 0;
        for (std::int32_t i = 0; i < letterCount(); ++i) {
            if (letterAt(i).lead == letter.lead) {
                ++total;
                if (i == index) {
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
    // COURIER CASE. The tile shows the live errand while one runs -- the
    // identical active-case rule the composed page keeps; see
    // casebookPageState.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    view.speaker = "THE CASEBOOK";
    view.epithet = std::string(raws.title());
    // THE ATTITUDE FIELD IS SHORT BY CONSTRUCTION -- in a conversation it
    // holds "WARM" or "HOSTILE" -- and the top-right of that band is where
    // the HUD draws the clock over it. The first S10 capture put a
    // twenty-nine character dread band there and the clock landed in the
    // middle of it. The ward's nerve moved down onto the line, where it is
    // the first thing you read in your own notes, which is also better.
    view.attitude = book.closed() ? "CLOSED" : "OPEN";
    const std::vector<std::int32_t> heard = book.known();
    // MERGE FIX: `book` is the active casebook now (courier lane); the legend
    // -- what the ward calls you -- takes its own name.
    const sim::Legend called = legend();
    if (caseEntry_ >= 0 && static_cast<std::size_t>(caseEntry_) < heard.size()) {
        const std::int32_t leadIndex = heard[static_cast<std::size_t>(caseEntry_)];
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(leadIndex)];
        const sim::LeadState what = book.state(leadIndex);
        view.line = what == sim::LeadState::Open ? lead.place + ". " + lead.what + "."
                                                  : lead.found + " " + lead.detail;
        // TASK #82. THE DATELINE, AND THE CROSS-REFERENCE -- what a
        // detective's log keeps that a bare topic list does not: when
        // this went in the book, what told you to come here, and (once
        // followed) what it put in the book next. See
        // DialogueViewState::caseRef's own header on why this is a
        // separate row rather than folded into `line`.
        std::string ref = formatCaseDay(book.heardAt(leadIndex));
        const std::vector<std::int32_t> from = raws.openedBy(leadIndex);
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
                const sim::Lead& opener = raws.leads()[static_cast<std::size_t>(from[i])];
                ref += opener.brief.empty() ? opener.place : opener.brief;
            }
        }
        if (what == sim::LeadState::Followed && !lead.opens.empty()) {
            ref += "  OPENED ";
            for (std::size_t i = 0; i < lead.opens.size(); ++i) {
                if (i > 0) {
                    ref += ", ";
                }
                const std::int32_t opened = raws.indexOf(lead.opens[i]);
                if (opened >= 0) {
                    const sim::Lead& next = raws.leads()[static_cast<std::size_t>(opened)];
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
            if (letterAt(li).lead == lead.id) {
                ref += "  L READS HIS LETTERS";
                break;
            }
        }
        view.caseRef = ref;
    } else if (book.readCount() == 0) {
        // THE OPENING PAGE OF A NEW GAME: the hook, and nothing else. It is
        // the first thing a player ever reads in this game and it gets the
        // band to itself.
        view.line = std::string(raws.hook());
    } else {
        // And afterwards: what the ward's nerve is doing, and what it calls
        // you for the work so far. TWO SHORT SENTENCES, because the band
        // wraps to three lines and the S10 capture that ran to four lost
        // "OF THE FLAME" off the end of its own title.
        view.line = std::string(raws.dreadLabel(book.dread())) + ". THEY CALL YOU " +
                    std::string(called.title()) + ".";
    }
    for (const std::int32_t index : heard) {
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(index)];
        std::string row;
        switch (book.state(index)) {
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
    // THE WAITING SENTENCE IS RETIRED HERE TOO (UI-EA-SPEC 1.5, prose 14->0,
    // and the two-surfaces-one-wording rule cuts both ways): the casebook
    // page stopped saying it, so the tile stops with it. The stippled field
    // still says "left on purpose"; the hook on a fresh book still leads.
    view.cursor = caseCursor_;
    view.page = casePage_;
    return view;
}

DialogueViewState Session::dialogueView() const {
    DialogueViewState view;
    // SHIP NOTE MOVE 3. Assembled fresh every frame, so every wording below
    // switches live the moment the other hand speaks. `confirm`/`back` are
    // the page grammar main.cpp's router hard-codes (ENTER/A, ESC/B); the
    // keyboard strings are character-for-character what this function always
    // printed, so a keyboard session draws byte-identical frames.
    const InputDevice dev = promptDevice_;
    const std::string confirm(promptConfirmKey(dev));
    const std::string back(promptBackKey(dev));
    view.confirmKey = confirm;
    view.backKey = back;
    // The haggle's third verb rides Action::PageNext in the router ("]"
    // carries no glyph in the 4x6 font; T is the advertised key and now
    // routed too -- see main.cpp's haggle branch); the pad's half is RB.
    if (dev == InputDevice::Pad) {
        view.takeKey = std::string(promptLabel(controls_, Action::PageNext, dev));
        // The pad has no L; its B BACK already names the way out of a letter.
        view.letterDownLine.clear();
    }
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
        view.epithet = confirm + " PASSES THE HOURS  " + back + " BACKS OUT";
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
        // The resume key is Pause's OWN binding -- ESC, or START, the key
        // that opened the page (this file's "the key that opened it closes
        // it" rule); the cancel on an armed QUIT is the universal back (B on
        // a pad -- see route_menu_key's parity-pass remap).
        view.epithet =
            quitArmed_ ? confirm + " QUITS  " + back + " CANCELS"
                       : confirm + " SELECTS  " +
                             std::string(promptLabel(controls_, Action::Pause, dev)) + " RESUMES";
        // NOT "PAUSED", DELIBERATELY. This page does not stop PhasedEngine --
        // nothing in this build does, not the casebook, not the keys page, not
        // options, and a menu that promised a freeze the game does not deliver
        // would be exactly the class of bug the copy bar exists to catch. Said
        // once, here, where a player opening this for the first time reads it.
        view.line = "THE DOCKS DO NOT WAIT ON YOU. SETTINGS KEEP THEMSELVES.";
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
        // F1 is a keyboard convenience key (main.cpp hard-codes it); a pad
        // player backed in through the pause menu and backs out with B.
        view.line = "THE DOCKS OF GRANADAD. THE DISTRICT KEEPS ITS OWN HOURS. " +
                    (dev == InputDevice::Pad ? back : std::string("F1")) + " PUTS THIS DOWN.";
        for (const std::string& row : keyRows()) {
            view.topics.push_back(row);
        }
        view.cursor = caseCursor_;
        view.page = casePage_;
        return view;
    }
    if (searchOpen()) {
        // KIT BUILD. The corpse's kit on the same widget: his name on the
        // badge, the verbs on the epithet, TAKE ALL as the last row.
        view.open = true;
        const sim::Actor* corpse = tavern_->actorById(searchActorId_);
        view.speaker = corpse != nullptr ? upperAscii(corpse->name()) : std::string("THE DEAD");
        view.epithet = confirm + " TAKES  " + back + " LEAVES HIM";
        const std::vector<std::string> rows = grimoireRows();
        view.line = rows.empty() ? "NOTHING ON HIM." : "WHAT HE HAD ON HIM. DR IS DRAMS.";
        for (const std::string& row : rows) {
            view.topics.push_back(row);
        }
        view.cursor = grimoireCursor_;
        view.page = grimoirePage_;
        return view;
    }
    if (grimoireOpen_) {
        // SPELLS BUILD. The same one list widget every page is -- see
        // toggleGrimoire()'s own header. The rows carry the difficulty out of
        // the cost model beside every name: what the linkcraft check will be
        // rolled against, which is information, never a discount.
        view.open = true;
        view.speaker = "GRIMOIRE";
        // LEFT RIGHT is honest on both devices -- the D-pad IS the pad's
        // left and right on a list; only the commit verb changes hands.
        view.epithet = "LEFT RIGHT BIND A SLOT  " + confirm + " READIES";
        const std::vector<std::string> rows = grimoireRows();
        if (rows.empty()) {
            // THE COMMON STATE, in the cast refusal's own words: the page and
            // the C key must name the same door or one of them is lying.
            view.line = "NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES.";
        } else {
            view.line =
                "EVERY CRAFTING THE HAND KNOWS. D IS WHAT THE LINK ASKS. A SLOT PUTS "
                "IT ON THE NUMBER ROW.";
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
        DialogueViewState tile;
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                tile = characterPanelView();
                break;
            case kMenuFocusMap:
                tile = mapPanelView();
                break;
            case kMenuFocusLetters:
                tile = lettersPanelView();
                break;
            case kMenuFocusJournal:
            default:
                tile = journalPanelView();
                break;
        }
        // SHIP NOTE MOVE 3: the tile views build their own state, so the
        // device wording set at the top of this function has to be restated
        // on the one that is actually returned -- an open letter's foot in
        // particular (backKey/letterDownLine).
        tile.confirmKey = confirm;
        tile.backKey = back;
        if (dev == InputDevice::Pad) {
            tile.takeKey = std::string(promptLabel(controls_, Action::PageNext, dev));
            tile.letterDownLine.clear();
        }
        return tile;
    }
    if (optionsOpen_) {
        view.open = true;
        view.speaker = "OPTIONS";
        // The awaiting line stays "ESC CANCELS" on BOTH devices, deliberately:
        // the capture is raw (main.cpp's bindAwaited eats the next key ahead
        // of every remap), so a pad's B would be CAPTURED as the binding, and
        // ESC really is the one cancel there is.
        view.epithet = awaitingKey_ ? "PRESS A KEY  (ESC CANCELS)"
                                    : "LEFT RIGHT CHANGE  " + confirm + " REBIND";
        view.line = "MOUSE LOOK IS RAW -- NO SMOOTHING, NO ACCELERATION. A KEY YOU BIND IS TAKEN "
                    "OFF WHATEVER HAD IT. " +
                    (dev == InputDevice::Pad ? back : std::string("F2")) + " PUTS THIS DOWN.";
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

CreationPage Session::stripCard() const {
    // UI-EA-SPEC 1.7 (LANE PAGES, strip->card): the pause stack's four strips
    // -- pause, wait, options, grimoire -- drawn as ONE composed card through
    // the same generic composition every creation step already uses
    // (drawCreationPage): framed, seated, measured, a numbered master list
    // with an inverted-fill cursor. The SHIP NOTE's standing item ("the
    // options page onto the master/detail card"), extended to the family.
    //
    // INPUT IS UNTOUCHED: rows, cursor and the nine-key digit windows keep
    // wrapCursorAndPage's own arithmetic, so every press lands where it
    // always did -- only the drawing changed register. The digits print on
    // the current nine-key window and on nothing else, the same honesty the
    // tiles and the casebook keep.
    CreationPage out;
    out.alpha = 1.0F;
    out.hasDetail = false;
    out.shape = CreationListShape::Columns;
    out.maxColumns = 1;
    out.stipple = false;
    const InputDevice dev = promptDevice_;
    const std::string back(promptBackKey(dev));

    std::vector<std::string> rows;
    int cursor = 0;
    int window = 0;
    if (pauseOpen_) {
        // THE BUILD STAMP RIDES THE PAUSE TITLE (UI-EA-SPEC sec. 2) -- the
        // HUD's own corner is quiet now, and the one page a player opens to
        // stand still is where a version belongs.
        out.title = "GRANADAD " + std::string(sim::build_info().version);
        rows = pauseRows();
        cursor = pauseCursor_;
        out.bodyHoldRows = static_cast<int>(rows.size());
    } else if (waitOpen_) {
        out.title = waitSleep_ ? "SLEEP" : "WAIT";
        // The flavour is four words (spec #38, 15 -> 4) -- or the live
        // refusal, which outranks it and is register, not chrome. Sleep keeps
        // its one honest clause: it is the only wait that mends.
        const std::string refusal = waitSleep_ ? std::string() : waitRefusal();
        if (!refusal.empty()) {
            out.instruction = refusal;
        } else {
            out.instruction = waitSleep_ ? "SLEEP MENDS. PICK THE HOUR." : "TIME PASSES. HEAT COOLS.";
        }
        rows = waitRows();
        cursor = waitCursor_;
        window = waitPage_;
        out.bodyHoldRows = 12;
    } else if (optionsOpen_) {
        out.title = "OPTIONS";
        // Instruction 22 -> 4 (spec #37): the value hint, or the capture
        // state's own modal line, which is load-bearing while the game is
        // listening for a raw key.
        out.instruction = awaitingKey_ ? "PRESS A KEY. ESC CANCELS." : "BINDS TRADE KEYS.";
        rows = optionRows();
        cursor = optionCursor_;
        window = optionPage_;
        out.bodyHoldRows = 14;
    } else if (searchOpen()) {
        // KIT BUILD: the corpse's kit as the composed card.
        const sim::Actor* corpse = tavern_->actorById(searchActorId_);
        out.title = corpse != nullptr ? upperAscii(corpse->name()) : std::string("THE DEAD");
        rows = grimoireRows();
        out.instruction = rows.empty() ? "NOTHING ON HIM." : "DR - DRAMS.";
        cursor = grimoireCursor_;
        window = grimoirePage_;
    } else if (grimoireOpen_) {
        out.title = "GRIMOIRE";
        rows = grimoireRows();
        // Instruction 19 -> 2: the one column that needs naming. The empty
        // state is the cast refusal's OWN words -- the page and the C key
        // must name the same door or one of them is lying (test_tavern pins
        // the line), so the register literal outranks the six-word rule here.
        out.instruction = rows.empty() ? "NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES."
                                       : "D - THE ASK.";
        cursor = grimoireCursor_;
        window = grimoirePage_;
        out.bodyHoldRows = 8;
    }

    // THE CARD PAGES AT TWELVE (UI-EA-SPEC 1.7: the wait clock over two
    // pages, the options table likewise) -- a screenful anchored on the
    // cursor, `+N` as the last, unselectable row saying what waits past the
    // fold, and the digit keys printed only on the nine-key window the
    // direct-select arithmetic really answers to.
    constexpr int kCardRows = 12;
    const int total = static_cast<int>(rows.size());
    const int screen = total > kCardRows ? std::clamp(cursor, 0, total - 1) / kCardRows : 0;
    const int first = screen * kCardRows;
    const int last = std::min(total, first + kCardRows);
    out.rows.reserve(static_cast<std::size_t>(last - first) + 1);
    const int windowFirst = window * kTopicPageSize;
    // The wait rows number THEMSELVES (`7 - DAWN 06:00`), so a printed key
    // column would say every digit twice -- digits pick what they print, and
    // those rows already print them.
    const bool selfNumbered = waitOpen_;
    for (int i = first; i < last; ++i) {
        CreationPageRow row;
        const int slot = i - windowFirst;
        if (!selfNumbered && slot >= 0 && slot < kTopicPageSize) {
            row.key = std::to_string(slot + 1);
        }
        row.label = rows[static_cast<std::size_t>(i)];
        row.accent = panelInk().accent;
        out.rows.push_back(std::move(row));
    }
    if (last < total) {
        CreationPageRow more;
        more.label = "+" + std::to_string(total - last);
        more.accent = panelInk().dim;
        more.selectable = false;
        out.rows.push_back(std::move(more));
    }
    out.cursor = std::clamp(cursor, 0, std::max(0, total - 1)) - first;
    // The one keycap foot every page keeps at rest; the word rides the tutor
    // tier, which the pause stack leaves at rest -- these pages ARE their
    // rows.
    PanelOption backFoot;
    backFoot.key = back;
    backFoot.label = "BACK";
    backFoot.valueInk = InkRole::Dim;
    backFoot.selectable = false;
    out.nav.push_back(std::move(backFoot));
    return out;
}

/// A landed blow of this many hit points or fewer is a GRAZE -- the light
/// audio band. Fists tap for 3-5 at the base sheet, so this catches the
/// glancing end of a bare-handed swing and lets a solid or hard blow land
/// medium instead. Splits the plan's single PunchMedium by band (section 5,
/// channel 4); presentation-only, no sim number depends on it.
namespace {
constexpr std::int32_t kGrazeDamageBand = 4;
/// BARKS LANE: the gain of the graze that stands in for a found-body whiff
/// (attackUp). Quieter than any landed blow, so the ear ranks it below one.
constexpr float kFoundBodyWhiffGain = 0.35F;
}  // namespace

void Session::attackDown() {
    if (refusedInCustody()) {
        return;
    }
    // The down-edge. A Punch watch-op is recorded here (case_watch replays it
    // as punch()); dismissOverlays and the sim's hold clock start exactly where
    // the legacy punch() started them.
    recordWatchOp(WatchOpKind::Punch);
    const WatchDepthGuard watchGuard(watchDepth_);
    dismissOverlays();
    tavern_->playerAttackDown();
}

void Session::attackUp() {
    if (refusedInCustody()) {
        return;
    }
    const WatchDepthGuard watchGuard(watchDepth_);
    const sim::Tavern::PlayerSwingResult result = tavern_->playerAttackUp();
    if (result.refused) {
        // A hard swing the wind would not buy. A winded TAP still swings (its
        // fatigue whiff band is the penalty), so a refusal is only ever the
        // hard tier saying so, once, on the alert row.
        say("TOO WINDED TO SWING HARD.");
        return;
    }
    if (!result.swung) {
        // A release with no charge behind it -- an edge that arrived in
        // recovery or idle. It throws nothing and says nothing.
        return;
    }
    // A committed HARD swing dips the camera forward on release -- render-only,
    // composed in camera() and never written to sim yaw.
    if (result.hard) {
        hardSwingDipPulse_.trigger();
    }
    // 3D BUILD, V LANE. The hands throw the swing the room just resolved,
    // on the next step, at the tier the room read off the hold.
    viewmodelSwingPending_ = static_cast<std::uint8_t>(result.hard ? 2 : 1);
    // THE LOT PASS: the HARD release has its own air (whoosh_strong / swing
    // sword) under whatever it finds -- a body or nothing; a tap at nothing
    // is the plain whoosh set. Spoken before the band below, so the ear gets
    // swing-then-impact in that order on a hard hit.
    if (audio_ != nullptr && result.hard) {
        audio_->playOneShot(audio::SoundId::WhooshHard);
    }
    if (result.targetId < 0) {
        // Committed and paid, but the crosshair passed through nobody. The arm
        // still swung, so the wind is spent and the swing gets its air.
        say("NOBODY IN REACH.");
        if (audio_ != nullptr) {
            if (!result.hard) {
                audio_->playOneShot(audio::SoundId::Whoosh);
            }
            // A blow thrown keeps the combat music alive; it starts nothing.
            audio_->music().noteSwing();
        }
        return;
    }
    // SAY-ROW DIET (section 5, channel 3). The per-blow "HIT X FOR N."/"MISSED
    // X." log is RETIRED -- a landed, taken or blocked blow speaks through the
    // washes, the reticle and the audio below, not the alert row. The row keeps
    // only EVENTS and REFUSALS: a kill, a crowning, a down. The steel-out flip
    // and its SwordDraw are spoken once on the escalation EDGE in step(), not
    // per swing, so a fight that stays lethal does not re-announce itself.
    if (result.killed) {
        // A killing blow under lethal rules -- the register literal, veto-blessed
        // (section 5). Names the man the boards just took.
        say(result.targetName + " DIES ON THE BOARDS.");
    } else if (result.blow.crowned) {
        // THE EVICTOR'S OWN LINE. crowned is downed by construction and never
        // kills (it was forged to put a man OUT), so it reads before the plain
        // fall and only this weapon ever throws one.
        say("CAUGHT " + result.targetName + " ACROSS THE CROWN. OUT COLD.");
    } else if (result.blow.downed) {
        say(result.targetName + " GOES DOWN.");
    }
    // AUDIO WIRING (section 5, channel 4). One sound per swing, by blow band:
    // the helmet on a crowning, the heavy on a kill or a knockdown, the light
    // graze on the low damage band and the medium above it (the plan's single
    // PunchMedium split), and the whoosh for a swing that found a body but
    // glanced off nothing worth a mark (a fully absorbed hit). SwordDraw is the
    // Lethal FLIP, wired on the escalation edge in step(), not here.
    if (audio_ != nullptr) {
        // THE LOT PASS re-cut the bands over the Malbers set: hit_grave for
        // the blow that ends a man (a crowning or a kill), the heavy band
        // plus fall_land (ThudHeavy) for a body going down, the clash for a
        // guard that caught it, the medium band for a blow that landed.
        if (result.blow.crowned || result.killed) {
            audio_->playOneShot(audio::SoundId::HitGrave);
        } else if (result.blow.downed) {
            audio_->playOneShot(audio::SoundId::PunchHeavy);
            audio_->playOneShot(audio::SoundId::ThudHeavy);
        } else if (result.blow.blocked) {
            audio_->playOneShot(audio::SoundId::SwordClash);
        } else if (result.blow.landed) {
            audio_->playOneShot(result.blow.damage <= kGrazeDamageBand
                                    ? audio::SoundId::GrazeLight
                                    : audio::SoundId::PunchMedium);
        } else {
            // BARKS LANE (feel/build) -- THE HONEST WHIFF. The reticle
            // promised a body on the line and the die (1-in-8, plus the
            // fatigue band -- kept, the owner's ruling) said the arm found
            // nothing worth a mark. That is NOT the same sound as swinging at
            // air (Whoosh, the targetId < 0 branch above): the fist brushed a
            // coat. The light graze at a third of its gain and no wash --
            // presentation only, zero law contact.
            audio_->playOneShot(audio::SoundId::GrazeLight, kFoundBodyWhiffGain);
        }
        // THE LOT PASS: a blow LANDED is the fight's first edge for the
        // music; a whiff on a body only keeps a running fight alive.
        if (result.blow.landed) {
            audio_->music().noteCombat();
        } else {
            audio_->music().noteSwing();
        }
    }
    // A LANDED SWING FINALLY HAS SOME WEIGHT -- the connecting wash. A whiff on
    // a body that was on the line stays silent on the wash, exactly as a miss
    // always did: this is punctuation for a hit, not for the swing.
    if (result.blow.landed) {
        punchLandedPulse_.trigger();
    }
}

void Session::punch() {
    if (refusedInCustody()) {
        return;
    }
    // ACTION-COMBAT BUILD: a TAP of the new verbs -- down then immediately up
    // with no movement step between, so the sim's charge counter never leaves
    // zero and the swing is always the light (Subdue) tier. The six un-migrated
    // call sites (the scripted drives, case_watch, the render suite) drive real
    // sightline combat through this, the same path a mouse press will.
    attackDown();
    attackUp();
}

void Session::castEquipped() {
    if (refusedInCustody()) {
        return;
    }
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
    const std::int64_t coolingBefore = tavern_->castCooldownLeft();
    const sim::Tavern::CastResult result = tavern_->playerCastEquipped();
    say(result.line);
    // 3D BUILD, V LANE. The hand reaches out when the room let it TRY: a
    // link that opened, or one that slipped (a fizzle still starts the
    // recovery clock -- the comparison-not-flag shape lastPlayerHp_ uses,
    // so no sim field is added for a gesture). A refusal moves nothing.
    if (result.cast || tavern_->castCooldownLeft() > coolingBefore) {
        viewmodelCastPending_ = true;
    }
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
    if (refusedInCustody()) {
        return;
    }
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
    recordWatchOp(WatchOpKind::SkipHour, hour);
    const WatchDepthGuard watchGuard(watchDepth_);
    const int wrapped = ((hour % 24) + 24) % 24;
    tavern_->skipTo(wrapped * 3600);
    syncClockAfterSkip();
}

void Session::skipSeconds(int seconds) {
    // FAST TRAVEL (TRAVEL lane). skipToHour's own two calls with the truncation
    // taken out: Tavern::skipTo already measures the jump forward round the
    // clock face and cools heat on every second of it, and syncClockAfterSkip()
    // runs the ward's calendar and the population to the new moment. Nothing
    // here is a second time system -- a travel IS a wait, plus a relocation the
    // caller makes separately.
    if (seconds <= 0) {
        return;
    }
    tavern_->skipTo((timeOfDay_ + seconds) % sim::kSecondsPerDay);
    syncClockAfterSkip();
}

const sim::Tavern::SentenceReport& Session::serveSentence() {
    // JUSTICE BUILD (SENTENCES LANE). A sentence is a clock jump with a
    // record behind it, and it rides the identical pair every other jump
    // does: the room's own verb, then syncClockAfterSkip() -- the ward's
    // roll to dayNumber() (a day in a cell is a day the land grows, the wage
    // is paid and, on a quarter, the ground penny falls) and the people to
    // the clock. The room fires the arrest's release for the body; step()
    // reads it exactly as it reads the release after the arrest. THE ROPE
    // jumps nothing and releases nothing, so nothing is synced: the clock is
    // where it was, and the end is on tavern().runEnd().
    const sim::Tavern::SentenceReport& served = tavern_->serveSentence();
    if (served.served && !served.terms.rope) {
        syncClockAfterSkip();
    }
    return served;
}

// ---------------------------------------------------------------------------
// JUSTICE BUILD (HEARING PAGE LANE): the court, the rope, the end
// ---------------------------------------------------------------------------
//
// Spec sections 5 and 6, as presentation over the SIM the two lanes before
// this one built: the hearing on the crime ledger (justice.hpp's HearingState,
// hashed and codec'd), Tavern::plead as the stepped input, Tavern::serveSentence
// as the clock and the coin, tavern().runEnd() as the end. Nothing here rolls,
// nothing here decides; this file reads the record, draws it in the terminal
// register, and turns keypresses into the two stepped inputs the sim owns.

namespace {

/// The count words the reading uses: "TWO LIFTS", "THREE SAW IT". One is the
/// article ("A LIFT"), so it is the caller's; past twelve the figure is the
/// word, as the census's own tallies are.
[[nodiscard]] std::string countWord(std::int32_t n) {
    static constexpr std::string_view kWords[] = {
        "",     "ONE", "TWO",   "THREE", "FOUR",  "FIVE",   "SIX",
        "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN", "TWELVE",
    };
    if (n >= 1 && n <= 12) {
        return std::string(kWords[n]);
    }
    return std::to_string(n);
}

/// "THE FOURTH DAY" -- the ordinal the plate reads. Day one, not day zero:
/// formatCaseDay's own translation, applied to the ordinal here. Past what
/// the words carry the plate says the figure, which is still the ward's roll.
[[nodiscard]] std::string ordinalWord(std::int32_t day) {
    static constexpr std::string_view kOnes[] = {
        "",        "FIRST",   "SECOND",     "THIRD",      "FOURTH",    "FIFTH",     "SIXTH",
        "SEVENTH", "EIGHTH",  "NINTH",      "TENTH",      "ELEVENTH",  "TWELFTH",   "THIRTEENTH",
        "FOURTEENTH", "FIFTEENTH", "SIXTEENTH", "SEVENTEENTH", "EIGHTEENTH", "NINETEENTH",
    };
    static constexpr std::string_view kTens[] = {
        "", "", "TWENTY", "THIRTY", "FORTY", "FIFTY", "SIXTY", "SEVENTY", "EIGHTY", "NINETY",
    };
    static constexpr std::string_view kTensOrdinal[] = {
        "",         "",          "TWENTIETH", "THIRTIETH", "FORTIETH",
        "FIFTIETH", "SIXTIETH",  "SEVENTIETH", "EIGHTIETH", "NINETIETH",
    };
    if (day >= 1 && day < 20) {
        return std::string(kOnes[day]);
    }
    if (day >= 20 && day < 100) {
        const int tens = day / 10;
        const int ones = day % 10;
        if (ones == 0) {
            return std::string(kTensOrdinal[tens]);
        }
        return std::string(kTens[tens]) + "-" + std::string(kOnes[ones]);
    }
    return std::to_string(day) + "TH";
}

/// The six acts, in the reading's own words: "A LIFT" / "TWO LIFTS", "A
/// CRACKED BOX" / "TWO CRACKED BOXES". The tally words crime.hpp's own
/// crimeTally uses (lifts, cracks, runs, fences, leans, roofs), spoken.
[[nodiscard]] std::string crimePhrase(sim::Crime crime, std::int32_t count) {
    const bool one = count == 1;
    const std::string n = one ? std::string("A") : countWord(count);
    switch (crime) {
        case sim::Crime::Lift:
            return n + (one ? " LIFT" : " LIFTS");
        case sim::Crime::Burgle:
            return n + (one ? " CRACKED BOX" : " CRACKED BOXES");
        case sim::Crime::Smuggle:
            return n + (one ? " RUN" : " RUNS");
        case sim::Crime::Fence:
            return n + (one ? " FENCED LOT" : " FENCED LOTS");
        case sim::Crime::Extort:
            return n + (one ? " LEAN" : " LEANS");
        case sim::Crime::RoofRun:
            return n + (one ? " ROOF-RUN" : " ROOF-RUNS");
    }
    return n;
}

/// Which rows the page prints, by kind, so the cursor and the digits and the
/// confirm all read one list.
enum class CourtRowKind : std::uint8_t { Guilty, NotGuilty, Paper, Back, NoPlea, Serve };

[[nodiscard]] std::vector<CourtRowKind> courtRowKindsFor(const sim::HearingState& hearing,
                                                          bool paperOpen, bool offered) {
    std::vector<CourtRowKind> kinds;
    if (hearing.awaitingPlea()) {
        if (hearing.sheet.tier == sim::Sentence::Condemned && hearing.sheet.commutedBefore) {
            // MERCY IS GIVEN ONCE. The page opens, the priest speaks, and
            // the one row is I HAVE NOTHING TO SAY.
            kinds.push_back(CourtRowKind::NoPlea);
            return kinds;
        }
        kinds.push_back(CourtRowKind::Guilty);
        kinds.push_back(CourtRowKind::NotGuilty);
        kinds.push_back(CourtRowKind::Paper);
        if (paperOpen) {
            kinds.push_back(CourtRowKind::Back);
        }
        return kinds;
    }
    if (hearing.judged() && offered) {
        kinds.push_back(CourtRowKind::Serve);
    }
    return kinds;
}

/// The judgment as the badge prints it.
[[nodiscard]] std::string judgmentWord(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return "SPARED";
        case sim::Judgment::Fined:
            return "FINED";
        case sim::Judgment::Held:
            return "HELD";
        case sim::Judgment::Bound:
            return "BOUND";
        case sim::Judgment::TheHand:
            return "THE HAND";
        case sim::Judgment::Commuted:
            return "COMMUTED";
        case sim::Judgment::TheRope:
            return "THE ROPE";
        case sim::Judgment::None:
            break;
    }
    return {};
}

/// The verdict badge's own accent: SPARED in the number green, the coin and
/// the cell in the page accent, the hand and the rope in the tag's red.
[[nodiscard]] Rgb judgmentAccent(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return panelInk().number;
        case sim::Judgment::TheHand:
        case sim::Judgment::Commuted:
        case sim::Judgment::TheRope:
            return Rgb{0.88F, 0.34F, 0.26F};
        default:
            return panelInk().accent;
    }
}

/// The one row after the judgment: what the player does about it. The drop
/// is taken by the player's own hand, like everything else on this page.
[[nodiscard]] std::string serveWord(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return "WALK OUT.";
        case sim::Judgment::Fined:
            return "PAY IT.";
        case sim::Judgment::TheRope:
            return "THE DROP.";
        default:
            return "SERVE IT.";
    }
}

/// "TWO NIGHTS." / "SIX NIGHTS." off the cell's hours -- whole nights, one at
/// least: the shipped one-to-three nights doubled reads as its days.
[[nodiscard]] std::string nightsWord(std::int32_t cellHours) {
    const std::int32_t nights = std::max<std::int32_t>(1, cellHours / 24);
    return countWord(nights) + (nights == 1 ? " NIGHT." : " NIGHTS.");
}

[[nodiscard]] std::string daysWord(std::int32_t days) {
    return "BONDSWORN " + std::to_string(days) + (days == 1 ? " DAY." : " DAYS.");
}

/// THE SENTENCE, IN NUMBERS -- the last row of the check block, the way
/// "HELD HARD -- CUDGEL 14-18" states its span. Off the pure branch.
[[nodiscard]] std::string sentenceLine(const sim::SentenceTerms& terms) {
    if (!terms.served) {
        return {};
    }
    std::string out;
    const auto coin = [&terms]() { return std::to_string(terms.finePaid) + " ROYALS."; };
    switch (terms.judgment) {
        case sim::Judgment::Spared:
            return "NOTHING OWED.";
        case sim::Judgment::Fined:
            out = coin();
            break;
        case sim::Judgment::Held:
            out = nightsWord(terms.cellHours) + " " + coin();
            break;
        case sim::Judgment::Bound:
            return daysWord(terms.bondDays);
        case sim::Judgment::TheHand:
            out = "THE HAND. ";
            if (terms.band == sim::Judgment::Bound) {
                return out + daysWord(terms.bondDays);
            }
            out += nightsWord(terms.cellHours) + " " + coin();
            break;
        case sim::Judgment::Commuted:
            return "THE HAND. " + daysWord(terms.bondDays);
        case sim::Judgment::TheRope:
            // Nothing in numbers here: the rope's row is the post and the
            // hour, which the page composes off the clock (hearingPageState)
            // -- the badge already says THE ROPE and the row does not echo it.
            return {};
        case sim::Judgment::None:
            return {};
    }
    if (terms.shortfallDays > 0) {
        out += " " + daysWord(terms.shortfallDays);
    }
    return out;
}

/// The priest's own words for the answer -- the bark tables keyed like the
/// shipped watch.*, rotated on the count of hearings so a second visit to the
/// bench hears a second row.
[[nodiscard]] std::string_view judgmentTable(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return "court.spared";
        case sim::Judgment::Fined:
            return "court.fined";
        case sim::Judgment::Held:
            return "court.held";
        case sim::Judgment::Bound:
            return "court.bound";
        case sim::Judgment::TheHand:
            return "court.hand";
        case sim::Judgment::Commuted:
            return "court.commuted";
        case sim::Judgment::TheRope:
            return "court.rope";
        case sim::Judgment::None:
            break;
    }
    return "court.paper";
}

/// The consequence of the hovered row, in the pane beside it -- the
/// informed choice (UI-REFERENCE: "View X's stats"). Fixed literals.
[[nodiscard]] std::string rowConsequence(CourtRowKind kind, sim::Sentence tier) {
    // EACH TIER'S LITERAL SAYS ONLY WHAT IS ON THE TABLE: the rope tier has
    // two answers and names them; a cell or a hand charge has no rope on the
    // table and does not mention one. A confession is never doubled and never
    // spared on any tier; a denial can be either, on the tiers that have
    // either.
    const bool rope = tier == sim::Sentence::Condemned;
    switch (kind) {
        case CourtRowKind::Guilty:
            return rope ? "A CONFESSION IS WEIGHED AS IT IS GIVEN. THE FLAME'S ANSWER IS FIXED "
                          "BEFORE YOU SPEAK IT. MERCY OR THE ROPE, AND NOTHING ELSE."
                        : "A CONFESSION IS WEIGHED AS IT IS GIVEN. THE FLAME'S ANSWER IS FIXED "
                          "BEFORE YOU SPEAK IT. NEVER DOUBLED, AND NEVER SPARED.";
        case CourtRowKind::NotGuilty:
            return rope ? "A DENIAL IS WEIGHED WITH THE PRIEST'S OWN DOUBT IN IT. TEN POINTS "
                          "EITHER WAY. MERCY OR THE ROPE, AND NEVER SPARED."
                        : "A DENIAL IS WEIGHED WITH THE PRIEST'S OWN DOUBT IN IT. TEN POINTS "
                          "EITHER WAY. DENIED AND DISBELIEVED, THE SENTENCE DOUBLES AND THE "
                          "MISSION REMEMBERS THE LIE.";
        case CourtRowKind::Paper:
            return "THE CHARGE, WHAT THE PAPER ASKS, AND WHAT THE PRIEST WILL WEIGH -- IN "
                   "WORDS, BEFORE YOU SPEAK.";
        case CourtRowKind::Back:
            return "BACK TO THE ROWS.";
        case CourtRowKind::NoPlea:
            return "MERCY WAS GIVEN ONCE. NOTHING IS WEIGHED: THE PRIEST SPEAKS, AND THE WARD "
                   "HAS YOU.";
        case CourtRowKind::Serve:
            break;
    }
    return {};
}

/// THE SHEET AS PHRASES, NEVER NUMBERS (spec 3.2): what the priest will
/// weigh, in words, before the plea. The arithmetic is shown after.
[[nodiscard]] std::vector<std::string> paperPhrases(const sim::ChargeSheet& sheet) {
    std::vector<std::string> out;
    if (sheet.blood) {
        out.push_back("BLOOD ON THE PAPER.");
        if (sheet.witnesses > 0) {
            out.push_back(countWord(sheet.witnesses) + " SAW IT.");
        }
    }
    if (sheet.secondRung) {
        out.push_back("THE ROOFS, A SECOND TIME.");
    } else if (sheet.skyrunner && sheet.tier == sim::Sentence::Maimed) {
        out.push_back("A SKYRUNNER'S FIRST.");
    }
    if (sheet.priors <= 0) {
        out.push_back("NEVER TAKEN BEFORE.");
    } else if (sheet.priors == 1) {
        out.push_back("TAKEN ONCE BEFORE.");
    } else if (sheet.priors == 2) {
        out.push_back("TAKEN TWICE BEFORE.");
    } else {
        out.push_back("TAKEN " + countWord(sheet.priors) + " TIMES BEFORE.");
    }
    if (sheet.heatAtArrest - sim::kWarrantAt >= 16) {
        out.push_back("THE PAPER IS HOT.");
    }
    if (sheet.templeStanding >= 52) {
        out.push_back("THE MISSION KNOWS YOU WELL.");
    } else if (sheet.templeStanding > 0) {
        out.push_back("THE MISSION KNOWS YOUR NAME.");
    } else {
        out.push_back("THE MISSION DOES NOT KNOW YOU.");
    }
    if (sheet.reputation >= 10) {
        out.push_back("THE WARD THINKS WELL OF YOU.");
    } else if (sheet.reputation <= -10) {
        out.push_back("THE WARD THINKS ILL OF YOU.");
    }
    if (sheet.streetwise >= 20) {
        out.push_back("A QUICK TONGUE.");
    } else if (sheet.streetwise <= 0) {
        out.push_back("NO TONGUE TO SPEAK OF.");
    }
    if (sheet.condemnedBefore) {
        out.push_back("THE ROPE PASSED YOU ONCE.");
    }
    if (sheet.commutedBefore) {
        out.push_back("MERCY WAS GIVEN ONCE.");
    }
    return out;
}

}  // namespace

bool Session::courtSentenceOffered() const noexcept {
    return courtOpen_ && tavern_->hearing().judged() && courtJudgmentHold_ <= 0;
}

std::vector<std::string> Session::courtRows() const {
    std::vector<std::string> out;
    const HearingPageState page = hearingPageState();
    out.reserve(page.rows.size());
    for (const HearingRow& row : page.rows) {
        out.push_back(row.key + " - " + row.label);
    }
    return out;
}

bool Session::missionArrivalTile(std::int32_t& outX, std::int32_t& outY,
                                 std::int32_t& outBand) const {
    // THE MISSION IS ALREADY A TRAVEL TARGET with an arrival tile: the sign's
    // own aim point (the door you knock on) snapped to standable ground on
    // the place's band -- exactly the travel verb's landing rule, so the
    // Watch's walk lands where a walked-to Mission lands.
    const int at = mapPlaceIndex("Mission of the Flame");
    if (at < 0) {
        return false;
    }
    const MapPlace& place = mapPlaces()[static_cast<std::size_t>(at)];
    std::int32_t aimX = 0;
    std::int32_t aimY = 0;
    mapAimPoint(place, body_->tileX(), body_->tileY(), aimX, aimY);
    if (!travelStandable(*tiles_, aimX, aimY, place.band, &outX, &outY)) {
        return false;
    }
    outBand = place.band;
    return true;
}

void Session::openCourt() {
    if (courtOpen_) {
        return;
    }
    // CUSTODY. Whatever the player was doing, they are not doing it: the wire
    // out of the lock, the conversation ended, every page and the pause menu
    // down. The TAKEN line follows and outranks any of theirs.
    if (picking()) {
        tavern_->abandonPick();
    }
    if (talking()) {
        tavern_->endConversation();
        wardTalkId_ = -1;
        topicCursor_ = 0;
        topicPage_ = 0;
        haggleOffer_ = 0;
        forgeOpen_ = false;
    }
    dismissOverlays();
    // THE WALK ELIDED. The body to the Mission's arrival tile through
    // placeBodyAt (a relocation like any other: the room you were taken in
    // must not ghost over the Mission's door), the seam dipped to black --
    // the same cloth every scripted cut wears -- and the arrival line.
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    if (missionArrivalTile(x, y, band)) {
        placeBodyAt(x, y, band);
    }
    awaitingLanding_ = false;
    syncTavernToBody();
    dressInstantCut();
    courtOpen_ = true;
    courtCursor_ = 0;
    courtPaperOpen_ = false;
    courtArmed_ = -1;
    courtServing_ = false;
    // A hearing already judged (a run reopened between the plea and the
    // sentence) holds its badge the same beat a fresh judgment does.
    courtJudgmentHold_ = tavern_->hearing().judged() ? sim::kJudgmentHoldSteps : 0;
    say("TAKEN TO THE MISSION. " + travelClockText(timeOfDay_) + ".");
    syncPanelAnim();
}

void Session::closeCourt() {
    courtOpen_ = false;
    courtServing_ = false;
    courtArmed_ = -1;
    courtPaperOpen_ = false;
    courtJudgmentHold_ = 0;
    courtCursor_ = 0;
    // TURNED LOOSE. Where the sentence left the body: SPARED and FINED walk
    // out of the Mission's door where they stand; everything that cost a
    // cell ends on the shipped Tarwalk, exactly as the paperless search
    // does. The line prints the day and the clock face the sentence ended
    // on -- day one, not day zero, formatCaseDay's own translation.
    const sim::Tavern::SentenceReport& served = tavern_->lastServed();
    const std::string when = "DAY " + std::to_string(served.dayReleased + 1) + ". " +
                             travelClockText(served.timeReleased) + ".";
    if (served.served && served.terms.releaseHere) {
        say("TURNED LOOSE AT THE MISSION'S DOOR. " + when);
    } else {
        placeBodyAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
        awaitingLanding_ = false;
        syncTavernToBody();
        dressInstantCut();
        say("TURNED LOOSE ON THE TARWALK. " + when);
    }
    syncPanelAnim();
}

HearingPageState Session::hearingPageState() const {
    HearingPageState out;
    out.open = courtOpen_;
    if (!courtOpen_) {
        return out;
    }
    const sim::HearingState& hearing = tavern_->hearing();
    const sim::ChargeSheet& sheet = hearing.sheet;
    const sim::CrimeLedger& crimes = tavern_->dialogue().crimes();
    const sim::BarkTables& barks = tavern_->dialogue().barks();
    const InputDevice dev = promptDevice_;
    out.confirmKey = std::string(promptConfirmKey(dev));
    out.backKey = std::string(promptBackKey(dev));
    out.tutor = courtTutor_.value();
    out.commitPulse = commitPulse_.value();
    // THE READOUT: the day and the live clock (UI-EA-SPEC sec. 4 #10), the
    // two facts a man at the bench wants -- how long he has been in the
    // ward, and what hour the sentence starts from.
    out.readout = "DAY " + std::to_string(tavern_->dayNumber() + 1) + "  " +
                  travelClockText(timeOfDay_);

    // --- the reading ------------------------------------------------------
    out.laid = (hearing.officer.empty() ? std::string("THE WATCH") : upperAscii(hearing.officer)) +
               " LAYS THE PAPER ON THE TABLE.";
    if (sheet.blood) {
        const std::string slain = tavern_->slainName();
        out.charge = "THE WARD SAYS YOU PUT " + (slain.empty() ? std::string("A MAN") : upperAscii(slain)) +
                     " DOWN IN THE GILDED GULL.";
        if (sheet.witnesses > 0) {
            out.charge += " " + countWord(sheet.witnesses) + " SAW IT.";
        }
    } else {
        // Everything new since the bench last heard you, in the ledger's own
        // order -- "TWO LIFTS AND A CRACKED BOX" reads the way the sheet is
        // written; the worst line is what the paper ASKS off, not a sorting.
        std::vector<std::string> parts;
        for (std::size_t c = 0; c < sim::kSheetCrimes; ++c) {
            if (sheet.since[c] <= 0) {
                continue;
            }
            parts.push_back(crimePhrase(static_cast<sim::Crime>(c), sheet.since[c]));
        }
        if (parts.empty()) {
            out.charge = sheet.secondRung ? std::string("THE WARD HAS YOU FOR THE ROOFS, A SECOND TIME.")
                                          : std::string("THE WARD HAS PAPER ON YOU.");
        } else {
            std::string list;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    list += (i + 1 == parts.size()) ? " AND " : ", ";
                }
                list += parts[i];
            }
            out.charge = "THE WARD HAS YOU FOR " + list + ".";
        }
    }
    switch (sheet.tier) {
        case sim::Sentence::Maimed:
            out.asks = "THE PAPER ASKS FOR THE HAND.";
            break;
        case sim::Sentence::Condemned:
            out.asks = "THE PAPER ASKS FOR THE ROPE.";
            break;
        default:
            out.asks = "THE PAPER ASKS FOR A CELL.";
            break;
    }

    // THE OFFICER WHO WALKED YOU IN, and his line as he lays the paper
    // (court.taken), off the record's own officer and rotated with the
    // priest's opening -- one hearing, one rotation, both voices. He stands
    // by the wall UNTIL THE ANSWER: his walking-in line is said as the paper
    // is laid, and a judged page has nothing of his on it -- the bench has
    // spoken, and "sit where he points" beside a sentence is a line from
    // before the sentence.
    if (!hearing.officer.empty() && hearing.awaitingPlea()) {
        out.officerName = upperAscii(hearing.officer);
        out.officerSays = std::string(barks.line("court.taken", crimes.hearings()));
    }

    // --- the rows ---------------------------------------------------------
    const bool offered = courtSentenceOffered();
    const std::vector<CourtRowKind> kinds = courtRowKindsFor(hearing, courtPaperOpen_, offered);
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        HearingRow row;
        switch (kinds[i]) {
            case CourtRowKind::Guilty:
                row.key = "1";
                row.label = "I DID IT.";
                break;
            case CourtRowKind::NotGuilty:
                row.key = "2";
                row.label = "I DID NOT.";
                break;
            case CourtRowKind::Paper:
                row.key = "3";
                row.label = "HEAR THE PAPER";
                break;
            case CourtRowKind::Back:
                row.key = "0";
                row.label = "BACK";
                row.accent = panelInk().dim;
                break;
            case CourtRowKind::NoPlea:
                row.key = "1";
                row.label = "I HAVE NOTHING TO SAY.";
                break;
            case CourtRowKind::Serve:
                row.key = "1";
                row.label = serveWord(hearing.judgment);
                row.accent = judgmentAccent(hearing.judgment);
                break;
        }
        // THE ARMED ROW NAMES THE DEVICE'S OWN CONFIRM -- the QUIT row's
        // exact shape: "1 - I DID IT. -- SURE? ENTER".
        if (static_cast<int>(i) == courtArmed_) {
            row.label += " -- SURE? " + out.confirmKey;
        }
        out.rows.push_back(std::move(row));
    }
    out.cursor = kinds.empty() ? 0 : std::clamp(courtCursor_, 0, static_cast<int>(kinds.size()) - 1);
    out.armed = courtArmed_;

    // --- the detail pane ----------------------------------------------------
    // ROTATED ON THE HEARINGS BEFORE THIS ONE: plead() counts the hearing
    // the moment the plea lands, so the verdict's row reads the count back
    // by one -- the opening and the verdict of one hearing rotate together,
    // and the first hearing hears row zero of both.
    const std::int32_t rotation =
        hearing.judged() ? std::max<std::int32_t>(0, crimes.hearings() - 1) : crimes.hearings();
    const auto bark = [&barks, rotation](std::string_view key) {
        return std::string(barks.line(key, rotation));
    };
    if (hearing.awaitingPlea()) {
        out.view = courtPaperOpen_ ? HearingView::Paper : HearingView::Plea;
        // THE PRIEST OPENS, BY THE CASE, and the rotation runs only within
        // the case: every row of the table chosen is true of the sheet in
        // front of him. A commuted man back for the rope; a killing; a
        // Skyrunner's second rung (the rope with no corpse) or his first (the
        // hand); and a thief -- one the Mission knows from its own door, or
        // one it has never seen (the sheet's own THE DOOR term, and HEAR THE
        // PAPER's own phrase, decide which).
        std::string_view opening = sheet.templeStanding > 0 ? "court.paper.door" : "court.paper";
        if (sheet.tier == sim::Sentence::Condemned && sheet.commutedBefore) {
            opening = "court.nothing";
        } else if (sheet.blood) {
            opening = "court.blood";
        } else if (sheet.skyrunner && sheet.secondRung) {
            opening = "court.roofs.rope";
        } else if (sheet.skyrunner && sheet.tier == sim::Sentence::Maimed) {
            opening = "court.roofs.hand";
        }
        out.priest = bark(opening);
        // THE PRIEST PRESSES once a plea is armed (court.plead): the "-- SURE?"
        // beat in his own mouth, the question the row is about to answer.
        // A commuted man's one row is not a plea and the opening stands.
        if (courtArmed_ >= 0 && courtArmed_ < static_cast<int>(kinds.size())) {
            const CourtRowKind armedKind = kinds[static_cast<std::size_t>(courtArmed_)];
            if (armedKind == CourtRowKind::Guilty || armedKind == CourtRowKind::NotGuilty) {
                out.priest = bark("court.plead");
            }
        }
        if (!kinds.empty()) {
            out.consequence = rowConsequence(kinds[static_cast<std::size_t>(out.cursor)], sheet.tier);
        }
        out.paper = paperPhrases(sheet);
        out.backVerb = courtPaperOpen_ ? "THE ROWS" : (courtArmed_ >= 0 ? "DISARM" : "");
        return out;
    }
    if (hearing.judged()) {
        out.view = HearingView::Judged;
        // THE CHECK BLOCK, RE-DERIVED from the hashed record: the weighing is
        // a pure function of the sheet and the plea (weighArraignment), so
        // the page prints the same lines the sim scored, whenever it is
        // drawn, without the sim keeping a term list.
        // THE SUM READS "MAKES" -- the 4x6 font carries no `=` and the font
        // is not touched (the brief's own line); the ward's arithmetic is
        // spoken, not typeset. The plea's own term (CONFESSED / THE PRIEST IS
        // A MAN) rides the terms list the sim returns and is printed on its
        // OWN line under the record's sum, the reference's threshold-then-
        // verdict order.
        // EACH TERM IS ONE TOKEN with its sign glued on ("- 20 THE WARD"),
        // and the sum is a token of its own ("MAKES 27"): the pane packs the
        // tokens by row and never splits one, so a sign cannot wrap away
        // from its term at any width. The joined string is the same record
        // for the summary and the suite.
        const sim::Arraignment answer = sim::weighArraignment(sheet, hearing.plea);
        std::vector<std::string> terms;
        for (const sim::ArraignmentTerm& term : answer.terms) {
            if (term.name == sim::kTermConfessed || term.name == sim::kTermPriestIsAMan) {
                continue;
            }
            const std::string name = term.name == sim::kTermSawIt
                                         ? countWord(term.count) + " " + std::string(term.name)
                                         : std::string(term.name);
            if (terms.empty()) {
                terms.push_back(std::to_string(term.value) + " " + name);
            } else {
                terms.push_back(std::string(term.value >= 0 ? "+ " : "- ") +
                                std::to_string(std::abs(term.value)) + " " + name);
            }
        }
        if (!terms.empty()) {
            terms.push_back("MAKES " + std::to_string(answer.weight));
        }
        const auto joined = [](const std::vector<std::string>& tokens) {
            std::string out;
            for (const std::string& token : tokens) {
                if (!out.empty()) {
                    out += ' ';
                }
                out += token;
            }
            return out;
        };
        out.arithmeticTerms = terms;
        out.arithmetic = joined(terms);
        std::vector<std::string> pleaTerms;
        switch (answer.plea) {
            case sim::Plea::Guilty:
                pleaTerms.push_back("+ " + std::to_string(answer.pleaTerm) + " " +
                                    std::string(sim::kTermConfessed));
                pleaTerms.push_back("MAKES " + std::to_string(answer.scored));
                break;
            case sim::Plea::NotGuilty:
                pleaTerms.push_back(std::string(answer.pleaTerm >= 0 ? "+ " : "- ") +
                                    std::to_string(std::abs(answer.pleaTerm)) + " " +
                                    std::string(sim::kTermPriestIsAMan));
                pleaTerms.push_back("MAKES " + std::to_string(answer.scored));
                break;
            default:
                break;
        }
        out.pleaTerms = pleaTerms;
        out.pleaTerm = joined(pleaTerms);
        // THE LINES THE SCORE WAS READ AGAINST -- and only those. SPARED is a
        // denial's line and no other plea's (paperBand): a confession's
        // block does not print a line it could never have crossed. The rope
        // tier has the one line. Nothing weighed, nothing printed.
        if (answer.plea == sim::Plea::Guilty || answer.plea == sim::Plea::NotGuilty) {
            if (sheet.tier == sim::Sentence::Condemned) {
                out.lines = "THE LINE: " + std::to_string(sim::kMercyLine) + " MERCY";
            } else {
                // Each tier's own ladder (justice.cpp's substitution): on the
                // PAPER tier 38 buys the fine and 14 the cell; on THE HAND
                // tier 38 buys HELD with the hand spared and 14 THE HAND
                // with nights -- there is no FINED on it, so the row does
                // not print one.
                const bool handTier = sheet.tier == sim::Sentence::Maimed;
                out.lines = "THE LINES: ";
                if (answer.plea == sim::Plea::NotGuilty) {
                    out.lines += std::to_string(sim::kSparedLine) + " SPARED  ";
                }
                out.lines += std::to_string(sim::kFinedLine) + (handTier ? " HELD  " : " FINED  ") +
                             std::to_string(sim::kHeldLine) + (handTier ? " THE HAND" : " HELD");
            }
        }
        out.verdict = judgmentWord(hearing.judgment);
        out.verdictAccent = judgmentAccent(hearing.judgment);
        out.sentence = sentenceLine(sim::sentenceTerms(hearing, tavern_->playerCoin()));
        if (hearing.judgment == sim::Judgment::TheRope) {
            // THE ROPE'S ROW: where and when, the plate's own two facts --
            // the post the ward hangs a man at and the hour on the clock --
            // in the place the other judgments state their nights and coin.
            out.sentence = std::string(sim::kRopePlace) + ". " + travelClockText(timeOfDay_) + ".";
        }
        out.priest = hearing.doubled ? bark("court.lie") : bark(judgmentTable(hearing.judgment));
        out.backVerb.clear();
        return out;
    }
    return out;
}

void Session::moveCourtCursor(int delta) {
    if (!courtOpen_) {
        return;
    }
    // MOVING THE CURSOR DISARMS THE PLEA -- movePauseCursor's own rule for
    // QUIT: a player who backed off the row plainly changed their mind.
    courtArmed_ = -1;
    const std::vector<CourtRowKind> kinds =
        courtRowKindsFor(tavern_->hearing(), courtPaperOpen_, courtSentenceOffered());
    const int count = static_cast<int>(kinds.size());
    if (count <= 0) {
        return;
    }
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    courtCursor_ = ((std::clamp(courtCursor_, 0, count - 1) + delta) % count + count) % count;
}

void Session::chooseCourtVisibleRow(int slot) {
    if (!courtOpen_) {
        return;
    }
    const std::vector<CourtRowKind> kinds =
        courtRowKindsFor(tavern_->hearing(), courtPaperOpen_, courtSentenceOffered());
    if (slot < 0) {
        // THE ZERO KEY: the digit BACK prints while the paper is open, and
        // nothing otherwise.
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            if (kinds[i] == CourtRowKind::Back) {
                courtCursor_ = static_cast<int>(i);
                chooseCourtRow();
                return;
            }
        }
        return;
    }
    // THE PRINTED DIGIT PICKS THE ROW IT PRINTS. BACK prints 0, never a
    // slot digit, so the slots address the rows above it only.
    int printed = 0;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        if (kinds[i] == CourtRowKind::Back) {
            continue;
        }
        if (printed == slot) {
            // A digit on a row other than the armed one disarms it -- the
            // pause menu's own "picking a different row calls it off".
            if (courtArmed_ >= 0 && courtArmed_ != static_cast<int>(i)) {
                courtArmed_ = -1;
            }
            courtCursor_ = static_cast<int>(i);
            chooseCourtRow();
            return;
        }
        ++printed;
    }
}

void Session::chooseCourtRow() {
    if (!courtOpen_) {
        return;
    }
    const sim::HearingState& hearing = tavern_->hearing();
    const std::vector<CourtRowKind> kinds =
        courtRowKindsFor(hearing, courtPaperOpen_, courtSentenceOffered());
    if (kinds.empty()) {
        return;
    }
    const int at = std::clamp(courtCursor_, 0, static_cast<int>(kinds.size()) - 1);
    courtCursor_ = at;
    // AUDIO: the press is what is acknowledged, arms and refusals included.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    switch (kinds[static_cast<std::size_t>(at)]) {
        case CourtRowKind::Paper:
            courtArmed_ = -1;
            courtPaperOpen_ = true;
            return;
        case CourtRowKind::Back:
            courtArmed_ = -1;
            courtPaperOpen_ = false;
            // Back to the row that opened the paper.
            courtCursor_ = 2;
            return;
        case CourtRowKind::Guilty:
        case CourtRowKind::NotGuilty:
        case CourtRowKind::NoPlea: {
            // ARMED ON THE FIRST PRESS, CONFIRMED ON THE SECOND -- the QUIT
            // pattern, so a leaned-on ENTER cannot plead.
            if (courtArmed_ != at) {
                courtArmed_ = at;
                return;
            }
            courtArmed_ = -1;
            const CourtRowKind kind = kinds[static_cast<std::size_t>(at)];
            const sim::Plea plea = kind == CourtRowKind::Guilty      ? sim::Plea::Guilty
                                   : kind == CourtRowKind::NotGuilty ? sim::Plea::NotGuilty
                                                                     : sim::Plea::NoPlea;
            // THE PLEA: the sim's own stepped input. Recorded on the watch
            // tape like every other verb the page owns.
            const sim::Arraignment answer = tavern_->plead(plea);
            courtPaperOpen_ = false;
            courtCursor_ = 0;
            if (answer.heard) {
                // The badge lands with the commit beat and is HELD before the
                // row that serves it is offered.
                commitPulse_.trigger();
                courtJudgmentHold_ = sim::kJudgmentHoldSteps;
            }
            return;
        }
        case CourtRowKind::Serve: {
            // THE SENTENCE, SERVED THROUGH THE WAIT MACHINERY (Session::
            // serveSentence): the coin, the clock, the mirror, the record,
            // and the arrest's own release -- which the next step() reads and
            // closes the page on. THE ROPE serves no clock and fires no
            // release: the page comes down here and the ceremony goes up.
            commitPulse_.trigger();
            courtServing_ = true;
            const sim::Tavern::SentenceReport& served = serveSentence();
            if (!served.served) {
                courtServing_ = false;
                return;
            }
            if (served.terms.rope) {
                courtServing_ = false;
                courtOpen_ = false;
                courtArmed_ = -1;
                courtPaperOpen_ = false;
                courtJudgmentHold_ = 0;
                armRopeCeremony();
                syncPanelAnim();
            }
            return;
        }
    }
}

void Session::courtBack() {
    if (!courtOpen_) {
        return;
    }
    // THE GRAMMAR EXCEPTION: ESC disarms an armed plea, closes the paper,
    // and does nothing else. There is no leaving the bench.
    if (courtArmed_ >= 0) {
        courtArmed_ = -1;
        return;
    }
    if (courtPaperOpen_) {
        courtPaperOpen_ = false;
        courtCursor_ = 2;
    }
}

bool Session::routeCourtKey(Key key) {
    if (!inCustody() || key == Key::None) {
        return false;
    }
    if (awaitingKey_) {
        return false;
    }
    const Action action = controls_.actionFor(key);
    const bool up = key == Key::Up || key == Key::PadUp || action == Action::Forward ||
                    action == Action::QuickPrev;
    const bool downward = key == Key::Down || key == Key::PadDown || action == Action::Back ||
                          action == Action::QuickNext;
    const bool confirm = key == Key::Enter || key == Key::PadSouth || action == Action::Interact;
    const bool back = key == Key::Escape || key == Key::PadEast;
    const int slotBase = static_cast<int>(Action::QuickSlot1);
    const int slot = static_cast<int>(action) - slotBase;
    const bool numbered = slot >= 0 && slot < 9;
    const bool zero = action == Action::QuickSlot0;
    if (ropeRowsUp_) {
        // THE END ROWS: the only live input. ESC disarms an armed row and
        // does nothing else on the plate, PAUSE is refused, and nothing
        // reaches the world.
        if (up) {
            moveRopeCursor(-1);
        } else if (downward) {
            moveRopeCursor(1);
        } else if (confirm) {
            chooseRopeRow();
        } else if (numbered) {
            chooseRopeVisibleRow(slot);
        } else if (back) {
            ropeArmed_ = -1;
        }
        return true;
    }
    if (ropeCeremonySteps_ > 0) {
        // THE PLATE: no input accepted.
        return true;
    }
    if (takenHold_ > 0) {
        // THE ARREST BEAT: the officer's hand is on you and the page is not
        // up yet. Every key is swallowed but Pause (a player can always
        // quit the game), which falls through to the client as it does
        // from the page.
        return !(action == Action::Pause || key == Key::Escape);
    }
    // THE PAGE. PAUSE falls through to the client, which opens the pause
    // menu over the court (a player can always quit the game); ESC is Pause's
    // own key, so it backs the page out of the paper or an armed plea FIRST
    // and only reaches the pause menu when there is nothing to back out of.
    if (back && (courtPaperOpen_ || courtArmed_ >= 0)) {
        courtBack();
        return true;
    }
    if (action == Action::Pause || key == Key::Escape) {
        return false;
    }
    if (up) {
        moveCourtCursor(-1);
    } else if (downward) {
        moveCourtCursor(1);
    } else if (confirm) {
        chooseCourtRow();
    } else if (numbered) {
        chooseCourtVisibleRow(slot);
    } else if (zero) {
        chooseCourtVisibleRow(-1);
    }
    // EVERY OTHER KEY IS SWALLOWED: no world verb reaches the body from the
    // bench, and no page opens over the court but the pause menu.
    return true;
}

// --- the rope -----------------------------------------------------------------

namespace {
/// The dip before the plate: one page-ease, the death ceremony's own. The
/// HOLD is the sim's number (kDeathHoldSteps); there is no fade-out.
constexpr std::int32_t kRopeDipInSteps = kPageEaseSteps;
}  // namespace

void Session::armRopeCeremony() {
    // From the run's end, written by the room at the drop off hashed state:
    // the place the ward hangs a man, the ward as the killer, the reason --
    // the corpse's roster name, or THE SECOND RUNG -- and the dateline. The
    // end's grammar, distinct from the revive's on purpose (spec 5): "HANGED
    // AT ... / BY THE WARD. FOR ..." can never be confused with "PUT DOWN IN
    // ... / BY <killer>. BY <weapon>." on screen.
    const sim::RunEnd& end = tavern_->runEnd();
    ropePlateTop_ = "HANGED AT " + (end.place.empty() ? std::string(sim::kRopePlace) : end.place) + ".";
    ropePlateMid_ = "BY THE WARD. FOR " +
                    (end.reason.empty() ? std::string(sim::kRopeForSecondRung) : upperAscii(end.reason)) +
                    ".";
    ropePlateFoot_ = "THE " + ordinalWord(end.day + 1) + " DAY. " + travelClockText(end.secondOfDay) + ".";
    ropeCeremonySteps_ = kRopeDipInSteps + sim::kDeathHoldSteps;
    ropeRowsUp_ = false;
    ropeCursor_ = 0;
    ropeArmed_ = -1;
    // SILENCE. No one-shot: ThudHeavy is "every path to the floor" and this
    // is not the floor. The drop is silent by ruling.
}

namespace {
/// The two end rows, bare. No save row: no save exists, so there is nothing
/// to load and the run is genuinely over. The day a save lands, "1 - THE
/// LAST SAVE" goes first.
constexpr std::string_view kRopeRowLabels[] = {"1 - A NEW MAN", "2 - LEAVE"};
}  // namespace

std::vector<std::string> Session::ropeRows() const {
    // THE ARMED ROW NAMES THE DEVICE'S OWN CONFIRM on its tail -- the QUIT
    // row's exact shape, and the plea rows' -- so the state of the row is
    // in the row.
    std::vector<std::string> out;
    for (std::size_t i = 0; i < std::size(kRopeRowLabels); ++i) {
        std::string row(kRopeRowLabels[i]);
        if (static_cast<int>(i) == ropeArmed_) {
            row += " -- SURE? " + std::string(promptConfirmKey(promptDevice_));
        }
        out.push_back(std::move(row));
    }
    return out;
}

void Session::moveRopeCursor(int delta) {
    if (!ropeRowsUp_) {
        return;
    }
    // Moving the cursor disarms the row -- the pause card's own rule for
    // QUIT: a player who backed off the row plainly changed their mind.
    ropeArmed_ = -1;
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    const int count = static_cast<int>(ropeRows().size());
    ropeCursor_ = ((ropeCursor_ + delta) % count + count) % count;
}

void Session::chooseRopeVisibleRow(int slot) {
    if (!ropeRowsUp_ || slot < 0 || slot >= static_cast<int>(ropeRows().size())) {
        return;
    }
    // A digit on a row other than the armed one disarms it -- the pause
    // menu's own "picking a different row calls it off".
    if (ropeArmed_ >= 0 && ropeArmed_ != slot) {
        ropeArmed_ = -1;
    }
    ropeCursor_ = slot;
    chooseRopeRow();
}

void Session::chooseRopeRow() {
    if (!ropeRowsUp_ || runEnded_) {
        return;
    }
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    // ARMED ON THE FIRST PRESS, CONFIRMED ON THE SECOND -- the QUIT pattern
    // on the two rows that end a run, so a leaned-on ENTER after the plate's
    // hold cannot start a new man or leave the game. The armed row keeps its
    // tail once taken: the run is over and the row that ended it stays lit.
    if (ropeArmed_ != ropeCursor_) {
        ropeArmed_ = ropeCursor_;
        return;
    }
    runEnded_ = true;
    if (ropeCursor_ == 0) {
        // A NEW MAN: a fresh run through the boot path. main.cpp's loop
        // reads this beside quitRequested and goes back to the creation
        // window; there is no save to erase.
        runEndReason_ = RunEndChoice::NewMan;
    } else {
        // LEAVE: the shipped quit, the one exit that has always existed.
        runEndReason_ = RunEndChoice::Leave;
        quitRequested_ = true;
    }
}

void Session::composeRopeCeremony(Framebuffer& target) const {
    if (ropeCeremonySteps_ <= 0 && !ropeRowsUp_) {
        return;
    }
    constexpr std::int32_t kTotal = kRopeDipInSteps + sim::kDeathHoldSteps;
    // THE VEIL: ramps up over the dip and then HOLDS. There is no fade-out
    // phase; the frame under it is never seen again.
    float dip = 1.0F;
    if (ropeCeremonySteps_ > sim::kDeathHoldSteps) {
        const std::int32_t elapsed = kTotal - ropeCeremonySteps_;  // 0 .. kRopeDipInSteps
        dip = static_cast<float>(elapsed) / static_cast<float>(kRopeDipInSteps);
    }
    dip = dip < 0.0F ? 0.0F : (dip > 1.0F ? 1.0F : dip);
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F}, dip);
    // THE PLATE, centred, in the epitaph's own bone register: the place, the
    // ward and the reason; the dateline dim under them, drawRouteCard's foot
    // idiom. The black field IS its backing.
    const int scale = hudScale(target.height());
    const int glyphH = 6 * scale;
    const int lineStep = glyphH + 2 * scale;
    const int cx = target.width() / 2;
    const int topY = target.height() / 2 - 2 * lineStep;
    const Rgb bone{0.86F, 0.82F, 0.72F};
    const Rgb dim{0.56F, 0.53F, 0.46F};
    const auto centre = [&](const std::string& line, int y, const Rgb& ink) {
        if (line.empty()) {
            return;
        }
        const int w = textWidth(line, scale);
        drawText(target, cx - w / 2, y, line, ink, dip, scale);
    };
    centre(ropePlateTop_, topY, bone);
    centre(ropePlateMid_, topY + lineStep, bone);
    centre(ropePlateFoot_, topY + 2 * lineStep, dim);
    if (!ropeRowsUp_) {
        return;
    }
    // THE PENDING-DECISION ZONE RISES UNDER IT once the hold has run:
    // option-list idiom, inverted-fill selection, never an arrow. The rows
    // are drawn in the page grid's own cells so the fill is the same shape
    // the pause card's cursor wears.
    const PanelMetric metric = panelMetric(target.height());
    const std::vector<std::string> rows = ropeRows();
    // THE ZONE HOLDS THE ARMED WIDTH WHETHER OR NOT A ROW IS ARMED, so
    // arming one moves no row: the tail every row could grow is measured
    // for all of them.
    const std::string tail = " -- SURE? " + std::string(promptConfirmKey(promptDevice_));
    int widest = 0;
    for (const std::string_view row : kRopeRowLabels) {
        widest = std::max(widest, static_cast<int>(row.size() + tail.size()));
    }
    for (const std::string& row : rows) {
        widest = std::max(widest, static_cast<int>(row.size()));
    }
    const int rowsY = topY + 4 * lineStep;
    const PanelRect zone{cx - metric.widthOf(widest + 2) / 2, rowsY, metric.widthOf(widest + 2),
                         metric.heightOf(static_cast<int>(rows.size()))};
    const PanelInk& ink = panelInk();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int r = static_cast<int>(i);
        if (r == ropeCursor_) {
            drawInvertedFill(target, zone, metric, 0, r, widest + 2, ink.accent, 1.0F);
            drawCellTextKnockout(target, zone, metric, 1, r, rows[i], ink.knockout, 1.0F);
        } else {
            drawCellText(target, zone, metric, 1, r, rows[i], ink.prose, 1.0F);
        }
    }
}

void Session::composeTakenPlate(Framebuffer& target) const {
    if (!takenPlateUp()) {
        return;
    }
    // THE CUT: an instant black, the one line on it in the rope plate's own
    // register (bone, centred), at full alpha for the whole hold -- the
    // "TAKEN TO THE MISSION. 23:00." the spec promised, on the frame for as
    // long as it takes to read, and nothing under it (no page yet, no HUD
    // to hide it). The page opens over it when the hold runs out.
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F}, 1.0F);
    const int scale = hudScale(target.height());
    const Rgb bone{0.86F, 0.82F, 0.72F};
    const int w = textWidth(takenPlate_, scale);
    const int glyphH = 6 * scale;
    drawText(target, target.width() / 2 - w / 2, target.height() / 2 - glyphH / 2, takenPlate_,
             bone, 1.0F, scale);
}

// ---------------------------------------------------------------------------
// FAST TRAVEL: the cost of a walk, in the sim's own integers
// ---------------------------------------------------------------------------

std::int32_t travelRouteUnits(const sim::PathStep& from,
                              const std::vector<sim::PathStep>& route) noexcept {
    // The router's own octile currency, recomputed over the route it returned:
    // 10 per orthogonal step, 14 per diagonal (path_finder.hpp's kStepCost
    // pair, restated in test_travel.cpp's own pins). A walk-gait band change
    // -- a stair -- rides its step at no surcharge, exactly as PathFinder
    // charges it under Gait::Walk.
    std::int32_t units = 0;
    const sim::PathStep* at = &from;
    for (const sim::PathStep& step : route) {
        const std::int32_t dx = step.x > at->x ? step.x - at->x : at->x - step.x;
        const std::int32_t dy = step.y > at->y ? step.y - at->y : at->y - step.y;
        units += (dx != 0 && dy != 0) ? 14 : 10;
        at = &step;
    }
    return units;
}

std::int32_t travelWalkSeconds(std::int32_t units) noexcept {
    if (units <= 0) {
        return 0;
    }
    // One octile unit is a tenth of a tile: 256 Q8 / 10. The body walks
    // kWalkSpeed Q8 per movement step, kStepsPerSecond steps a second, so
    // seconds = units * 256 / (10 * kWalkSpeed * kStepsPerSecond), rounded UP
    // -- a walk is never free and never rounds itself shorter.
    const std::int64_t num = static_cast<std::int64_t>(units) * 256;
    const std::int64_t den =
        10LL * sim::kWalkSpeed * sim::kStepsPerSecond;
    return static_cast<std::int32_t>((num + den - 1) / den);
}

std::int32_t travelClockMinutes(std::int32_t seconds) noexcept {
    // NEVER FREE (the owner's ruling, item 1): the floor is one whole minute,
    // and the verb's restated minutes are exactly the minutes delivered.
    if (seconds <= 60) {
        return 1;
    }
    return (seconds + 59) / 60;
}

std::string travelCostLabel(std::int32_t minutes) {
    if (minutes >= 60) {
        return "ABOUT AN HOUR";
    }
    return std::to_string(minutes) + " MIN";
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
    // ACTION-COMBAT BUILD (section 5, channel 5). THE IMPACT IMPULSE, composed
    // at the one Camera::fromBody site as a BAM offset on RENDER pitch and never
    // on sim yaw -- a render-only courtesy the Camera contract permits here
    // (the field is a float, the sim never reads it back). Peaks are capped in
    // BAM well under the spec's angles: a hard-release forward dip ~2deg, a
    // blow-taken jolt ~1.5deg, a caught-blow nudge ~1deg. Every pulse decays to
    // zero within kPageEaseSteps, so the offset is exactly 0 at rest -- every
    // settled capture is byte-identical and --settle-steps photographs no
    // impulse. Pitch is radians, positive up: a hard release nods the eye DOWN,
    // a blow taken kicks it up, a caught blow nudges it up a touch less.
    constexpr std::int32_t kHardSwingDipBam = 364;  // ~2.0 deg
    constexpr std::int32_t kTakenJoltBam = 273;     // ~1.5 deg
    constexpr std::int32_t kBlockNudgeBam = 182;    // ~1.0 deg
    const std::int32_t dip =
        static_cast<std::int32_t>(std::lround(hardSwingDipPulse_.value() * kHardSwingDipBam));
    const std::int32_t taken =
        static_cast<std::int32_t>(std::lround(punchTakenPulse_.value() * kTakenJoltBam));
    const std::int32_t nudge =
        static_cast<std::int32_t>(std::lround(blockPulse_.value() * kBlockNudgeBam));
    const std::int32_t pitchBam = body_->pitch() - dip + taken + nudge;
    return Camera::fromBody(body_->x(), body_->y(), body_->eyeZ(), body_->yaw(), pitchBam,
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
    // COURIER CASE. The orientation row follows the live errand -- the same
    // one rule every case surface keeps (sheetCaseLive), so the corner names
    // the Gull while the sheet is the work and the Bloodletter before and
    // after.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    if (!book.active() || !raws.loaded()) {
        return {};
    }
    const std::vector<std::int32_t> heard = book.known();
    std::string line = "CASE " + std::to_string(book.readCount()) + "/" +
                       std::to_string(heard.size());
    // WHERE TO GO NEXT, ON THE SAME ROW. This is the orientation line: a player
    // who put the game down for a week and came back to a district of 692
    // people gets one line telling them where they were walking. Until this
    // sprint the corner of a new game was empty, which is exactly the "dropped
    // into a systems demo with no orientation" the demo brief names.
    const std::int32_t lead = book.nextOpen();
    if (lead >= 0 && static_cast<std::size_t>(lead) < raws.leads().size()) {
        line += " > " + raws.leads()[static_cast<std::size_t>(lead)].place;
    } else if (book.closed()) {
        line += " > " + std::string(raws.close());
    } else {
        const std::string_view mood = raws.dreadLabel(book.dread());
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
    // +4, not +3: room for the IN HAND row the armed sheet adds at the end.
    rows.reserve(sim::kLegendTracks + 4 + 5 + 4);
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
    // IN HAND -- the reference sheet's w slot ("cane 1-6 Impact"). KIT
    // BUILD (D10): ALWAYS printed now, because fists are a real state once
    // an equipment model exists -- the sheet says what the hand holds, the
    // item by name when a Kit row is worn there, the class line when a
    // sheet armed the hand directly, FISTS 3-5 IMPACT otherwise.
    if (const sim::ItemDef* held = tavern_->heldItem(); held != nullptr) {
        rows.push_back("IN HAND  " + sim::itemSheetLine(*held));
    } else {
        rows.push_back("IN HAND  " + sim::weaponSheetLine(tavern_->playerWeapon()));
    }
    // THE WORN SLOTS, printed ONLY for a slot the raws hold at least one
    // item for (D10's own rule against furniture): the thing worn there by
    // name, or NOTHING -- the reference's "no trinket" wording.
    const sim::ItemRegistry& items = tavern_->items();
    for (std::int32_t slotIndex = 0; slotIndex < static_cast<std::int32_t>(sim::kWornSlotCount);
         ++slotIndex) {
        const sim::ItemSlot slot = sim::wornSlotAt(slotIndex);
        if (slot == sim::ItemSlot::Hand || !items.slotHasItems(slot)) {
            continue;
        }
        const sim::ItemDef* worn = items.at(tavern_->kit().worn(slot));
        std::string line = std::string(sim::itemSlotSheetLabel(slot)) + "  ";
        if (worn != nullptr) {
            line += worn->name;
            if (worn->dr > 0) {
                line += "  DR " + std::to_string(worn->dr);
            }
        } else {
            line += "NOTHING";
        }
        rows.push_back(std::move(line));
    }
    // THE LOAD, the one visible budget, then every carried row.
    rows.push_back(loadLine());
    for (const KitRow& row : kitRows()) {
        const sim::ItemDef* thing = items.at(row.item);
        if (thing == nullptr) {
            continue;
        }
        std::string line;
        if (row.count > 1) {
            line += std::to_string(row.count) + " ";
        }
        line += thing->name;
        // Weight and worth, the reference's right-aligned numbers, in the
        // font's own letters (no icons): DR is drams, C is coin.
        const std::int32_t drams = row.item == tavern_->items().indexOf("bale")
                                       ? tavern_->dialogue().crimes().baleUnits() *
                                             sim::contrabandWeight(
                                                 tavern_->dialogue().crimes().baleGood())
                                       : thing->drams * row.count;
        line += "  " + std::to_string(drams) + "DR";
        if (thing->royals > 0) {
            line += "  " + std::to_string(thing->royals * row.count) + "C";
        }
        if (row.inKit && tavern_->kit().isWorn(row.item)) {
            line += thing->slot == sim::ItemSlot::Hand ? "  IN HAND" : "  WORN";
        }
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            if (tavern_->slotItemIndex(slot) == row.item) {
                line += "  SLOT " + std::to_string(slot + 1);
                break;
            }
        }
        if (thing->heat > 0) {
            line += "  HOT";
        }
        rows.push_back(std::move(line));
    }
    return rows;
}

// ---------------------------------------------------------------------------
// KIT BUILD: the Kit on the Character tile, and the search list
// ---------------------------------------------------------------------------

std::vector<Session::KitRow> Session::kitRows() const {
    // ONE LIST OUT OF THREE STORES, in the registry's document order: the
    // Kit's own count, or the sack's for a contraband row, or the bale, or
    // the picks. Rows with nothing on them are not rows.
    std::vector<KitRow> rows;
    const sim::ItemRegistry& items = tavern_->items();
    const sim::CrimeLedger& crimes = tavern_->dialogue().crimes();
    const std::int32_t picks = items.indexOf("picks");
    const std::int32_t bale = items.indexOf("bale");
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(items.size()); ++i) {
        const sim::ItemDef& thing = items.items()[static_cast<std::size_t>(i)];
        KitRow row;
        row.item = i;
        if (!thing.contraband.empty()) {
            sim::Contraband good = sim::Contraband::Scalp;
            if (sim::contrabandFromSymbol(thing.contraband, good)) {
                row.count = crimes.stash().count(good);
            }
        } else if (i == picks) {
            row.count = tavern_->picks();
        } else if (i == bale) {
            row.count = crimes.carryingBale() ? 1 : 0;
        } else {
            row.count = tavern_->kit().count(i);
            row.inKit = row.count > 0;
        }
        if (row.count > 0) {
            rows.push_back(row);
        }
    }
    return rows;
}

std::size_t Session::characterKitOffset() const {
    // The standings block (five tracks, four skills, five ladders, three
    // ward rows), IN HAND, one row per worn slot the raws populate, LOAD.
    std::size_t offset = sim::kLegendTracks + 4 + 5 + 3 + 1 + 1;
    for (std::int32_t slotIndex = 0; slotIndex < static_cast<std::int32_t>(sim::kWornSlotCount);
         ++slotIndex) {
        const sim::ItemSlot slot = sim::wornSlotAt(slotIndex);
        if (slot != sim::ItemSlot::Hand && tavern_->items().slotHasItems(slot)) {
            ++offset;
        }
    }
    return offset;
}

std::optional<Session::KitRow> Session::highlightedKitRow() const {
    const std::size_t offset = characterKitOffset();
    if (characterCursor_ < 0 || static_cast<std::size_t>(characterCursor_) < offset) {
        return std::nullopt;
    }
    const std::vector<KitRow> rows = kitRows();
    const std::size_t at = static_cast<std::size_t>(characterCursor_) - offset;
    if (at >= rows.size()) {
        return std::nullopt;
    }
    return rows[at];
}

std::string Session::loadLine() const {
    return "LOAD  " + std::to_string(tavern_->loadDrams()) + " / " +
           std::to_string(tavern_->loadBudget()) + " DRAMS";
}

void Session::wearHighlightedKitRow() {
    const std::optional<KitRow> row = highlightedKitRow();
    if (!row.has_value()) {
        return;
    }
    const sim::ItemDef* thing = tavern_->items().at(row->item);
    if (thing == nullptr) {
        return;
    }
    if (!row->inKit) {
        // The sack, the bale, the picks: not worn, and the room says so in
        // the words wearItem would.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiTick);
        }
        say("THE " + thing->name + " IS NOT WORN.");
        return;
    }
    const bool wasWorn = tavern_->kit().isWorn(row->item);
    const sim::Tavern::StealResult done = tavern_->wearItem(row->item);
    if (audio_ != nullptr) {
        if (done.result != sim::ServiceResult::Served) {
            audio_->playOneShot(audio::SoundId::UiTick);
        } else if (thing->slot == sim::ItemSlot::Hand) {
            // A blade leaves its sheath; a cudgel is picked up off the table.
            audio_->playOneShot(thing->weaponClass >= sim::kFirstLethalWeapon
                                    ? audio::SoundId::KnifeDraw
                                    : audio::SoundId::MetalClick);
        } else {
            audio_->playOneShot(audio::SoundId::ClothRustle);
        }
    }
    (void)wasWorn;
    say(done.line);
}

void Session::dropHighlightedKitRow() {
    const std::optional<KitRow> row = highlightedKitRow();
    if (!row.has_value()) {
        return;
    }
    syncTavernToBody();
    const sim::Tavern::StealResult done = tavern_->dropItem(row->item);
    if (audio_ != nullptr) {
        audio_->playOneShot(done.result == sim::ServiceResult::Served
                                ? audio::SoundId::ThudMedium
                                : audio::SoundId::UiTick);
    }
    say(done.line);
    // The cursor stays on the row; a row that emptied is gone from the
    // list, so keep the cursor inside what is left.
    const int count = static_cast<int>(characterRows().size());
    if (count > 0 && characterCursor_ >= count) {
        characterCursor_ = count - 1;
        characterPage_ = topicPageOf(characterCursor_);
    }
}

void Session::adjustKitSlot(int delta) {
    if (delta == 0) {
        return;
    }
    const std::optional<KitRow> row = highlightedKitRow();
    if (!row.has_value() || !row->inKit) {
        return;
    }
    const sim::ItemDef* thing = tavern_->items().at(row->item);
    if (thing == nullptr) {
        return;
    }
    if (thing->slot == sim::ItemSlot::None) {
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiTick);
        }
        say("THE " + thing->name + " IS NOT WORN. NO SLOT.");
        return;
    }
    // Which slot holds it now, or -1 -- the Grimoire's own cycle: NONE,
    // 1..10 and round. Moving it clears the slot it leaves.
    std::int32_t current = -1;
    for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
        if (tavern_->slotItemIndex(slot) == row->item) {
            current = slot;
            break;
        }
    }
    const std::int32_t positions = sim::Tavern::kQuickSlotCount + 1;
    const std::int32_t next = ((current + 1 + delta) % positions + positions) % positions - 1;
    if (current >= 0) {
        tavern_->clearSlot(current);
    }
    if (next >= 0) {
        tavern_->bindItemToSlot(next, row->item);
    }
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    say(next >= 0 ? thing->name + " -- SLOT " + std::to_string(next + 1) + "."
                  : thing->name + " -- NO SLOT.");
}

bool Session::takeNearestItem() {
    if (tavern_->groundItemInReach() < 0) {
        return false;
    }
    const sim::Tavern::StealResult took = tavern_->takeGroundItem();
    if (took.result == sim::ServiceResult::TooFar) {
        return false;
    }
    if (audio_ != nullptr) {
        // A pickup click; a refusal ticks. The sack's own CoinHandle stays
        // the purse's.
        audio_->playOneShot(took.result == sim::ServiceResult::Served
                                ? audio::SoundId::UiSelect
                                : audio::SoundId::UiTick);
    }
    say(took.line);
    return true;
}

bool Session::searchNearestCorpse() {
    if (refusedInCustody() || talking() || picking()) {
        return false;
    }
    syncTavernToBody();
    const sim::Actor* corpse = corpseToSearch();
    if (corpse == nullptr) {
        return false;
    }
    // The Grimoire widget, wearing the search list: every other overlay
    // stands down exactly as toggleGrimoire does, and the id says which
    // list it is.
    casebookOpen_ = false;
    keysOpen_ = false;
    waitOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    pauseOpen_ = false;
    quitArmed_ = false;
    awaitingKey_ = false;
    menuFocus_ = kMenuFocusJournal;
    grimoireOpen_ = true;
    searchActorId_ = corpse->id();
    grimoireCursor_ = 0;
    grimoirePage_ = 0;
    syncPanelAnim();
    return true;
}

std::vector<std::string> Session::mapRows() const {
    // THREE SECTIONS, EACH BUILT FROM WHAT THE CASEBOOK ALREADY KNOWS -- see
    // toggleMap's own header on why nothing here is new state. All three grow
    // by the identical act: hearing a lead. There is no separate "have you
    // been here" or "have you met them" flag to invent, forget to update, or
    // let drift from the trail itself.
    std::vector<std::string> rows;
    // COURIER CASE. The map's three sections follow the live errand, the one
    // active-case rule again -- see casebookPageState.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    if (!book.active()) {
        return rows;
    }
    const std::vector<std::int32_t> heard = book.known();
    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();

    // -- known ground: the named places a heard lead has put in the book,
    // once each, in the order they were first heard of. The casebook's own
    // `place` field, authored, never derived -- these are the district's
    // named sites and the street table (docks::kPlaces) only knows the
    // streets between them.
    std::vector<std::string_view> places;
    for (const std::int32_t index : heard) {
        const std::string_view place = raws.leads()[static_cast<std::size_t>(index)].place;
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
        if (book.state(index) != sim::LeadState::Open) {
            continue;
        }
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(index)];
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
        const std::string_view who = raws.leads()[static_cast<std::size_t>(index)].who;
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
        !crimes.maimed() && !crimes.condemned() && !sheetCarry_) {
        return {};
    }
    // THE ROW IS THIRTY-FOUR COLUMNS AND IT FITS THEM BY CONSTRUCTION: the
    // tag and the number are never clipped, and a rider (the loot, the
    // bale, the man on your shoulder) is appended only while the row still
    // holds it whole -- a rider that does not fit is dropped, never cut in
    // half, and the number is never lost to it.
    constexpr std::size_t kHeatColumns = 34;
    std::string line;
    // THE CRIMINAL TAG (JUSTICE BUILD, section 1.2). Three states the ledger
    // keeps, one row. The two the ward has done TO you outrank everything
    // else on the line: a condemned man wants to know he is one before he
    // wants his heat. CONDEMNED is the shipped word with the court's meaning
    // -- the rope passed and commuted, for the rest of the run. WANTED is
    // paper; WANTED FOR BLOOD is paper with a corpse behind it, which only
    // the bench clears. A murderer whose heat has cooled is not WANTED on
    // this row -- the paper lapsed -- but the next arrest on any paper is a
    // rope hearing, and the row does not pretend otherwise by inventing a
    // word for it: standing stays a phrase, heat stays a number. A commuted
    // man with fresh blood on him is WANTED FOR BLOOD and nothing else: the
    // two tags together do not fit the row, and of the two it is the blood
    // that decides what the next arrest is (mercy is given once).
    const bool blood = crimes.warrant() && crimes.murderer();
    if (crimes.condemned() && !blood) {
        line = "CONDEMNED  ";
    } else if (crimes.maimed()) {
        // "MAIMED  WANTED FOR BLOOD  HEAT 100" is the row's full width.
        line = "MAIMED  ";
    }
    if (crimes.warrant()) {
        line += blood ? "WANTED FOR BLOOD  " : "WANTED  ";
    }
    line += "HEAT " + std::to_string(crimes.heat());
    const auto rider = [&line, kHeatColumns](const std::string& tail) {
        if (line.size() + tail.size() <= kHeatColumns) {
            line += tail;
        }
    };
    if (crimes.loot() > 0) {
        rider("  LOOT " + std::to_string(crimes.loot()));
    }
    if (crimes.carryingBale()) {
        rider("  BALE");
    }
    // COURIER CASE. The man in hand rides the same row the bale does -- a
    // carried thing the ward would mind, worn on the HUD until the back room
    // takes him. The rendered over-the-shoulder body is flagged follow-up
    // work; this line is the honest interim. TRAVEL LANE RENAME, flagged for
    // the owner: he read "FINCH IN HAND" as unclear, so the row now says what
    // the body is doing rather than naming an idiom.
    if (sheetCarry_) {
        rider("  CARRYING FINCH");
    }
    return clip(std::move(line), kHeatColumns);
}

bool Session::conversingNow() const noexcept {
    // THE ONE FORMULA. Repeated in drawFrame() as `conversing` until this
    // pass, which is exactly the shape of drift that let the S7 review's
    // overprint findings happen: two places computing the same fact, and
    // nothing catching them when a seventh page joined the list and only one
    // of the two remembered to add it.
    // JUSTICE BUILD: the hearing page is a page like the others for the
    // panel ease and the HUD's stand-down -- and unlike them it is not in
    // dismissOverlays(), which is the whole of its exception.
    return talking() || casebookOpen_ || keysOpen_ || grimoireOpen_ || waitOpen_ ||
           districtMapOpen_ || optionsOpen_ || pauseOpen_ || courtOpen_;
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
    // THE CROSSHAIR PASS. THE AIM PROMPT'S FOUR PARTS, ON THE ONE TOGGLE.
    //
    // #85 composed a single "<key>  <label>" row here. The renderer now
    // colours the verb, the subject and the note by three different roles, so
    // Session hands over three strings and a kind and joins nothing -- a
    // pre-joined row would have to be taken apart again in hud.cpp, which is
    // the second-description-of-a-layout defect the panel pass banned.
    //
    // ONE EasedToggle STILL, and interactAnim_ is still it: the four parts
    // appear and disappear together because they are one element. The subject
    // and the note ride interactCache_'s own visibility so a name cannot
    // outlive the verb it belongs to mid-fade.
    const InteractTarget aim = interactTarget();
    const bool aimVisible = !conversing && !aim.verb.empty();
    if (aimVisible) {
        interactCache_ = aim.verb;
        interactSubjectCache_ = aim.subject;
        interactNoteCache_ = aim.note;
        interactKindCache_ = aim.kind;
    }
    interactAnim_.setTarget(aimVisible);
    sync(lockAnim_, lockCache_, lockLine());
    sync(rivalAnim_, rivalCache_, rivalLine());
    sync(stealthAnim_, stealthCache_, tavern_->playerInside() ? stealthLine() : std::string());

    // UI-EA (LANE HUD): THE LAW OF EARNED TEXT. Text prints when it CHANGES,
    // when it is aimed at, or when the player hesitates -- never merely
    // because it is true. The four reference rows below (case, room, guild,
    // objective) and the clock/purse pair used to be furniture; each is an
    // EVENT now: an edge detected here arms a ~2.5s countdown
    // (kHudWakeSteps), step() spends it, and the row's own EasedToggle eases
    // it down. THE EDGES ARE DETECTED AGAINST LAST-SEEN VALUES and this
    // function runs several times a step, so every compare below is
    // idempotent by construction -- lastPlaceName_'s own pattern, whole
    // family. The first call ever (the constructor's) SEEDS and arms
    // nothing: a session does not boot with its corner announcing itself.
    const std::string caseNow = caseLine();
    const std::string roomNow = roomLine();
    const std::string guildNow = guildLine();
    const std::string objectiveNow = objectiveLine();
    const bool bookUp = casebookOpen_ || casebookPageOpen();
    const int hourNow = timeOfDay_ / 3600;
    const std::int32_t coinNow = tavern_->playerCoin();
    if (!hudEdgesSeeded_) {
        hudEdgesSeeded_ = true;
        lastClockHour_ = hourNow;
        lastClockTod_ = timeOfDay_;
        lastCoinSeen_ = coinNow;
        lastCaseSeen_ = caseNow;
        lastRoomSeen_ = roomNow;
        lastGuildSeen_ = guildNow;
        lastObjectiveSeen_ = objectiveNow;
        lastBookOpen_ = bookUp;
    }
    // THE CLOCK: an hour turning over is the ward's own bell, and a time
    // CHARGE (travel's restated minute, a wait pick, a sleep) is the one
    // moment a player is owed the hour they just spent. A charge is a jump
    // of a minute or more between two syncs -- the ordinary tick advances by
    // single seconds and never trips it.
    if (hourNow != lastClockHour_) {
        lastClockHour_ = hourNow;
        clockShowSteps_ = kHudWakeSteps;
    }
    {
        int jump = timeOfDay_ - lastClockTod_;
        if (jump < 0) {
            jump += sim::kSecondsPerDay;
        }
        if (jump >= 60) {
            clockShowSteps_ = kHudWakeSteps;
        }
        lastClockTod_ = timeOfDay_;
    }
    // THE PURSE: money is on screen when it moves.
    if (coinNow != lastCoinSeen_) {
        lastCoinSeen_ = coinNow;
        purseShowSteps_ = kHudWakeSteps;
    }
    // THE CASE ROW: a beat or lead moving is news; the book CLOSING is the
    // recap a player putting it down actually wants. And while the
    // lead-opened plate is up the row stays down -- the notice IS the case
    // news (UI-EA-SPEC #16), and one piece of news does not print twice.
    if (caseNow != lastCaseSeen_) {
        if (!caseNow.empty()) {
            caseShowSteps_ = kHudWakeSteps;
        }
        lastCaseSeen_ = caseNow;
    }
    if (lastBookOpen_ && !bookUp) {
        caseShowSteps_ = kHudWakeSteps;
    }
    lastBookOpen_ = bookUp;
    if (casePlateShowSteps_ > 0) {
        caseShowSteps_ = 0;
    }
    // THE ROOM ROW: entry and loudness are events; a head-count drifting by
    // one body is not, so the compare is made with the digits struck out --
    // "THE GULL 14 IN LOUD" and "THE GULL 15 IN LOUD" are one state.
    const auto strippedOfDigits = [](const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (const char c : text) {
            if (c < '0' || c > '9') {
                out.push_back(c);
            }
        }
        return out;
    };
    if (strippedOfDigits(roomNow) != strippedOfDigits(lastRoomSeen_)) {
        if (!roomNow.empty()) {
            roomShowSteps_ = kHudWakeSteps;
        }
    }
    lastRoomSeen_ = roomNow;
    // THE RUNG AND THE ERRAND: they wake when they change. A title held for
    // a week is reference material, and reference material lives on a page.
    if (guildNow != lastGuildSeen_) {
        if (!guildNow.empty()) {
            guildShowSteps_ = kHudWakeSteps;
        }
        lastGuildSeen_ = guildNow;
    }
    if (objectiveNow != lastObjectiveSeen_) {
        if (!objectiveNow.empty()) {
            objectiveShowSteps_ = kHudWakeSteps;
        }
        lastObjectiveSeen_ = objectiveNow;
    }
    // The four rows ride the sync() shape with one more condition: WANTED
    // means "recently woken", not "true". The caches still hold the last
    // shown words through the fade, exactly as every row above.
    const auto syncWake = [conversing](EasedToggle& anim, std::string& cache,
                                       const std::string& text, int wakeSteps) {
        const bool visible = !conversing && !text.empty() && wakeSteps > 0;
        if (visible) {
            cache = text;
        }
        anim.setTarget(visible);
    };
    syncWake(caseAnim_, caseCache_, caseNow, caseShowSteps_);
    syncWake(roomAnim_, roomCache_, roomNow, roomShowSteps_);
    syncWake(guildAnim_, guildCache_, guildNow, guildShowSteps_);
    syncWake(objectiveAnim_, objectiveCache_, objectiveNow, objectiveShowSteps_);
    // The clock and the purse draw live numbers (no cache -- fatigueAnim_'s
    // reasoning). The clock is additionally up for the life of the wait
    // page (its rows price the very hours it shows) and the pause stack --
    // the spec's own #34 keeps "rows, clock, title" on the pause, and the
    // hour before quitting is exactly a fact a player came to check. Neither
    // is gated on `conversing` -- a full page zeroes the fields at assembly,
    // and an hour striking or a price being paid mid-conversation is still
    // an event.
    clockAnim_.setTarget(waitOpen_ || pauseOpen_ || clockShowSteps_ > 0);
    purseAnim_.setTarget(purseShowSteps_ > 0);
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
    // ACTION-COMBAT BUILD. THE SAME sync() SHAPE for the HELD HARD charge row,
    // on its own toggle: a swing charging hard has nothing to do with a guard
    // going up, and the two are mutually exclusive anyway (the guard drops the
    // instant the hand leaves Idle).
    sync(chargeAnim_, chargeCache_, chargeLine());
    // STANCE & ROOM BUILD. THE SAME sync() SHAPE for the FISTS UP row, on its
    // own toggle: fighting mode outlives any one guard or swing.
    sync(handsAnim_, handsCache_, handsLine());
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
            // KIT BUILD: a slot is a spell OR an item; the strip prints
            // whichever it holds by name, the same cell either way.
            const sim::ItemDef* thing = tavern_->slotItem(slot);
            quickBarNames_[static_cast<std::size_t>(slot)] =
                bound != nullptr ? upperAscii(bound->displayName)
                : thing != nullptr ? thing->name
                                   : std::string();
        }
    }
    quickBarAnim_.setTarget(barWanted);
    // UI-EA (LANE HUD): THE Q-HOLD TUTOR TOAST, on the strip's own rising
    // edge and only its first two ever. The grimoire's tap-vs-hold split is
    // kept (flow map #9, a modern idiom); the toast is how it is taught --
    // whichever way the bar first came up (a number press, a wheel hold),
    // the player learns the hold exists, in the hand's own vocabulary
    // through promptLabel. It rides the strip's countdown so the pair rise
    // and fall as one, and after two showings it is retired for the session.
    if (barWanted && !lastQuickBarUp_ && wheelHintShows_ < kWheelHintShows) {
        ++wheelHintShows_;
        wheelHint_.raise(kQuickBarShowSteps);
        wheelHintText_ =
            std::string(promptLabel(controls_, Action::QuickWheel, promptDevice_)) +
            " HOLD - WHEEL";
    }
    lastQuickBarUp_ = barWanted;
    wheelHint_.sync(conversing);

    // DISTRICT PHASE D: THE THRESHOLD MOMENT.
    //
    // THE RISING EDGE ONLY, the identical shape alertPulse_ uses above and for
    // the identical reason: `here` is re-read on every call (this function runs
    // once a step AND out of every toggle, see its own header), so what fires
    // the plate has to be the CHANGE and never the state. Standing still in
    // Tarwalk must not re-arm it sixty times a second.
    //
    // NAMED PLACES ONLY. placeNameAt is empty for the two thirds of the
    // district that is compounds, yards and back lanes -- see its own note --
    // and lastPlaceName_ deliberately does not record those (see its header):
    // an unnamed tile is a gap between places, not a place, and treating it as
    // one would re-announce TARWALK every time the player stepped into an
    // alley and back out, and re-announce SALTGATE RISE at every seam between
    // the three rectangles that road is authored as.
    const std::string_view here =
        sim::docks::placeNameAt(body_->tileX(), body_->tileY(), body_->band());
    // THE STAND-DOWN, and it is the same list every other overlay stands down
    // for. A panel owns the screen: the plate does not draw over the topic
    // list, the tiled Menu, the ward map, the keys page or the pause menu, and
    // a crossing walked while one of them is up is simply not announced --
    // stood down rather than queued, because a notice that pops the instant a
    // menu closes is a notice about the menu.
    const bool suppressed = conversingNow();
    if (!here.empty() && here != lastPlaceName_) {
        lastPlaceName_.assign(here);
        if (!suppressed) {
            // THE NEWEST CROSSING WINS, say()'s own rule: a second boundary
            // crossed while the first plate is still up swaps the words and
            // re-arms the hold rather than queueing. It does NOT restart the
            // rise -- the toggle is already open and setTarget is idempotent --
            // so the plate reads as one notice being corrected, not as two
            // notices fighting.
            placePlateName_.assign(here);
            placePlateShowSteps_ = kPlacePlateShowSteps;
        }
    }
    if (suppressed) {
        placePlateShowSteps_ = 0;
    }
    // THE CASEBOOK PASS. THE SAME STAND-DOWN, AND IT IS NOT A COURTESY.
    // examine() is the one thing that arms this notice and examine() refuses
    // while a page or a conversation owns the keyboard, so in practice the
    // plate is armed with the world on screen -- but the rule is stated here
    // anyway, in the one place every other overlay's stand-down is stated, so
    // a later caller that arms it from somewhere else cannot quietly put a
    // notice over a topic list. STOOD DOWN, never queued: a notice that pops
    // the instant a menu closes is a notice about the menu.
    if (suppressed) {
        casePlateShowSteps_ = 0;
    }
    // AND THE TWO NOTICES DO NOT SHARE A FRAME. There is one announcement slot
    // under the ribbon (hud.cpp's drawAnnouncePlate) and the case outranks the
    // crossing, so a lead opened on the step the body walked into a named place
    // takes the band and the crossing is dropped rather than drawn under it.
    if (casePlateShowSteps_ > 0) {
        placePlateShowSteps_ = 0;
    }
    placePlateAnim_.setTarget(placePlateShowSteps_ > 0);
    casePlateAnim_.setTarget(casePlateShowSteps_ > 0);
}

std::string Session::rivalLine() const {
    // BOTTOM-LEFT, ON THE EDGE, ONE LINE. The HUD rule is not a preference
    // (COMBAT-FEEL-REFERENCE section 3): the centre stays empty and an
    // inventory, a guild or a rivalry is one row in a corner until it has
    // earned more. This is the only thing in the game that says the man across
    // the room is the man who put you here.
    //
    // UI-EA (LANE HUD): DIETED TO NAME AND COUNT -- the spec's "rank 6 -> 3".
    // "RIVAL " and the title were reference words (his corner and his sheet
    // already say what he is), and HUNTING leaves the label for the row's red
    // ink (HudState::rivalHunts) -- the glance was always the point of that
    // word, and the colour IS the glance. The count is a value; values never
    // get vaguer.
    const sim::Nemesis* worst = tavern_->nemesis().worst();
    if (worst == nullptr) {
        return {};
    }
    std::string line = upperAscii(worst->who) + " x" + std::to_string(worst->wins);
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

std::string Session::chargeLine() const {
    // ACTION-COMBAT BUILD (section 5, channel 2). "HELD HARD -- CUDGEL 14-18"
    // while the swing is charged past the hard threshold, and empty otherwise
    // -- the hard tier made visible in the sheet's own NAME low-high grammar.
    // The reticle carries the light charge; this row is only the committed hard
    // one, so it never competes with the guard (the guard holds only in Idle).
    if (!tavern_->playerChargeHard()) {
        return {};
    }
    const sim::Weapon weapon = tavern_->playerHeldWeapon();
    // THE HARD SPAN, which is the swing span through the hard tier: the same
    // ((base + variance) * chargeQ8) >> 8 strike() applies, at kHardSwingChargeQ8
    // -- so the numbers on the row are exactly the numbers a release will throw.
    const std::int32_t base = sim::baseDamage(weapon);
    const std::int32_t lo = (base * sim::kHardSwingChargeQ8) >> 8;
    const std::int32_t hi =
        ((base + sim::kStrikeVarianceMax) * sim::kHardSwingChargeQ8) >> 8;
    // THE WEAPON NAME is weaponSheetLine's own casing (which is why this reads
    // through it rather than keeping a second copy of the name table): take
    // everything up to the sheet's own span, so "THE EVICTOR 7-9 IMPACT" yields
    // "THE EVICTOR" and the numbers below are the doubled hard span, not its.
    std::string sheet = sim::weaponSheetLine(weapon);
    std::size_t span = 0;
    while (span < sheet.size() && (sheet[span] < '0' || sheet[span] > '9')) {
        ++span;
    }
    std::string name = sheet.substr(0, span);
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return "HELD HARD -- " + name + " " + std::to_string(lo) + "-" + std::to_string(hi);
}

std::string Session::handsLine() const {
    // STANCE & ROOM BUILD. THE ROOM'S OWN FACT, the blockLine shape:
    // playerHandsUp() is the hashed bit the room, the street and the Watch
    // react to, so this row can never say FISTS UP while the sim says down.
    // Weapon-named off the sheet's own casing (the same table chargeLine
    // reads through weaponSheetLine), with the register's own word for a
    // blade -- STEEL, as the flip line and the epitaph already say it.
    if (!tavern_->playerHandsUp()) {
        return {};
    }
    // KIT BUILD: a thing worn in the hand is named as itself -- KNIFE UP,
    // MACE UP -- the sheet's own word; the class reading stands for a hand
    // a sheet armed directly.
    if (const sim::ItemDef* held = tavern_->heldItem(); held != nullptr) {
        return held->name + " UP";
    }
    const sim::Weapon weapon = tavern_->playerHeldWeapon();
    if (weapon == sim::Weapon::Edged) {
        return "STEEL UP";
    }
    std::string sheet = sim::weaponSheetLine(weapon);
    std::size_t span = 0;
    while (span < sheet.size() && (sheet[span] < '0' || sheet[span] > '9')) {
        ++span;
    }
    std::string name = sheet.substr(0, span);
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return name + " UP";
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

namespace {

/// FAST TRAVEL's arrival seam: the finished frame dipped toward black by
/// `amount`, whatever was composed under it -- the world, the map's own close
/// tail, or a page opened mid-fade. The last act of every drawFrame() return
/// path, so the commit frame is already dark and the destination eases up
/// from under it. Render-only; nothing here is read back.
void dipTravelSeam(Framebuffer& target, float amount) {
    if (amount <= 0.0F) {
        return;
    }
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F},
                    amount > 1.0F ? 1.0F : amount);
}

}  // namespace

FrameStats Session::drawFrame(Framebuffer& target, FramePasses passes) const {
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

    FrameStats stats;
    if (passes.world) {
        stats = renderer_->renderFrame(target, view, settings, sprites);
    } else {
        // 3D BUILD. THE WORLD IS THE RAYLIB BACKEND'S FRAME NOW, and this
        // target is the OVERLAY it composites on top: cleared to nothing,
        // then every pass below draws exactly as it always has. The one
        // thing that changes is what the signage sees -- an infinite depth
        // buffer, so no sign is occluded by a wall until the V lane gives
        // signage its line-of-sight projector. The stats are empty because
        // no world was drawn here; the backend reports its own.
        target.clearTransparent();
    }

    // World content, not interface -- drawn like the sprites above rather
    // than gated behind config_.hud, and BEFORE the HUD/menu/dialogue passes
    // below so none of them paint over a legible sign. See
    // signage_renderer.hpp's own header for why occlusion needs the depth
    // buffer renderFrame just wrote and nothing drawn after it yet.
    // AND THE ONE THING ON THE SCREEN THE SIGNS COULD NOT SEE.
    //
    // Signage is drawn before the HUD so nothing paints over a legible sign --
    // and the consequence was that the HUD painted over one. The crosshair
    // prompt sits at the exact centre of the frame, which is where you are
    // looking, which is where the nearest sign projects: on the Tarwalk the
    // ward's own "TARWALK" and the prompt's "E - TALK" landed on the same
    // pixels and the prompt won by being second, cutting a street name through
    // the middle.
    //
    // The signage renderer already knew how to place labels around each other.
    // It just needed to be told the interface is on the screen too. The
    // rectangle is the prompt's OWN footprint this frame -- built from the same
    // aim fields the HUD is about to be handed, four lines below -- and not the
    // whole clamp fence, which would cost every sign in the centre band.
    //
    // A frame with nothing under the reticle hands back an empty rect and the
    // signs are placed exactly as they were before this existed.
    //
    // THE FURNITURE, IN ONE PLACE. `config_.hud` is the --nohud instrument and
    // `hudStandDown_` is a card owning the frame (see Session::setHudStandDown);
    // below, every interface pass reads THIS rather than testing the two
    // itself, so the two switches cannot drift apart one branch at a time.
    const bool furniture = config_.hud && !hudStandDown_;

    SignageSettings signage;
    // ...AND THE EXCLUSION IS COMPUTED ONLY WHEN THERE IS A PROMPT TO EXCLUDE.
    //
    // It used to be computed unconditionally, so under --nohud the signs still
    // stepped around an aim prompt that was never drawn. Zero gameplay impact
    // and a real measurement one: --nohud is the RULER this project takes every
    // interface number with, and a ruler that moves the world it is measuring
    // overstates every reading it gives. 368 to 656 pixels of a 640x360 frame
    // (0.16-0.29%) were counted as interface that were signs standing somewhere
    // else. See docs/HUD-REAL-ESTATE.md.
    if (config_.hud) {
        HudState aim;
        // SHIP NOTE MOVE 3: the half of Interact's row that belongs to the
        // device that last spoke -- "E" or "A" on the shipped table. Must
        // match the HUD's own read four blocks below, or the signage would
        // step around a prompt of a different width than the one drawn.
        aim.aimKey = promptKeyName(promptKey(controls_, Action::Interact, promptDevice_));
        aim.aimVerb = std::string_view{interactCache_};
        aim.aimSubject = std::string_view{interactSubjectCache_};
        aim.aimNote = std::string_view{interactNoteCache_};
        aim.interactFade = interactAnim_.value();
        signage.exclusion = hudAimPromptRect(aim, target.width(), target.height());
        // ...AND THE NAME IT IS ALREADY SAYING. See
        // SignageSettings::namedByPrompt: the prompt carries the verb and the
        // key, so where the two would say the same building the SIGN yields.
        // Gated on the prompt actually being on screen (interactFade), or a
        // sign would vanish for a subject the crosshair had already left.
        if (aim.interactFade > 0.0F) {
            signage.namedByPrompt = aim.aimSubject;
        }
    }
    // Signage is WORLD CONTENT and stays outside config_.hud -- the ruler has
    // to keep measuring it. A card is the one thing it yields to.
    if (!hudStandDown_) {
        drawSignage(target, view, signage);
    }

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
    // UI-EA (LANE HUD): NO STREET SUB-LABEL. placeLabel() still answers for
    // the map's title and the dialogue epithet; the street's own copy of it
    // is deleted -- the threshold plate announces every crossing, and a row
    // that restated it every frame was the diet's first cut. The clock and
    // the purse are earned text on their own toggles: numbers stay live
    // (they are what wakes), the fades are what sleep.
    hud.timeOfDaySeconds = timeOfDay_;
    hud.clockFade = clockAnim_.value();
    hud.coin = tavern_->playerCoin();
    hud.purseFade = purseAnim_.value();
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
    // ACTION-COMBAT BUILD (section 5, channel 2). The HELD HARD charge row,
    // its own cache and fade the identical shape the guard row just used.
    hud.chargeLabel = std::string_view{chargeCache_};
    hud.chargeFade = chargeAnim_.value();
    // STANCE & ROOM BUILD. The FISTS UP row, its own cache and fade.
    hud.handsLabel = std::string_view{handsCache_};
    hud.handsFade = handsAnim_.value();
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
    // UI-EA (LANE HUD): the Q-hold tutor toast, riding the strip.
    hud.wheelHint = std::string_view{wheelHintText_};
    hud.wheelHintFade = wheelHint_.value();
    // S9. Whether the room can see you, and the lock under the wire. Both on
    // edges, both empty when they have nothing to say -- the right-hand stack
    // for the first, the bottom band for the second. Both read their own
    // cache and fade the identical way roomLabel just did, above.
    hud.stealthLabel = std::string_view{stealthCache_};
    hud.stealthFade = stealthAnim_.value();
    hud.lockLabel = std::string_view{lockCache_};
    hud.lockFade = lockAnim_.value();
    // THE CROSSHAIR PASS. THE AIM PROMPT, on the reticle rather than along the
    // bottom edge -- the owner's own sentence is at the top of hud.hpp. The
    // verb still changes to PICKPOCKET the instant the player crouches facing
    // somebody (#85's whole point, and Eli's brief: "the player must SEE what
    // pressing it will do before they press it"); what the crosshair pass adds
    // is the name of whoever that is.
    //
    // THE KEY NAME IS READ off the same binding row the CONTROLS page
    // prints, so a rebound Interact renames itself on the crosshair by the
    // act of being rebound -- and SHIP NOTE MOVE 3 made it the half of that
    // row belonging to the device that last spoke: "E - TALK" with a
    // keyboard in hand, "A - TALK" the moment a pad button lands, live.
    hud.aimKey = promptKeyName(promptKey(controls_, Action::Interact, promptDevice_));
    hud.aimVerb = std::string_view{interactCache_};
    hud.aimSubject = std::string_view{interactSubjectCache_};
    hud.aimNote = std::string_view{interactNoteCache_};
    hud.aimKind = static_cast<int>(interactKindCache_);
    hud.interactFade = interactAnim_.value();
    // ACTION-COMBAT BUILD (section 5, channel 1). THE RETICLE'S CHARGE READOUT,
    // read straight off the sim's own swing state and fed INDEPENDENT of the
    // interact prompt above -- the reticle draws during a hold even when
    // nothing is in reach (aimVerb empty), which drawAim's interact-only
    // early-return could not do. The fraction is the hold's progress toward the
    // hard threshold, the tier and the on-line flag are the sim's live reads.
    {
        float held = static_cast<float>(tavern_->playerChargeSteps()) /
                     static_cast<float>(sim::kHardSwingHoldSteps);
        held = held < 0.0F ? 0.0F : (held > 1.0F ? 1.0F : held);
        hud.aimChargeFrac = held;
        hud.aimChargeHard = tavern_->playerChargeHard();
        hud.aimChargeOnLine = tavern_->playerSightlineTarget();
    }
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
    // UI-EA (LANE HUD): the HUNTING word is off the label (rank 6 -> 3); the
    // fact rides this bool and the row's red ink, read fresh off the same
    // Nemesis the line is built from so the two can never disagree.
    if (const sim::Nemesis* worstRival = tavern_->nemesis().worst(); worstRival != nullptr) {
        hud.rivalHunts = worstRival->hunts();
    }
    hud.showCompass = !conversing;
    // DISTRICT PHASE D: THE THRESHOLD MOMENT, under the ribbon it shares a
    // band with. See HudState::placePlate and drawPlacePlate.
    //
    // `conversing ? 0 : ...` IS THE SAME LINE showCompass JUST WROTE, on
    // purpose. The plate lives in the compass's own band and it stands down
    // under the compass's own rule: while a panel owns the screen there is no
    // ribbon for it to sit under and nothing up there but somebody else's
    // page. syncPanelAnim() has already put the toggle's target down for the
    // same cases (and zeroed the countdown, so nothing resurfaces when the
    // panel closes); this is what stops the handful of frames it spends easing
    // out from being painted over the panel that suppressed it.
    hud.placePlate = std::string_view{placePlateName_};
    // THE CASEBOOK PASS. The lead-opened notice, on the identical three fields
    // and the identical conversing gate -- see HudState::casePlate.
    hud.casePlate = std::string_view{casePlateText_};
    hud.casePlateFade = conversing ? 0.0F : casePlateAnim_.value();
    hud.casePlateDrift = casePlateAnim_.target() ? -(1.0F - casePlateAnim_.value())
                                                 : (1.0F - casePlateAnim_.value());
    hud.placePlateFade = conversing ? 0.0F : placePlateAnim_.value();
    // THE SIGN COMES OFF THE TOGGLE'S OWN TARGET, so the plate rises THROUGH
    // its settled row rather than sliding back down the way it came -- see
    // HudState::placePlateDrift. Rising (target true): still below, closing on
    // zero. Fading (target false): already past, drifting on up and out.
    hud.placePlateDrift = placePlateAnim_.target() ? -(1.0F - placePlateAnim_.value())
                                                   : (1.0F - placePlateAnim_.value());
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
        // THE MAP PASS. The page is a COMPOSED PANE with a cursor now, so the
        // state it draws from is built in one place (districtMapState()) that a
        // case can read, rather than assembled inline where nothing but a
        // screenshot could ever check it.
        DistrictMapState plan = districtMapState();
        // Cross-lane contract (b), wired at integration: FLOW arms the pulse
        // at commit routing, PAGES renders it off the state field -- this is
        // the one assignment that joins them, per composition.
        plan.commitPulse = commitPulse_.value();
        plan.tutor = mapTutor_.value();  // contract (c): the raised verb words
        // THE HUD STANDS DOWN UNDER IT, which the old static page could get
        // away with not doing and this one cannot. The compass ribbon prints
        // the place name across the top centre and the clock/purse stack sits
        // top right -- exactly where this composition's breadcrumb row and its
        // right-aligned readout now live, and the first capture of this page
        // had the ward's own street name printed straight through its
        // breadcrumb. Same ruling the controls page already made and for the
        // same reason: nothing is being discussed and no door is about to shut
        // while a map is up, and a warning still outranks the page -- it is
        // routed into the page's own header band rather than painted over it.
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        hud.showCompass = false;
        hud.placePlateFade = 0.0F;
        hud.casePlateFade = 0.0F;
        hud.showHealth = false;
        hud.showAlert = false;
        if (warned) {
            // The bouncer's warning outranks a map read -- the same routing
            // the tiled Menu gives its journal tile, below. It takes the
            // breadcrumb row, which the composition holds open regardless, so
            // nothing under it moves when a bouncer starts talking.
            plan.alert = tavern_->lastWarning();
        }
        if (furniture) {
            drawDistrictMap(target, plan);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        panelTailCourt_ = false;
        return stats;
    }
    // PANES PASS: THE CONTROLS PAGE IS THE FIRST SURFACE DRAWN IN THE
    // TERMINAL-PANEL REGISTER (render/keys_page.hpp, on render/panel.hpp's
    // vocabulary) rather than through the single conversation panel. It is a
    // FULL-PAGE composition, like the tiled Menu and like every one of the
    // owner's reference frames, and for the same reason menu_view.hpp already
    // gives: nobody is standing in front of you while you read your own key
    // bindings, so there is nothing in the centre of the frame to leave clear.
    //
    // THE CLOCK AND THE PURSE STAND DOWN WHILE IT IS UP. That is this build's
    // existing rule about overlays, applied to a new one: the top-right stack
    // is drawn AFTER the panel and would otherwise land in the tab row, on top
    // of the build readout that row right-aligns. Nothing is being discussed
    // and no door is about to shut, so the hour and the purse have nothing to
    // say for the few seconds this page is open. Everything else about the HUD
    // is untouched, and a warning still outranks the page -- it is routed into
    // the page's own header band instead of being painted over it.
    if (keysOpen_) {
        KeysPageState page = keysPageState();
        page.commitPulse = commitPulse_.value();  // contract (b), see the map pass
        page.tutor = keysTutor_.value();          // contract (c)
        page.openAmount = panelAnim_.value();
        page.open = page.open || panelAnim_.value() > 0.0F;
        if (warned) {
            page.alert = std::string(tavern_->lastWarning());
        }
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        if (furniture) {
            drawKeysPage(target, page);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        panelTailCourt_ = false;
        return stats;
    }
    // MORROWIND ROUND: THE TILED MENU IS A DIFFERENT SURFACE FROM THE SINGLE
    // CONVERSATION PANEL, drawn by a different function (menu_view.hpp's
    // drawMenuTiles rather than dialogue_view.hpp's drawDialogue) because it
    // deliberately does NOT respect the HUD's centre-clear rule the way every
    // other overlay in this build still does -- see menu_view.hpp's own
    // header on why a Morrowind-style overview is exempt and a live
    // conversation is not.
    // THE CASEBOOK PASS. THE JOURNAL TILE IS A COMPOSED PAGE NOW, drawn by
    // casebook_page.hpp in the terminal-panel register rather than as the
    // bottom third of four tiles.
    //
    // WHY THE OTHER THREE TILES ARE UNTOUCHED, said plainly rather than left to
    // be discovered: this phase is contracted for the casebook and the casebook
    // alone, and converting Character/Chart/Letters would be three more surfaces
    // this pass has no frames of and no cases for. The seam that leaves is real
    // -- the book is a composed page and its three siblings are still tiles --
    // and a player only ever sees one of the two at a time, because the Menu
    // draws whichever tile has FOCUS. The bumpers still step between them and
    // the book's own nav band says so.
    //
    // THE HUD STANDS DOWN THE SAME WAY IT DOES UNDER THE WARD MAP, and for the
    // same reason: this composition's breadcrumb runs across the top left and
    // its readout is right-aligned in the tab row, which is exactly where the
    // compass ribbon and the clock/purse stack live. A warning still outranks
    // the page -- it is routed into the page's own header band, which the
    // composition holds open regardless, so nothing under it moves when a
    // bouncer starts talking.
    if (casebookPageOpen()) {
        CasebookPageState page = casebookPageState();
        page.commitPulse = commitPulse_.value();  // contract (b), see the map pass
        page.tutor = casebookTutor_.value();      // contract (c)
        page.openAmount = panelAnim_.value();
        page.open = page.open || panelAnim_.value() > 0.0F;
        if (warned) {
            page.alert = std::string(tavern_->lastWarning());
        }
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        hud.showCompass = false;
        hud.placePlateFade = 0.0F;
        hud.casePlateFade = 0.0F;
        hud.showHealth = false;
        hud.showAlert = false;
        if (furniture) {
            drawCasebookPage(target, page);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        panelTailCourt_ = false;
        return stats;
    }
    // UI-EA-SPEC sec. 3 rule 4 -- CLOSE HONESTY: the tiled Menu's close tail
    // draws as the TILES easing out, not as the empty single panel the old
    // handover fell through to (the seam the comment at the dialogue branch
    // below used to admit). panelTailTiles_ is drawFrame's own memo of what
    // it drew the panel fade for last frame -- see its header in session.hpp.
    if (casebookOpen_ ||
        (panelTailTiles_ && !conversing && panelAnim_.value() > 0.0F)) {
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
        if (furniture) {
            drawMenuTiles(target, tiles);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        // The memo: this frame's panel fade belonged to the tiles. Only the
        // LIVE menu re-arms it -- a tail frame keeps it as-is, so the tail
        // keeps drawing tiles until panelAnim_ settles and the condition
        // above goes quiet on its own.
        panelTailTiles_ = panelTailTiles_ || casebookOpen_;
        return stats;
    }
    // UI-EA-SPEC 1.7 (LANE PAGES, strip->card): pause, wait, options and the
    // grimoire draw as the composed card now, not as a HUD strip -- the ship
    // note's one remaining strip surface, converted with its family. Input
    // and close routing are untouched (the flags, cursors and digit windows
    // are exactly the strip's own); the CLOSE fade still runs through the
    // panel path below until the close-honesty pass (sec. 3 rule 4, LANE
    // FLOW) teaches it to fade as what it was.
    if (stripCardOpen()) {
        CreationPage card = stripCard();
        card.alpha = panelAnim_.value();
        // THE CLOCK KEEPS THE PAUSE (spec #34: "rows, clock, title stay") --
        // integration kept HUD's ruling over the family stand-down the other
        // page passes make: the pause stack is where a player stands still to
        // read the hour, and HUD's own earned-text logic already wakes the
        // clock for it. The purse still has nothing to say here.
        hud.coin = -1;
        if (furniture) {
            drawCreationPage(target, card);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        return stats;
    }
    // JUSTICE BUILD (HEARING PAGE LANE). THE HEARING PAGE: a full-page
    // composition in the terminal register (hearing_page.hpp), drawn into
    // the framebuffer exactly as the casebook page is, so the 3D overlay
    // composites it unchanged. Below the pause card on purpose -- PAUSE is
    // the one page allowed over the bench -- and above the conversation
    // panel, which cannot be open under it (openCourt ends any talk). The
    // HUD stands down the way it does under every composed page; a bouncer's
    // warning still outranks it and rides the page's own header band.
    if (courtOpen_ || (panelTailCourt_ && !conversing && panelAnim_.value() > 0.0F)) {
        HearingPageState page = hearingPageState();
        page.openAmount = panelAnim_.value();
        page.open = page.open || panelAnim_.value() > 0.0F;
        if (warned) {
            page.alert = std::string(tavern_->lastWarning());
        }
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        hud.showCompass = false;
        hud.placePlateFade = 0.0F;
        hud.casePlateFade = 0.0F;
        hud.showHealth = false;
        hud.showAlert = false;
        if (furniture) {
            drawHearingPage(target, page);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        // The memo: this frame's panel fade belonged to the bench. Only the
        // LIVE page re-arms it, exactly as the tiles' memo works.
        panelTailCourt_ = panelTailCourt_ || courtOpen_;
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
    // UI-EA-SPEC sec. 3 rule 4: the tiled Menu's closing tail no longer
    // lands here -- the branch above catches it off panelTailTiles_ and
    // fades the TILES, which is the close-honesty rule ("a page fades as
    // what it was"). What still lands here is every single-panel surface's
    // own tail -- Keys, Options, Wait, a conversation -- which genuinely
    // WAS this panel, so fading as it is honest. The memo is cleared here
    // whenever a live panel draws, so a keys tail after a menu visit cannot
    // resurrect the tiles.
    panel.openAmount = panelAnim_.value();
    if (conversing) {
        panelTailTiles_ = false;
        panelTailCourt_ = false;
    }
    panel.open = panel.open || panelAnim_.value() > 0.0F;
    // The panel FIRST, the HUD over it: a bouncer's warning has to survive
    // being told mid-conversation, and it is the one line that outranks a menu.
    if (furniture) {
        drawDialogue(target, panel);
        drawHud(target, hud);
        dipTravelSeam(target, travelFadeAnim_.value());
        composeDeathCeremony(target);
        composeRopeCeremony(target);
        composeTakenPlate(target);
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

/// Turns the player's body to look STRAIGHT at a Q8 world point, then steps once
/// so the room hears the new facing (setPlayerYaw runs in syncTavernToBody, which
/// only a step calls). RENDER-SIDE, so the atan2 is legal here -- it never
/// touches the sim's hashed yaw math, it only sets the body's own facing, which
/// the sightline raycast (VETO 1) then reads. The scripted drives use this to
/// put a body DEAD ON the crosshair before a swing: the old radial punch hit by
/// proximity, but the action-combat swing hits the first body on the look-ray,
/// so a man who is trading blows and shifting his feet has to be re-faced or the
/// swing casts past him. One step of the target's own drift is far inside the
/// beam's half-cell, so the aim holds.
void facePlayerAndSync(Session& session, std::int32_t xQ8, std::int32_t yQ8) {
    const std::int32_t dx = xQ8 - session.body().x();
    const std::int32_t dy = yQ8 - session.body().y();
    if (dx != 0 || dy != 0) {
        // North is -Y and forward is (sin(yaw), -cos(yaw)), so the bearing to
        // (dx, dy) is atan2(dx, -dy), turned from radians into the BAM turn.
        constexpr double kTwoPi = 6.283185307179586;
        const double ang = std::atan2(static_cast<double>(dx), static_cast<double>(-dy));
        session.body().setYaw(static_cast<sim::Angle>(
            std::lround(ang * static_cast<double>(sim::kTurnFull) / kTwoPi)));
    }
    session.stepMany(sim::MoveInput{}, 1);
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

/// The nearest PERSON on the player's own band -- present, on their feet, not
/// a rat -- or nullptr. What `--punch` and `--block` mean by "whoever is
/// nearest": the pre-sightline radial punch picked by proximity, and the two
/// flags keep that meaning, they just walk it over now. Same band, because the
/// scripted walker routes on the body's band and a stool upstairs is nearer by
/// a count that means nothing to a fist. Ties break on the lower id (roster
/// order), so two runs cannot disagree about who got hit.
[[nodiscard]] const sim::Actor* nearestMark(const Session& session) {
    const sim::Actor* best = nullptr;
    std::int32_t bestDistance = 0;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (!actor.present() || sim::isFloored(actor.activity()) ||
            actor.role() == sim::ActorRole::Vermin ||
            actor.band() != session.body().band()) {
            continue;
        }
        const std::int32_t distance =
            actor.distanceTo(session.body().x(), session.body().y());
        if (best == nullptr || distance < bestDistance) {
            best = &actor;
            bestDistance = distance;
        }
    }
    return best;
}

/// Whether this body is in the fight the room is resolving -- currentFight()
/// is the player plus brawlers_, by actor id. The one honest reading of "the
/// fight is on with HIM": the house minds a swing the instant the sightline
/// finds a person (joinBrawl fires before the roll), landed or not.
[[nodiscard]] bool inTheFight(const Session& session, std::int32_t markId) {
    for (const sim::Fighter& fighter : session.tavern().currentFight()) {
        if (fighter.actorId == markId) {
            return true;
        }
    }
    return false;
}

/// The first half of every scripted swing: walks up to a body if it is out of
/// reach, steps out of its tile if the walk ended IN it, and puts it DEAD ON
/// the crosshair. False when the mark is gone or on the floor.
[[nodiscard]] bool closeOnAndFace(Session& session, std::int32_t markId) {
    const sim::Actor* mark = session.tavern().actorById(markId);
    if (mark == nullptr || !mark->present() || sim::isFloored(mark->activity())) {
        return false;
    }
    if (mark->distanceTo(session.body().x(), session.body().y()) > sim::kMeleeReach) {
        walkToTile(session, mark->tileX(), mark->tileY());
        mark = session.tavern().actorById(markId);
        if (mark == nullptr) {
            return false;
        }
    }
    if (mark->x() == session.body().x() && mark->y() == session.body().y()) {
        // Standing IN him: `along` is zero and the ray finds nobody. One
        // step of clear ground opens the gap the crosshair needs; tried
        // in each direction until the body actually moved, since the
        // first way may be a wall.
        const std::int32_t beforeX = session.body().x();
        const std::int32_t beforeY = session.body().y();
        const std::int32_t nudges[4][2] = {{-1, 0}, {0, -1}, {0, 1}, {1, 0}};
        for (const auto& nudge : nudges) {
            sim::MoveInput back;
            back.forward = nudge[0];
            back.strafe = nudge[1];
            back.autoTraverse = false;
            back.snapVelocity = true;
            session.step(back);
            if (session.body().x() != beforeX || session.body().y() != beforeY) {
                break;
            }
        }
        mark = session.tavern().actorById(markId);
        if (mark == nullptr) {
            return false;
        }
    }
    facePlayerAndSync(session, mark->x(), mark->y());
    return true;
}

/// Walks up to a body, puts it DEAD ON the crosshair and taps -- the same
/// walk-up / re-face / tap / step-past-recovery beat the tenant and rat drives
/// throw -- until a blow CONNECTS or the tries run out. Connection is read
/// from the body itself: its hit points moving, or it going to the floor,
/// which is strike()'s own output and the one thing a whiffed roll never
/// changes (the house minds a swing the moment the sightline finds a person,
/// landed or not, so the player's standing is no proof of a blow). Between
/// taps the man may shift his feet or trade back, so each tap re-closes if he
/// left reach and re-faces him regardless; and the lockout is stepped through
/// in full first, because a down-edge in recovery is dropped, not buffered.
[[nodiscard]] bool tapUntilItConnects(Session& session, std::int32_t markId, int tries) {
    for (int attempt = 0; attempt < tries; ++attempt) {
        if (!closeOnAndFace(session, markId)) {
            return false;
        }
        const sim::Actor* mark = session.tavern().actorById(markId);
        if (mark == nullptr) {
            return false;
        }
        const std::int32_t hpBefore = mark->hp();
        session.punch();
        const sim::Actor* struck = session.tavern().actorById(markId);
        if (struck != nullptr &&
            (struck->hp() < hpBefore || sim::isFloored(struck->activity()))) {
            return true;
        }
        session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps + 1);
    }
    return false;
}

/// Walks up to a body, puts it on the crosshair and swings ONCE -- the swing a
/// fight needs and not one more. STANCE & ROOM BUILD: the nemesis line throws
/// this and not tapUntilItConnects, because every CONNECTED tap is hit points
/// off a man the line needs standing, and the fight does not need the blow to
/// land: the house minds a swing the instant the sightline finds a person
/// (joinBrawl fires before the roll), so one swing that found him has the room
/// resolving the fight with him whether or not the roll did. Retried only
/// while the ray found nobody, or somebody else (he shifted his feet), which
/// costs him nothing; the lockout is stepped through between tries.
[[nodiscard]] bool swingOnceAt(Session& session, std::int32_t markId, int tries) {
    for (int attempt = 0; attempt < tries && !inTheFight(session, markId); ++attempt) {
        if (!closeOnAndFace(session, markId)) {
            return false;
        }
        session.punch();
        if (inTheFight(session, markId)) {
            return true;
        }
        session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps + 1);
    }
    return inTheFight(session, markId);
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
            // ACTION-COMBAT BUILD: the swing is a sightline TAP with a recovery
            // lockout now, so re-approach the rat each swing (re-facing the
            // crosshair onto it, since it can scurry between blows) and let the
            // lockout clear before the next lands.
            walkToTile(session, still->tileX(), still->tileY());
            session.punch();
            session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps + 1);
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
/// wake up on the quay, walk back in the next evening and fight it twice more
/// as it goes lethal in the world.
///
/// EVERY BEAT IS A SESSION CALL A KEYPRESS MAKES. The walk is real movement
/// through real collision, the fight is the Attack key (punch() is now a tap of
/// the action-combat verbs) and then the room resolving the brawl and its flip
/// to lethal a second at a time, and the respawn is the same settleDefeat() the
/// client's own step loop reaches -- with the death ceremony over it once steel
/// is out. STANCE & ROOM BUILD: every defeat in the arc is now DELIVERED BY
/// THE ROOM. stepBrawl resolves the NPC's blows under lethal rules with the
/// floor lifted to zero, so the rematch that flips lethal is fought to the
/// boards by the man himself, and Tavern::concedeTo -- the scripted defeat
/// seam -- is no longer used anywhere in this line. A beat that the room
/// cannot finish does not land, which is the proof.
/// STANCE & ROOM BUILD. The mask of which --nemesis beats landed, for the
/// summary -- the eviction line's own gEvictBeatMask shape, so a build log says
/// WHICH beat fell short rather than how many did.
std::int32_t gNemesisBeatMask = 0;

[[nodiscard]] int runNemesisLine(Session& session, const std::string& ending) {
    // TARN WRENHALE, "Two-Loads": a docker on the evening shift, fists, no
    // rung, nobody's rival. Eli's own example is "killed by a laborer in a fist
    // fight", and this is the labourer.
    constexpr std::string_view kMark = "Tarn Wrenhale";
    int landed = 0;
    gNemesisBeatMask = 0;
    const auto land = [&landed](int beat) {
        gNemesisBeatMask |= 1 << (beat - 1);
        ++landed;
    };

    const sim::Actor* mark = actorNamed(session, kMark);
    if (mark == nullptr) {
        return landed;
    }
    const std::int32_t id = mark->id();

    // 1. HE IS NOBODY. The proof is worth nothing without the before.
    if (session.tavern().nemesis().of(id) == nullptr && mark->weapon() == sim::Weapon::Fists) {
        land(1);
    }

    // A round of the real thing: walk up to him, put him dead on the crosshair,
    // swing ONCE (swingOnceAt -- past two wins he HUNTS, walking onto the
    // player's own tile, so a blind swing from wherever the walk stopped casts
    // past him), and let the room resolve it a second at a time. The loop waits
    // for BOTH his win and the player being back on their feet --
    // Session::step() finds the release flag itself, so a loop that stopped at
    // the win would walk into the next round with the player still on the
    // boards.
    //
    // AND IT PICKS THE FIGHT AGAIN WHEN THE ROOM ENDS IT WITHOUT HIM WINNING.
    // STANCE & ROOM BUILD: now that every loss in this line is delivered by the
    // room's own blows, the line lives or dies on the fight actually reaching
    // its end WITH HIM -- and the house has honest ways of stopping it short.
    // The door policy puts a brawler out (the ejection shove, and tickBouncers
    // clears the brawl the moment the player is off the footprint), tickBrawl
    // clears any fight the player is not in the house for, and a swing that
    // found whoever else stood on the crosshair started a fight he is not in.
    // Either way the drive does what a player who is about to lose does: walks
    // back in, swings at HIM once more, and takes the beating.
    //
    // ONCE, AND ONLY AT A MAN WHO CAN STAND IT. The first draft of this build
    // re-engaged with tapUntilItConnects, and in three of eleven smoke
    // timelines it tapped its own nemesis to the boards: he got up at a
    // quarter of his health (advanceSecond), bloodied, and the next tap on a
    // bloodied man who means Harm is a lethal blow with the floors lifted
    // (brawl.hpp B3) -- the drive killed him, and the third win never came.
    // So: one swing per engagement (the sightline that found him put him in
    // the brawl whether or not the roll landed), never a swing at a bloodied
    // man, only inside the house (he only hunts indoors, and the room clears a
    // fight the player is outside for), and a corpse ends the round -- nobody
    // rises from one. Bounded twice: the seconds, and the re-engagements.
    const auto pickAFight = [&session, id]() {
        const sim::Actor* him = session.tavern().actorById(id);
        if (him == nullptr || !him->present()) {
            return false;
        }
        const sim::Nemesis* before = session.tavern().nemesis().of(id);
        const std::int32_t had = before == nullptr ? 0 : before->wins;
        session.closeConversation();
        constexpr int kReengagements = 4;
        int engaged = 0;
        for (int second = 0; second < 300; ++second) {
            him = session.tavern().actorById(id);
            if (him == nullptr || !him->present() || him->activity() == sim::Activity::Dead) {
                return false;
            }
            if (!session.tavern().playerFloored() && !inTheFight(session, id)) {
                if (!session.tavern().playerInside()) {
                    // Put out, or woken on the quay: back in through the door
                    // first -- by walking up to HIM, no swing, which is the
                    // one walk every spot the smoke leaves a body on has been
                    // proved to route from (a walk to a fixed interior tile
                    // stalled at the threshold from two of them). If he is
                    // outside too, this follows him in.
                    (void)closeOnAndFace(session, id);
                } else if (!sim::isFloored(him->activity()) &&
                           !sim::isBloodied(him->hp(), him->hpMax()) &&
                           sim::gull::insideFootprint(him->tileX(), him->tileY()) &&
                           engaged < kReengagements) {
                    ++engaged;
                    (void)swingOnceAt(session, id, 4);
                }
            }
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
        land(2);
    }
    session.skipToHour(20);

    // 3. AND THE REMATCH IS FOUGHT IN THE WORLD, TO THE BOARDS.
    //
    //    THE RULE STILL WORKS; what changed is that the flip is resolved rather
    //    than parked. A nemesis MEANS IT from his first win on (nemesisIntent),
    //    and brawl.hpp's third clause says beating a BLOODIED man while meaning
    //    him Harm is not a bar fight. So the rematch opens as a brawl and flips
    //    the instant the player is under a quarter (the "STEEL OUT. THE ROOM
    //    STANDS BACK." line and the SwordDraw fire on the edge in step()) --
    //    and STANCE & ROOM BUILD, the room keeps swinging under lethal rules
    //    with the floor lifted: he finishes the player himself, the death
    //    ceremony plays over the quay revive, and his second win is the room's
    //    own doing. The beat lands only when BOTH are true: the fight flipped
    //    lethal, and he put the player down in it. clearEscalation() first so
    //    the flip is a fresh edge; it is deliberately NOT cleared again below,
    //    so it stays latched through the defeat that follows and that one
    //    plays the death ceremony too.
    session.tavern().clearEscalation();
    const bool rematchLost = pickAFight();
    if (rematchLost && session.tavern().escalated() && !session.tavern().playerFloored()) {
        land(3);
    }
    if (ending == "death") {
        // 3D SLICE ONE (ship lane). STOP HERE, on the boards of beat 3, with
        // the death ceremony settleDefeat has just armed still playing over
        // the quay revive -- the one shutter that photographs the plate,
        // because beat 4's walk back in steps it out. Nothing past this line
        // is proved by this ending; the summary's beat mask says so.
        return landed;
    }
    session.skipToHour(20);

    // 4. AND THE THIRD LOSS IS FOUGHT AND LOST THE SAME WAY.
    //
    //    One more real fight -- real movement, a real swing, the room resolving
    //    the brawl, its flip, and the killing -- with a cudgel in his hand now
    //    (nemesisWeapon at two wins) and Harm behind it: a bar fight until the
    //    player is bloodied, lethal after, and the room finishes it. Nothing is
    //    conceded: Tavern::concedeTo SURVIVES as the defeat seam (COMBAT-
    //    ACTION-SPEC section 7, and the gate workload still reports through
    //    it) but this line has no use for it, because the room delivers the
    //    rise by its own hand now. Steel is still out from beat 3, so this too
    //    plays the DEATH CEREMONY -- settleDefeat lays the dip and the epitaph
    //    over the quay revive. Three wins is the top of the ladder the arc
    //    climbs (a rung, a house, a charge on the roll), which is why there is
    //    exactly one more fight here and not two.
    {
        const sim::Nemesis* before = session.tavern().nemesis().of(id);
        const std::int32_t had = before == nullptr ? 0 : before->wins;
        if (pickAFight()) {
            const sim::Nemesis* now = session.tavern().nemesis().of(id);
            if (now != nullptr && now->wins == had + 1) {
                land(4);
            }
        }
        session.skipToHour(20);
    }

    // 5. AND HE COMES PREPARED. Three wins put a blade in his hand and Kill
    //    behind it (nemesisWeapon/nemesisIntent) -- the next rematch is lethal
    //    from its first blow, and beats 3 and 4 have just proved the room will
    //    finish one. Read off the roster the way beat 1 read his fists.
    if (const sim::Actor* armed = session.tavern().actorById(id);
        armed != nullptr && armed->weapon() == sim::Weapon::Edged &&
        armed->intent() == sim::Intent::Kill) {
        land(5);
    }

    // 6. A house with members in it, and 7. ground on the ward's own roll --
    //    and none of it came off when the player got up.
    const sim::Nemesis* risen = session.tavern().nemesis().of(id);
    if (risen != nullptr && risen->foundedAHouse() && !risen->members.empty()) {
        land(6);
    }
    if (risen != nullptr && risen->holdsGround()) {
        land(7);
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
/// lost in the world, a rematch fought to the lethal flip AND lost to the
/// room's own blows, a third loss fought and lost the same way with a cudgel
/// in his hand (each with the death ceremony over the revive), the blade and
/// the Kill three wins put in his hands, a house, and a charge on the roll.
constexpr std::int32_t kNemesisBeats = 7;
/// 3D SLICE ONE (ship lane). What `--nemesis=death` owes: it stops ON PURPOSE
/// on the boards of the rematch (beats 1-3), the death ceremony still
/// playing, so three is the whole of what that ending can land.
constexpr std::int32_t kNemesisDeathBeats = 3;

/// BARKS LANE (feel/build). THE WATCH ON SEEN VIOLENCE, played through the
/// real verbs: the walk in, the blade, one swing at a man Cull can see, the
/// halt on the alert row, the arrest at reach, the street. Five beats:
///   1. Watchman Cull is in the house and can SEE the player where they stand
///      (Tavern::noticeBy, the three-clause rule -- asked, not assumed).
///   2. Steel drawn and a swing thrown at a patron: a LETHAL-class fight is
///      live with the fists-up stance holding a blade.
///   3. He goes Closing on VIOLENCE within seconds -- no glance gate, no die --
///      and the halt is on the alert row in HIS name, out of watch.halt.
///   4. The arrest at reach, the report's cause VIOLENCE (the contraband path,
///      applyArrest), the sentence in the report.
///   5. Turned loose on the Tarwalk with the arrest line on the row.
/// "halt" ends the drive at beat 3 so a capture holds the halt itself.
constexpr std::int32_t kWatchHaltBeats = 5;
/// What `--watch-halt=halt` owes: it stops the step he starts Closing.
constexpr std::int32_t kWatchHaltStopBeats = 3;

/// Who the Watch line swung at and what came of it, for the summary -- so a
/// capture cannot quietly photograph the wrong man (the nemesis line's rule).
std::string gWatchHaltNote;

[[nodiscard]] int runWatchHaltLine(Session& session, const std::string& ending) {
    int landed = 0;
    gWatchHaltNote.clear();
    const sim::Tavern& tavern = session.tavern();
    // In through the door to the bar, the smoke's own route-walk.
    walkToTile(session, sim::gull::kBartenderX, sim::gull::kBartenderY + 1);
    session.closeConversation();
    const sim::Actor* cull = actorNamed(session, "Watchman Cull");
    if (cull == nullptr || !cull->present()) {
        gWatchHaltNote = " cull=absent";
        return landed;
    }
    const std::int32_t cullId = cull->id();

    // THE MARK, AND THE SPOT: exactly what test_watch_rhythm asks of the room
    // -- an upright PATRON (not the Watch, not the house's bouncer who refuses
    // steel, not the staff behind the bar, not a rat) the player can stand
    // beside, facing him, with Cull able to SEE the player from there by the
    // three-clause notice rule. Asked, not assumed: the first draft stood at
    // the bar and swung, and Cull's line to the bar is a table. Nearest to
    // Cull first, since that is where his line is shortest.
    std::vector<std::pair<std::int32_t, std::int32_t>> candidates;  // distance-to-Cull, id
    for (const sim::Actor& actor : tavern.actors()) {
        if (!actor.present() || sim::isFloored(actor.activity()) ||
            actor.role() != sim::ActorRole::Patron || tavern.isProfessional(actor) ||
            actor.band() != session.body().band()) {
            continue;
        }
        candidates.emplace_back(actor.distanceTo(cull->x(), cull->y()), actor.id());
    }
    std::sort(candidates.begin(), candidates.end());
    std::int32_t markId = -1;
    for (const auto& [distance, id] : candidates) {
        (void)distance;
        const sim::Actor* patron = tavern.actorById(id);
        if (patron == nullptr || !patron->present()) {
            continue;
        }
        const std::int32_t sides[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (const auto& side : sides) {
            const std::int32_t px = patron->tileX() + side[0];
            const std::int32_t py = patron->tileY() + side[1];
            if (!session.tiles().standable(px, py, patron->band())) {
                continue;
            }
            walkToTile(session, px, py);
            if (session.body().tileX() != px || session.body().tileY() != py) {
                continue;
            }
            patron = tavern.actorById(id);
            if (patron == nullptr) {
                break;
            }
            facePlayerAndSync(session, patron->x(), patron->y());
            // A second of standing still, so the walk's own noise is off the
            // rule and he has looked up.
            session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
            cull = tavern.actorById(cullId);
            if (cull != nullptr && cull->present() && tavern.noticeBy(*cull).seen) {
                markId = id;
            }
            break;
        }
        if (markId >= 0) {
            break;
        }
    }
    if (markId < 0) {
        gWatchHaltNote = " mark=none-in-culls-sight candidates=" + std::to_string(candidates.size());
        return landed;
    }
    ++landed;  // 1: Cull sees the player, standing beside a patron
    const sim::Actor* mark = tavern.actorById(markId);
    if (mark == nullptr) {
        return landed;
    }
    // THE BLADE. Armed the way test_street_panic arms its client -- the sheet
    // that puts steel in a hand is the weapon lane's; the sim's own setter is
    // what it will call. Kill is what a drawn blade means (intent-by-verb
    // reads the weapon class: Edged is Lethal from the first exchange).
    session.tavern().setPlayerCombat(sim::Weapon::Edged, sim::Intent::Kill);
    const std::int32_t hpBefore = mark->hp();
    const bool swung = swingOnceAt(session, markId, 8);
    if (const sim::Actor* struck = tavern.actorById(markId); struck != nullptr) {
        gWatchHaltNote = " mark=" + struck->name() + " hp=" + std::to_string(hpBefore) + "->" +
                         std::to_string(struck->hp()) + (swung ? " swung" : " no-swing") +
                         (tavern.lethalFightLive() ? " lethal" : " not-lethal") +
                         (tavern.playerHandsUp() ? " hands-up" : " hands-down") +
                         " brawlers=" + std::to_string(tavern.currentFight().size()) +
                         " inside=" + (tavern.playerInside() ? "yes" : "no");
    }
    if (swung && tavern.lethalFightLive() && tavern.playerHandsUp() &&
        tavern.playerWeapon() == sim::Weapon::Edged) {
        ++landed;  // 2
    }

    // THE HALT. Stand still. Within a few seconds he is Closing on Violence
    // and the row reads what he said.
    bool closing = false;
    for (int second = 0; second < 20 && !closing; ++second) {
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        closing = tavern.watchStance() == sim::Tavern::WatchStance::Closing &&
                  tavern.watchInterest() == sim::WatchCause::Violence;
    }
    if (closing && !tavern.lastDemand().empty() &&
        tavern.lastDemand().rfind("Watchman Cull: ", 0) == 0 &&
        session.lastMessage() == tavern.lastDemand()) {
        ++landed;  // 3
    }
    if (ending == "halt") {
        return landed;
    }

    // THE ARREST AT REACH -- step() itself consumes the release and says the
    // arrest line, so the proof is the report: it happened, the cause is
    // VIOLENCE and a sentence was passed.
    bool arrested = false;
    for (int second = 0; second < 60 && !arrested; ++second) {
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        arrested = tavern.lastArrest().happened &&
                   tavern.lastArrest().cause == sim::WatchCause::Violence;
    }
    if (arrested && tavern.lastArrest().sentence != sim::Sentence::None) {
        ++landed;  // 4
    }
    // AND THE STREET: turned loose outside the walls with his words on the row.
    if (arrested && !tavern.playerInside() && !tavern.lastArrest().line.empty() &&
        session.lastMessage() == tavern.lastArrest().line) {
        ++landed;  // 5
    }
    return landed;
}


// ---------------------------------------------------------------------------
// KIT BUILD: the Kit, played
// ---------------------------------------------------------------------------

/// THE KIT LINE, through the real verbs. Two in the afternoon on the Tarwalk
/// (daylight for the coil on the boards), the clock forward to four the next
/// morning for the lifts (the house empty, so a thing on a chair can be
/// named and taken with nobody to see it and no bouncer's ladder started),
/// and forward again to two for Ox on the door. Ten beats, each an ending:
///   1. take     the crosshair reads TAKE ROPE  48DR over the coil on the
///               Tarwalk (nobody's), and the press takes it
///   2. theirs   in the house at four: the lantern on Hobbin's table from
///               Maell's chair -- TAKE LANTERN  THEIRS in the Owned accent --
///               then the knife on Edda's and the coat on Colm's, each a lift
///               under the witness rule with nobody to witness it
///   3. sheet    the clock at two; the Character tile open on the carried
///               rows: IN HAND FISTS, the four worn slots at NOTHING, LOAD n
///               / 240 DRAMS, the rows with their weights and worths
///   4. equip    the coat WORN and the knife IN HAND by the tile's own press
///   5. slot     the knife bound to slot 3 by LEFT/RIGHT, the number pressed,
///               the strip up with KNIFE on it
///   6. dr       the knife bared, fists on Ox Gullbane, his blow turned by the
///               coat -- COAT TURNS n on the row, stopped on that step
///   7. search   the knife back in hand, Ox put down for good, SEARCH OX
///               GULLBANE  DEAD on the crosshair and his kit on the list
///   8. load     everything off him and the bottle and the hood off the
///               tables: the body near its budget, the legs at half, the tile
///               open on the LOAD line
///   9. drop     out to the Tarwalk, the coil put down through the tile's own
///               X, the body stepped back to look at it on the boards -- the
///               3D frame
///  10. done     the tile again on what is left (the default)
constexpr std::int32_t kKitBeats = 10;

[[nodiscard]] std::int32_t kitBeatsFor(const std::string& ending) {
    if (ending == "take") {
        return 1;
    }
    if (ending == "theirs") {
        return 2;
    }
    if (ending == "sheet") {
        return 3;
    }
    if (ending == "equip") {
        return 4;
    }
    if (ending == "slot") {
        return 5;
    }
    if (ending == "dr") {
        return 6;
    }
    if (ending == "search") {
        return 7;
    }
    if (ending == "load") {
        return 8;
    }
    if (ending == "drop") {
        return 9;
    }
    return kKitBeats;
}

/// What the Kit line saw, for the summary.
std::string gKitNote;

/// Opens the tiled Menu on the Character tile and walks its cursor onto the
/// carried row for `itemId`. False when the row is not on the tile.
[[nodiscard]] bool openKitRow(Session& session, std::string_view itemId) {
    if (!session.characterOpen()) {
        session.toggleCharacter();
    } else {
        session.setMenuFocus(kMenuFocusCharacter);
    }
    const std::int32_t index = session.tavern().items().indexOf(itemId);
    const std::vector<Session::KitRow> rows = session.kitRows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].item != index) {
            continue;
        }
        const int target = static_cast<int>(session.characterKitOffset() + i);
        // Walk, never jump: the same wrap the arrows make.
        for (int guard = 0; guard < 64; ++guard) {
            const std::optional<Session::KitRow> at = session.highlightedKitRow();
            if (at.has_value() && at->item == index) {
                return true;
            }
            const int cursor = session.dialogueView().cursor;
            session.moveTopicCursor(cursor < target ? 1 : -1);
        }
        return false;
    }
    return false;
}

/// Walks to a tile and faces a neighbouring tile, so the crosshair reads
/// what lies there.
void standAtFacing(Session& session, std::int32_t standX, std::int32_t standY,
                   std::int32_t faceX, std::int32_t faceY) {
    walkToTile(session, standX, standY);
    session.closeConversation();
    facePlayerAndSync(session, sim::q8_tile_centre(faceX), sim::q8_tile_centre(faceY));
}

[[nodiscard]] int runKitLine(Session& session, const std::string& ending) {
    int landed = 0;
    gKitNote.clear();
    const sim::Tavern& tavern = session.tavern();
    if (!tavern.items().loaded()) {
        gKitNote = " items=unloaded";
        return landed;
    }
    const auto item = [&](std::string_view id) { return tavern.items().indexOf(id); };
    const auto carried = [&](std::string_view id) {
        return tavern.kit().count(item(id)) > 0;
    };
    const auto noteAim = [&](const char* key, const Session::InteractTarget& aim) {
        gKitNote += std::string(" ") + key + "=" + aim.verb +
                    (aim.subject.empty() ? "" : ":" + aim.subject) +
                    (aim.note.empty() ? "" : "/" + aim.note);
    };

    // 1. TAKE. The rope at (151,63), from the tile east of it, facing west.
    // Two in the afternoon: daylight on the quay, and the thing you walked
    // to outranks the passers-by.
    standAtFacing(session, 152, 63, 151, 63);
    Session::InteractTarget aim = session.interactTarget();
    noteAim("take", aim);
    if (ending == "take") {
        return aim.verb == "TAKE" && aim.subject == "ROPE" ? 1 : 0;
    }
    session.interact();
    if (aim.verb == "TAKE" && aim.subject == "ROPE" && carried("rope")) {
        ++landed;  // 1
    }

    // 2. THEIRS. Four the next morning, the house empty. The lantern on
    // Hobbin's table from Maell's chair, then the knife and the coat.
    session.skipToHour(4);
    session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
    standAtFacing(session, 149, 74, 150, 74);
    aim = session.interactTarget();
    noteAim("theirs", aim);
    const bool theirsCue = aim.verb == "TAKE" && aim.subject == "LANTERN" && aim.note == "THEIRS" &&
                           aim.kind == AimKind::Owned;
    if (ending == "theirs") {
        return landed + (theirsCue ? 1 : 0);
    }
    session.interact();
    standAtFacing(session, 155, 73, 156, 73);
    session.interact();  // the knife
    walkToTile(session, 148, 72);
    session.closeConversation();
    session.interact();  // the coat, underfoot
    if (theirsCue && carried("lantern") && carried("knife") && carried("coat")) {
        ++landed;  // 2
    }
    gKitNote += " lifts=" + std::to_string(tavern.dialogue().crimes().tally(sim::Crime::Lift));

    // 3. THE SHEET, at two, on the carried rows.
    session.skipToHour(14);
    session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
    if (openKitRow(session, "knife")) {
        ++landed;  // 3
    }
    if (ending == "sheet") {
        return landed;
    }

    // 4. EQUIP: the coat, then the knife, by the tile's own press.
    if (openKitRow(session, "coat")) {
        session.chooseTopic(0);
    }
    if (openKitRow(session, "knife")) {
        session.chooseTopic(0);
    }
    if (tavern.kit().isWorn(item("coat")) && tavern.playerWeapon() == sim::Weapon::Edged) {
        ++landed;  // 4
    }
    if (ending == "equip") {
        return landed;
    }

    // 5. SLOT: the knife to slot 3 by three presses of RIGHT, then the key.
    if (openKitRow(session, "knife")) {
        session.adjustKitSlot(3);
    }
    session.closeConversation();
    session.selectQuickSlot(2);
    if (tavern.slotItemIndex(2) == item("knife")) {
        ++landed;  // 5
    }
    if (ending == "slot") {
        return landed;
    }

    // 6. DR: knife bared (the tile's press again), fists on Ox at the door,
    // his blow turned by the coat -- stepped ONE STEP AT A TIME so the run
    // stops on the exact step the row says COAT TURNS n, before the house's
    // own barks overwrite it.
    if (openKitRow(session, "knife")) {
        session.chooseTopic(0);
    }
    session.closeConversation();
    const sim::Actor* ox = actorNamed(session, "Ox Gullbane");
    const std::int32_t oxId = ox != nullptr ? ox->id() : -1;
    const std::int32_t turnedBefore = tavern.blowsTurned();
    if (oxId >= 0 && tavern.playerWeapon() == sim::Weapon::Fists) {
        (void)swingOnceAt(session, oxId, 8);
        for (int step = 0; step < 20 * sim::kStepsPerSecond && tavern.blowsTurned() == turnedBefore;
             ++step) {
            if (step % sim::kStepsPerSecond == 0) {
                (void)closeOnAndFace(session, oxId);
            }
            session.stepMany(sim::MoveInput{}, 1);
        }
    }
    gKitNote += " turned=" + std::to_string(tavern.blowsTurned() - turnedBefore) +
                " last=" + std::to_string(tavern.lastTurned());
    if (tavern.blowsTurned() > turnedBefore) {
        ++landed;  // 6
    }
    if (ending == "dr") {
        return landed;
    }

    // 7. SEARCH: the knife back in hand off its slot, Ox put down for good,
    // the crosshair over him, the list.
    session.selectQuickSlot(2);
    bool dead = false;
    if (oxId >= 0 && tavern.playerWeapon() == sim::Weapon::Edged) {
        for (int swings = 0; swings < 24 && !dead; ++swings) {
            if (!closeOnAndFace(session, oxId)) {
                break;
            }
            session.attackDown();
            session.stepMany(sim::MoveInput{}, sim::kHardSwingHoldSteps + 1);
            session.attackUp();
            session.stepMany(sim::MoveInput{}, sim::kHardSwingRecoverySteps + 2);
            const sim::Actor* struck = tavern.actorById(oxId);
            dead = struck != nullptr && struck->activity() == sim::Activity::Dead;
        }
    }
    if (dead) {
        const sim::Actor* corpse = tavern.actorById(oxId);
        walkToTile(session, corpse->tileX(), corpse->tileY());
        session.closeConversation();
        session.stepMany(sim::MoveInput{}, 1);
        aim = session.interactTarget();
        noteAim("search", aim);
        session.interact();
    }
    if (dead && session.searchOpen() && !session.grimoireRows().empty()) {
        ++landed;  // 7
    }
    if (ending == "search") {
        return landed;
    }

    // 8. LOAD: everything off him (TAKE ALL is the last row), the bottle and
    // the hood off the tables, the body near its budget, the tile open on
    // the LOAD line.
    if (session.searchOpen()) {
        session.chooseGrimoireRow(static_cast<int>(session.grimoireRows().size()) - 1);
    }
    session.closeConversation();
    standAtFacing(session, 150, 69, 150, 70);  // Bram's chair: the bottle on Wick's
    session.interact();
    walkToTile(session, 149, 69);  // the hood on Bram's own
    session.closeConversation();
    session.interact();
    gKitNote += " load=" + std::to_string(tavern.loadDrams()) + "/" +
                std::to_string(tavern.loadBudget()) + " legs=" + std::to_string(tavern.loadSpeedQ8()) +
                "/256";
    const bool loaded = openKitRow(session, "coat");
    if (loaded && tavern.loadSpeedQ8() < 256 && tavern.loadDrams() * 4 >= tavern.loadBudget() * 3) {
        ++landed;  // 8
    }
    if (ending == "load") {
        return landed;
    }

    // 9. DROP: out to the Tarwalk in daylight, the coil put down through the
    // tile's own X on a tile clear of the quay's clutter, then two tiles
    // back, facing it, so the frame has it on the boards.
    session.closeConversation();
    walkToTile(session, 154, 62);
    session.closeConversation();
    if (openKitRow(session, "rope")) {
        session.dropHighlightedKitRow();
    }
    session.closeConversation();
    standAtFacing(session, 156, 62, 154, 62);
    bool coilDown = false;
    for (const sim::GroundItem& entry : tavern.groundItems()) {
        if (entry.item == item("rope") && entry.x == 154 && entry.y == 62) {
            coilDown = true;
        }
    }
    if (!carried("rope") && coilDown) {
        ++landed;  // 9
    }
    if (ending == "drop") {
        return landed;
    }

    // 10. The tile again, on what is left.
    if (openKitRow(session, "coat")) {
        ++landed;  // 10
    }
    return landed;
}

// ---------------------------------------------------------------------------
// JUSTICE BUILD (HEARING PAGE LANE): the court, played
// ---------------------------------------------------------------------------

/// How many beats runCourtLine tries to land, by ending. THE PAGE (the
/// default): Cull can SEE the player beside a patron; the paper -- lifts in
/// his sight until the row reads WANTED; taken at reach with paper, his own
/// line on the row with his hand on you; the plate, and the body at the
/// Mission's door with the page up. Three. "cull" stops on the second beat,
/// his line on the alert row in the room (two); "taken" stops on the plate,
/// TAKEN TO THE MISSION over black before the page (three); "paper" adds
/// HEAR THE PAPER open (four); "armed" presses I DID IT once, the row armed
/// with SURE on its tail and the priest pressing for the answer (four);
/// "plea" adds I DID IT pleaded and the check block on the page (four);
/// "deny" is the other plea, I DID NOT, weighed with THE PRIEST IS A MAN in
/// the block (four); "hand" takes the Skyrunners' oath off Finch first and
/// then the same road, so the paper asks for the hand and I DID IT lands
/// THE HAND (four); "serve" pleads I DID IT and TAKES the sentence row --
/// the coin paid, the nights or the days on the world clock, the record
/// closed, TURNED LOOSE on the row with the day it ended on (five).
///
/// The killing's endings: "bloodtag" stops on the corpse, WANTED FOR BLOOD
/// on the HUD (one); "ropepage" is a killing before the Watch drinks, the
/// paper with blood on it, taken to a rope hearing, I DID IT and THE ROPE
/// passed -- the rope hearing page, THE DROP offered and not taken (four);
/// "rope" takes the drop: the plate with the end rows under it (five);
/// "newman" arms the rope's own first row after the plate (six) and takes
/// it, the answer main() reads to open the creation window again (seven).
/// "wanted" stops on the first beat: the tag on the HUD, paper out, nobody's
/// hand on you yet (one).
///
/// BARKS & GATE LANE. A line before this one may leave the clock at eight
/// (--flame, the Mission's own hour: what you gave at the door is the only
/// coin the Flame reads, so the fine's tier is reached by GIVING first); the
/// court then waits for Cull through the wait page's own jump, exactly as
/// the rope line does after its killing.
constexpr std::int32_t kCourtBeats = 3;
constexpr std::int32_t kCourtCullBeats = 2;
constexpr std::int32_t kCourtTakenBeats = 3;
constexpr std::int32_t kCourtPaperBeats = 4;
constexpr std::int32_t kCourtArmedBeats = 4;
constexpr std::int32_t kCourtPleaBeats = 4;
constexpr std::int32_t kCourtServeBeats = 5;
constexpr std::int32_t kCourtBloodTagBeats = 1;
constexpr std::int32_t kCourtRopePageBeats = 4;
constexpr std::int32_t kCourtRopeBeats = 5;
constexpr std::int32_t kCourtNewManBeats = 7;

[[nodiscard]] std::int32_t courtBeatsFor(const std::string& ending) {
    if (ending == "wanted" || ending == "bloodtag") {
        return ending == "wanted" ? 1 : kCourtBloodTagBeats;
    }
    if (ending == "cull") {
        return kCourtCullBeats;
    }
    if (ending == "taken") {
        return kCourtTakenBeats;
    }
    if (ending == "paper") {
        return kCourtPaperBeats;
    }
    if (ending == "armed") {
        return kCourtArmedBeats;
    }
    if (ending == "plea" || ending == "deny" || ending == "hand") {
        return kCourtPleaBeats;
    }
    if (ending == "serve") {
        return kCourtServeBeats;
    }
    if (ending == "ropepage") {
        return kCourtRopePageBeats;
    }
    if (ending == "rope") {
        return kCourtRopeBeats;
    }
    if (ending == "newman") {
        return kCourtNewManBeats;
    }
    return kCourtBeats;
}

/// The endings whose line is the killing's: the tag it leaves, the rope
/// hearing, the rope, and the new man after it.
[[nodiscard]] bool courtEndingHangs(const std::string& ending) {
    return ending == "bloodtag" || ending == "ropepage" || ending == "rope" || ending == "newman";
}

/// What the court line found, for the summary -- the nemesis line's rule: a
/// capture cannot quietly photograph the wrong thing.
std::string gCourtNote;

/// A patron Cull can SEE the player beside, and the body stood there:
/// runWatchHaltLine's own search, nearest to Cull first. Returns the mark's
/// id, or -1 with the note filled.
[[nodiscard]] std::int32_t standBesidePatronInCullsSight(Session& session, std::int32_t cullId) {
    const sim::Tavern& tavern = session.tavern();
    const sim::Actor* cull = tavern.actorById(cullId);
    if (cull == nullptr || !cull->present()) {
        gCourtNote += " cull=absent";
        return -1;
    }
    std::vector<std::pair<std::int32_t, std::int32_t>> candidates;
    for (const sim::Actor& actor : tavern.actors()) {
        if (!actor.present() || sim::isFloored(actor.activity()) ||
            actor.role() != sim::ActorRole::Patron || tavern.isProfessional(actor) ||
            actor.band() != session.body().band()) {
            continue;
        }
        candidates.emplace_back(actor.distanceTo(cull->x(), cull->y()), actor.id());
    }
    std::sort(candidates.begin(), candidates.end());
    for (const auto& [distance, id] : candidates) {
        (void)distance;
        const sim::Actor* patron = tavern.actorById(id);
        if (patron == nullptr || !patron->present()) {
            continue;
        }
        const std::int32_t sides[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (const auto& side : sides) {
            const std::int32_t px = patron->tileX() + side[0];
            const std::int32_t py = patron->tileY() + side[1];
            if (!session.tiles().standable(px, py, patron->band())) {
                continue;
            }
            walkToTile(session, px, py);
            if (session.body().tileX() != px || session.body().tileY() != py) {
                continue;
            }
            patron = tavern.actorById(id);
            if (patron == nullptr) {
                break;
            }
            facePlayerAndSync(session, patron->x(), patron->y());
            session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
            cull = tavern.actorById(cullId);
            if (cull != nullptr && cull->present() && tavern.noticeBy(*cull).seen) {
                return id;
            }
            break;
        }
    }
    gCourtNote += " mark=none-in-culls-sight candidates=" + std::to_string(candidates.size());
    return -1;
}

/// Steel up in Cull's sight and stand still until he takes you at reach --
/// the feel build's Closing on VIOLENCE, no glance gate, no die. True once
/// the arrest has happened.
[[nodiscard]] bool standUntilTakenBy(Session& session, std::int32_t cullId) {
    sim::Tavern& tavern = session.tavern();
    tavern.setPlayerCombat(sim::Weapon::Edged, sim::Intent::Subdue);
    session.setBlocking(true);
    session.stepMany(sim::MoveInput{}, 1);
    session.setBlocking(false);
    session.stepMany(sim::MoveInput{}, 1);
    for (int second = 0; second < 90 && !tavern.lastArrest().happened; ++second) {
        if (const sim::Actor* cull = tavern.actorById(cullId); cull != nullptr && cull->present()) {
            // In his face: distance zero is inside reach whatever the room
            // does with the line. The room is told where the body is by the
            // step that follows, exactly as every relocation is.
            session.placeBodyAt(cull->tileX(), cull->tileY(), cull->band());
        }
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
    }
    return tavern.lastArrest().happened;
}

/// Puts `markId` down for good through the Attack verbs -- hard swings until
/// the roster says Dead. True on a corpse.
[[nodiscard]] bool killWithHardSwings(Session& session, std::int32_t markId) {
    sim::Tavern& tavern = session.tavern();
    tavern.setPlayerCombat(sim::Weapon::Edged, sim::Intent::Kill);
    for (int swings = 0; swings < 40; ++swings) {
        const sim::Actor* mark = tavern.actorById(markId);
        if (mark == nullptr) {
            return false;
        }
        if (mark->activity() == sim::Activity::Dead) {
            return true;
        }
        for (int i = 0; i < sim::kRecoilSteps + sim::kBlockStaggerSteps +
                                sim::kHardSwingRecoverySteps + 8 &&
                        (tavern.playerRecoilSteps() > 0 || tavern.playerBlockStaggerSteps() > 0 ||
                         !tavern.playerCombatIdle());
             ++i) {
            session.stepMany(sim::MoveInput{}, 1);
        }
        if (!closeOnAndFace(session, markId)) {
            return false;
        }
        session.attackDown();
        session.stepMany(sim::MoveInput{}, sim::kHardSwingHoldSteps + 1);
        session.attackUp();
        session.stepMany(sim::MoveInput{}, 1);
    }
    return false;
}

[[nodiscard]] int runCourtLine(Session& session, const std::string& ending) {
    int landed = 0;
    gCourtNote.clear();
    const sim::Tavern& tavern = session.tavern();
    const sim::CrimeLedger& crimes = tavern.dialogue().crimes();
    // In through the door to the bar, the smoke's own route-walk.
    walkToTile(session, sim::gull::kBartenderX, sim::gull::kBartenderY + 1);
    session.closeConversation();
    const bool hangs = courtEndingHangs(ending);

    if (ending == "hand") {
        // THE ROOFS FIRST: the Skyrunners' oath off Finch (the skyrun line's
        // own first beat; he keeps the snug after ten), so the paper the
        // lifts earn asks for the hand -- a Skyrunner's first. Then the same
        // road as everybody else's.
        if (speakTo(session, "Finch")) {
            pick(session, sim::TopicKind::Join);
            session.closeConversation();
        }
        const std::int32_t roofs = tavern.dialogue().factions().indexOf("skyrunners");
        if (roofs < 0 || !tavern.dialogue().standings().isMember(roofs)) {
            gCourtNote += " oath=refused";
            return landed;
        }
        gCourtNote += " oath=sworn";
    }

    if (hangs) {
        // THE KILLING, BEFORE THE WATCH DRINKS: the line starts at eight, the
        // room full and Cull not yet on his stool, so the corpse is made in
        // front of the room and not under a watchman's hand mid-swing. The
        // mark is the upright patron with the most people close enough to
        // see it done.
        std::int32_t markId = -1;
        int bestNear = -1;
        for (const sim::Actor& actor : tavern.actors()) {
            if (!actor.present() || sim::isFloored(actor.activity()) ||
                actor.role() != sim::ActorRole::Patron || tavern.isProfessional(actor) ||
                actor.band() != session.body().band()) {
                continue;
            }
            int near = 0;
            for (const sim::Actor& other : tavern.actors()) {
                if (other.id() != actor.id() && other.present() &&
                    !sim::isFloored(other.activity()) && other.role() != sim::ActorRole::Vermin &&
                    other.band() == actor.band() &&
                    std::max(std::abs(other.tileX() - actor.tileX()),
                             std::abs(other.tileY() - actor.tileY())) <= 4) {
                    ++near;
                }
            }
            if (near > bestNear) {
                bestNear = near;
                markId = actor.id();
            }
        }
        if (markId < 0 || !killWithHardSwings(session, markId) || !crimes.murderer()) {
            gCourtNote += " kill=failed";
            return landed;
        }
        gCourtNote += " slew=" + tavern.slainName() + " saw=" + std::to_string(crimes.slewWitnesses());
        ++landed;  // 1: a corpse on the roster, the ward knows whose hand
        if (ending == "bloodtag") {
            // THE TAG, photographed: WANTED FOR BLOOD on the row, the room
            // still around you, nobody's hand on you yet. The row's own
            // ease, fully up.
            session.stepMany(sim::MoveInput{}, 16);
            gCourtNote += " heat=" + std::to_string(crimes.heat());
            if (session.heatLine().rfind("WANTED FOR BLOOD", 0) != 0) {
                gCourtNote += " tag=none";
                --landed;
            }
            return landed;
        }
        // THE WAIT: the wait page's own jump to the hour the Watch drinks,
        // the body out of the fight's reach first (skipToHour is refused
        // with fists up) and the heat cooling honestly through the hours --
        // a murder's sixty is still paper at ten.
        session.tavern().lowerPlayerHands();
        session.tavern().setPlayerCombat(sim::Weapon::Fists, sim::Intent::Subdue);
        session.skipToHour(22);
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        if (!crimes.warrant()) {
            gCourtNote += " paper=lapsed";
            return landed;
        }
    }

    const sim::Actor* cull = actorNamed(session, "Watchman Cull");
    if ((cull == nullptr || !cull->present()) && !hangs) {
        // THE WATCH IS NOT ON HIS STOOL YET -- a line before this one (the
        // flame's, at eight) left the clock early. The wait page's own jump
        // to the hour he keeps, hands down first as the rope line does.
        session.tavern().lowerPlayerHands();
        session.tavern().setPlayerCombat(sim::Weapon::Fists, sim::Intent::Subdue);
        session.closeConversation();
        session.skipToHour(23);
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        gCourtNote += " waited=23";
        cull = actorNamed(session, "Watchman Cull");
    }
    if (cull == nullptr || !cull->present()) {
        gCourtNote += " cull=absent";
        return landed;
    }
    const std::int32_t cullId = cull->id();
    const std::int32_t markId = standBesidePatronInCullsSight(session, cullId);
    if (markId < 0) {
        return landed;
    }

    if (!hangs) {
        // THE PAPER, THROUGH THE VERB: a hand in a coat in Cull's sight until
        // the row reads WANTED. Eight heat a witnessed lift; a caught hand
        // is witnessed too. The mark's purse runs out before the paper does,
        // so the line moves to the next patron in Cull's sight when it must.
        for (int lifts = 0; lifts < 40 && !crimes.warrant(); ++lifts) {
            session.lift();
            session.stepMany(sim::MoveInput{}, 4);
            if (const sim::Actor* mark = tavern.actorById(markId);
                mark == nullptr || mark->coin() <= 0) {
                if (standBesidePatronInCullsSight(session, cullId) < 0) {
                    break;
                }
            }
        }
        gCourtNote += " heat=" + std::to_string(crimes.heat());
        if (!crimes.warrant() || session.heatLine().rfind("WANTED", 0) != 0) {
            gCourtNote += " paper=none";
            return landed;
        }
        ++landed;  // 1: WANTED on the row, off real lifts
        if (ending == "wanted") {
            // THE TAG, photographed: paper out, nobody's hand on you yet.
            return landed;
        }
    }

    // TAKEN AT REACH, WITH PAPER. The arrest opens the hearing on the ledger
    // and the ARREST BEAT on the screen: the officer's own line on the row
    // with his hand on you, then the plate, then the page.
    if (!standUntilTakenBy(session, cullId) || tavern.lastArrest().sentence == sim::Sentence::Fined) {
        gCourtNote += " arrest=" + std::string(tavern.lastArrest().happened ? "paperless" : "none");
        return landed;
    }
    gCourtNote += " ask=" + std::string(sim::sentenceName(tavern.lastArrest().sentence));
    if (!session.takenBeatUp() || session.courtOpen() || !tavern.playerInside() ||
        session.lastMessage().rfind(tavern.lastArrest().officer + ": ", 0) != 0) {
        gCourtNote += " beat=no";
        return landed;
    }
    ++landed;  // 2: taken with paper, his line on the row, his hand on you
    if (ending == "cull") {
        // The row's own ease, fully up, well inside the beat's hold. A
        // capture of this ending wants --settle-steps=0: the rest steps
        // would walk the beat through to the page.
        session.stepMany(sim::MoveInput{}, 16);
        return landed;
    }
    // THE PLATE: the beat spent to the cut, one step at a time, so the frame
    // is the plate's first.
    for (int step = 0; step < kTakenOfficerSteps + 2 && !session.takenPlateUp(); ++step) {
        session.stepMany(sim::MoveInput{}, 1);
    }
    if (!session.takenPlateUp() || session.courtOpen() ||
        session.lastMessage().rfind("TAKEN TO THE MISSION. ", 0) != 0 ||
        session.takenPlate() != session.lastMessage()) {
        gCourtNote += " plate=no";
        return landed;
    }
    if (ending == "taken") {
        ++landed;  // 3: the plate, TAKEN TO THE MISSION over black
        return landed;
    }
    // THE PAGE, when the plate's hold runs out: the body at the Mission's
    // door, the page up.
    for (int step = 0; step < kTakenPlateSteps + 2 && !session.courtOpen(); ++step) {
        session.stepMany(sim::MoveInput{}, 1);
    }
    if (!session.courtOpen() || tavern.playerInside() || session.takenPlateUp() ||
        session.lastMessage().rfind("TAKEN TO THE MISSION. ", 0) != 0) {
        gCourtNote += " page=no";
        return landed;
    }
    ++landed;  // 3: TAKEN TO THE MISSION, the page up
    // The page's own ease, fully open, so a capture photographs the bench.
    session.stepMany(sim::MoveInput{}, 16);

    if (ending == "paper") {
        (void)session.routeCourtKey(Key::Num3);
        if (session.courtPaperOpen()) {
            ++landed;  // 4: HEAR THE PAPER
        }
        return landed;
    }
    if (ending == "armed") {
        // THE ROW ARMED: one press on I DID IT, SURE on its tail, the priest
        // pressing for the answer (court.plead) where his opening was.
        (void)session.routeCourtKey(Key::Num1);
        const HearingPageState page = session.hearingPageState();
        const std::string pressing =
            std::string(tavern.dialogue().barks().line("court.plead", crimes.hearings()));
        if (session.courtPleaArmed() && page.armed == 0 && !page.rows.empty() &&
            page.rows[0].label.find("-- SURE? ") != std::string::npos && page.priest == pressing) {
            ++landed;  // 4: armed, the question in the priest's mouth
        } else {
            gCourtNote += " armed=no";
        }
        return landed;
    }
    if (ending == "plea" || ending == "deny" || ending == "hand" || ending == "serve" || hangs) {
        // THE PLEA, armed and confirmed -- I DID IT on every ending but the
        // denial's -- then the judgment's own hold so the sentence row is on
        // the list under the check block.
        const Key pleaKey = ending == "deny" ? Key::Num2 : Key::Num1;
        (void)session.routeCourtKey(pleaKey);
        (void)session.routeCourtKey(pleaKey);
        session.stepMany(sim::MoveInput{}, sim::kJudgmentHoldSteps + 1);
        const sim::HearingState& hearing = tavern.hearing();
        gCourtNote += " plea=" + std::string(sim::pleaName(hearing.plea)) +
                      " judgment=" + std::string(sim::judgmentName(hearing.judgment)) +
                      (hearing.doubled ? " doubled=yes" : "");
        if (hearing.judged() && session.courtSentenceOffered()) {
            ++landed;  // 4: pleaded, weighed, the row offered
        }
        if (ending == "hand" && hearing.judgment != sim::Judgment::TheHand) {
            // The oath was sworn and the paper asked for the hand; a
            // judgment that spared it is the bench working, but it is not
            // the frame this ending owes.
            gCourtNote += " hand=no";
            --landed;
        }
        if (ending == "plea" || ending == "deny" || ending == "hand") {
            return landed;
        }
        if (ending == "ropepage") {
            // THE ROPE HEARING PAGE: the blood reading, THE ROPE on the
            // badge, THE DROP offered and not taken.
            if (hearing.judgment != sim::Judgment::TheRope) {
                gCourtNote += " rope=no";
            }
            return landed;
        }
        if (ending == "serve") {
            // THE SENTENCE TAKEN by the player's own row: the coin, the clock,
            // the record closed, and the release -- at the Mission's door or
            // on the Tarwalk -- with the day it ended on in the row.
            if (hearing.judgment == sim::Judgment::TheRope) {
                gCourtNote += " serve=rope";
                return landed;
            }
            const std::int32_t coinBefore = tavern.playerCoin();
            const std::int32_t dayBefore = tavern.dayNumber();
            (void)session.routeCourtKey(Key::Num1);
            session.stepMany(sim::MoveInput{}, 2);
            const sim::Tavern::SentenceReport& served = tavern.lastServed();
            gCourtNote += " served=" + std::string(served.served ? "yes" : "no") +
                          " coin=" + std::to_string(coinBefore) + "->" +
                          std::to_string(tavern.playerCoin()) + " day=" +
                          std::to_string(dayBefore + 1) + "->" +
                          std::to_string(tavern.dayNumber() + 1) +
                          " heat=" + std::to_string(crimes.heat());
            if (served.served && !session.courtOpen() && !tavern.hearingPending() &&
                !crimes.warrant() && session.lastMessage().rfind("TURNED LOOSE ", 0) == 0) {
                ++landed;  // 5: served, the paper off, turned loose with the day on the row
            }
            // The seam's own dip, spent, so a capture photographs the street
            // or the door and not the cut.
            session.stepMany(sim::MoveInput{}, kPageEaseSteps + 1);
            return landed;
        }
        if (hearing.judgment != sim::Judgment::TheRope) {
            gCourtNote += " rope=no";
            return landed;
        }
        // THE DROP, by the player's own hand: the dip, the held plate, then
        // the rows under it -- the frame a capture of the end wants.
        (void)session.routeCourtKey(Key::Num1);
        session.stepMany(sim::MoveInput{}, kPageEaseSteps + sim::kDeathHoldSteps + 1);
        if (tavern.executed() && session.ropeRowsUp()) {
            ++landed;  // 5: hanged, the plate and the rows
        }
        if (ending == "newman" && session.ropeRowsUp()) {
            // A NEW MAN, by the row itself: ARMED on the first press (SURE
            // on its tail -- the one state of the plate that is not the
            // plate), TAKEN on the second: the answer main() reads beside
            // quitRequested to open the creation window again. Not a quit.
            (void)session.routeCourtKey(Key::Num1);
            session.stepMany(sim::MoveInput{}, 1);
            if (session.ropeRowArmed() == 0 && !session.runEnded() &&
                session.ropeRows()[0].find("-- SURE? ") != std::string::npos) {
                ++landed;  // 6: the row armed
                gCourtNote += " armed=new-man";
            } else {
                gCourtNote += " armed=no";
            }
            (void)session.routeCourtKey(Key::Num1);
            session.stepMany(sim::MoveInput{}, 1);
            const Session::RunEndChoice reason = session.runEndReason();
            gCourtNote += std::string(" end=") +
                          (reason == Session::RunEndChoice::NewMan  ? "new-man"
                           : reason == Session::RunEndChoice::Leave ? "leave"
                                                                    : "none");
            if (session.runEnded() && reason == Session::RunEndChoice::NewMan &&
                !session.quitRequested()) {
                ++landed;  // 7: a new man asked for, the window's loop owed
            }
        }
    }
    return landed;
}

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
/// COURIER CASE. Which of the errand's beats the last --case run landed, one
/// bit each in order -- the burglary's own count-plus-mask discipline.
std::int32_t gCaseBeatMask = 0;
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
        // THE CASEBOOK PASS. `opened` STOPS ON THE STEP THE NOTICE FIRES and
        // does nothing afterwards -- no walk back, no page opened. It exists
        // because the lead-opened plate is up for three seconds and for no
        // other reason, so without it there is no headless path to a picture of
        // the one moment this whole pass is about. Same hole `--threshold`
        // states for the crossing plate, same answer.
        if (ending == "opened" && lead.id == "weighhouse-ledger") {
            return gTrailRead;
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

/// How many beats runCaseLine tries to land: the sheet in hand, the sheet
/// read, the Gull door lead, the wait to the small hours, the box lead on the
/// guest floor, Finch put down with fists, taken up, and delivered to the
/// Mission. Eight.
constexpr std::int32_t kCaseBeats = 8;

/// COURIER CASE. PLAYS THE QUIET TENANT END TO END, through the same Session
/// calls a keypress makes -- courierDeliverNow (the courier's own beat, its
/// countdown skipped for the shutter), toggleLetters, walkAcrossDistrict, the
/// Wait page's own skipToHour, toggleCrouch, examine, punch and interact.
/// Nothing here reaches into the simulation sideways, which is the only thing
/// that makes a captured frame evidence of the errand rather than a diagram of
/// it. `ending` stops the run for a shutter of one beat; empty delivers the
/// whole errand.
[[nodiscard]] int runCaseLine(Session& session, const std::string& ending) {
    gCaseBeatMask = 0;
    int landed = 0;
    std::int32_t beat = 0;
    const auto mark = [&](bool ok) {
        // CASE WATCH. The verdict lands on the tape as well as in the mask, so
        // the watch director's shutter can go on the beat's own payoff frame.
        // Recorder-only; with no tape attached this whole line is a no-op.
        session.watchBeatLanded(beat, ok);
        if (ok) {
            gCaseBeatMask |= 1 << beat;
            ++landed;
        }
        ++beat;
    };
    // CASE WATCH. Announces "beat N plays now" onto the tape ahead of each
    // block below -- the caption hook, and nothing else; see watchChapter.
    const auto chapter = [&]() { session.watchChapter(beat); };
    const sim::CasebookRaws& raws = session.sheetRaws();
    if (!raws.loaded()) {
        return 0;
    }

    chapter();
    // 1. THE SHEET INTO THE HAND. The courier's own beat, countdown skipped so
    // a headless run does not walk in place for six seconds. The book goes
    // live and the handed letter turns up on the Letters tile at once.
    session.courierDeliverNow();
    mark(session.sheetCaseLive());

    chapter();
    // 2. THE SHEET READ. The first thing the errand teaches is that paper is
    // read here -- the handed document unfolds on the Letters tile the moment
    // its lead is heard, no walk owed. A `sheet` shutter stops here with it
    // open; every other ending closes the menu and walks on.
    session.toggleLetters();
    session.chooseVisibleTopic(0);
    mark(!session.unlockedLetters().empty());

    if (ending == "sheet") {
        return landed;
    }
    session.toggleLetters();  // put the paper down and get the world back

    chapter();
    // 3. THE GULL. Walk to the door lead and look -- the map-and-casebook beat,
    // and it opens both the box and the tenant.
    const sim::Lead& door = raws.leads()[static_cast<std::size_t>(raws.indexOf("gull-door"))];
    (void)walkAcrossDistrict(session, door.site.x, door.site.y);
    session.examine();
    mark(session.sheetBook().state(raws.indexOf("gull-door")) != sim::LeadState::Open);

    if (ending == "gull") {
        session.toggleCasebook();
        return landed;
    }

    chapter();
    // 4. THE WAIT. To two in the morning: the doors just barred, the candles
    // out, the hearth dying, and the one hour Finch keeps the snug that the
    // room is also dark enough to work. skipToHour is exactly what a WAIT pick
    // spends -- the clock the tutorial teaches, driven the way the page drives
    // it.
    session.skipToHour(2);
    mark(session.timeOfDay() / 3600 == 2);

    chapter();
    // 5. THE BOX, ON THE GUEST FLOOR. Up the stair, crouched, and a look at the
    // box lead -- the break-in taught as a place stood over in the dark. The
    // box lead sits on the upper band, so the climb is part of the beat.
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    climbAndLand(session);
    if (session.stance() != sim::Stance::Crouched) {
        session.toggleCrouch();
    }
    const sim::Lead& box = raws.leads()[static_cast<std::size_t>(raws.indexOf("stair-box"))];
    walkToTile(session, box.site.x, box.site.y);
    session.examine();
    const bool boxRead = session.sheetBook().state(raws.indexOf("stair-box")) !=
                         sim::LeadState::Open;
    mark(boxRead && session.stance() == sim::Stance::Crouched);

    if (ending == "night") {
        return landed;
    }

    chapter();
    // 6. THE TENANT, PUT DOWN WITH FISTS. Down off the guest floor, upright
    // again (a man is taken up standing over him, not from a crouch), and to
    // Finch's own snug post. Punch -- NOTHING EDGED, the player carries fists,
    // so the fight resolves in the world by the brawl line's own law -- until
    // he is on the boards. Looped without stepping the room between blows, so
    // the bouncer does not cross the floor mid-beat and nobody regenerates:
    // the same room a player who kept their nerve would face.
    // GATE FIX: dropDown() alone refuses over the box -- no ledge ahead --
    // and left the body on the guest floor (beats 7-8 dead, mask 0x3F). The
    // way down is the way up: back to the stair-head and off it, the exact
    // helper every other Gull line descends by.
    comeDownstairs(session);
    if (session.stance() == sim::Stance::Crouched) {
        session.toggleCrouch();
    }
    const sim::Actor* tenant = nullptr;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.role() == sim::ActorRole::SkyrunnerContact) {
            tenant = &actor;
            break;
        }
    }
    if (tenant != nullptr) {
        const std::int32_t tenantId = tenant->id();
        walkToTile(session, tenant->tileX(), tenant->tileY());
        // ACTION-COMBAT BUILD: punch() is now a sightline TAP with a recovery
        // lockout, so the old zero-step rapid punch is gone. This tenant FIGHTS
        // BACK (unlike the rat, whom nobody defends and who does not swing), so
        // chasing him between blows never lets a swing settle and drags the fight
        // across the floor. Stand where the approach left us and face him HEAD-ON
        // each swing (the crosshair dead on him, the door crossing in behind us
        // rather than between), tapping with FISTS -- Subdue, so he goes DOWN and
        // not dead -- until he is on the boards, re-closing only if he drifts out
        // of reach. Six clean taps put a full man down well inside the door's
        // cross-warn-grace, so the beat lands before the bouncer can.
        for (int guard = 0; guard < 40 && !session.tenantDown(); ++guard) {
            const sim::Actor* mark = session.tavern().actorById(tenantId);
            if (mark == nullptr || mark->activity() == sim::Activity::Downed) {
                break;
            }
            if (mark->distanceTo(session.body().x(), session.body().y()) > sim::kMeleeReach) {
                walkToTile(session, mark->tileX(), mark->tileY());
            }
            facePlayerAndSync(session, mark->x(), mark->y());
            session.punch();
            session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps);
        }
    }
    const bool down = session.tenantDown();
    mark(down);

    if (ending == "down") {
        return landed;
    }

    chapter();
    // 7. TAKE HIM UP. The one press the errand adds -- interact() resolves it
    // ahead of TALK when a downed tenant is in reach.
    session.interact();
    mark(session.sheetCarry());

    if (ending == "taken") {
        // TRAVEL lane: stop with the man genuinely in hand -- the one state
        // the errand never otherwise parks in -- so the travel probe (and the
        // case that drives it) can press TRAVEL against the carry refusal for
        // real rather than against a flag a test set sideways.
        return landed;
    }

    chapter();
    // 8. TO THE MISSION. The back room the sheet named. Walking in with the man
    // IS the delivery -- stepSheetCase() closes the book on arrival, no press
    // owed -- so the run steps the pump a beat at the anchor to let that fire.
    const sim::Lead& close =
        raws.leads()[static_cast<std::size_t>(raws.indexOf("bring-him-in"))];
    (void)walkAcrossDistrict(session, close.site.x, close.site.y);
    for (int guard = 0; guard < 8 && !session.sheetBook().closed(); ++guard) {
        session.stepMany(sim::MoveInput{}, 1);
    }
    mark(session.sheetBook().closed());
    return landed;
}

/// EVICTION CASE. The mask of which --eviction beats landed, for the summary.
std::int32_t gEvictBeatMask = 0;

/// How many beats runEvictionLine tries to land on the PARTICIPATE path: the
/// hire off Maell (the writ heard, the open-hand bar passed), the writ read
/// on the Letters tile, the Netters' gate lead read, the wait to the evening
/// the door will answer, the Marrow door lead read, the knock, the serve, and
/// the walk back to the Mission with the writ -- The Evictor in hand and the
/// book closed. Nine.
constexpr std::int32_t kEvictBeats = 9;
/// The DISRUPT path owes fewer: everything up to and including the knock (six
/// beats), then the walk-back-unserved to Maell and the yield -- eight, and
/// no weapon.
constexpr std::int32_t kEvictRefuseBeats = 8;

/// EVICTION CASE. PLAYS THE OWNER'S THIRD CASE END TO END, both paths, through
/// exactly the Session verbs a keypress makes -- speakTo/pick for the hire and
/// the yield, walkAcrossDistrict for the ward crossings, skipToHour for the
/// evening the rota brings the family home, interact for the knock and the
/// serve. Nothing reaches into the sim sideways: the open-hand skill is set
/// the one honest way a hired hand has it (the brawl does not train it yet --
/// the flagged gap), and every other beat is the real machinery.
[[nodiscard]] int runEvictionLine(Session& session, const std::string& ending) {
    gEvictBeatMask = 0;
    int landed = 0;
    std::int32_t beat = 0;
    const auto mark = [&](bool ok) {
        session.watchBeatLanded(beat, ok);
        if (ok) {
            gEvictBeatMask |= 1 << beat;
            ++landed;
        }
        ++beat;
    };
    const auto chapter = [&]() { session.watchChapter(beat); };
    const sim::CasebookRaws& raws = session.evictRaws();
    if (!raws.loaded()) {
        return 0;
    }

    // THE HIRED HAND HAS THE SKILL. The priest's bar is open hand at fifteen
    // of the hundred, the major-designation line; a hired hand qualifies, so
    // the drive seeds it the one way it can be seeded today -- chargen writes
    // this level and nothing in play yet raises it (the flagged progression
    // gap). test_eviction_line proves the gate REFUSES below the bar
    // separately, so this is arming a qualifying character, not dodging a
    // check.
    (void)session.tavern().dialogue().skills().setLevel(sim::kOpenHandSkill,
                                                        sim::kEvictionOpenHandBar);

    chapter();
    // 1. THE HIRE, at Maell's evening table in the Gull. The writ tops his
    // topic list only for the case's own priest and only while it is dormant
    // -- speakTo opens the conversation (syncEvictionTopics ran in interact),
    // pick chooses TakeWrit, and the session settles the case half.
    if (speakTo(session, "Father Maell")) {
        pick(session, sim::TopicKind::TakeWrit);
        session.closeConversation();
    }
    mark(session.evictCaseLive());

    chapter();
    // 2. THE WRIT READ. The handed document unfolds on the Letters tile the
    // moment the hire lead is heard -- no walk owed, the mission sheet's own
    // contract. A `writ` shutter stops here with it open.
    session.toggleLetters();
    session.chooseVisibleTopic(0);
    const bool writRead = !session.unlockedLetters().empty();
    mark(writRead);

    if (ending == "writ") {
        return landed;
    }
    session.toggleLetters();  // put the paper down, get the world back

    chapter();
    // 3. THE GATE. Walk east to the Netters' gate and look -- the roll and
    // the pledge, the compound the writ names. Reading it opens the roof.
    const sim::Lead& gate = raws.leads()[static_cast<std::size_t>(raws.indexOf("netters-gate"))];
    (void)walkAcrossDistrict(session, gate.site.x, gate.site.y);
    session.examine();
    mark(session.evictBook().state(raws.indexOf("netters-gate")) == sim::LeadState::Followed);

    if (ending == "gate") {
        session.toggleCasebook();
        return landed;
    }

    chapter();
    // 4. THE WAIT. To eight in the evening, when the rota has walked the
    // family home off the quay -- skipToHour, the clock a WAIT pick spends,
    // the brief's "once people are home" reached the way the game reaches it.
    session.skipToHour(20);
    mark(session.timeOfDay() / 3600 == 20);

    chapter();
    // 5. THE DOOR LEAD. Walk to the Marrow door on the Gullet lane and look:
    // the family, the four heads, the arrears -- the case's turn read as a
    // place stood over, and the lead the knock verb watches for.
    const sim::Lead& door = raws.leads()[static_cast<std::size_t>(raws.indexOf("family-door"))];
    (void)walkAcrossDistrict(session, door.site.x, door.site.y);
    session.examine();
    mark(session.evictBook().state(raws.indexOf("family-door")) == sim::LeadState::Followed);

    chapter();
    // 6. THE KNOCK. Stand at the door in the evening and press interact --
    // evictDoorReady is live (case running, writ unserved, in reach, the
    // hour home), so the first press knocks and the door answers.
    walkToTile(session, door.site.x, door.site.y);
    session.interact();
    mark(session.evictKnocked());

    if (ending == "knock") {
        return landed;
    }

    // THE FORK. "refused" is the disrupt path: carry the writ back to Maell
    // whole and give it up. Everything else serves.
    if (ending == "refused") {
        chapter();
        // 7r. THE WALK BACK, UNSERVED, and the yield in the priest's own
        // conversation -- YieldWrit tops his list only while the case runs
        // unserved. Maell keeps his evening hour in the Gull, so the
        // walk-back is to wherever his body actually stands: the district
        // router carries the long leg, and speakTo makes the final approach
        // and opens the conversation. The book closes on the stood-down
        // lead; no weapon.
        if (const sim::Actor* priest = actorNamed(session, "Father Maell")) {
            (void)walkAcrossDistrict(session, priest->tileX(), priest->tileY());
        }
        if (speakTo(session, "Father Maell")) {
            pick(session, sim::TopicKind::YieldWrit);
            session.closeConversation();
        }
        mark(session.evictBook().closed());
        // 8r. AND NO EVICTOR: the disrupt path's whole point, asserted as a
        // beat so a run that quietly armed the player would go red.
        mark(session.tavern().playerWeapon() == sim::Weapon::Fists);
        return landed;
    }

    chapter();
    // 7. THE SERVE. A second press at the answered door: the paper changes
    // hands, nobody swings, and the objective swings to the Mission.
    session.interact();
    mark(session.writServed());

    if (ending == "served") {
        return landed;
    }

    chapter();
    // 8. TO THE MISSION, THE WRIT SIGNED. Walking into the back room IS the
    // delivery -- stepEvictCase closes the book and pays The Evictor on
    // arrival, no press owed -- so the run steps the pump a beat at the
    // anchor to let that fire.
    const sim::Lead& close = raws.leads()[static_cast<std::size_t>(raws.indexOf("served"))];
    (void)walkAcrossDistrict(session, close.site.x, close.site.y);
    for (int guard = 0; guard < 8 && !session.evictBook().closed(); ++guard) {
        session.stepMany(sim::MoveInput{}, 1);
    }
    mark(session.evictBook().closed());

    chapter();
    // 9. THE REWARD IN HAND. The Evictor granted through the one seam -- the
    // participate path's payoff, asserted so the grant cannot silently drop.
    mark(session.tavern().playerWeapon() != sim::Weapon::Fists);
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
    session.placeBodyAt(standX, standY, target->band);
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

namespace {

/// DISTRICT PHASE D. ONE AUTHORED CROSSING: a pair of world tiles either side
/// of a docks::kPlaces boundary, on one band.
///
/// EVERY ONE OF THESE FOUR WAS MEASURED OFF THE SHIPPED BINARY, not read off
/// the table and hoped for. `granadad --smoke=0 --hold --spawn=X,Y,Z` prints
/// placeLabel() for the tile it stood on, so each pair below is two runs: the
/// near tile answering one thing and the far tile answering another. The
/// crossings a reader might expect to find here and does not are the point of
/// the exercise as much as the ones that are -- see the note on `saltgate`.
struct Threshold {
    const char* word;
    /// The near side: OUTSIDE the place being entered.
    std::int32_t fromX;
    std::int32_t fromY;
    /// The far side: inside it.
    std::int32_t toX;
    std::int32_t toY;
    std::int32_t band;
};

constexpr Threshold kThresholds[] = {
    // THE PAYOFF SHOT. Out of the Quayward compound's courtyard, east through
    // its ring gate, onto Saltgate Rise -- and the gate's mouth (103,136-139)
    // wears DISTRICT PHASE B's own one-band reman frame at world z21, directly
    // overhead as the body passes under it. (104,137) is the first tile of the
    // Rise's mid-slope leg; (103,137) is compound ground and has no name at
    // all, which is why walking IN through this gate announces nothing and
    // walking OUT announces the road.
    //
    // AND IT IS THIS GATE AND NOT THE SALTGATE GATE-HOUSE, which is the
    // structure Phase B named the phase after. That gate-house straddles the
    // Rise at y147-148 -- the road runs THROUGH it, so both sides of it are
    // SALTGATE RISE and crossing it is not crossing a boundary. The plate is
    // honest about that: there is nothing to announce, so it says nothing.
    // Flagged in the phase report rather than worked around here.
    {"saltgate", 101, 137, 105, 137, sim::docks::kBandMidSlope},
    // Off the working spine, through the Gilded Gull's door. The one crossing
    // in this table that is a BUILDING rather than a reach of street, and the
    // one a player meets first: the Gull is four seconds from the spawn.
    {"gull", 153, 64, 153, 67, sim::docks::kBandQuayside},
    // North off the Tarwalk, over the quay lip, onto the finger piers. Two
    // tiles of unnamed apron in between (y58-59), which is exactly the gap
    // lastPlaceName_ is written to ignore.
    {"piers", 156, 60, 156, 57, sim::docks::kBandQuayside},
    // East along Gallows Row onto the head of Saltgate Rise, up where the
    // watch-post and the gibbet are. The only crossing here that starts on
    // NAMED ground, and it is in the table on purpose: it is the one that
    // proves the placement's own plate is run out before the walk begins.
    {"gallows", 103, 153, 107, 153, sim::docks::kBandUpper},
};

}  // namespace

ThresholdLineResult runThresholdLine(Session& session, const std::string& which,
                                     const std::string& end) {
    ThresholdLineResult out;
    const Threshold* crossing = nullptr;
    for (const Threshold& candidate : kThresholds) {
        if (which == candidate.word) {
            crossing = &candidate;
        }
    }
    if (crossing == nullptr) {
        return out;
    }
    out.found = true;
    out.from.assign(sim::docks::placeNameAt(crossing->fromX, crossing->fromY, crossing->band));
    out.to.assign(sim::docks::placeNameAt(crossing->toX, crossing->toY, crossing->band));

    // THE ONE PLACEMENT, and runStreetLine's own reasoning for it applies
    // unchanged: the capture harness's router box is the Gilded Gull and its
    // street, and walking the body to the head of Saltgate Rise would be
    // photographing the pathfinder. Everything after this line is the game.
    session.placeBodyAt(crossing->fromX, crossing->fromY, crossing->band);
    const std::int32_t dx = crossing->toX - crossing->fromX;
    const std::int32_t dy = crossing->toY - crossing->fromY;
    const sim::Angle inward = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)
                                  ? (dx > 0 ? sim::kFacingEast : sim::kFacingWest)
                                  : (dy > 0 ? sim::kFacingSouth : sim::kFacingNorth);
    session.body().setYaw(inward);
    const sim::MoveInput still{};
    session.stepMany(still, 1);

    // THE PLACEMENT IS NOT A CROSSING, AND THE CAPTURE MUST NOT BE ABLE TO
    // CLAIM IT WAS. Landing on named ground -- `gallows` starts on Gallows Row
    // -- is a change of place as far as syncPanelAnim() can tell, and it fires
    // the plate. That is CORRECT for the machinery (a body that teleports has
    // arrived somewhere) and completely wrong for the photograph, which is
    // supposed to be evidence of a walk. So the placement's plate is run all
    // the way out through real steps, and its fade after it, before the walk
    // begins: whatever is on the frame at the shutter got there by walking.
    for (int guard = 0; guard < 400 && session.placePlateWanted(); ++guard) {
        session.stepMany(still, 1);
    }
    session.stepMany(still, 16);

    // THE CROSSING, WALKED -- the same movement steps a held forward key
    // produces, through the same Session::step(), collided against the same
    // geometry. This is the only part of the run the frame is evidence of.
    walkStraightTo(session, crossing->toX, crossing->toY);
    const std::string_view landed = sim::docks::placeNameAt(
        session.body().tileX(), session.body().tileY(), session.body().band());
    out.crossed = !out.to.empty() && landed == out.to;
    out.announced = session.placePlateWanted();
    out.plate.assign(session.placePlateLabel());

    // "back" TURNS AND LOOKS AT WHAT IT JUST CAME THROUGH. For `saltgate` that
    // is Phase B's gate frame standing over the mouth, with the plate up --
    // which is the whole composition this flag exists to make possible. A
    // half-turn only: the body has not moved, so nothing about the crossing
    // this result reports can change.
    if (end == "back") {
        session.body().setYaw(inward + sim::kTurnHalf);
    }
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
    session.placeBodyAt(standX, standY, target->band);
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
    session.placeBodyAt(standX, standY, giver->band);
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
    // KIT BUILD. Two in the afternoon: daylight on the quay for the coil
    // the crosshair names and the coil put down at the end. The line moves
    // its own clock to four the next morning for the lifts (the house
    // empty, nobody to see them, no bouncer's ladder started) and back to
    // two for Ox and the coat's turn -- see runKitLine.
    if (config.kit) {
        return 14;
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
    // The Watch line wants the impound keeper in the room and the room full
    // enough to swing at somebody in his sight: Watchman Cull keeps the Gull
    // from nine until one. Eleven, the watch tests' own hour.
    if (config.watchHalt) {
        return 23;
    }
    // JUSTICE BUILD. The court line wants Cull on his stool for the arrest
    // (eleven, the Watch line's own hour) -- except the rope, whose killing
    // is made at eight before he arrives and waits for him through the wait
    // page's own jump.
    if (config.court) {
        return courtEndingHangs(config.courtEnd) ? 20 : 23;
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
    // COURIER CASE. Eight in the evening: the courier hail and the door lead
    // want the Gull open and the ward awake, and runCaseLine waits to two on
    // its own for the break-in -- demonstrating the clock the tutorial
    // teaches rather than skipping the lesson by starting in the dark.
    if (config.caseRun) {
        return 20;
    }
    // EVICTION CASE. Eight in the evening: Father Maell takes his hour in the
    // Gull between seven and half past nine (the courier and flame lines lean
    // on the same fact), so the hire is answerable at the start, and the
    // family-door beat wants the rota to have walked the serfs home -- which
    // eight is past. runEvictionLine skips forward to the door hour on its
    // own if the run started earlier, but starting IN Maell's hour is what
    // lets the hire be the first thing that happens.
    if (config.evictionRun) {
        return 20;
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
        // SAME key F makes -- Session::punch() -- thrown at whoever is
        // nearest and retried until it actually connects, since a swing that
        // misses leaves nothing on screen for punchLandedPulse_ to draw.
        //
        // ACTION-COMBAT BUILD: a swing hits the first body ON THE LOOK-RAY
        // (Tavern::sightlineTarget), not the nearest body in reach, so a tap
        // thrown from wherever the smoke walk left the body is a tap at air --
        // this drive landed 0/1 at the Tarwalk spawn for exactly that reason.
        // It now throws the beat the nemesis and tenant lines throw: walk up
        // to the mark, put him dead on the crosshair, tap, step the recovery
        // lockout through, re-face him (a man who is hit back shifts his
        // feet), tap again. See tapUntilItConnects.
        session.closeConversation();
        bool landed = false;
        if (const sim::Actor* mark = nearestMark(session)) {
            landed = tapUntilItConnects(session, mark->id(), 16);
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += landed ? 1 : 0;
    }

    if (config.chargeSteps > 0) {
        // VERIFICATION ONLY. See SmokeRunConfig::chargeSteps. The same
        // walk-up and re-face --punch uses, then the Attack DOWN-EDGE a held
        // left mouse button makes and NO release: the room's own hold clock
        // (Tavern::playerAttackDown) measures the charge in steps, and the
        // shutter goes with the key still down. Landed means the man was
        // closed on and faced, so the wind-up is photographed over a person.
        session.closeConversation();
        bool faced = false;
        if (const sim::Actor* mark = nearestMark(session)) {
            faced = closeOnAndFace(session, mark->id());
        }
        session.attackDown();
        session.stepMany(sim::MoveInput{}, config.chargeSteps);
        result.scriptedWanted += 1;
        result.scriptedLanded += faced ? 1 : 0;
    }

    if (config.block) {
        // VERIFICATION ONLY. See SmokeRunConfig::block's own header. The
        // brawl starts the way --punch starts one -- walked up to, faced and
        // tapped until the tap connects, so there is a man IN the fight to
        // throw the blows the guard is meant to catch -- the guard goes up
        // through the SAME Session::setBlocking() the right mouse button
        // calls, and the wait is until the room itself says a blow was
        // softened -- blowsBlocked() moving -- because a run of whiffs leaves
        // a GUARD UP row over a fight the guard never actually worked in.
        // The wait runs even if no tap connected: a sightline that found him
        // put him in the brawl whether or not the roll landed, and his blows
        // are what the beat is about.
        session.closeConversation();
        if (const sim::Actor* mark = nearestMark(session)) {
            (void)tapUntilItConnects(session, mark->id(), 16);
        }
        // ACTION-COMBAT BUILD: the swing dropped the guard clause to false for
        // its recovery window (section 1.3), so let the hand return to idle
        // before raising the guard -- otherwise the first steps of the wait
        // below would be spent with the guard derived down.
        session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps + 1);
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
        result.scriptedWanted +=
            config.nemesisEnd == "death" ? kNemesisDeathBeats : kNemesisBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.watchHalt) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runWatchHaltLine(session, config.watchHaltEnd));
        result.watchHaltBeats = landed;
        // "halt" stops on the third beat by design; it owes three, not five.
        result.scriptedWanted += config.watchHaltEnd == "halt" ? kWatchHaltStopBeats : kWatchHaltBeats;
        result.scriptedLanded += landed;
    }

    if (config.kit) {
        // KIT BUILD. The Kit, played through the real verbs -- see
        // runKitLine. Each ending owes its own count.
        const std::int32_t landed =
            static_cast<std::int32_t>(runKitLine(session, config.kitEnd));
        result.kitBeats = landed;
        result.scriptedWanted += kitBeatsFor(config.kitEnd);
        result.scriptedLanded += landed;
    }

    if (config.court) {
        // JUSTICE BUILD (HEARING PAGE LANE). The bench, played through the
        // real verbs -- see runCourtLine. Each ending owes its own count.
        const std::int32_t landed =
            static_cast<std::int32_t>(runCourtLine(session, config.courtEnd));
        result.courtBeats = landed;
        result.scriptedWanted += courtBeatsFor(config.courtEnd);
        result.scriptedLanded += landed;
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

    if (config.caseRun) {
        // COURIER CASE. A short `ending` owes only the beats up to its shutter,
        // exactly runContractLine's `held` rule -- so the fell-short warning
        // never fires over a run that stopped where it was told to.
        const std::int32_t landed =
            static_cast<std::int32_t>(runCaseLine(session, config.caseEnd));
        result.caseBeats = landed;
        result.caseBeatMask = gCaseBeatMask;
        const std::int32_t owed = config.caseEnd == "sheet"  ? 2
                                  : config.caseEnd == "gull"  ? 3
                                  : config.caseEnd == "night" ? 5
                                  : config.caseEnd == "down"  ? 6
                                  : config.caseEnd == "taken" ? 7
                                                              : kCaseBeats;
        result.scriptedWanted += owed;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.evictionRun) {
        // EVICTION CASE. A short `ending` owes only the beats up to its
        // shutter, and the two paths owe different totals -- the participate
        // path nine, the disrupt path eight -- so the fell-short warning
        // never fires over a run that stopped where it was told to, on either
        // fork.
        const std::int32_t landed =
            static_cast<std::int32_t>(runEvictionLine(session, config.evictionEnd));
        result.evictBeats = landed;
        result.evictBeatMask = gEvictBeatMask;
        const std::int32_t owed = config.evictionEnd == "writ"    ? 2
                                  : config.evictionEnd == "gate"  ? 3
                                  : config.evictionEnd == "knock" ? 6
                                  : config.evictionEnd == "served" ? 7
                                  : config.evictionEnd == "refused" ? kEvictRefuseBeats
                                                                    : kEvictBeats;
        result.scriptedWanted += owed;
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

    // DISTRICT PHASE D. THE CROSSING, AFTER every scripted line above it and
    // BEFORE the menu-opening flags below -- which is the only order that lets
    // `--threshold=saltgate --map-overlay` photograph the stand-down rule
    // itself: the plate is armed by a real walk and then a page takes the
    // screen, and the frame proves it does not draw over one. See
    // SmokeRunConfig::threshold.
    if (!config.threshold.empty()) {
        result.thresholdResult =
            runThresholdLine(session, config.threshold, config.thresholdEnd);
        // THREE BEATS, AND THE THIRD IS THE DELIVERABLE: the crossing is in
        // the table, the body genuinely ended up on the far side of it, and
        // the plate was live when the shutter went. A capture that walks the
        // walk and announces nothing is the failure this counter exists to
        // make loud rather than leave for somebody to notice in a PNG.
        result.scriptedWanted += 3;
        result.scriptedLanded += result.thresholdResult.found ? 1 : 0;
        result.scriptedLanded += result.thresholdResult.crossed ? 1 : 0;
        result.scriptedLanded += result.thresholdResult.announced ? 1 : 0;
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

    // THE CASEBOOK PASS. The book's own cursor, view and commit verb, each
    // through the same public method a key press calls. Runs BEFORE the ward
    // map's flags on purpose: `--case-route` OPENS the ward map, so a command
    // line that asks for both gets the route's answer rather than a map the
    // route then overwrote.
    if (!config.caseLead.empty() || !config.caseTab.empty() || config.caseRoute) {
        if (!session.casebookPageOpen()) {
            // THE SAME CALL THE MENU KEY MAKES.
            session.toggleCasebook();
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += session.casebookPageOpen() ? 1 : 0;
        if (!config.caseLead.empty()) {
            result.scriptedWanted += 1;
            result.scriptedLanded += session.selectCasebookLead(config.caseLead) ? 1 : 0;
        }
        if (!config.caseTab.empty()) {
            std::string want = config.caseTab;
            for (char& c : want) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            result.scriptedWanted += 1;
            if (want == "leads") {
                while (session.casebookTab() != CasebookTab::Leads) {
                    session.cycleCasebookTab(1);
                }
                result.scriptedLanded += 1;
            } else if (want == "case") {
                while (session.casebookTab() != CasebookTab::Case) {
                    session.cycleCasebookTab(1);
                }
                result.scriptedLanded += 1;
            }
        }
        if (config.caseRoute) {
            // ENTER ON THE COMMIT VERB. What LANDED is the ward map being open
            // on the lead's own place -- a route that quietly did nothing is
            // exactly the failure this flag exists to catch.
            const std::vector<std::int32_t> book = session.casebook().known();
            std::int32_t lead = -1;
            if (!book.empty()) {
                const int at = std::clamp(session.casebookLeadCursor(), 0,
                                          static_cast<int>(book.size()) - 1);
                lead = book[static_cast<std::size_t>(at)];
            }
            const int wanted = session.mapPlaceForLead(lead);
            session.commitCasebookLead();
            result.scriptedWanted += 1;
            result.scriptedLanded +=
                (wanted >= 0 && session.districtMapOpen() &&
                 session.districtMapSelected() == wanted)
                    ? 1
                    : 0;
        }
    }

    if (config.mapOverlay) {
        // THE WARD MAP -- THE SAME CALL `M` MAKES. See
        // SmokeRunConfig::mapOverlay's own header.
        session.toggleDistrictMap();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.districtMapOpen() ? 1 : 0;
        // THE MAP PASS. The page has a cursor, four views and a zoom ladder,
        // and every one of them goes through the SAME public method a key
        // press calls -- selectDistrictMapPlace, setDistrictMapTab,
        // adjustDistrictMapZoom -- so a captured frame is a picture of the game
        // and not of a capture path that happens to look like it.
        if (!config.mapPlace.empty()) {
            result.scriptedWanted += 1;
            result.scriptedLanded += session.selectDistrictMapPlace(config.mapPlace) ? 1 : 0;
        }
        if (!config.mapTab.empty()) {
            std::string want = config.mapTab;
            for (char& c : want) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            const int index = want == "overview" ? 0
                              : want == "people" ? 1
                              : want == "index"  ? 2
                              : want == "legend" ? 3
                                                 : -1;
            result.scriptedWanted += 1;
            if (index >= 0) {
                session.setDistrictMapTab(index);
                result.scriptedLanded += 1;
            }
        }
        if (config.mapZoom > 0) {
            session.adjustDistrictMapZoom(config.mapZoom);
            result.scriptedWanted += 1;
            result.scriptedLanded += session.districtMapZoom() == config.mapZoom ? 1 : 0;
        }
    }

    // FAST TRAVEL (TRAVEL lane). The probe: cursor onto the named place and
    // the TRAVEL verb pressed, through the same public methods the T key
    // spends. AFTER the scripted lines on purpose, so a carry or a stance a
    // line just drove is what the press is refused against (`--case=taken
    // --travel=...` is the carry refusal probed for real); BEFORE `--face`,
    // which closes the page this needs open. Two beats: the name matched,
    // and the press RESOLVED HONESTLY -- an available plan must land the
    // body on its own tile with the clock advanced by exactly the restated
    // minutes, and a refused one must move nothing at all with the page
    // still up. Anything between those is the failure this probe exists to
    // make loud.
    if (!config.travelTo.empty()) {
        TravelLineResult& probe = result.travelResult;
        if (!session.districtMapOpen()) {
            session.toggleDistrictMap();
        }
        probe.found = session.selectDistrictMapPlace(config.travelTo);
        result.scriptedWanted += 2;
        if (probe.found) {
            result.scriptedLanded += 1;
            probe.plan = session.districtMapTravelPlan();
            probe.clockFrom = session.timeOfDay();
            session.travelDistrictMapSelection();
            probe.clockTo = session.timeOfDay();
            probe.endX = session.body().tileX();
            probe.endY = session.body().tileY();
            probe.endBand = session.body().band();
            probe.plateUp = session.placePlateWanted();
            probe.plate = std::string(session.placePlateLabel());
            probe.moved = probe.plan.available && !session.districtMapOpen() &&
                          probe.endX == probe.plan.toX && probe.endY == probe.plan.toY &&
                          probe.endBand == probe.plan.toBand;
            const bool clockExact =
                (probe.clockFrom + probe.plan.minutes * 60) % sim::kSecondsPerDay ==
                probe.clockTo;
            const bool refusedClean = !probe.plan.available && session.districtMapOpen() &&
                                      probe.clockFrom == probe.clockTo;
            result.scriptedLanded +=
                (probe.plan.available ? (probe.moved && clockExact) : refusedClean) ? 1 : 0;
        }
    }

    // THE CROSSHAIR PASS. TURN AND STAND. See SmokeRunConfig::face's own
    // header: this is how a door gets photographed with its name on the
    // crosshair. It runs AFTER the menu flags and closes the ward map by
    // construction (faceDistrictMapSelection does), so `--face` and
    // `--map-overlay` in the same command line resolve the way a player
    // pressing ENTER on the map does rather than fighting.
    if (!config.face.empty()) {
        result.scriptedWanted += 1;
        if (session.selectDistrictMapPlace(config.face)) {
            session.faceDistrictMapSelection();
            result.scriptedLanded += 1;
        }
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
        // UI-EA-SPEC sec. 0 (FLOW): the settle default is kCaptureRestSteps
        // now -- see the general loop below for the whole argument.
        const int preSteps = config.settleSteps >= 0 ? config.settleSteps
                                                     : (config.settle ? kCaptureRestSteps : 0);
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
    // PANES PASS: THE SAME IDEA ON THE CONVERTED CONTROLS PAGE -- see
    // SmokeRunConfig::keysRow on why it is its own flag. moveTopicCursor
    // already routes to the keys page's own cursor (its keysOpen_ branch), so
    // this is the identical call the arrow keys make and there is no
    // capture-only path through the drawing.
    if (config.keysRow > 0 && session.keysOpen()) {
        session.moveTopicCursor(config.keysRow - 1);
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
        // UI-EA-SPEC sec. 0 (FLOW lane): THE DEFAULT SHUTTER IS THE AT-REST
        // FRAME. kCaptureRestSteps (~4s of zero-input steps, anim.hpp) is
        // past every disclosure countdown -- the tutor bands' page-open
        // raise has held its kTutorHoldSteps and fully eased down -- and
        // short of the kIdleWakeSteps re-raise, so what a default
        // screenshot shows is the state the word budgets bind. It was 16:
        // enough for the panel's own rise, nothing else. --settle-steps=N
        // still overrides exactly (0 photographs the raised state;
        // --no-settle still means the opening bump).
        const int steps = config.settleSteps >= 0 ? config.settleSteps
                                                  : (config.settle ? kCaptureRestSteps : 0);
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
    if (!config.threshold.empty()) {
        // DISTRICT PHASE D: WHICH BOUNDARY, WHICH WAY, AND WHETHER THE PLATE
        // WAS ACTUALLY UP. A screenshot of a plate that never fired is
        // indistinguishable from a screenshot of a street, so the one thing a
        // PNG cannot report about itself is printed beside it.
        const ThresholdLineResult& cross = result.thresholdResult;
        summary << " | threshold " << config.threshold
                << " from=\"" << (cross.from.empty() ? "-" : cross.from) << "\""
                << " to=\"" << (cross.to.empty() ? "-" : cross.to) << "\""
                << " crossed=" << (cross.crossed ? "yes" : "no")
                << " plate=\"" << cross.plate << "\""
                << " up=" << (cross.announced ? "yes" : "no")
                << " end=" << config.thresholdEnd;
    }
    if (config.wait) {
        summary << " | wait open=" << (session.waitOpen() ? "yes" : "no")
                << " rows=" << session.waitRows().size()
                << " refusal=\"" << session.waitRefusal() << "\"";
    }
    if (!config.travelTo.empty()) {
        // TRAVEL lane. The whole claim on one line, byte-comparable across
        // two runs: the plan (route steps, octile units, honest seconds, the
        // minutes actually charged), the clock either side of the press, the
        // landing, the plate, and any refusal in its exact words.
        const TravelLineResult& probe = result.travelResult;
        summary << " | travel to=\"" << config.travelTo << "\""
                << " found=" << (probe.found ? "yes" : "no")
                << " route=" << probe.plan.routeSteps
                << " units=" << probe.plan.units
                << " walk=" << probe.plan.seconds << "s"
                << " charged=" << probe.plan.minutes << "min"
                << " clock=" << probe.clockFrom << "->" << probe.clockTo
                << " body=(" << probe.endX << "," << probe.endY << "," << probe.endBand
                << ")"
                << " moved=" << (probe.moved ? "yes" : "no")
                << " plate=\"" << probe.plate << "\""
                << " up=" << (probe.plateUp ? "yes" : "no")
                << " refusal=\"" << probe.plan.refusal << "\"";
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
    if (config.caseRun) {
        // COURIER CASE. The errand's own summary: beats and the mask (which
        // one dropped, not only how many), the live book's read/known, whether
        // the tenant is down and whether he is in hand, and where the case
        // stands -- so a short run says exactly what it proved.
        const sim::Casebook& book = session.sheetBook();
        summary << " | case beats=" << result.caseBeats << '/' << kCaseBeats
                << " mask=" << gCaseBeatMask
                << " read=" << book.readCount() << '/' << book.known().size()
                << " dread=" << book.dread()
                << " tenant=" << (session.tenantDown() ? "down" : "up")
                << " carry=" << (session.sheetCarry() ? "yes" : "no")
                << " closed=" << (book.closed() ? "yes" : "no")
                << " live=" << (session.sheetCaseLive() ? "yes" : "no")
                << " letters=" << session.unlockedLetters().size();
    }
    if (config.evictionRun) {
        // EVICTION CASE. The errand's own summary, the courier segment's
        // twin: beats and the mask, the live book's read/known, whether the
        // door was knocked and the writ served, whether the book closed, and
        // -- the fact both paths are judged on -- what is in the player's
        // hands. `served=no weapon=fists closed=yes` is the disrupt path
        // proved; `served=yes weapon=armed closed=yes` is the participate
        // path proved.
        const sim::Casebook& book = session.evictBook();
        summary << " | evict beats=" << result.evictBeats << '/' << kEvictBeats
                << " mask=" << gEvictBeatMask
                << " read=" << book.readCount() << '/' << book.known().size()
                << " dread=" << book.dread()
                << " knocked=" << (session.evictEverKnocked() ? "yes" : "no")
                << " served=" << (session.writServed() ? "yes" : "no")
                << " closed=" << (book.closed() ? "yes" : "no")
                << " live=" << (session.evictCaseLive() ? "yes" : "no")
                << " weapon="
                << (session.tavern().playerWeapon() == sim::Weapon::Fists ? "fists" : "armed")
                << " letters=" << session.unlockedLetters().size();
    }
    if (config.watchHalt) {
        summary << " | watch-halt beats=" << result.watchHaltBeats << '/' << kWatchHaltBeats
                << " stance="
                << (session.tavern().watchStance() == sim::Tavern::WatchStance::Closing
                        ? "closing"
                        : "idle")
                << " cause=" << sim::watchCauseName(session.tavern().watchInterest())
                << " arrest="
                << (session.tavern().lastArrest().happened
                        ? sim::watchCauseName(session.tavern().lastArrest().cause)
                        : "no")
                << gWatchHaltNote << " row=\"" << session.lastMessage() << '"';
    }
    if (config.kit) {
        summary << " | kit beats=" << result.kitBeats << '/' << kitBeatsFor(config.kitEnd)
                << gKitNote << " hand=" << sim::weaponName(session.tavern().playerWeapon())
                << " dr=" << session.tavern().wornDr()
                << " ground=" << session.tavern().groundItems().size();
    }
    if (config.court) {
        summary << " | court beats=" << result.courtBeats << '/' << courtBeatsFor(config.courtEnd)
                << " page=" << (session.courtOpen() ? "up" : "down")
                << " plate=" << (session.takenPlateUp() ? "up" : (session.takenBeatUp() ? "beat" : "down"))
                << " hearing=" << (session.tavern().hearingPending() ? "pending" : "none")
                << " executed=" << (session.tavern().executed() ? "yes" : "no")
                << " rows=" << (session.ropeRowsUp() ? "up" : "down")
                << " armed=" << (session.courtPleaArmed() ? "yes" : (session.ropeRowArmed() >= 0 ? "end-row" : "no"))
                << gCourtNote << " row=\""
                << session.lastMessage() << '"';
    }
    if (config.nemesis) {
        const sim::Nemesis* worst = session.tavern().nemesis().worst();
        summary << " | nemesis beats=" << result.nemesisBeats << '/'
                << (config.nemesisEnd == "death" ? kNemesisDeathBeats : kNemesisBeats)
                << " mask=" << gNemesisBeatMask;
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

    // UI-EA (LANE HUD): THE CORNER STAMP IS GONE, from captures too. The
    // spec's row table deletes "GRANADAD 0.10.0" from every frame -- the
    // version rides the pause/keys title rule instead (dialogueView already
    // prints it in the CONTROLS epithet, and the pause title carries it under
    // the PAGES lane), which is where a player actually goes to ask what
    // build this is. The census transcribes captures, so the two words had to
    // leave the capture path as well as the HUD; provenance lives in the PNG
    // filenames, the ship notes and the F1 page, not in the sky. config.stamp
    // is kept as an accepted no-op so every existing caller and capture
    // script still parses.
    (void)config.stamp;

    result.ok = !result.scriptFellShort();
    if (!config.screenshot.empty()) {
        // The PNG is still written. A frame of a run that fell short is
        // evidence OF the shortfall, and deleting it would make the failure
        // harder to diagnose rather than easier -- but ok stays false.
        if (config.shutter) {
            // 3D BUILD: the client's shutter composites this frame through
            // the raylib backend and writes what the window shows.
            result.ok = config.shutter(session, frame) && result.ok;
        } else {
            const Framebuffer output =
                config.captureScale > 1 ? upscaleNearest(frame, config.captureScale) : frame;
            result.ok = writePng(output, config.screenshot) && result.ok;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// CASE WATCH -- the drive, recorded
// ---------------------------------------------------------------------------

WatchFingerprint watchFingerprintOf(const Session& session) {
    WatchFingerprint print;
    print.timeOfDay = session.timeOfDay();
    print.elapsedSeconds = session.elapsedSeconds();
    print.tileX = session.body().tileX();
    print.tileY = session.body().tileY();
    print.band = session.body().band();
    print.yaw = session.body().yaw();
    const sim::Casebook& book = session.sheetBook();
    print.read = book.readCount();
    print.known = static_cast<std::int32_t>(book.known().size());
    print.dread = book.dread();
    print.letters = static_cast<std::int32_t>(session.unlockedLetters().size());
    print.closed = book.closed();
    print.live = session.sheetCaseLive();
    print.carry = session.sheetCarry();
    print.tenantDown = session.tenantDown();
    return print;
}

CaseWatchDrive recordCaseDrive(const SmokeRunConfig& config) {
    CaseWatchDrive drive;
    // THE SAME SESSION runSmoke BUILDS for --case: same config, and the same
    // scripted hour unless the caller named one -- a tape recorded against a
    // different ward would be a tape of a different errand.
    SessionConfig started = config.session;
    if (!started.timeOfDayGiven) {
        const int hour = scriptedStartHour(config);
        if (hour >= 0) {
            started.timeOfDay = hour * 3600;
        }
    }
    Session session(started);
    session.setWatchRecorder(&drive.ops);
    drive.beats = static_cast<std::int32_t>(runCaseLine(session, config.caseEnd));
    session.setWatchRecorder(nullptr);
    drive.mask = gCaseBeatMask;
    // A short ending owes only the beats up to its shutter -- runSmoke's own
    // rule, restated here so the watch's end line says X/owed and not X/8.
    drive.beatsWanted = config.caseEnd == "sheet"   ? 2
                        : config.caseEnd == "gull"  ? 3
                        : config.caseEnd == "night" ? 5
                        : config.caseEnd == "down"  ? 6
                                                    : kCaseBeats;
    for (const WatchOp& op : drive.ops) {
        if (op.kind == WatchOpKind::Step) {
            ++drive.stepCount;
        }
    }
    drive.end = watchFingerprintOf(session);
    return drive;
}

}  // namespace granadad::render
